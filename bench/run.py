#!/usr/bin/env python3
"""Benchmark campaign for stdnet (issue #10).

Runs every scenario against every server variant, repeats each measurement, and writes
bench/results/<name>/results.json plus a Markdown summary and charts.

    python3 bench/run.py --build build-bench/bench --out bench/results/2026-10-11 [--quick]

The server and the load generator run on the same host over loopback. Server CPU time is read
from /proc so that efficiency (CPU microseconds per operation) can be compared even when the
load generator competes for the same cores.
"""
import argparse
import atexit
import json
import os
import platform
import select
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

VARIANTS = ["raw_blocking", "raw_poll", "raw_epoll", "asio_callbacks", "asio_coroutines", "stdnet_senders", "stdnet_coroutines"]
LABELS = {
    "raw_blocking": "raw blocking",
    "raw_poll": "raw poll (BSD sockets)",
    "raw_epoll": "raw epoll",
    "asio_callbacks": "Asio callbacks",
    "asio_coroutines": "Asio coroutines",
    "stdnet_senders": "stdnet senders",
    "stdnet_coroutines": "stdnet task",
    "stdnet_senders_poll": "stdnet senders (poll)",
}
CLK_TCK = os.sysconf("SC_CLK_TCK")
PORT = [21000]
STARTUP_TIMEOUT = 20.0  # seconds a server may take to print READY
CHILDREN = set()  # every process started by this script, killed at exit


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
    utime, stime = int(fields[11]), int(fields[12])
    return (utime + stime) / CLK_TCK


def proc_rss_kb(pid):
    with open(f"/proc/{pid}/status") as f:
        for line in f:
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    return 0


class Server:
    def __init__(self, build, variant, mode, threads=1, wrapper=None):
        self.variant = variant
        self.port = next_port()
        cmd = [str(build / f"srv_{variant}"), "--port", str(self.port), "--mode", mode, "--threads", str(threads)]
        if wrapper:
            cmd = wrapper + cmd
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        CHILDREN.add(self.proc)
        self.pid = self.proc.pid
        ready, _, _ = select.select([self.proc.stdout], [], [], STARTUP_TIMEOUT)
        line = self.proc.stdout.readline() if ready else ""
        if not line.startswith("READY"):
            self.kill()
            raise RunError(f"{variant} did not print READY within {STARTUP_TIMEOUT:.0f} s: {line!r}")
        if wrapper:
            self.pid = self._child_pid()

    def kill(self):
        try:
            if self.pid != self.proc.pid:  # strace wrapper: its tracee must not survive it
                os.kill(self.pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            self.proc.kill()
            self.proc.communicate(timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            pass
        CHILDREN.discard(self.proc)

    def _child_pid(self):
        # Under strace the measured process is strace's child.
        for _ in range(50):
            try:
                kids = Path(f"/proc/{self.proc.pid}/task/{self.proc.pid}/children").read_text().split()
                if kids:
                    return int(kids[0])
            except OSError:
                pass
            time.sleep(0.01)
        return self.proc.pid

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
    A watchdog kills a load generator that exceeds its time budget; the process never outlives
    this call."""
    p = subprocess.Popen(loadgen_cmd(build, mode, srv.port, hold=1, **kw), stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, text=True)
    CHILDREN.add(p)
    budget = 120 + 4 * (float(kw.get("duration", 3)) + float(kw.get("warmup", 1))) + 0.02 * int(kw.get("conns", 1))
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
        problems.append("load generator produced no result")
        return problems
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


def failed_run(variant, mode, threads, kw, why):
    PROBLEMS.append({"variant": variant, "mode": mode, "threads": threads, **kw, "problems": [why]})
    log(f"  INVALID {variant} {mode} {kw}: {why}")
    return {"variant": variant, "threads": threads, "problems": [why]}


def measure(build, variant, mode, threads=1, **kw):
    """One run. Every per-operation ratio uses the load generator's measurement window for both
    its numerator (server CPU, allocations, counters) and its denominator (operations), and the
    RSS is read at the end of that window, with the connections still established."""
    try:
        srv = Server(build, variant, mode, threads)
    except RunError as e:
        return failed_run(variant, mode, threads, kw, str(e))
    try:
        base = srv.sample()  # before any connection
        lg, marks, rc = windowed_load(build, srv, mode, **kw)
        report = srv.stop()
    except Exception as e:  # a failed run is recorded, it must not end the campaign
        srv.kill()
        return failed_run(variant, mode, threads, kw, f"{type(e).__name__}: {e}")
    problems = validate(mode, lg, rc, marks, report)
    run = dict(lg, variant=variant, threads=threads, problems=problems)
    if problems:
        PROBLEMS.append({"variant": variant, "mode": mode, "threads": threads, **kw, "problems": problems})
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
    return run


def repeat(n, fn):
    """n runs; mean, sample standard deviation (n - 1) and coefficient of variation of each
    metric over the valid runs. The per-run values stay in `runs` for significance tests."""
    runs = [fn() for _ in range(n)]
    valid = [r for r in runs if not r["problems"]]
    keys = ["ops_per_s", "mb_per_s", "p50_us", "p99_us", "p999_us", "cpu_us_per_op", "allocs_per_op", "rss_delta_kb", "rss_kb", "errors", "lost"]
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


def log(*a):
    print(*a, flush=True, file=sys.stderr)


def campaign(build, reps, quick, only=None):
    dur, warm = (1, 0.3) if quick else (3, 1)
    S = {}

    def m(variant, mode, **kw):
        return lambda: measure(build, variant, mode, duration=dur, warmup=warm, **kw)

    def echo():  # 1 + 2. throughput and latency
        out = []
        for v in VARIANTS:
            for c in ([1, 16] if quick else [1, 16, 256]):
                for s in ([64, 4096] if quick else [64, 4096, 65536]):
                    log(f"echo {v} conns={c} size={s}")
                    out.append(repeat(reps, m(v, "echo", conns=c, size=s)))
        return out

    def churn():  # 3. connection churn
        return [repeat(reps, m(v, "churn", conns=16, size=64)) for v in VARIANTS if not log(f"churn {v}")]

    def idle():  # 4. many idle connections (thread-per-connection left out: 10k threads)
        n = 2000 if quick else 10000
        return [repeat(max(1, reps - 1), m(v, "idle", conns=n, active=n // 100, size=64, threads=2))
                for v in VARIANTS[1:] if not log(f"idle {v}")]

    def http():  # 5. HTTP hello
        return [repeat(reps, m(v, "http", conns=64)) for v in VARIANTS if not log(f"http {v}")]

    def udp():  # 6. UDP
        return [repeat(reps, m(v, "udp", conns=4, size=s, window=16)) for v in VARIANTS for s in (64, 1024)
                if not log(f"udp {v} size={s}")]

    def timers():  # 7. timers, in process
        n = "20000" if quick else "100000"
        out = []
        for _ in range(reps):
            try:
                r = subprocess.run([str(build / "bench_timers"), n], capture_output=True, text=True, timeout=300)
            except subprocess.TimeoutExpired:
                PROBLEMS.append({"variant": "timers", "mode": "timers", "problems": ["timer benchmark timed out"]})
                continue
            if r.returncode != 0:
                PROBLEMS.append({"variant": "timers", "mode": "timers", "problems": [r.stderr.strip() or f"exit code {r.returncode}"]})
            else:
                out.append(json.loads(r.stdout))
        return out

    def scaling():  # 8. server threads (the load generator uses one thread)
        return [repeat(reps, m(v, "echo", threads=t, conns=64, size=64))
                for v in ["raw_epoll", "asio_callbacks", "asio_coroutines", "stdnet_senders", "stdnet_coroutines"]
                for t in (1, 2) if not log(f"scaling {v} threads={t}")]

    def backend():  # stdnet epoll vs poll
        return [repeat(reps, m(v, "echo", conns=c, size=64)) for v in ["stdnet_senders", "stdnet_senders_poll"]
                for c in (16, 256) if not log(f"backend {v} conns={c}")]

    for name, fn in [("echo", echo), ("churn", churn), ("idle", idle), ("http", http), ("udp", udp),
                     ("timers", timers), ("scaling", scaling), ("backend", backend)]:
        if only is None or name in only:
            try:
                S[name] = fn()
            except Exception as e:  # keep the other scenarios; the failure is reported
                PROBLEMS.append({"variant": "*", "mode": name, "problems": [f"{type(e).__name__}: {e}"]})
                if only is None:
                    S[name] = []
    return {"scenarios": S}


def count_syscalls(trace_file, t0, t1):
    """Counts the system calls of an `strace -f -ttt` log whose timestamps fall in [t0, t1]."""
    calls = {}
    for line in Path(trace_file).read_text(errors="replace").splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3:
            continue
        try:
            ts = float(parts[1])
        except ValueError:
            continue
        if not (t0 <= ts <= t1):
            continue
        rest = parts[2]
        if rest.startswith("<...") or rest.startswith("+++") or rest.startswith("---"):
            continue  # "resumed" halves and signal/exit notes are not new calls
        name = rest.split("(", 1)[0]
        if name.isidentifier():
            calls[name] = calls.get(name, 0) + 1
    return calls


def diagnostics(build, quick):
    """Reactor counters (instrumented builds) and syscall profiles, over the measurement window."""
    dur = 1 if quick else 2
    diag = {"stats": [], "syscalls": []}
    for v in ("stdnet_senders_stats", "stdnet_coroutines_stats"):
        for c, t in ((1, 1), (64, 1), (64, 2)):
            srv = None
            try:
                srv = Server(build, v, "echo", threads=t)
                lg, marks, rc = windowed_load(build, srv, "echo", conns=c, size=64, duration=dur, warmup=0.5)
                report = srv.stop()
            except Exception as e:
                if srv:
                    srv.kill()
                PROBLEMS.append({"variant": v, "mode": "echo", "conns": c, "threads": t, "problems": [f"{type(e).__name__}: {e}"]})
                continue
            problems = validate("echo", lg, rc, marks, report)
            if problems:
                PROBLEMS.append({"variant": v, "mode": "echo", "conns": c, "threads": t, "problems": problems})
                continue
            st = delta(report["snapshots"], None, sub="stats")
            diag["stats"].append({"variant": v, "conns": c, "threads": t, "ops": lg["ops"],
                                  "per_op": {k: val / lg["ops"] for k, val in st.items()}, "window": st})
    if shutil.which("strace"):
        for v in ("raw_epoll", "asio_callbacks", "stdnet_senders"):
            trace = Path(f"/tmp/strace_{v}_{os.getpid()}.txt")
            srv = None
            try:
                srv = Server(build, v, "echo", wrapper=["strace", "-f", "-ttt", "-qq", "-o", str(trace)])
                lg, marks, rc = windowed_load(build, srv, "echo", conns=16, size=64, duration=dur, warmup=0.5)
                report = srv.stop()
            except Exception as e:
                if srv:
                    srv.kill()
                trace.unlink(missing_ok=True)
                PROBLEMS.append({"variant": v, "mode": "strace echo", "problems": [f"{type(e).__name__}: {e}"]})
                continue
            problems = validate("echo", lg, rc, marks, report)
            if problems:
                PROBLEMS.append({"variant": v, "mode": "strace echo", "problems": problems})
                continue
            time.sleep(0.3)
            calls = count_syscalls(trace, marks["start"]["t"], marks["end"]["t"])
            trace.unlink(missing_ok=True)
            ops = lg["ops"]
            diag["syscalls"].append({"variant": v, "ops": ops,
                                     "per_op": {k: n / ops for k, n in sorted(calls.items(), key=lambda x: -x[1])}})
    sizes = subprocess.run([str(build / "bench_opstate_sizes")], capture_output=True, text=True, timeout=60).stdout
    diag["opstate_sizes"] = json.loads(sizes)
    return diag


def build_metrics(build):
    """Compile time of each server translation unit and text size of each binary."""
    root = build.parent
    metrics = []
    for v in VARIANTS:
        target = f"srv_{v}"
        obj_dir = build / "CMakeFiles" / f"{target}.dir"
        for o in obj_dir.rglob("*.o"):
            o.unlink()
        t0 = time.monotonic()
        subprocess.run(["cmake", "--build", str(root), "--target", target, "-j1"], capture_output=True)
        secs = time.monotonic() - t0
        size = subprocess.run(["size", str(build / target)], capture_output=True, text=True).stdout.splitlines()[-1].split()
        metrics.append({"variant": v, "compile_s": secs, "text_bytes": int(size[0])})
    return metrics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build-bench/bench")
    ap.add_argument("--out", default="bench/results/latest")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--quick", action="store_true", help="short smoke run (CI)")
    ap.add_argument("--skip-build-metrics", action="store_true")
    ap.add_argument("--only", help="comma-separated scenarios to (re)run; merged into an existing results.json")
    args = ap.parse_args()

    build = Path(args.build).resolve()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    meta = {
        "date": time.strftime("%Y-%m-%d %H:%M"),
        "cpu": next((l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo") if l.startswith("model name")), "?"),
        "cores": os.cpu_count(),
        "kernel": platform.release(),
        "compiler": subprocess.run(["c++", "--version"], capture_output=True, text=True).stdout.splitlines()[0],
        "reps": args.reps,
        "quick": args.quick,
    }
    log("meta", meta)
    only = set(args.only.split(",")) if args.only else None
    fresh = campaign(build, args.reps, args.quick, only)
    if only and (out / "results.json").exists():
        results = json.loads((out / "results.json").read_text())
        results["scenarios"].update(fresh["scenarios"])
        results.setdefault("reruns", []).append({"date": meta["date"], "scenarios": sorted(only)})
        if "diagnostics" in only:
            results["diagnostics"] = diagnostics(build, args.quick)
    else:
        results = fresh
        results["meta"] = meta
        results["diagnostics"] = diagnostics(build, args.quick)
        if not args.skip_build_metrics:
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
