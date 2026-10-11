#!/usr/bin/env python3
"""Draws docs/infographic.png (the summary figure of the README) from a results.json.

    python3 bench/infographic.py bench/results/2026-10-11 docs/infographic.png
"""
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch  # noqa: E402

sys.path.insert(0, str(Path(__file__).parent))
from report import COLOR, LABEL, one, samples, welch  # noqa: E402

SURFACE, INK, INK2, GRID, TILE = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df", "#f3f2ee"
VARS = ["raw_poll", "raw_epoll", "asio_callbacks", "stdnet_senders"]


def main(run_dir, out):
    R = json.loads((Path(run_dir) / "results.json").read_text())
    S, meta = R["scenarios"], R["meta"]

    scen = [
        ("Echo\n16 conns · 64 B", "echo", dict(conns=16, size=64)),
        ("Echo\n256 conns · 64 B", "echo", dict(conns=256, size=64)),
        ("HTTP\n64 conns", "http", {}),
        ("UDP\n64 B", "udp", dict(size=64)),
        ("Connection\nchurn", "churn", {}),
    ]
    g = lambda sc, v, **f: one(S[sc], variant=v, **f)  # noqa: E731
    r_poll = welch(samples(g("echo", "stdnet_senders", conns=16, size=64), "cpu_us_per_op"),
                   samples(g("echo", "raw_poll", conns=16, size=64), "cpu_us_per_op"))
    r_p99 = welch(samples(g("http", "stdnet_senders"), "p99_us"), samples(g("http", "asio_callbacks"), "p99_us"))
    allocs = g("echo", "stdnet_senders", conns=16, size=64)["allocs_per_op"]
    posts = {d["variant"]: d["per_op"]["posts"] for d in R["diagnostics"]["stats"] if d["conns"] == 64 and d["threads"] == 1}
    T = S["timers"]
    tstart = welch([x["stdnet"]["start_ms"] for x in T], [x["asio"]["start_ms"] for x in T])[0]
    tcancel = welch([x["stdnet"]["cancel_ms"] for x in T], [x["asio"]["cancel_ms"] for x in T])[0]

    plt.rcParams.update({"font.family": "DejaVu Sans", "text.color": INK})
    fig = plt.figure(figsize=(12, 9.2), facecolor=SURFACE)
    fig.text(0.04, 0.955, "stdnet vs BSD sockets and Asio", fontsize=22, fontweight="bold", va="center")
    fig.text(0.04, 0.917, f"Server CPU per operation, loopback, {meta['cores']} cores, {meta['reps']} runs per point · "
             "Linux, GCC 13, Asio 1.38.2 · lower is better", fontsize=10.5, color=INK2, va="center")

    # headline tiles
    tiles = [
        (f"+{(r_poll[0] - 1) * 100:.0f} %", "CPU per small echo message\nvs raw poll(2) sockets, 16 conns",
         f"95 % CI {r_poll[1][0]:.2f}–{r_poll[1][1]:.2f}×, significant"),
        (f"{allocs:.1f}", "heap allocations per operation\n(raw sockets and Asio: 0)", "reactor map node, issue #11"),
        (f"{r_p99[0]:.1f}×", "HTTP p99 latency vs Asio\n64 connections", f"CI {r_p99[1][0]:.1f}–{r_p99[1][1]:.1f}×, cause unknown"),
        (f"{posts['stdnet_coroutines_stats'] / posts['stdnet_senders_stats']:.0f}×", "completions posted by\nstd::execution::task vs senders", "issue #12"),
    ]
    for i, (big, label, sub) in enumerate(tiles):
        x0 = 0.04 + i * 0.2375
        fig.add_artist(FancyBboxPatch((x0, 0.745), 0.222, 0.135, boxstyle="round,pad=0,rounding_size=0.008",
                                      transform=fig.transFigure, facecolor=TILE, edgecolor="none"))
        fig.text(x0 + 0.014, 0.842, big, fontsize=26, fontweight="bold", va="center")
        fig.text(x0 + 0.014, 0.794, label, fontsize=8.6, color=INK, va="center", linespacing=1.3)
        fig.text(x0 + 0.014, 0.757, sub, fontsize=7.8, color=INK2, va="center")

    # main chart
    ax = fig.add_axes([0.075, 0.30, 0.9, 0.38], facecolor=SURFACE)
    n = len(VARS)
    w = 0.8 / n
    for vi, v in enumerate(VARS):
        ys, es, xs = [], [], []
        for si, (_, sc, f) in enumerate(scen):
            r = g(sc, v, **f)
            ys.append(r["cpu_us_per_op"])
            es.append(1.96 * r.get("cpu_us_per_op_sd", 0) / max(1, r["valid_runs"]) ** 0.5)
            xs.append(si + (vi - (n - 1) / 2) * w)
        ax.bar(xs, ys, w * 0.9, color=COLOR[v], linewidth=0, yerr=es, error_kw=dict(ecolor=INK2, elinewidth=0.8, capsize=2),
               label=LABEL[v])
        for x, y, e in zip(xs, ys, es):
            ax.text(x, y + e + 0.4, f"{y:.1f}", ha="center", va="bottom", fontsize=7.5, color=INK2)
    ax.set_xticks(range(len(scen)), [s[0] for s in scen], fontsize=9)
    ax.set_ylabel("server CPU µs per operation", color=INK2, fontsize=9)
    ax.set_ylim(0, 31)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(colors=INK2, length=0)
    ax.legend(loc="upper left", ncol=4, frameon=False, fontsize=9, bbox_to_anchor=(0, 1.1))
    ax.text(4.0, 30.0, "churn = per connection", fontsize=7.5, color=INK2, ha="center")

    # bottom panels
    def panel(x0, w_, title, lines):
        fig.add_artist(FancyBboxPatch((x0, 0.04), w_, 0.215, boxstyle="round,pad=0,rounding_size=0.008",
                                      transform=fig.transFigure, facecolor=TILE, edgecolor="none"))
        fig.text(x0 + 0.014, 0.232, title, fontsize=11, fontweight="bold", va="center")
        for i, (ok, t) in enumerate(lines):
            fig.text(x0 + 0.014, 0.198 - i * 0.034, ("▲ " if ok is False else "● " if ok is None else "▼ ") + t,
                     fontsize=8.8, color=INK, va="center")

    panel(0.04, 0.45, "Where stdnet is within noise (n = 6, corrected tests)",
          [(None, "Echo at 256 connections, HTTP and UDP CPU, 64 KB messages, churn"),
           (None, "Versus Asio callbacks: no significant CPU difference in any scenario"),
           (None, "Latency at one connection: p50 52.5 µs vs 50.9 µs (raw)"),
           (None, "Memory of an idle connection: 4.7 KB vs Asio 4.6 KB (raw: 0.1 KB)"),
           (None, "Sender server code size 102 KB vs Asio 118 KB")])
    panel(0.5, 0.46, "Measured differences, with their cause",
          [(False, f"Timer start {tstart:.1f}× slower than Asio (100 000 timers)"),
           (True, f"Timer cancellation {1 / tcancel:.1f}× faster (stop tokens)"),
           (False, "poll backend beats epoll at 16 conns: ADD + DEL per wait"),
           (False, "Compile time 13.2 s vs 6.6 s per translation unit"),
           (None, "Winsock not measured: the suite is Linux-only")])
    fig.text(0.04, 0.018, "▲ stdnet worse   ▼ stdnet better   ● no significant difference · bars: mean ± 1.96 standard errors · "
             "details and tests: docs/PERF.md", fontsize=8, color=INK2, va="center")
    fig.savefig(out, dpi=130, facecolor=SURFACE)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
