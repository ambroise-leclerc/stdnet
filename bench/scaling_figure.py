#!/usr/bin/env python3
"""Draws docs/scaling.png (multi-thread echo throughput, #14) from a `run.py --only scaling` results.json.

    python3 bench/scaling_figure.py <results dir> docs/scaling.png [<baseline results dir>]

With a baseline (the same campaign before a change) stdnet's earlier curve is drawn dashed.
"""
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

sys.path.insert(0, str(Path(__file__).parent))
from stats import COLOR, LABEL  # noqa: E402

VARS = ["raw_epoll", "asio_callbacks", "stdnet_senders"]


def curve(rows, conns, v):
    pts = sorted((r["srv_threads"], r["ops_per_s"]) for r in rows
                 if r["variant"] == v and r["conns"] == conns and "ops_per_s" in r)
    return [p[0] for p in pts], [p[1] / 1e3 for p in pts]


def main(run_dir, out, base_dir=None):
    load = lambda d: json.loads((Path(d) / "results.json").read_text())  # noqa: E731
    R = load(run_dir)
    rows = R["scenarios"]["scaling"]
    base = load(base_dir)["scenarios"]["scaling"] if base_dir else None
    conns = sorted({r["conns"] for r in rows}, reverse=True)
    fig, axes = plt.subplots(1, len(conns), figsize=(5.4 * len(conns), 3.8), squeeze=False, sharey=True)
    for ax, c in zip(axes[0], conns):
        for v in VARS:
            x, y = curve(rows, c, v)
            ax.plot(x, y, marker="o", color=COLOR[v], label=LABEL[v], lw=2.2 if v == "stdnet_senders" else 1.6)
        if base:
            x, y = curve(base, c, "stdnet_senders")
            ax.plot(x, y, marker="o", ls="--", color=COLOR["stdnet_senders"], alpha=0.6, label="stdnet before #14")
        ax.set_title(f"echo, {c} connections, 64 B")
        ax.set_xlabel("server threads (one physical core each)")
        ax.set_xscale("log", base=2)
        ax.set_xticks(sorted({r["srv_threads"] for r in rows}), [str(t) for t in sorted({r["srv_threads"] for r in rows})])
        ax.grid(alpha=0.3)
    axes[0][0].set_ylabel("k messages / s")
    axes[0][-1].legend(fontsize=8, loc="upper left")
    meta = R["meta"]
    fig.suptitle(f"Multi-thread scaling — {meta['cpu']}, load generator on separate cores, "
                 f"mean of {meta['reps']} runs", fontsize=10)
    fig.tight_layout()
    fig.savefig(out, dpi=140)


if __name__ == "__main__":
    main(*sys.argv[1:4])
