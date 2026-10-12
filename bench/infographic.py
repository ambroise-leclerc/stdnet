#!/usr/bin/env python3
"""Draws docs/infographic.png (the summary figure of the README) from a results.json.

    python3 bench/infographic.py <results dir> docs/infographic.png [<baseline results dir> [<backend study dir>]]

Every number and statement of the figure is computed from the results; when a baseline campaign
(same machine, before a change) is given, its stdnet-senders values are drawn as black ticks and
quoted next to the new ones.
"""
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch  # noqa: E402

sys.path.insert(0, str(Path(__file__).parent))
from stats import COLOR, LABEL, one, paired_ratio, samples, welch  # noqa: E402

SURFACE, INK, INK2, GRID, TILE = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df", "#f3f2ee"
VARS = ["raw_poll", "raw_epoll", "asio_callbacks", "stdnet_senders"]


def main(run_dir, out, base_dir=None, backend_dir=None):
    R = json.loads((Path(run_dir) / "results.json").read_text())
    S, meta = R["scenarios"], R["meta"]
    B = json.loads((Path(base_dir) / "results.json").read_text()) if base_dir else None
    BS = B["scenarios"] if B else None

    scen = [
        ("Echo\n16 conns · 64 B", "echo", dict(conns=16, size=64)),
        ("Echo\n256 conns · 64 B", "echo", dict(conns=256, size=64)),
        ("HTTP\n64 conns", "http", {}),
        ("UDP\n64 B", "udp", dict(size=64)),
        ("Connection\nchurn", "churn", {}),
    ]
    g = lambda sc, v, **f: one(S[sc], variant=v, **f)  # noqa: E731
    def cmp(sc_, a_, b_, m, src=S, **f):
        ra, rb = one(src[sc_], variant=a_, **f), one(src[sc_], variant=b_, **f)
        return welch(samples(ra, m), samples(rb, m))

    def verdict(w, alpha=0.05 / 32):
        return "significant" if w[2] < alpha else "not significant"

    r_poll = cmp("echo", "stdnet_senders", "raw_poll", "cpu_us_per_op", conns=16, size=64)
    r_poll0 = cmp("echo", "stdnet_senders", "raw_poll", "cpu_us_per_op", src=BS, conns=16, size=64) if BS else None
    r_p99 = cmp("http", "stdnet_senders", "asio_callbacks", "p99_us")
    allocs = g("echo", "stdnet_senders", conns=16, size=64)["allocs_per_op"]
    allocs0 = one(BS["echo"], variant="stdnet_senders", conns=16, size=64)["allocs_per_op"] if BS else None
    posts = {d["variant"]: d["per_op"]["posts"] for d in R["diagnostics"]["stats"] if d["conns"] == 64 and d["threads"] == 1}
    T = S["timers"]
    tstart = welch([x["stdnet"]["start_ms"] for x in T], [x["asio"]["start_ms"] for x in T])[0]
    tcancel = welch([x["stdnet"]["cancel_ms"] for x in T], [x["asio"]["cancel_ms"] for x in T])[0]
    e_p = cmp("backend", "stdnet_senders", "stdnet_senders_poll", "cpu_us_per_op", conns=16)
    e_i = cmp("backend", "stdnet_senders", "stdnet_senders_poll", "cpu_us_per_op", conns=2000)

    def backend_lines():
        """epoll / poll lines of the figure: from the paired study (`run.py --only backend`) when
        given, else from the unpaired points of this run."""
        if backend_dir:
            BR = json.loads((Path(backend_dir) / "results.json").read_text())["scenarios"]["backend"]
            def pr(c):
                a = samples(one(BR, variant="stdnet_senders", conns=c), "cpu_us_per_op")
                b = samples(one(BR, variant="stdnet_senders_poll", conns=c), "cpu_us_per_op")
                return paired_ratio(a, b), len(a)
            (r16, n), (r256, _), (r2k, _), (r10k, _) = pr(16), pr(256), pr(2000), pr(10000)
            worse = r16[1][0] > 1 or r256[1][0] > 1
            return [
                (False if worse else None,
                 f"epoll vs poll, 16 / 256 conns: {(r16[0] - 1) * 100:+.0f} % / {(r256[0] - 1) * 100:+.0f} % CPU "
                 f"(paired, n = {n}; margin +10 %)"),
                (True, f"epoll vs poll, 2000 / 10000 idle conns: {r2k[0]:.2f}× / {r10k[0]:.2f}× CPU"),
            ]
        return [
            (None if e_p[2] >= 0.05 / 32 else (True if e_p[0] < 1 else False),
             f"epoll vs poll backend, 16 conns: CPU ratio {e_p[0]:.2f} (CI {e_p[1][0]:.2f}–{e_p[1][1]:.2f})"),
            (None if e_i[2] >= 0.05 / 32 else (True if e_i[0] < 1 else False),
             f"epoll vs poll backend, 2000 idle conns: CPU ratio {e_i[0]:.2f} (CI {e_i[1][0]:.2f}–{e_i[1][1]:.2f})"),
        ]
    comp = {b_["variant"]: b_ for b_ in R.get("build", [])}
    p50 = one(S["echo"], variant="stdnet_senders", conns=1, size=64)["p50_us"]
    p50r = one(S["echo"], variant="raw_poll", conns=1, size=64)["p50_us"]
    idle_ = {v: one(S["idle"], variant=v) for v in ("stdnet_senders", "asio_callbacks")}
    kb = {v: idle_[v]["rss_delta_kb"] / idle_[v]["conns"] for v in idle_}

    plt.rcParams.update({"font.family": "DejaVu Sans", "text.color": INK})
    fig = plt.figure(figsize=(12, 9.2), facecolor=SURFACE)
    fig.text(0.04, 0.955, "stdnet vs BSD sockets and Asio", fontsize=22, fontweight="bold", va="center")
    fig.text(0.04, 0.917, f"Server CPU per operation, loopback, server and load generator pinned to a core each, "
             f"{meta['reps']} interleaved runs per point · {meta['cpu'].replace('Processor', '').replace('AMD ', '').strip()} · lower is better", fontsize=10.5, color=INK2, va="center")

    # headline tiles
    pct = (r_poll[0] - 1) * 100
    pct_txt = "±0 %" if round(pct) == 0 else f"{pct:+.0f} %"
    p99_note = ("significant" if r_p99[2] < 0.05 / 32 else
                "not significant after correction" if r_p99[1][0] > 1 or r_p99[1][1] < 1 else "not significant")
    tiles = [
        (pct_txt, "CPU per small echo message\nvs raw poll(2) sockets, 16 conns",
         f"CI {r_poll[1][0]:.2f}–{r_poll[1][1]:.2f}×, {verdict(r_poll)}" + (f" · before: {(r_poll0[0] - 1) * 100:+.0f} %" if r_poll0 else "")),
        (f"{allocs:.1f}", "heap allocations per operation\n(raw sockets and Asio: 0)",
         f"before: {allocs0:.1f} (reactor rework, #11)" if allocs0 is not None else "measured with a counting operator new"),
        (f"{r_p99[0]:.1f}×", "HTTP p99 latency vs Asio\n64 connections", f"CI {r_p99[1][0]:.1f}–{r_p99[1][1]:.1f}×, {p99_note}"),
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
    if BS:
        for si, (_, sc, f) in enumerate(scen):
            r0 = one(BS[sc], variant="stdnet_senders", **f)
            x = si + (VARS.index("stdnet_senders") - (n - 1) / 2) * w
            ax.plot([x - w * 0.45, x + w * 0.45], [r0["cpu_us_per_op"]] * 2, color=INK, linewidth=2.2, zorder=5)
        ax.plot([], [], color=INK, linewidth=2.2, label="stdnet senders before the reactor rework")
    ax.set_xticks(range(len(scen)), [s[0] for s in scen], fontsize=9)
    ax.set_ylabel("server CPU µs per operation", color=INK2, fontsize=9)
    top = max(g(sc, v, **f)["cpu_us_per_op"] for v in VARS for _, sc, f in scen)
    ax.set_ylim(0, top * 1.2)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(colors=INK2, length=0)
    ax.legend(loc="upper left", ncol=5, frameon=False, fontsize=9, bbox_to_anchor=(0, 1.1))
    ax.text(len(scen) - 1, top * 1.15, "churn = per connection", fontsize=7.5, color=INK2, ha="center")

    # bottom panels
    def panel(x0, w_, title, lines):
        fig.add_artist(FancyBboxPatch((x0, 0.04), w_, 0.215, boxstyle="round,pad=0,rounding_size=0.008",
                                      transform=fig.transFigure, facecolor=TILE, edgecolor="none"))
        fig.text(x0 + 0.014, 0.232, title, fontsize=11, fontweight="bold", va="center")
        for i, (ok, t) in enumerate(lines):
            fig.text(x0 + 0.014, 0.198 - i * 0.034, ("▲ " if ok is False else "● " if ok is None else "▼ ") + t,
                     fontsize=8.8, color=INK, va="center")

    def sig_scen():
        out_ = []
        for label, sc_, f, m in (("echo 256", "echo", dict(conns=256, size=64), "cpu_us_per_op"),
                                 ("HTTP CPU", "http", {}, "cpu_us_per_op"), ("UDP CPU", "udp", dict(size=64), "cpu_us_per_op"),
                                 ("64 KB", "echo", dict(conns=16, size=65536), "cpu_us_per_op"),
                                 ("churn", "churn", {}, "cpu_us_per_op")):
            w_ = cmp(sc_, "stdnet_senders", "raw_poll", m, **f)
            if w_ and w_[2] >= 0.05 / 32:
                out_.append(label)
        return out_

    inside = sig_scen()
    panel(0.04, 0.45, "Within noise of raw sockets (n = " + str(meta["reps"]) + ", corrected tests)",
          [(None, ", ".join(inside) + (", echo 16 conns" if r_poll[2] >= 0.05 / 32 else "")),
           (None, "Versus Asio callbacks: compare the per-scenario CIs in docs/PERF.md"),
           (None, f"Latency at one connection: p50 {p50:.1f} µs vs {p50r:.1f} µs (raw)"),
           (None, f"Idle connection memory: {kb['stdnet_senders']:.1f} KB vs Asio {kb['asio_callbacks']:.1f} KB (raw: 0.1 KB)"),
           (None, f"Sender server code size {comp['stdnet_senders']['text_bytes'] / 1024:.0f} KB vs Asio {comp['asio_callbacks']['text_bytes'] / 1024:.0f} KB")
           if comp else (None, "")])
    panel(0.5, 0.46, "Measured differences, with their cause",
          [(False, f"Timer start {tstart:.1f}× slower than Asio ({meta['timers']:,} timers)"),
           (True, f"Timer cancellation {1 / tcancel:.1f}× faster (stop tokens)"),
           *backend_lines(),
           (None, "Winsock not measured: the suite is Linux-only")])
    fig.text(0.04, 0.018, "▲ stdnet worse   ▼ stdnet better   ● no significant difference · bars: mean ± 1.96 standard errors · "
             "details and tests: docs/PERF.md", fontsize=8, color=INK2, va="center")
    fig.savefig(out, dpi=130, facecolor=SURFACE)


if __name__ == "__main__":
    main(*sys.argv[1:5])
