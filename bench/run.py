#!/usr/bin/env python3
"""The stdnet performance test: one run, about two minutes, writes the results that the figure of
the README is drawn from.

    python3 bench/run.py --build build-bench/bench --out bench/results/latest
    python3 bench/infographic.py bench/results/latest docs/infographic.png [<baseline results>]

What makes it fast, and as repeatable as a loopback benchmark can be:

* a small fixed set of points (echo, HTTP, UDP, churn, idle connections, timers, reactor
  counters) with short windows, every run validated;
* the server and the load generator are each pinned to one core (distinct physical cores when
  the machine has them): the scheduler does not move them and they do not share a core;
* repetitions are interleaved (every repetition visits every point, and the order of the
  implementations rotates), so a drift of the machine hits all of them alike;
* what can be counted is counted, not timed: allocations, completions posted and `epoll_ctl`
  calls per operation come from counters read over the same window as the operations;
* the server's CPU time is read from /proc over the load generator's window, so efficiency
  (CPU microseconds per operation) does not depend on how much CPU the load generator leaves.

For a before/after comparison run it on both builds, on the same quiet machine, and give the
first results directory to infographic.py as the baseline.
"""
import argparse
import atexit
import json
import os
import platform
import select
import signal
import socket
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

VARIANTS = ["raw_poll", "raw_epoll", "asio_callbacks", "stdnet_senders"]
CLK_TCK = os.sysconf("SC_CLK_TCK")
PORT = [21000]
STARTUP_TIMEOUT = 20.0  # seconds a server may take to print READY
CHILDREN = set()  # every process started by this script, killed at exit
PINS = {}  # "server" / "loadgen" -> cpu


def _kill_children():
    for p in list(CHILDREN):
        try:
            p.kill()
        except OSError:
            pass


atexit.register(_kill_children)
signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))


class RunError(Exception):
    """A run that could not be carried out (server did not start, load generator hung...)."""


def log(*a):
    print(*a, flush=True, file=sys.stderr)


def choose_pins():
    """One cpu for the server, one for the load generator, on different physical cores if possible."""
    allowed = sorted(os.sched_getaffinity(0))
    cores = []
    for cpu in allowed:
        try:
            sib = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology/thread_siblings_list").read_text().strip()
        except OSError:
            sib = str(cpu)
        if sib not in [c[0] for c in cores]:
            cores.append((sib, cpu))
    cpus = [c[1] for c in cores]
    if len(cpus) >= 2:
        return {"server": cpus[0], "loadgen": cpus[1]}
    if len(allowed) >= 2:
        return {"server": allowed[0], "loadgen": allowed[1]}
    return {"server": allowed[0], "loadgen": allowed[0]}


def pinned(role):
    cpu = PINS.get(role)
    if cpu is None:
        return None
    return lambda: os.sched_setaffinity(0, {cpu})


def port_is_free(port):
    for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
        with socket.socket(socket.AF_INET, kind) as s:
            try:
                s.bind(("127.0.0.1", port))
            except OSError:
                return False
    return True


def next_port():
    """Next port that is free for both TCP and UDP on loopback."""
    while True:
        PORT[0] += 1
        if PORT[0] > 65000:
            raise RunError("no free port left")
        if port_is_free(PORT[0]):
            return PORT[0]


def proc_cpu_seconds(pid):
    with open(f"/proc/{pid}/stat") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / CLK_TCK


def proc_rss_kb(pid):
    with open(f"/proc/{pid}/status") as f:
        for line in f:
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    return 0


class Server:
    def __init__(self, build, variant, mode):
        self.variant = variant
        self.port = next_port()
        cmd = [str(build / f"srv_{variant}"), "--port", str(self.port), "--mode", mode, "--threads", "1"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                                     preexec_fn=pinned("server"))
        CHILDREN.add(self.proc)
        self.pid = self.proc.pid
        ready, _, _ = select.select([self.proc.stdout], [], [], STARTUP_TIMEOUT)
        line = self.proc.stdout.readline() if ready else ""
        if not line.startswith("READY"):
            self.kill()
            raise RunError(f"{variant} did not print READY within {STARTUP_TIMEOUT:.0f} s: {line!r}")

    def kill(self):
        try:
            self.proc.kill()
            self.proc.communicate(timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            pass
        CHILDREN.discard(self.proc)

    def alive(self):
        return self.proc.poll() is None and Path(f"/proc/{self.pid}").exists()

    def sample(self):
        """CPU time and RSS of the server now (None if it is gone)."""
        try:
            return {"t": time.time(), "cpu": proc_cpu_seconds(self.pid), "rss": proc_rss_kb(self.pid)}
        except OSError:
            return None

    def snapshot(self, which):
        """Asks the server to print its counters (window start / end)."""
        try:
            os.kill(self.pid, signal.SIGUSR1 if which == "start" else signal.SIGUSR2)
        except OSError:
            pass

    def stop(self):
        alive = self.alive()
        if alive:
            os.kill(self.pid, signal.SIGINT)
        try:
            out, _ = self.proc.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            out, _ = self.proc.communicate()
            alive = False
        CHILDREN.discard(self.proc)
        final, snaps = {}, {}
        for line in out.splitlines():
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                d = json.loads(line)
            except ValueError:
                continue
            if "snapshot" in d:
                snaps[d["snapshot"]] = d
            else:
                final.update(d)
        return {"alive": alive, "returncode": self.proc.returncode, "final": final, "snapshots": snaps}


def loadgen_cmd(build, mode, port, **kw):
    cmd = [str(build / "loadgen"), "--mode", mode, "--port", str(port)]
    for k, v in kw.items():
        cmd += [f"--{k}", str(v)]
    return cmd


def windowed_load(build, srv, mode, **kw):
    """Runs the load generator in --hold mode and samples the server at the boundaries of its
    measurement window, while every connection is still open. Returns (result, marks, returncode).
    A watchdog kills a load generator that exceeds its time budget."""
    p = subprocess.Popen(loadgen_cmd(build, mode, srv.port, hold=1, **kw), stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, text=True, preexec_fn=pinned("loadgen"))
    CHILDREN.add(p)
    budget = 60 + 4 * (float(kw.get("duration", 3)) + float(kw.get("warmup", 1))) + 0.02 * int(kw.get("conns", 1))
    timed_out = []
    dog = threading.Timer(budget, lambda: (timed_out.append(1), p.kill()))
    dog.start()
    result, marks = {}, {}
    try:
        for line in p.stdout:
            line = line.strip()
            if line == "MEASURE_START":
                marks["start"] = srv.sample()
                srv.snapshot("start")
            elif line == "MEASURE_END":
                marks["end"] = srv.sample()
                srv.snapshot("end")
                time.sleep(0.05)  # let the server print its snapshot before connections close
                try:
                    p.stdin.write("go\n")
                    p.stdin.flush()
                except OSError:
                    pass
            elif line.startswith("{"):
                try:
                    result = json.loads(line)
                except ValueError:
                    pass
        p.wait(timeout=30)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
    finally:
        dog.cancel()
        if p.poll() is None:
            p.kill()
            p.wait()
        CHILDREN.discard(p)
    if timed_out:
        raise RunError(f"load generator exceeded its {budget:.0f} s budget")
    return result, marks, p.returncode


def delta(snaps, key, sub=None):
    a, b = snaps.get("start"), snaps.get("end")
    if not a or not b:
        return None
    if sub:
        a, b = a.get(sub, {}), b.get(sub, {})
        return {k: b[k] - a.get(k, 0) for k in b}
    return b[key] - a[key]


def validate(mode, lg, rc, marks, report):
    """Every problem that makes a run unusable; an empty list means the run is valid."""
    problems = []
    if not lg:
        return ["load generator produced no result"]
    if rc != 0:
        problems.append(f"load generator exit code {rc}")
    if lg.get("ops", 0) <= 0:
        problems.append("no operation completed")
    if lg.get("errors", 0) > 0:
        problems.append(f"{lg['errors']} connection errors")
    if mode == "udp" and lg.get("ops", 0) > 0 and lg.get("lost", 0) > 0.01 * lg["ops"]:
        problems.append(f"{lg['lost']} datagrams lost (> 1 %)")
    if not marks.get("start") or not marks.get("end"):
        problems.append("server not sampled over the measurement window")
    if not report["alive"]:
        problems.append("server died during the run")
    elif report["returncode"] != 0:
        problems.append(f"server exit code {report['returncode']}")
    if "start" not in report["snapshots"] or "end" not in report["snapshots"]:
        problems.append("server counters missing")
    return problems


PROBLEMS = []


def failed_run(variant, mode, kw, why):
    PROBLEMS.append({"variant": variant, "mode": mode, **kw, "problems": [why]})
    log(f"  INVALID {variant} {mode} {kw}: {why}")
    return {"variant": variant, "problems": [why]}


def measure(build, variant, mode, **kw):
    """One run. Every per-operation ratio uses the load generator's measurement window for both
    its numerator (server CPU, allocations, counters) and its denominator (operations), and the
    RSS is read at the end of that window, with the connections still established."""
    try:
        srv = Server(build, variant, mode)
    except RunError as e:
        return failed_run(variant, mode, kw, str(e))
    try:
        base = srv.sample()  # before any connection
        lg, marks, rc = windowed_load(build, srv, mode, **kw)
        report = srv.stop()
    except Exception as e:  # a failed run is recorded, it must not end the test
        srv.kill()
        return failed_run(variant, mode, kw, f"{type(e).__name__}: {e}")
    problems = validate(mode, lg, rc, marks, report)
    run = dict(lg, variant=variant, threads=1, problems=problems)
    if problems:
        PROBLEMS.append({"variant": variant, "mode": mode, **kw, "problems": problems})
        log(f"  INVALID {variant} {mode} {kw}: {'; '.join(problems)}")
        return run
    ops = lg["ops"]
    cpu = marks["end"]["cpu"] - marks["start"]["cpu"]
    allocs = delta(report["snapshots"], "allocs")
    run.update(
        server_cpu_s=cpu,
        cpu_us_per_op=cpu * 1e6 / ops,
        rss_kb=marks["end"]["rss"],
        rss_delta_kb=marks["end"]["rss"] - base["rss"],
        allocs=allocs,
        alloc_bytes=delta(report["snapshots"], "alloc_bytes"),
        allocs_per_op=allocs / ops,
    )
    if "stats" in report["snapshots"].get("end", {}):
        st = delta(report["snapshots"], None, sub="stats")
        run["stats_per_op"] = {k: v / ops for k, v in st.items()}
    return run


def aggregate(runs):
    """Mean, sample standard deviation (n - 1) and coefficient of variation of each metric over
    the valid runs; the per-run values stay in `runs` for the significance tests."""
    valid = [r for r in runs if not r["problems"]]
    keys = ["ops_per_s", "mb_per_s", "p50_us", "p99_us", "p999_us", "cpu_us_per_op", "allocs_per_op",
            "rss_delta_kb", "rss_kb", "errors", "lost"]
    agg = {"runs": runs, "valid_runs": len(valid)}
    for k in keys:
        vals = [r[k] for r in valid if k in r]
        if not vals:
            continue
        mean = statistics.fmean(vals)
        agg[k] = mean
        if len(vals) >= 2:
            sd = statistics.stdev(vals)
            agg[k + "_sd"] = sd
            agg[k + "_cv"] = sd / mean if mean else 0.0
    for k in ("variant", "mode", "conns", "size", "threads"):
        agg[k] = next((r[k] for r in runs if k in r), None)
    return agg


def points(args):
    """(scenario, mode, variants, load generator arguments). The order is the order of the run."""
    d = dict(duration=args.duration, warmup=args.warmup)
    P = []
    for conns, size in ((1, 64), (16, 64), (256, 64), (16, 65536)):
        P.append(("echo", "echo", VARIANTS, dict(conns=conns, size=size, **d)))
    P.append(("http", "http", VARIANTS, dict(conns=64, **d)))
    P.append(("udp", "udp", VARIANTS, dict(conns=4, size=64, window=16, **d)))
    P.append(("churn", "churn", VARIANTS, dict(conns=16, size=64, **d)))
    P.append(("idle", "idle", ["asio_callbacks", "stdnet_senders"], dict(conns=2000, active=20, size=64, **d)))
    for conns in (16, 256):
        P.append(("backend", "echo", ["stdnet_senders", "stdnet_senders_poll"], dict(conns=conns, size=64, **d)))
    P.append(("backend", "idle", ["stdnet_senders", "stdnet_senders_poll"], dict(conns=2000, active=20, size=64, **d)))
    return P


def timers(build, n):
    r = subprocess.run([str(build / "bench_timers"), str(n)], capture_output=True, text=True, timeout=120,
                       preexec_fn=pinned("server"))
    if r.returncode != 0:
        raise RunError(r.stderr.strip() or f"bench_timers exit code {r.returncode}")
    return json.loads(r.stdout)


def campaign(build, args):
    P = points(args)
    runs = {}  # (point index, variant) -> [run]
    T = []
    for rep in range(args.reps):
        t0 = time.monotonic()
        for i, (scenario, mode, variants, kw) in enumerate(P):
            rot = rep % len(variants)  # rotate the order so no implementation is always first
            for v in variants[rot:] + variants[:rot]:
                runs.setdefault((i, v), []).append(measure(build, v, mode, **kw))
        try:
            T.append(timers(build, args.timers))
        except (RunError, subprocess.TimeoutExpired, ValueError) as e:
            PROBLEMS.append({"variant": "timers", "mode": "timers", "problems": [str(e)]})
        log(f"repetition {rep + 1}/{args.reps} done ({time.monotonic() - t0:.0f} s)")
    S = {}
    for i, (scenario, _, variants, kw) in enumerate(P):
        for v in variants:
            row = aggregate(runs[(i, v)])
            row.update({k: kw[k] for k in ("conns", "size") if k in kw})
            S.setdefault(scenario, []).append(row)
    S["timers"] = T
    return S


def diagnostics(build, args):
    """Reactor counters of the instrumented builds (exact: no timing involved)."""
    stats = []
    for v in ("stdnet_senders_stats", "stdnet_coroutines_stats"):
        r = measure(build, v, "echo", conns=64, size=64, duration=args.duration, warmup=args.warmup)
        if not r["problems"]:
            stats.append({"variant": v, "conns": 64, "threads": 1, "ops": r["ops"], "per_op": r["stats_per_op"]})
    return {"stats": stats}


def build_metrics(build):
    """Text size of each server binary (deterministic)."""
    out = []
    for v in VARIANTS:
        lines = subprocess.run(["size", str(build / f"srv_{v}")], capture_output=True, text=True).stdout.splitlines()
        if len(lines) > 1:
            out.append({"variant": v, "text_bytes": int(lines[-1].split()[0])})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build-bench/bench")
    ap.add_argument("--out", default="bench/results/latest")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--duration", type=float, default=0.6, help="measured window of a run, seconds")
    ap.add_argument("--warmup", type=float, default=0.25)
    ap.add_argument("--timers", type=int, default=20000)
    args = ap.parse_args()

    build = Path(args.build).resolve()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    PINS.update(choose_pins())

    meta = {
        "date": time.strftime("%Y-%m-%d %H:%M"),
        "cpu": next((l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo") if l.startswith("model name")), "?"),
        "cores": os.cpu_count(),
        "pinned": PINS,
        "kernel": platform.release(),
        "compiler": subprocess.run(["c++", "--version"], capture_output=True, text=True).stdout.splitlines()[0],
        "reps": args.reps,
        "duration": args.duration,
        "timers": args.timers,
    }
    log("meta", meta)
    results = {"meta": meta, "scenarios": campaign(build, args)}
    results["diagnostics"] = diagnostics(build, args)
    results["build"] = build_metrics(build)
    results["problems"] = PROBLEMS
    (out / "results.json").write_text(json.dumps(results, indent=1))
    if PROBLEMS:
        log(f"{len(PROBLEMS)} invalid run(s):")
        for p in PROBLEMS:
            log("  ", p)
        sys.exit(1)
    log("results written to", out / "results.json")


if __name__ == "__main__":
    main()
