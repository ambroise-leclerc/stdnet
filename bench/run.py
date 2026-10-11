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
import json
import os
import platform
import shutil
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path

VARIANTS = ["raw_blocking", "raw_epoll", "asio_callbacks", "asio_coroutines", "stdnet_senders", "stdnet_coroutines"]
LABELS = {
    "raw_blocking": "raw blocking",
    "raw_epoll": "raw epoll",
    "asio_callbacks": "Asio callbacks",
    "asio_coroutines": "Asio coroutines",
    "stdnet_senders": "stdnet senders",
    "stdnet_coroutines": "stdnet task",
    "stdnet_senders_poll": "stdnet senders (poll)",
}
CLK_TCK = os.sysconf("SC_CLK_TCK")
PORT = [21000]


def next_port():
    PORT[0] += 1
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
        self.port = next_port()
        cmd = [str(build / f"srv_{variant}"), "--port", str(self.port), "--mode", mode, "--threads", str(threads)]
        if wrapper:
            cmd = wrapper + cmd
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        line = self.proc.stdout.readline()
        if not line.startswith("READY"):
            raise RuntimeError(f"{variant} did not start: {line!r}")
        self.pid = self.proc.pid if not wrapper else self._child_pid()

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

    def stop(self):
        os.kill(self.pid, signal.SIGINT)
        out, _ = self.proc.communicate(timeout=30)
        report = {}
        for line in out.splitlines():
            line = line.strip()
            if line.startswith("{"):
                report.update(json.loads(line))
        return report, out


def loadgen(build, mode, port, **kw):
    cmd = [str(build / "loadgen"), "--mode", mode, "--port", str(port)]
    for k, v in kw.items():
        cmd += [f"--{k}", str(v)]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=600).stdout
    return json.loads(out.strip().splitlines()[-1])


def measure(build, variant, mode, threads=1, **kw):
    srv = Server(build, variant, mode, threads)
    cpu0 = proc_cpu_seconds(srv.pid)
    rss0 = proc_rss_kb(srv.pid)
    t0 = time.monotonic()
    lg = loadgen(build, mode, srv.port, **kw)
    elapsed = time.monotonic() - t0
    cpu1 = proc_cpu_seconds(srv.pid)
    rss1 = proc_rss_kb(srv.pid)
    report, _ = srv.stop()
    ops_total = max(1, lg["ops"])
    lg.update(
        variant=variant,
        threads=threads,
        server_cpu_s=cpu1 - cpu0,
        # CPU over the whole loadgen run (warm-up included) divided by measured ops: an upper bound
        # that is identical in method for every variant.
        cpu_us_per_op=(cpu1 - cpu0) * 1e6 / ops_total,
        rss_kb=rss1,
        rss_delta_kb=rss1 - rss0,
        allocs=report.get("allocs", 0),
        alloc_bytes=report.get("alloc_bytes", 0),
        allocs_per_op=report.get("allocs", 0) / ops_total,
        wall_s=elapsed,
    )
    return lg


def repeat(n, fn):
    runs = [fn() for _ in range(n)]
    keys = ["ops_per_s", "mb_per_s", "p50_us", "p99_us", "p999_us", "cpu_us_per_op", "allocs_per_op", "rss_delta_kb", "rss_kb", "errors", "lost"]
    agg = {"runs": runs}
    for k in keys:
        vals = [r[k] for r in runs if k in r]
        if not vals:
            continue
        mean = statistics.fmean(vals)
        agg[k] = mean
        agg[k + "_cv"] = (statistics.pstdev(vals) / mean) if mean else 0.0
    for k in ("variant", "mode", "conns", "size", "threads"):
        agg[k] = runs[0].get(k)
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
        return [json.loads(subprocess.run([str(build / "bench_timers"), n], capture_output=True, text=True).stdout)
                for _ in range(reps)]

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
            S[name] = fn()
    return {"scenarios": S}


def diagnostics(build, quick):
    """Reactor counters (instrumented builds) and syscall profiles (strace -c)."""
    dur = 1 if quick else 2
    diag = {"stats": [], "syscalls": []}
    for v in ("stdnet_senders_stats", "stdnet_coroutines_stats"):
        for c, t in ((1, 1), (64, 1), (64, 2)):
            srv = Server(build, v, "echo", threads=t)
            lg = loadgen(build, "echo", srv.port, conns=c, size=64, duration=dur, warmup=0.3)
            report, _ = srv.stop()
            st = report.get("stats", {})
            ops = max(1, lg["ops"])
            diag["stats"].append({"variant": v, "conns": c, "threads": t, "ops": lg["ops"], "per_op": {k: val / ops for k, val in st.items()}, "raw": st})
    if shutil.which("strace"):
        for v in ("raw_epoll", "asio_callbacks", "stdnet_senders"):
            out_file = Path(f"/tmp/strace_{v}.txt")
            srv = Server(build, v, "echo", wrapper=["strace", "-c", "-f", "-o", str(out_file)])
            lg = loadgen(build, "echo", srv.port, conns=16, size=64, duration=dur, warmup=0)
            srv.stop()
            time.sleep(0.3)
            calls = {}
            for line in out_file.read_text().splitlines():
                parts = line.split()
                if len(parts) >= 5 and parts[0][0].isdigit() and not line.startswith("100.00"):
                    name = parts[-1]
                    count = int(parts[3])
                    calls[name] = count
            ops = max(1, lg["ops"])
            diag["syscalls"].append({"variant": v, "ops": lg["ops"], "per_op": {k: n / ops for k, n in sorted(calls.items(), key=lambda x: -x[1])}})
    sizes = subprocess.run([str(build / "bench_opstate_sizes")], capture_output=True, text=True).stdout
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
    (out / "results.json").write_text(json.dumps(results, indent=1))

    failures = [a for sc in ("echo", "http", "churn") for a in results["scenarios"].get(sc, []) if a.get("ops_per_s", 0) <= 0]
    if failures:
        log("FAILED scenarios:", [(f["variant"], f["mode"]) for f in failures])
        sys.exit(1)
    log("results written to", out / "results.json")


if __name__ == "__main__":
    main()
