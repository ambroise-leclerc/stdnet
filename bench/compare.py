#!/usr/bin/env python3
"""Compares two campaigns made on the same machine (before / after a change).

    python3 bench/compare.py bench/results/<before> bench/results/<after> [out.md]

For each point it prints the mean of a metric in both runs, the ratio after/before with its 95 %
confidence interval and Welch's p-value (Bonferroni-corrected over all the comparisons made).
Implementations that did not change (Asio, raw sockets) are included as a control: a ratio far
from 1 there means the machine drifted between the two campaigns.
"""
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from report import LABEL, pick, samples, welch  # noqa: E402

POINTS = [  # (label, scenario, filter, metric)
    ("echo 1 conn × 64 B", "echo", dict(conns=1, size=64), "cpu_us_per_op"),
    ("echo 16 conns × 64 B", "echo", dict(conns=16, size=64), "cpu_us_per_op"),
    ("echo 256 conns × 64 B", "echo", dict(conns=256, size=64), "cpu_us_per_op"),
    ("echo 16 conns × 64 KB", "echo", dict(conns=16, size=65536), "cpu_us_per_op"),
    ("HTTP 64 conns", "http", {}, "cpu_us_per_op"),
    ("UDP 64 B", "udp", dict(size=64), "cpu_us_per_op"),
    ("churn", "churn", {}, "cpu_us_per_op"),
    ("10 000 idle conns", "idle", {}, "cpu_us_per_op"),
    ("echo 16 conns × 64 B, p99 (µs)", "echo", dict(conns=16, size=64), "p99_us"),
    ("echo 256 conns × 64 B, p99 (µs)", "echo", dict(conns=256, size=64), "p99_us"),
    ("HTTP 64 conns, p99 (µs)", "http", {}, "p99_us"),
]
VARIANTS = ["stdnet_senders", "stdnet_coroutines", "asio_callbacks", "raw_epoll"]


def main(before, after, out=None):
    A = json.loads((Path(before) / "results.json").read_text())["scenarios"]
    B = json.loads((Path(after) / "results.json").read_text())["scenarios"]
    rows, n = [], 0
    for label, sc, flt, metric in POINTS:
        for v in VARIANTS:
            ra, rb = pick(A.get(sc, []), variant=v, **flt), pick(B.get(sc, []), variant=v, **flt)
            if not ra or not rb:
                continue
            a, b = samples(ra[0], metric), samples(rb[0], metric)
            w = welch(b, a)  # after / before
            if w:
                n += 1
                rows.append((label, v, statistics.fmean(a), statistics.fmean(b), w))
    alpha = 0.05 / max(n, 1)
    lines = [f"Ratio = after / before (lower is better); {n} comparisons, significant if p < {alpha:.4f} (Bonferroni).", "",
             "| Point | Implementation | before | after | ratio | 95 % CI | p | verdict |", "|---|---|---|---|---|---|---|---|"]
    for label, v, ma, mb, (r, (lo, hi), p) in rows:
        verdict = "not significant" if p >= alpha else ("faster" if r < 1 else "slower")
        lines.append(f"| {label} | {LABEL.get(v, v)} | {ma:.2f} | {mb:.2f} | {r:.2f} | {lo:.2f} – {hi:.2f} | {p:.4f} | {verdict} |")
    lines += ["", "Allocations per operation (deterministic, no test): " +
              ", ".join(f"{label} {v.replace('stdnet_', '')} {statistics.fmean(samples(pick(A.get(sc, []), variant=v, **flt)[0], 'allocs_per_op')):.2f} → "
                        f"{statistics.fmean(samples(pick(B.get(sc, []), variant=v, **flt)[0], 'allocs_per_op')):.2f}"
                        for label, sc, flt, m in POINTS[:1] + POINTS[1:2] + POINTS[4:6] for v in ("stdnet_senders",))]
    text = "\n".join(lines) + "\n"
    if out:
        Path(out).write_text(text)
    print(text)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
