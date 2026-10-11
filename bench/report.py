#!/usr/bin/env python3
"""Turns bench/results/<run>/results.json into Markdown tables and PNG charts.

    python3 bench/report.py bench/results/2026-10-11

Writes <run>/tables.md and <run>/fig_*.png. Colours: one fixed hue per implementation
(validated categorical palette, slots 1-6), identical in every chart.
"""
import json
import math
import statistics
import sys
from pathlib import Path

from scipy import stats as sps

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ORDER = ["raw_blocking", "raw_poll", "raw_epoll", "asio_callbacks", "asio_coroutines", "stdnet_senders", "stdnet_coroutines"]
LABEL = {
    "raw_blocking": "raw blocking",
    "raw_poll": "raw poll (BSD sockets)",
    "raw_epoll": "raw epoll",
    "asio_callbacks": "Asio callbacks",
    "asio_coroutines": "Asio coroutines",
    "stdnet_senders": "stdnet senders",
    "stdnet_coroutines": "stdnet task",
    "stdnet_senders_poll": "stdnet senders (poll)",
}
COLOR = {
    "raw_blocking": "#2a78d6",
    "raw_poll": "#8b6b4a",
    "raw_epoll": "#eb6834",
    "asio_callbacks": "#1baf7a",
    "asio_coroutines": "#eda100",
    "stdnet_senders": "#e87ba4",
    "stdnet_coroutines": "#008300",
    "stdnet_senders_poll": "#6250d6",
}
SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID, "axes.labelcolor": INK2, "text.color": INK, "xtick.color": INK2, "ytick.color": INK2,
    "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True, "axes.grid.axis": "y",
    "grid.color": GRID, "grid.linewidth": 0.8, "font.size": 9, "axes.titlesize": 10, "axes.titleweight": "bold",
    "legend.frameon": False, "axes.axisbelow": True,
})


def fmt(v, d=0):
    if v is None:
        return "-"
    if abs(v) >= 1000 and d == 0:
        return f"{v:,.0f}".replace(",", " ")
    return f"{v:.{d}f}"


def pick(rows, **kw):
    return [r for r in rows if all(r.get(k) == v for k, v in kw.items())]


def one(rows, **kw):
    m = pick(rows, **kw)
    return m[0] if m else None


def samples(row, key):
    """Per-run values of a metric (valid runs only)."""
    return [r[key] for r in row.get("runs", []) if not r.get("problems") and key in r]


def welch(a, b):
    """Welch's t-test on two samples. Returns (ratio a/b, 95 % CI of the ratio of means, p-value),
    or None when a sample has fewer than 2 values. The CI uses the delta method on the means."""
    if len(a) < 2 or len(b) < 2:
        return None
    ma, mb = statistics.fmean(a), statistics.fmean(b)
    if mb == 0:
        return None
    va, vb = statistics.variance(a) / len(a), statistics.variance(b) / len(b)
    if va + vb == 0:
        return (ma / mb, (ma / mb, ma / mb), 0.0 if ma != mb else 1.0)
    t, p = sps.ttest_ind(a, b, equal_var=False)
    df = (va + vb) ** 2 / (va ** 2 / (len(a) - 1) + vb ** 2 / (len(b) - 1))
    tcrit = sps.t.ppf(0.975, df)
    ratio = ma / mb
    se = math.sqrt(va / mb ** 2 + ma ** 2 * vb / mb ** 4)
    return ratio, (ratio - tcrit * se, ratio + tcrit * se), float(p)


COMPARISONS = [  # (label, scenario, filter, metric, unit, lower_is_better)
    ("echo 16 conns × 64 B, server CPU/msg", "echo", dict(conns=16, size=64), "cpu_us_per_op"),
    ("echo 256 conns × 64 B, server CPU/msg", "echo", dict(conns=256, size=64), "cpu_us_per_op"),
    ("echo 16 conns × 64 KB, server CPU/msg", "echo", dict(conns=16, size=65536), "cpu_us_per_op"),
    ("echo 1 conn × 64 B, p50 latency", "echo", dict(conns=1, size=64), "p50_us"),
    ("HTTP 64 conns, server CPU/request", "http", {}, "cpu_us_per_op"),
    ("HTTP 64 conns, p99 latency", "http", {}, "p99_us"),
    ("UDP 64 B, server CPU/datagram", "udp", dict(size=64), "cpu_us_per_op"),
    ("churn, server CPU/connection", "churn", {}, "cpu_us_per_op"),
]
PAIRS = [("stdnet_senders", "asio_callbacks"), ("stdnet_senders", "raw_poll"), ("stdnet_senders", "raw_epoll"),
         ("stdnet_coroutines", "asio_coroutines")]


def significance(S, md):
    """Ratio stdnet / reference with a 95 % CI and Welch's p-value, Bonferroni-corrected over all comparisons."""
    rows = []
    for label, sc, flt, metric in COMPARISONS:
        for a, b in PAIRS:
            ra, rb = one(S[sc], variant=a, **flt), one(S[sc], variant=b, **flt)
            if not ra or not rb:
                continue
            w = welch(samples(ra, metric), samples(rb, metric))
            if w:
                rows.append((label, a, b, w, len(samples(ra, metric)), len(samples(rb, metric))))
    m = len(rows)
    alpha = 0.05 / m if m else 0.05
    md.append("### Statistical significance of the differences (lower ratio = stdnet better)\n")
    md.append(f"Ratio of means with its 95 % confidence interval, and Welch's t-test; {m} comparisons, so a difference is called "
              f"*significant* only if p < 0.05 / {m} = {alpha:.4f} (Bonferroni). *not significant* means the data cannot "
              f"tell the two apart at this number of repetitions.\n")
    md.append("| Comparison | stdnet variant | vs | ratio | 95 % CI | p | n | verdict |")
    md.append("|---|---|---|---|---|---|---|---|")
    for label, a, b, (ratio, (lo, hi), p), na, nb in rows:
        verdict = "not significant" if p >= alpha else ("stdnet lower" if ratio < 1 else "stdnet higher")
        md.append(f"| {label} | {LABEL[a]} | {LABEL[b]} | {ratio:.2f} | {lo:.2f} – {hi:.2f} | {p:.4f} | {na}/{nb} | {verdict} |")
    md.append("")
    return rows, alpha


def legend(fig, variants):
    handles = [plt.Rectangle((0, 0), 1, 1, color=COLOR[v]) for v in variants]
    fig.legend(handles, [LABEL[v] for v in variants], loc="lower center", ncol=len(variants), fontsize=8,
               bbox_to_anchor=(0.5, -0.02))


def bars(ax, groups, variants, value, title, ylabel, label_max=True):
    n = len(variants)
    width = 0.8 / n
    best = None
    for gi, (gname, rows) in enumerate(groups):
        for vi, v in enumerate(variants):
            r = one(rows, variant=v)
            if not r:
                continue
            y = value(r)
            x = gi + (vi - (n - 1) / 2) * width
            ax.bar(x, y, width * 0.9, color=COLOR[v], linewidth=0)
            if best is None or y > best[2]:
                best = (x, v, y)
    ax.set_xticks(range(len(groups)), [g for g, _ in groups])
    ax.set_title(title, loc="left")
    ax.set_ylabel(ylabel)


def main(run_dir):
    run = Path(run_dir)
    R = json.loads((run / "results.json").read_text())
    S, D = R["scenarios"], R["diagnostics"]
    md = []
    meta = R["meta"]
    md.append(f"Run: {meta['date']} · {meta['cpu']} · {meta['cores']} cores · Linux {meta['kernel']} · "
              f"{meta['compiler']} · {meta['reps']} repetitions per point\n")

    # ---- Echo throughput table + figure
    echo = S["echo"]
    conns = sorted({r["conns"] for r in echo})
    sizes = sorted({r["size"] for r in echo})
    md.append("### Echo — throughput (messages/s) and server CPU per message (µs)\n")
    hdr = "| Implementation | " + " | ".join(f"{c} conn · {s} B" for c in conns for s in sizes) + " |"
    md.append(hdr)
    md.append("|" + "---|" * (1 + len(conns) * len(sizes)))
    for v in ORDER:
        cells = []
        for c in conns:
            for s in sizes:
                r = one(echo, variant=v, conns=c, size=s)
                cells.append(f"{fmt(r['ops_per_s'])} · {fmt(r['cpu_us_per_op'], 1)}" if r else "-")
        md.append(f"| {LABEL[v]} | " + " | ".join(cells) + " |")
    md.append("")

    fig, axes = plt.subplots(1, len(conns), figsize=(10, 3.4), sharey=False)
    for ax, c in zip(axes, conns):
        groups = [(f"{s} B", pick(echo, conns=c, size=s)) for s in sizes]
        bars(ax, groups, ORDER, lambda r: r["ops_per_s"] / 1000, f"{c} connection{'s' if c > 1 else ''}",
             "thousand messages/s" if c == conns[0] else "")
    legend(fig, ORDER)
    fig.suptitle("Echo throughput by message size (higher is better)", x=0.01, ha="left", fontweight="bold")
    fig.tight_layout(rect=(0, 0.07, 1, 0.95))
    fig.savefig(run / "fig_echo_throughput.png", dpi=150)
    plt.close(fig)

    # ---- Efficiency across scenarios
    scen = [
        ("echo 16×64 B", one, lambda v: one(echo, variant=v, conns=16, size=64)),
        ("echo 256×64 B", one, lambda v: one(echo, variant=v, conns=256, size=64)),
        ("HTTP 64 conns", one, lambda v: one(S["http"], variant=v)),
        ("UDP 64 B", one, lambda v: one(S["udp"], variant=v, size=64)),
        ("churn", one, lambda v: one(S["churn"], variant=v)),
    ]
    md.append("### Throughput and server CPU per operation across scenarios\n")
    md.append("| Implementation | " + " | ".join(f"{n} (ops/s · µs CPU/op)" for n, _, _ in scen) + " |")
    md.append("|" + "---|" * (1 + len(scen)))
    for v in ORDER:
        cells = []
        for _, _, get in scen:
            r = get(v)
            cells.append(f"{fmt(r['ops_per_s'])} · {fmt(r['cpu_us_per_op'], 1)}" if r else "-")
        md.append(f"| {LABEL[v]} | " + " | ".join(cells) + " |")
    md.append("")

    fig, axes = plt.subplots(1, len(scen), figsize=(11, 3.2))
    for ax, (name, _, get) in zip(axes, scen):
        vs = [v for v in ORDER if get(v)]
        ax.bar(range(len(vs)), [get(v)["cpu_us_per_op"] for v in vs], 0.75, color=[COLOR[v] for v in vs], linewidth=0,
               yerr=[1.96 * get(v).get("cpu_us_per_op_sd", 0) / max(1, get(v)["valid_runs"]) ** 0.5 for v in vs],
               error_kw=dict(ecolor=INK2, elinewidth=0.8, capsize=2))
        ax.set_xticks([])
        ax.set_title(name, loc="left")
    axes[0].set_ylabel("server CPU µs per operation (bars: ± 1.96 SE)")
    legend(fig, ORDER)
    fig.suptitle("Server CPU per operation (lower is better)", x=0.01, ha="left", fontweight="bold")
    fig.tight_layout(rect=(0, 0.08, 1, 0.94))
    fig.savefig(run / "fig_cpu_per_op.png", dpi=150)
    plt.close(fig)

    # ---- Latency
    md.append("### Latency — round trip, 1 connection, 64 B (µs)\n")
    md.append("| Implementation | p50 | p99 | p99.9 |")
    md.append("|---|---|---|---|")
    lat = {v: one(echo, variant=v, conns=1, size=64) for v in ORDER}
    for v in ORDER:
        r = lat[v]
        md.append(f"| {LABEL[v]} | {fmt(r['p50_us'], 1)} | {fmt(r['p99_us'], 1)} | {fmt(r['p999_us'], 1)} |")
    md.append("")
    fig, axes = plt.subplots(1, 3, figsize=(10, 3.0), sharey=True)
    for ax, (k, t) in zip(axes, [("p50_us", "p50"), ("p99_us", "p99"), ("p999_us", "p99.9")]):
        ax.bar(range(len(ORDER)), [lat[v][k] for v in ORDER], 0.75, color=[COLOR[v] for v in ORDER], linewidth=0)
        ax.set_xticks([])
        ax.set_title(t, loc="left")
    axes[0].set_ylabel("round trip µs")
    legend(fig, ORDER)
    fig.suptitle("Round-trip latency, 1 connection, 64 B (lower is better)", x=0.01, ha="left", fontweight="bold")
    fig.tight_layout(rect=(0, 0.08, 1, 0.93))
    fig.savefig(run / "fig_latency.png", dpi=150)
    plt.close(fig)

    # ---- Allocations and memory
    md.append("### Heap allocations per operation and memory per idle connection\n")
    md.append("| Implementation | allocs/op echo 16×64 B | allocs/op HTTP | allocs/op UDP | RSS per connection, 10 000 conns (KB) |")
    md.append("|---|---|---|---|---|")
    idle = S["idle"]
    for v in ORDER:
        e = one(echo, variant=v, conns=16, size=64)
        h = one(S["http"], variant=v)
        u = one(S["udp"], variant=v, size=64)
        i = one(idle, variant=v)
        per_conn = (i["rss_delta_kb"] / i["conns"]) if i else None
        md.append(f"| {LABEL[v]} | {fmt(e['allocs_per_op'], 2)} | {fmt(h['allocs_per_op'], 2)} | {fmt(u['allocs_per_op'], 2)} | "
                  f"{fmt(per_conn, 1) if per_conn is not None else 'n/a (10 000 threads)'} |")
    md.append("")
    fig, axes = plt.subplots(1, 2, figsize=(9, 3.0))
    axes[0].bar(range(len(ORDER)), [one(echo, variant=v, conns=16, size=64)["allocs_per_op"] for v in ORDER], 0.75,
                color=[COLOR[v] for v in ORDER], linewidth=0)
    for i, v in enumerate(ORDER):
        val = one(echo, variant=v, conns=16, size=64)["allocs_per_op"]
        axes[0].annotate(f"{val:.1f}", (i, val), ha="center", va="bottom", fontsize=8, color=INK2, xytext=(0, 2),
                         textcoords="offset points")
    axes[0].set_title("Heap allocations per echo message", loc="left")
    axes[0].set_xticks([])
    iv = [v for v in ORDER if one(idle, variant=v)]
    axes[1].bar(range(len(iv)), [one(idle, variant=v)["rss_delta_kb"] / one(idle, variant=v)["conns"] for v in iv], 0.75,
                color=[COLOR[v] for v in iv], linewidth=0)
    axes[1].set_title("Server RSS per connection, KB", loc="left")
    axes[1].set_xticks([])
    legend(fig, ORDER)
    fig.tight_layout(rect=(0, 0.1, 1, 1))
    fig.savefig(run / "fig_memory.png", dpi=150)
    plt.close(fig)

    # ---- Scaling, backend, timers
    md.append("### Server threads (echo, 64 connections × 64 B, messages/s)\n")
    md.append("| Implementation | 1 thread | 2 threads |")
    md.append("|---|---|---|")
    for v in ORDER:
        a, b = one(S["scaling"], variant=v, threads=1), one(S["scaling"], variant=v, threads=2)
        if a:
            md.append(f"| {LABEL[v]} | {fmt(a['ops_per_s'])} | {fmt(b['ops_per_s'])} |")
    md.append("")
    md.append("### stdnet backends (echo, 64 B, messages/s · p99 µs)\n")
    md.append("Measured in their own runs, separately from the echo table above: the same point can differ by the run-to-run noise.\n")
    md.append("| Backend | 16 connections | 256 connections |")
    md.append("|---|---|---|")
    for v in ("stdnet_senders", "stdnet_senders_poll"):
        a, b = one(S["backend"], variant=v, conns=16), one(S["backend"], variant=v, conns=256)
        md.append(f"| {LABEL[v]} | {fmt(a['ops_per_s'])} · {fmt(a['p99_us'])} | {fmt(b['ops_per_s'])} · {fmt(b['p99_us'])} |")
    md.append("")
    t = S["timers"]
    n = t[0]["timers"]
    avg = lambda lib, k: sum(x[lib][k] for x in t) / len(t)  # noqa: E731
    md.append(f"### Timers ({n:,} concurrent timers; durations in ms)\n".replace(",", "\u202f"))
    md.append("| | start all | drain (deadlines over 50 ms) | cancel all |")
    md.append("|---|---|---|---|")
    for lib, name in (("stdnet", "stdnet"), ("asio", "Asio")):
        md.append(f"| {name} | {avg(lib, 'start_ms'):.1f} | {avg(lib, 'drain_ms'):.1f} | {avg(lib, 'cancel_ms'):.1f} |")
    cells = []
    for k in ("start_ms", "drain_ms", "cancel_ms"):
        w = welch([x["stdnet"][k] for x in t], [x["asio"][k] for x in t])
        cells.append(f"{w[0]:.2f} (95 % CI {w[1][0]:.2f} – {w[1][1]:.2f}, p = {w[2]:.4f})" if w else "-")
    md.append("| stdnet / Asio | " + " | ".join(cells) + " |")
    md.append("")

    # ---- Diagnostics
    md.append("### Reactor counters per echo message (instrumented stdnet build, measurement window)\n")
    keys = ["io_started", "io_immediate", "io_would_block", "reactor_waits", "interest_changes", "posts", "cv_waits"]
    md.append("| Build | conns · threads | " + " | ".join(keys) + " |")
    md.append("|" + "---|" * (2 + len(keys)))
    for d in D["stats"]:
        md.append(f"| {d['variant'].replace('_stats', '')} | {d['conns']} · {d.get('threads', 1)} | "
                  + " | ".join(fmt(d["per_op"].get(k, 0), 2) for k in keys) + " |")
    md.append("")
    if D.get("syscalls"):
        md.append("### System calls per echo message (strace, 16 connections, measurement window only)\n")
        names = ["recvfrom", "recvmsg", "sendto", "sendmsg", "epoll_wait", "epoll_ctl", "futex", "write", "read"]
        md.append("| Implementation | " + " | ".join(names) + " | total |")
        md.append("|" + "---|" * (2 + len(names)))
        for d in D["syscalls"]:
            per = d["per_op"]
            total = sum(per.values())
            md.append(f"| {LABEL[d['variant']]} | " + " | ".join(fmt(per.get(k, 0), 2) for k in names) + f" | {total:.2f} |")
        md.append("")
    md.append("### Operation-state sizes (bytes)\n")
    md.append("| " + " | ".join(D["opstate_sizes"].keys()) + " |")
    md.append("|" + "---|" * len(D["opstate_sizes"]))
    md.append("| " + " | ".join(str(v) for v in D["opstate_sizes"].values()) + " |")
    md.append("")
    if "build" in R:
        md.append("### Build cost (one server translation unit, -O2)\n")
        md.append("| Implementation | compile time (s) | .text (KB) |")
        md.append("|---|---|---|")
        for b in R["build"]:
            md.append(f"| {LABEL[b['variant']]} | {b['compile_s']:.1f} | {b['text_bytes'] / 1024:.0f} |")
        md.append("")

    # ---- Cost of the abstraction over BSD sockets
    md.append("### Cost over raw BSD sockets: server CPU per operation (µs), stdnet senders minus the raw loop\n")
    md.append("`raw poll` is the portable model (non-blocking sockets + `poll`, i.e. what Winsock's `WSAPoll` offers); "
              "`raw epoll` is the best a Linux readiness loop can do.\n")
    md.append("| Scenario | raw poll | raw epoll | Asio | stdnet senders | stdnet − raw poll | stdnet − raw epoll |")
    md.append("|---|---|---|---|---|---|---|")
    for label, sc, flt, metric in COMPARISONS:
        if metric != "cpu_us_per_op":
            continue
        g = {v: one(S[sc], variant=v, **flt) for v in ("raw_poll", "raw_epoll", "asio_callbacks", "stdnet_senders")}
        if not all(g.values()):
            continue
        c = {v: g[v]["cpu_us_per_op"] for v in g}
        md.append(f"| {label} | {c['raw_poll']:.1f} | {c['raw_epoll']:.1f} | {c['asio_callbacks']:.1f} | {c['stdnet_senders']:.1f} | "
                  f"{c['stdnet_senders'] - c['raw_poll']:+.1f} ({(c['stdnet_senders'] / c['raw_poll'] - 1) * 100:+.0f} %) | "
                  f"{c['stdnet_senders'] - c['raw_epoll']:+.1f} ({(c['stdnet_senders'] / c['raw_epoll'] - 1) * 100:+.0f} %) |")
    md.append("")

    significance(S, md)

    # Variability summary
    cvs = [r[k] for sc in ("echo", "http", "churn", "udp", "scaling") for r in S[sc] for k in ("ops_per_s_cv",) if k in r]
    cvs_cpu = [r[k] for sc in ("echo", "http", "churn", "udp", "scaling") for r in S[sc] for k in ("cpu_us_per_op_cv",) if k in r]
    md.append(f"Run-to-run variability (coefficient of variation, sample standard deviation / mean, {meta['reps']} runs per point): "
              f"throughput median {statistics.median(cvs) * 100:.1f} %, max {max(cvs) * 100:.1f} %; "
              f"server CPU per operation median {statistics.median(cvs_cpu) * 100:.1f} %, max {max(cvs_cpu) * 100:.1f} %.\n")
    (run / "tables.md").write_text("\n".join(md))
    print(f"wrote {run / 'tables.md'} and charts")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "bench/results/latest")
