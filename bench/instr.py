#!/usr/bin/env python3
"""Deterministic user-space cost per operation, with valgrind/callgrind: seconds, no noise.

    python3 bench/instr.py --build build-bench/bench                       # echo 16, HTTP, UDP
    python3 bench/instr.py --build before=DIR --build after=DIR --top 12   # A/B with hot spots

Wall-clock CPU time on a shared machine varies by 10 % from run to run, and on loopback about 90 %
of it is the kernel (see docs/PERF.md), so it cannot resolve a change in the library's own code.
This script runs the server under callgrind with instrumentation switched on only inside the load
generator's measurement window, and reports **instructions executed in user space per operation**
(`Ir/op`): exact for a given binary and workload to well under 1 %. It also prints the functions
that cost the most per operation, which is where to look first.

It measures what the library controls, not the kernel or the cost of waking a thread; confirm a
promising change with bench/quick.py (end to end) before keeping it. Needs valgrind. The server
is about 20x slower under callgrind, so the windows are short and the load light.
"""
import argparse
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import run  # noqa: E402

POINTS = {
    "echo16": ("echo", dict(conns=16, size=64)),
    "echo256": ("echo", dict(conns=256, size=64)),
    "http": ("http", dict(conns=64)),
    "udp": ("udp", dict(conns=4, size=64, window=16)),
    "echo64k": ("echo", dict(conns=16, size=65536)),
}


def one_run(build, variant, mode, kw, duration, warmup, top):
    tmp = tempfile.mkdtemp(prefix="callgrind_")
    out = Path(tmp) / "cg.out"
    wrapper = ["valgrind", "--tool=callgrind", "--instr-atstart=no", f"--callgrind-out-file={out}",
               "--dump-instr=no", "--quiet"]
    srv = run.Server(build, variant, mode, 1, wrapper=wrapper)
    pid = srv.pid
    p = subprocess.Popen(run.loadgen_cmd(build, mode, srv.port, hold=1, duration=duration, warmup=warmup, **kw),
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    lg = {}
    for line in p.stdout:
        line = line.strip()
        if line == "MEASURE_START":
            subprocess.run(["callgrind_control", "-i", "on", str(pid)], capture_output=True)
        elif line == "MEASURE_END":
            subprocess.run(["callgrind_control", "-i", "off", str(pid)], capture_output=True)
            p.stdin.write("go\n")
            p.stdin.flush()
        elif line.startswith("{"):
            lg = run.json.loads(line)
    p.wait(timeout=120)
    srv.stop()
    time.sleep(0.5)
    ops = lg.get("ops", 0)
    files = sorted(Path(tmp).glob("cg.out*"))
    if not ops or not files:
        return None
    text = subprocess.run(["callgrind_annotate", "--threshold=100", str(files[0])], capture_output=True, text=True).stdout
    total = None
    funcs = []
    for l in text.splitlines():
        m = re.match(r"\s*([\d,]+)\s+\(\s*[\d.]+%\)\s+PROGRAM TOTALS", l)
        if m:
            total = int(m.group(1).replace(",", ""))
        m = re.match(r"\s*([\d,]+)\s+\(\s*([\d.]+)%\)\s+(.*)", l)
        if m and "PROGRAM TOTALS" not in l and "file:function" not in l:
            funcs.append((int(m.group(1).replace(",", "")), m.group(3)))
    if total is None:
        return None
    return {"ir_per_op": total / ops, "ops": ops, "funcs": [(c / ops, n) for c, n in funcs[:top]]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", action="append", required=True, metavar="LABEL=DIR")
    ap.add_argument("--variants", default="stdnet_senders,asio_callbacks,raw_epoll")
    ap.add_argument("--points", default="echo16,http,udp")
    ap.add_argument("--duration", type=float, default=1.0)
    ap.add_argument("--warmup", type=float, default=0.5)
    ap.add_argument("--top", type=int, default=0, help="also print the N most expensive functions per operation")
    args = ap.parse_args()
    builds = [(b.partition("=")[0], Path(b.partition("=")[2]).resolve()) if "=" in b else ("build", Path(b).resolve())
              for b in args.build]
    t0 = time.monotonic()
    print(f"user-space instructions per operation (callgrind), {len(builds)} build(s)")
    for pt in args.points.split(","):
        mode, kw = POINTS[pt]
        print(f"\n{pt}")
        for v in args.variants.split(","):
            cells = []
            for label, build in builds:
                r = one_run(build, v, mode, kw, args.duration, args.warmup, args.top)
                cells.append((label, r))
            line = f"  {v:20}" + "".join(f"  {label}: {r['ir_per_op']:8.0f}" if r else f"  {label}:      n/a" for label, r in cells)
            if len(cells) == 2 and all(r for _, r in cells):
                line += f"   ratio {cells[1][1]['ir_per_op'] / cells[0][1]['ir_per_op']:.3f}"
            print(line, flush=True)
            if args.top:
                for label, r in cells:
                    if r:
                        for c, n in r["funcs"]:
                            print(f"      [{label}] {c:7.1f}  {n[:110]}")
    print(f"\n{time.monotonic() - t0:.0f} s")


if __name__ == "__main__":
    main()
