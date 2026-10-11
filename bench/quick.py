#!/usr/bin/env python3
"""Fast benchmark for iterating on optimisations: about one minute.

    python3 bench/quick.py --build after=build-bench/bench                 # one build
    python3 bench/quick.py --build before=../base/build/bench --build after=build-bench/bench   # A/B

The full campaign (bench/run.py, an hour) answers "how good is stdnet"; this answers "did my
change help". It keeps what makes a comparison trustworthy and drops the rest:

* a handful of points (echo 16 connections, HTTP, UDP, optionally echo 256) with short windows
  (1 s after 0.3 s of warm-up) and the same validation as run.py;
* **interleaved** runs (every repetition visits every point, build and variant in turn) so that a
  drift of the machine hits all of them alike instead of the build that happened to run last;
* Asio and the raw epoll loop as controls: their ratio between builds measures the noise floor,
  and a stdnet change smaller than that is not a result;
* with two builds, a Welch test and a 95 % CI of the after/before ratio of every metric;
* allocations and (instrumented build only) reactor counters per operation, which are exact.

Defaults: 3 repetitions. With 3 runs only differences above about 10 % can be established; use
`--reps 6` (about two minutes) for finer effects, and the full campaign to publish numbers.
"""
import argparse
import json
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import run  # noqa: E402
from report import welch  # noqa: E402

POINTS = {
    "echo16": ("echo", dict(conns=16, size=64)),
    "echo256": ("echo", dict(conns=256, size=64)),
    "echo1": ("echo", dict(conns=1, size=64)),
    "http": ("http", dict(conns=64)),
    "udp": ("udp", dict(conns=4, size=64, window=16)),
}
METRICS = [("cpu_us_per_op", "CPU µs/op", 2), ("p99_us", "p99 µs", 0), ("allocs_per_op", "allocs/op", 2)]
CONTROLS = {"asio_callbacks", "asio_coroutines", "raw_epoll", "raw_poll", "raw_blocking"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", action="append", required=True, metavar="LABEL=DIR",
                    help="build directory of the bench binaries (repeat for an A/B comparison, first = before)")
    ap.add_argument("--variants", default="stdnet_senders,stdnet_coroutines,asio_callbacks,raw_epoll")
    ap.add_argument("--points", default="echo16,http,udp")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--duration", type=float, default=1.0)
    ap.add_argument("--warmup", type=float, default=0.3)
    ap.add_argument("--json", help="write the raw runs here")
    args = ap.parse_args()

    builds = []
    for b in args.build:
        label, _, d = b.partition("=")
        builds.append((label, Path(d).resolve()))
    variants = args.variants.split(",")
    points = args.points.split(",")
    t0 = time.monotonic()
    runs = {}  # (point, label, variant) -> [run]
    total = args.reps * len(points) * len(builds) * len(variants)
    done = 0
    for rep in range(args.reps):
        for pt in points:
            mode, kw = POINTS[pt]
            for label, build in builds:
                for v in variants:
                    r = run.measure(build, v, mode, duration=args.duration, warmup=args.warmup, **kw)
                    runs.setdefault((pt, label, v), []).append(r)
                    done += 1
        print(f"  repetition {rep + 1}/{args.reps} done ({time.monotonic() - t0:.0f} s)", file=sys.stderr, flush=True)

    def vals(pt, label, v, m):
        return [r[m] for r in runs.get((pt, label, v), []) if not r.get("problems") and m in r]

    out = []
    for pt in points:
        out.append(f"\n{pt}  ({POINTS[pt][0]}, {POINTS[pt][1]})")
        out.append(f"  {'variant':22}" + "".join(f"  {'CPU µs/op':>16}  {'p99 µs':>9}  {'allocs/op':>9}" for _ in builds)
                   + ("   after/before CPU (95 % CI, p)" if len(builds) == 2 else ""))
        for v in variants:
            cells = []
            for label, _ in builds:
                row = []
                for m, _, d in METRICS:
                    xs = vals(pt, label, v, m)
                    if not xs:
                        row.append("-")
                    elif m == "cpu_us_per_op" and len(xs) > 1:
                        row.append(f"{statistics.fmean(xs):.2f} ±{statistics.stdev(xs):.2f}")
                    else:
                        row.append(f"{statistics.fmean(xs):.{d}f}")
                cells.append(f"  {row[0]:>16}  {row[1]:>9}  {row[2]:>9}")
            line = f"  {v + (' (control)' if v in CONTROLS else ''):22}" + "".join(cells)
            if len(builds) == 2:
                w = welch(vals(pt, builds[1][0], v, "cpu_us_per_op"), vals(pt, builds[0][0], v, "cpu_us_per_op"))
                line += f"   {w[0]:.2f} ({w[1][0]:.2f}–{w[1][1]:.2f}, p={w[2]:.3f})" if w else "   -"
            out.append(line)
    if len(builds) == 2:
        ctl = [welch(vals(pt, builds[1][0], v, "cpu_us_per_op"), vals(pt, builds[0][0], v, "cpu_us_per_op"))
               for pt in points for v in variants if v in CONTROLS]
        ctl = [c[0] for c in ctl if c]
        if ctl:
            out.append(f"\nControls (unchanged code) moved by {min(ctl):.2f}–{max(ctl):.2f}× between the two builds: that is the "
                       f"noise floor of this run; a stdnet ratio inside it is not a result.")
    out.append(f"\n{done} runs in {time.monotonic() - t0:.0f} s; "
               f"{len(run.PROBLEMS)} invalid run(s).")
    print("\n".join(out))
    if args.json:
        Path(args.json).write_text(json.dumps({f"{k[0]}|{k[1]}|{k[2]}": v for k, v in runs.items()}, indent=1))
    sys.exit(1 if run.PROBLEMS else 0)


if __name__ == "__main__":
    main()
