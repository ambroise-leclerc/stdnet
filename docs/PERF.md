# stdnet performance

Status of stdnet against raw sockets and Asio, and the ranked list of improvements it implies.
Suite and method: [bench/README.md](../bench/README.md). Full tables of the run below:
[bench/results/2026-10-11/tables.md](../bench/results/2026-10-11/tables.md); raw data in
`results.json` next to it. The effect of the reactor change of #11 (measured on another machine,
before and after) is in [its own section](#effect-of-11-persistent-reactor-state).

## Run of 2026-10-11

Intel Xeon @ 2.80 GHz, **2 cores**, 8 GB, Linux 6.18, GCC 13.3 `-O2`, Asio 1.38.2, stdexec
`7df6de8`. Loopback; server and load generator share the two cores; **6 repetitions** of 3 s per
point after 1 s of warm-up. Coefficient of variation (sample standard deviation / mean): median
8.9 % for server CPU per operation, 9.4 % for throughput, up to 22 %. All runs passed validation
(no connection error, no lost datagram, server alive, counters present).

**What is claimed, and how.** Differences are tested with Welch's t-test; the table in
"Statistical significance" gives the ratio, its 95 % confidence interval and p, with a Bonferroni
correction over the 32 comparisons made. This page states a difference only when it passes that
test; the others are reported as *not significant at n = 6*, which means "not distinguishable
from noise on this machine", not "equal". Counters (allocations, system calls, posts) are
deterministic and need no test. An earlier version of this page, with 3 repetitions, called
several noise-level gaps wins or losses; those statements have been removed or qualified.

**Measurement window.** Server CPU, allocations, reactor counters and system calls are sampled
at the start and end of the load generator's measured window, and divided by the operations of
that same window; memory per connection is read at the end of the window with every connection
still open.

**Read throughput with care**: with two cores the load generator competes with the server, so
absolute message rates mostly say how CPU is shared (the raw epoll loop even comes out slower
than the raw poll loop at 256 connections, a gap that is not significant on CPU per message).
The comparable metric is **server CPU per operation**, together with allocations, system calls
and reactor counters. Linux only: Winsock is not measured (see below).

## Effect of #11: persistent reactor state

The reactor of #11 keeps the descriptor entry alive until `close()`/`cancel()`, registers it once
with epoll (`EPOLLIN | EPOLLOUT | EPOLLET`), and reuses its event buffers. It was measured **on a
different machine from the run above**, with the same suite, before and after, so that the two
campaigns are comparable with each other (and not with the Xeon numbers of this page):
AMD Ryzen 9 3900X (24 threads; the load generator no longer competes with the server), Linux
7.0, GCC 15 `-O3` (CMake `Release`), Asio 1.38.2, stdexec `7df6de8`, 6 repetitions of 3 s per
point. Before: `develop` at `0ec17e8`
([results](../bench/results/2026-10-11-develop/tables.md)); after: the branch of #17
([results](../bench/results/2026-10-11-reactor/tables.md)). The two campaigns ran one after
the other (not interleaved, 46 min each); Asio and the raw epoll loop, which did not change,
serve as controls: their before/after ratios are all within 0.97–1.05, so the machine did not
drift measurably. Full comparison with intervals:
[vs-develop.md](../bench/results/2026-10-11-reactor/vs-develop.md) (`compare.py`, since removed;
44 comparisons, Bonferroni threshold p < 0.0011).

**Counters (deterministic, no test needed)**

| | before | after |
|---|---|---|
| heap allocations per message, echo / HTTP / UDP / 10 000 idle connections (senders) | 2.0–3.0 | **0.00** |
| `epoll_ctl` per message, 1 connection / 64 connections (reactor counters) | 1.97 / 0.70 | **0.00** |
| allocations per connection, churn (senders / `task`; Asio and raw: 1.0) | 8.9 / 10.9 | 3.0 / 5.0 |
| memory per idle connection (senders) | 4.7 KB | 4.7 KB |

The remaining allocations of the churn scenario are the session state of each connection, not the
reactor.

**CPU per operation.** Smaller than the counters suggest, and mostly not distinguishable from
noise at n = 6 once corrected: echo, 1 connection × 64 B 19.6 → 17.2 µs (ratio 0.88, CI
0.81–0.95, p = 0.004, *not significant* at the corrected threshold); 16 connections 12.4 → 11.7 µs
(0.94, CI 0.87–1.00, p = 0.06); no change at 256 connections, 64 KB messages, HTTP, UDP, idle
connections and churn (ratios 0.95–1.01). The Asio and raw controls did not move. The allocator
and the syscalls removed were cheap compared with the 11–12 µs the kernel and the TCP stack
cost per message on this machine.

**Tail latency.** The significant effects are on p99: HTTP, 64 connections, senders 3.2 → 1.9 ms
(0.59×, CI 0.47–0.72, p = 0.0001); echo with `task`, 16 connections 881 → 606 µs (0.69×,
p = 0.0003) and 256 connections 42 → 18 ms (0.43×, p < 0.0001). For senders the echo p99 also
falls (16 connections 528 → 377 µs, 256 connections 10.9 → 7.1 ms) but not significantly after
correction. **One regression**: HTTP p99 with `task` rises from 2.6 to 7.8 ms (2.96×, CI
1.05–4.87, p = 0.0005); the interval is wide and the cause is not identified — it is the only
scenario where the tail got worse, to be looked at with #12 (the second scheduling hop of
`task`).

**Against raw sockets and Asio, after #11** (server CPU per message, ratios of stdnet senders with
95 % CI; none is significant): echo 16 connections 1.00 vs raw poll (0.93–1.08), 1.02 vs raw
epoll, 1.02 vs Asio (0.96–1.08); echo 256 connections 0.96 vs raw poll, 1.03 vs raw epoll, 1.01 vs
Asio; HTTP 1.01 vs raw poll and raw epoll, 0.99 vs Asio; UDP 1.05 vs raw (p = 0.045) and 0.94 vs Asio (p = 0.04). On this
machine stdnet senders were already within +8 % of raw epoll on small echo before the change
(12.4 vs 11.5 µs); the +31 % of the first Xeon run was re-measured on the Xeon itself (next section).

**epoll versus poll** (the stdnet senders server on each backend, own runs, CPU per message,
ratio epoll / poll): before, 16 connections 1.07 (CI 1.01–1.12, p = 0.02), 256 connections 1.01;
after, 16 connections 1.04 (0.98–1.09, p = 0.18), 256 connections 1.04 (0.97–1.12, p = 0.22).
The gap at 16 connections shrank and is no longer distinguishable from zero, but **epoll is not
shown to be at least as good as poll**: its point estimates remain 3–4 % above, with intervals
that include parity. That acceptance criterion of #11 is not met as stated; the remaining cost
of the epoll backend is outside what #11 changed (`epoll_wait` per wakeup, mutex, ready-queue
hand-off), and a larger n would be needed to resolve a 3 % difference.

**Not changed**: two server threads (stdnet senders 72.0 k → 72.5 k msg/s, `task` 63.7 k → 65.2 k;
the many-core question of #14 is untouched), memory per connection, compile time. The poll
backend now scans the persistent entries of idle sockets at each wait; it was not measured with
10 000 idle connections.

## Effect of #11 on the 2-core Xeon

The campaign of the first run (above) was repeated on the **same machine** with the code of the
branch of #17: 6 repetitions, same suite, same Asio and stdexec
([results](../bench/results/2026-10-11-xeon-reactor/tables.md), comparison with the first run:
[vs-main.md](../bench/results/2026-10-11-xeon-reactor/vs-main.md), 44 comparisons, Bonferroni
p < 0.0011). The two runs were made hours apart, so unchanged implementations measure the drift:
the Asio and raw epoll controls moved by −13 % to +27 % between them (for example Asio at 256
connections 7.1 → 9.0 µs, Asio 64 KB 24.7 → 28.3 µs), which is the real uncertainty of any
before/after reading on this machine. No CPU difference of stdnet between the two runs is
significant.

* **Counters (deterministic):** heap allocations per operation 2.0–3.0 → **0.00** in echo, HTTP
  and UDP; `epoll_ctl` per message 1.99 → **0.00**; posts unchanged (2.0 for senders, 4.0 for
  `task`).
* **CPU per small echo message, 16 connections, stdnet senders:** 8.1 → 6.9 µs. Against raw
  poll the gap went from **+31 % (significant, CI 1.15–1.47)** to **+12 % (CI 0.89–1.35, not
  significant)**; against raw epoll +29 % → +2 %; against Asio 1.15 → 1.04. At 256 connections the
  gap to raw poll is +30 % (6.3 → 8.2 µs) but its interval includes zero, as does every other
  scenario (HTTP +13 %, UDP +60 %, 64 KB +26 %, churn +6 %). The drift of the controls is as large
  as these changes, so the honest reading is: the large 16-connection penalty is gone or reduced,
  and what remains cannot be told from noise at n = 6.
* **epoll versus poll (own runs):** before, epoll used **significantly more CPU than poll** at 16
  connections (ratio 1.16, CI 1.09–1.22); now the ratio is 0.90 (CI 0.81–0.99, p = 0.035, not
  significant after correction) at 16 connections and 0.77 (0.55–0.98) at 256: epoll is no longer
  worse than poll, and the point estimates favour it. This is the acceptance criterion of #11 that
  the Ryzen run could not show.
* **Not fixed by #11:** HTTP p99 stays **2.2× Asio** (CI 2.0–2.5, p < 0.0001) and 1.9× raw epoll;
  timer start is 3.6× Asio's (73.7 vs 20.5 ms; 3.0× in the first run, so "about 3×"); compile time
  12.9 s vs 7.2 s; `task` still posts twice (3.99 vs 2.00).

## Spurious writability events (epoll backend)

With `EPOLLIN | EPOLLOUT | EPOLLET` registered permanently, the reactor counters of the
instrumented build show **one writable event per message with no write pending** (`idle_out` =
1.00 per operation at 1 and 256 connections, 0.88 at 16): after each send the peer's ACK frees
buffer space and the kernel reports a new writable edge. Every message therefore cost an extra
event, and at one connection an extra `epoll_wait` wake-up. The poll backend never sees them,
because it asks only for the directions that are pending.

`EPOLLOUT` is now armed (a `EPOLL_CTL_MOD`, which also reports the current state) only when a
write would block, and disarmed when the write queue is empty after an event. Counters, echo
64 B (instrumented build, same machine): `idle_out` 1.00 → 0 at 1, 16 and 256 connections;
events per message 1.00 → 1.00 / 0.88 → 0.79 / 1.00 → 0.94; `epoll_ctl` stays 0 per message
(blocked writes are rare and cost two calls each).

**CPU did not move measurably**: `bench/run.py`, 2 × 3 runs per build, interleaved, pinned,
Ryzen 9 3900X, every stdnet ratio after/before between 0.98 and 1.07 with intervals containing 1
(echo 16 connections 0.998, 0.91–1.09), like the controls. The wasted event costs about 0.2 µs
of 11. The **epoll / poll CPU ratio at 16 connections is 1.00 (0.91–1.09)**, before 1.01
(0.92–1.11): the 1.14 that appeared in the single run behind the README figure was not
reproduced, so on this machine epoll ≥ poll is still "not distinguishable", not shown. A new
test covers the arm / disarm cycle (a write blocked on a full socket buffer, three rounds).

## epoll against poll, closing #11

The last open criterion of #11 was "epoll backend ≥ poll backend". "≥" cannot be shown by a
test that finds no significant difference, so the criterion is stated as **non-inferiority with a
margin fixed before the measurement: epoll may cost at most 10 % more CPU per message than poll**
(an absolute "≥" is false where the backends differ in the other direction, see 256 below).

Study: `python3 bench/run.py --build build-bench/bench --out bench/results/2026-10-12-backend
--only backend --reps 30 --duration 1.0` (7 minutes). Raw data, every run:
[`bench/results/2026-10-12-backend/results.json`](../bench/results/2026-10-12-backend/results.json),
table: [`backend.md`](../bench/results/2026-10-12-backend/backend.md). Ryzen 9 3900X, GCC 16.1,
server and load generator pinned to one core each, 30 pairs per point, the two backends
interleaved inside each pair and the order alternated, so the drift of the machine cancels in the
per-pair ratio. CPU µs per echo message of 64 B; the ratio is the geometric mean of the 30 ratios
epoll / poll with a 95 % interval (t on the log ratios):

| connections | epoll | poll | epoll / poll | verdict |
|---|---|---|---|---|
| 16 | 11.21 | 11.02 | 1.018 (0.994–1.042) | non-inferior: upper bound 1.04 < 1.10 |
| 256 | 11.38 | 10.95 | **1.039 (1.016–1.063)** | non-inferior (upper 1.06 < 1.10); epoll **slightly worse**, significant |
| 2 000, 20 active | 10.97 | 19.51 | **0.563 (0.549–0.578)** | epoll better |
| 10 000, 100 active | 11.14 | 47.36 | **0.236 (0.227–0.245)** | epoll better |

Reading: with every connection active, epoll costs 2 % (not significant) to 4 % (significant)
more than poll at 16 and 256 connections, inside the 10 % margin; whatever causes the 4 % at 256
(an event harvested per descriptor, the edge-triggered bookkeeping) is not identified.
With mostly idle connections, where the poll backend pays per descriptor at every wait, epoll
needs 56 % and 24 % of poll's CPU. The earlier unpaired 6-run tests (interval 0.83–1.22 at 16
connections) could not decide either way; the gap of #10 (poll 140 k against epoll 121 k
messages per second) is gone.

## Why removing the allocations did not show in CPU time, and the tail it exposed

Two questions came out of the campaigns above: why did a change that removes 2–3 allocations and
2 system calls per message leave server CPU time unchanged (some points even got worse, though not
significantly), and where does the HTTP p99 gap come from? Both were investigated with two tools
that existed at the time and have since been replaced by the single test `bench/run.py`
(see [bench/README.md](../bench/README.md)): `instr.py`, which counted user-space instructions
under callgrind, and `quick.py`, an interleaved A/B run with controls.

**1. The change did what it should in user space: −30 % instructions.** Instructions executed
in user space per message (callgrind, window of the load generator; Asio and the raw loop as
controls, which did not move):

| Scenario | develop | #17 | raw epoll | Asio |
|---|---|---|---|---|
| echo 16 conns, senders | 1814 | **1266** (−30 %) | 253 | 1754 |
| HTTP 64 conns, senders | 2114 | **1495** (−29 %) | 494 | 1879 |
| UDP 64 B, senders | 1935 | **1414** (−27 %) | 152 | 1997 |
| echo 16 conns, `task` | 7563 | 6946 (−8 %) | | |
| HTTP 64 conns, `task` | 8020 | 6969 (−13 %) | | |

stdnet senders now execute fewer user-space instructions than Asio callbacks (1266 vs 1754), but
five times more than a hand-written epoll loop, and `task` (stdexec's coroutine machinery) four
to five times more than the senders. The biggest remaining items of a sender echo message are the
mutex lock/unlock pairs (about 230 instructions), `start_io`, the descriptor table lookup (a hash
map `operator[]`), the stop-callback registration and the stdexec `let_value` state.

**2. But user space is about 10 % of the CPU time.** Splitting the server's CPU into user and
system time (`/proc`, 3 runs, 16 connections, 64 B): **0.7–0.9 µs user and 6.3–7.0 µs system per
message**, for stdnet, Asio and raw sockets alike. A bare `send` + `recv` pair on loopback costs
4.2 µs in a C micro-benchmark (the receive-side TCP processing runs inside the sender's system
call). A 30 % cut of 0.8 µs is 0.25 µs, 3 % of 7.5 µs: below the 10 % run-to-run noise of this
machine (the unchanged Asio and raw controls drift by up to ±25 % between campaigns). So the
absence of a CPU gain is expected, and the apparent regressions (echo 256 connections +19 %, 64 KB
+11 %, UDP +8 %, all not significant) are not caused by user-space code, which got faster at every
point; they are kernel and scheduling variation. The effect of the allocator removal is real but
three orders of magnitude below what wall-clock CPU time on loopback can resolve; judge library
changes with instruction counts first (callgrind, `instr.py` in the history of this repository).

I also tested replacing `recvmsg`/`sendmsg` by `recv`/`send` for a single buffer (what Asio and
the raw loops use): the C micro-benchmark shows 0–3 % difference, so it was not kept.

**3. The tail latency problem was starvation of the reactor.** `io_context::run_one` polled the
reactor only when the ready queue was empty. An operation that completes immediately re-feeds the
queue (the next receive finds data already there), so with enough busy connections the queue never
empties and connections that have to wait for readiness are served only when it does. That shows
up as p99 far above p50 and, after #11, as a stable tail: HTTP p99 with `task` on the Ryzen was
7.5–8.2 ms in all six runs (before: 0.9–5.2 ms, erratic). Confirmed by experiment (server and
load generator pinned to separate cores, as on the Ryzen; 3 runs, mean; p99 in µs, `K` = reactor
polled without blocking every K completions, ∞ = the old behaviour):

| Point | ∞ (before) | K = 256 | K = 64 | K = 16 | K = 1 |
|---|---|---|---|---|---|
| HTTP 64, senders | 2525 | 1286 | 1118 | 1218 | 1356 |
| HTTP 64, `task` | 3248 | 1233 | 1534 | 1498 | 1921 |
| echo 256, senders | 7666 | 5732 | 5626 | 5593 | 6645 |
| echo 256, `task` | 22648 | 5576 | 6289 | 7144 | 8560 |

The reactor is now polled without blocking every `STDNET_REACTOR_INTERVAL` (256) completions.
Smaller values do not improve the tail further and cost throughput with `task` (CPU per message
7.6 → 9.3–10.4 µs at K ≤ 64 on HTTP: the user-space instruction count is unchanged, so the cost
is in system calls and wake-ups — more, smaller batches), hence 256. End to end, interleaved A/B
against #17 (5 runs, unpinned; `quick.py`): p99 HTTP senders 2192 → 1333 µs and `task`
3828 → 1301 µs; echo 256 connections senders 9411 → 4945 µs and `task` 28 070 → 6154 µs, the
level of Asio's (5667 µs). Mean CPU is not significantly different (ratios 1.03–1.18, CIs
including 1; the Asio control moved 0.83–1.18× in the same run), though `task` at 256 connections
is borderline (+14 %, p = 0.07) and worth a longer run. A new test
(`io_context: a never-empty ready queue does not starve operations waiting for readiness`) fails
with `-DSTDNET_REACTOR_INTERVAL=1000000000` and passes with the default.

The Ryzen regression of `task` (HTTP p99 2.6 → 7.8 ms) could not be reproduced on the Xeon
(there #17 alone already improved it), so the link between it and this starvation is a strong
explanation rather than a verified one: it needs the Ryzen campaign to be re-run with this change.

## Summary of the first run (code of `develop` before #11)

* **Cost over BSD sockets.** The portable raw loop (`poll`) spends 6.2 µs of server CPU per
  64-byte echo message at 16 connections; stdnet senders spend 8.1 µs: **+31 % (+1.9 µs),
  significant** (95 % CI of the ratio 1.15–1.47), the same against raw epoll (+29 %). At 256
  connections the gap shrinks to +0.8 µs and is not significant, and it is not significant for
  HTTP (+23 % vs poll, p = 0.03), 64 KB messages (+18 %), UDP (+43 %, p = 0.008) or churn
  (+3 %) after correction. Reading the intervals, stdnet costs between 0 and about 50 % more CPU
  than hand-written sockets on small messages, and the data cannot say more precisely with this
  machine and this many runs. Latency at one connection is the same (p50 52.5 vs 50.9 µs, not
  significant).
* **Against Asio**: no significant difference in CPU per message for the sender API in any
  scenario (16 connections: ratio 1.15, CI 1.01–1.30, p = 0.03 — suggestive, not established).
  The coroutine variant is significantly worse than Asio coroutines at 16 connections (1.52×,
  the double scheduling hop below). The earlier "stdnet is ahead on UDP" does **not** survive:
  5.1 µs vs Asio's 6.0, ratio 0.86, CI 0.70–1.02, p = 0.09.
* **Solid findings** (counters or large, significant effects):
  1. **2.0–2.3 heap allocations per operation** (Asio, raw: 0) and **~2 `epoll_ctl` per message**
     at one connection — from the per-descriptor map in `io_context` (**fixed by #11**, see above);
  2. **`std::execution::task` posts every completion twice** (4.0 vs 2.0 per message) and costs
     1.3–1.5× the CPU of the sender API (#12);
  3. **timer start is 3.0× slower than Asio** (68.9 vs 23.3 ms for 100 000; CI 2.27–3.64) while
     **cancellation is 2.7× faster** (15.9 vs 42.4 ms) and draining 1.3× faster (p = 0.015) (#13);
  4. **HTTP p99 latency is 2.2× Asio's** (CI 1.8–2.6, p = 0.0002) and 2.5× raw poll's at 64
     connections — significant, cause unknown, to be re-measured after #11;
  5. **compile time** 13.2 s vs 6.6 s per translation unit (stdexec headers); code size of the
     sender server (102 KB `.text`) is below Asio's (118 KB);
  6. memory of an idle connection: 4.7 KB vs 4.6 KB for Asio (ratio 1.03).
* The single-mutex design could not be judged on two cores (#14).

![Echo throughput](../bench/results/2026-10-11/fig_echo_throughput.png)

![Server CPU per operation](../bench/results/2026-10-11/fig_cpu_per_op.png)

| Implementation | echo 16×64 B | echo 256×64 B | HTTP, 64 conns | UDP 64 B | churn |
|---|---|---|---|---|---|
| raw blocking | 77 919 · 11.1 | 75 309 · 11.6 | 84 754 · 10.5 | 253 834 · 3.7 | 10 447 · 120.1 |
| raw poll (BSD sockets) | 149 645 · 6.2 | 161 480 · 6.1 | 159 607 · 6.1 | 258 047 · 3.6 | 20 752 · 24.9 |
| raw epoll | 136 833 · 6.3 | 121 887 · 7.1 | 127 346 · 6.7 | 231 559 · 4.0 | 20 751 · 24.7 |
| Asio callbacks | 128 077 · 7.0 | 132 750 · 7.1 | 127 035 · 7.2 | 161 570 · 6.0 | 21 347 · 25.0 |
| Asio coroutines | 132 230 · 6.8 | 126 493 · 7.5 | 127 595 · 7.6 | 149 105 · 6.5 | 24 476 · 28.3 |
| stdnet senders | 117 986 · 8.1 | 141 436 · 6.9 | 130 645 · 7.5 | 190 418 · 5.1 | 25 634 · 25.8 |
| stdnet task | 92 911 · 10.4 | 114 648 · 8.9 | 101 728 · 9.8 | 129 129 · 7.6 | 26 032 · 30.4 |

*Operations per second · server CPU µs per operation (means of 6 runs; the error bars of the
figure above are ±1.96 standard errors).*

## stdnet versus BSD sockets (and Winsock)

Server CPU per operation, µs. `raw poll` is the portable BSD model (non-blocking sockets +
`poll`, the same readiness model as Winsock's `WSAPoll`); `raw epoll` is the best a Linux
readiness loop can do.

| Scenario | raw poll | raw epoll | Asio | stdnet senders | stdnet − raw poll | significant? |
|---|---|---|---|---|---|---|
| echo 16 conns × 64 B | 6.2 | 6.3 | 7.0 | 8.1 | +1.9 (+31 %) | yes |
| echo 256 conns × 64 B | 6.1 | 7.1 | 7.1 | 6.9 | +0.8 (+14 %) | no |
| echo 16 conns × 64 KB | 22.4 | 22.0 | 24.7 | 26.4 | +4.0 (+18 %) | no |
| HTTP 64 conns | 6.1 | 6.7 | 7.2 | 7.5 | +1.4 (+23 %) | no (p = 0.03) |
| UDP 64 B | 3.6 | 4.0 | 6.0 | 5.1 | +1.5 (+43 %) | no (p = 0.008, above the corrected threshold 0.0016) |
| churn | 24.9 | 24.7 | 25.0 | 25.8 | +0.8 (+3 %) | no |

The same table with confidence intervals for every pair is in
[tables.md](../bench/results/2026-10-11/tables.md). The only effect that is both large and
robust is the small-message echo at 16 connections; on the other scenarios stdnet is within
noise of both the raw sockets and Asio, but the intervals are wide (up to ±25 % at n = 6).
Heap allocations are the unambiguous cost: 0 for the raw loops, 2.0–2.3 per operation for stdnet.

**Winsock.** The suite is Linux-only (epoll, `/proc`, `strace`), so no Winsock number exists.
stdnet's Windows backend (`WSAPoll`) is exercised by the conformance tests in CI, not
benchmarked; a Windows port of `raw_poll` and of the load generator would be needed to compare
it with raw Winsock. The Linux `raw poll` result is the closest available proxy for the cost of
the portable readiness model, not a Winsock measurement.

![Latency](../bench/results/2026-10-11/fig_latency.png)

![Allocations and memory](../bench/results/2026-10-11/fig_memory.png)

## What the diagnostics show

**Allocations** (before #11). Tracing the allocations of the sender server (`BENCH_TRACE_ALLOCS`) attributes
them to `std::unordered_map::operator[]` in `io_context::start_io()`: the entry of a descriptor
is created when an operation starts and erased as soon as no operation is pending, so each
operation allocates (and frees) a 56-byte node — 2 operations, 2 allocations per echo message.
The remainder came from the event `std::vector` built at each reactor wait. Both are gone since #11.

**Reactor counters** (instrumented build, per echo message, measurement window; Xeon run, before #11 —
`epoll_ctl` is 0 everywhere after it):

| Build | conns · threads | operations started | completed in `start()` | had to wait | `epoll_wait` | `epoll_ctl` | posts |
|---|---|---|---|---|---|---|---|
| senders | 1 · 1 | 2.00 | 1.00 | 0.99 | 0.99 | 1.99 | 2.00 |
| senders | 64 · 1 | 2.00 | 1.75 | 0.25 | 0.02 | 0.49 | 2.00 |
| task | 1 · 1 | 2.00 | 1.02 | 0.98 | 0.98 | 1.96 | 4.00 |
| task | 64 · 1 | 2.00 | 1.84 | 0.16 | 0.02 | 0.32 | 4.00 |

Every operation that has to wait costs an `EPOLL_CTL_ADD` and an `EPOLL_CTL_DEL` (1.99 calls for
0.99 waiting operations). The portable poll backend, which rebuilds its array without system
calls, uses **significantly less CPU than epoll at 16 connections** (ratio 1.16 in favour of
poll, CI 1.09–1.22; 140 k vs 121 k msg/s) and has a **2.3× lower p99 at 256 connections**
(8.8 vs 20.5 ms, p = 0.002); at 256 connections the CPU difference is not significant. With
`task`, posts double: the coroutine is re-scheduled after each `co_await` even though the
operation already completed on the right context. (The backend table is measured in its own
runs, so a point can differ slightly from the same point of the echo table.)

**System calls** inside the window at 16 connections: one `recvmsg`/`recvfrom` and one
`sendmsg`/`sendto` per message for stdnet, Asio and the raw loop alike (Asio adds 0.13 `epoll_wait`). Under `strace` the
server is slowed down so much that almost every operation finds its socket ready, so this
profile shows which calls are made, not how often operations wait — the counters above do.

**Operation states**: 112 bytes for `async_receive`, 120 for `async_write`, 200 for a
receive-then-write iteration, 80 for a timer. A 64-byte-message server needs no heap beyond its
per-connection state once #11 is fixed.

**Memory per connection** (10 000 connections, 100 active, read with the connections open):
0.1 KB for the raw loops, 4.6 KB Asio callbacks, 4.7 KB stdnet senders, 5.3–5.4 KB for the
coroutine variants. Most of it is the touched part of each session's 64 KB receive buffer.
Kernel socket buffers are not part of the process RSS.

## Hypotheses of issue #10

| Hypothesis | Verdict |
|---|---|
| Single mutex, system calls under the lock, limits scaling | **Not measurable here** — on 2 cores every library slows down with a second server thread (the raw epoll loop, with one reactor per thread, loses only 9 %: 149 k → 135 k msg/s, against 152 k → 111 k for stdnet senders); condition-variable waits stay ≤ 0.01 per message. Needs a many-core run (#14). |
| `epoll_ctl(MOD)` at each interest change | **Confirmed, and worse**: ADD + DEL per waiting operation (1.99 per message at one connection); poll beats epoll at 16 connections. **Fixed by #11**: 0 per message; epoll vs poll no longer distinguishable. |
| Map lookup per readiness event | **Confirmed, and worse**: one node allocation per operation, the main source of allocations. **Fixed by #11**: 0 allocations (the lookup itself remains). |
| Immediate success still goes through the ready queue | **Confirmed in counts** (1.75 of 2 operations per message at 64 connections); cost not isolated (#15). |
| Buffer sequences copied; resolver allocations | Single-buffer copies are free; the resolver is not exercised by these scenarios — not measured. |
| iovec array rebuilt at each partial transfer | **Not a bottleneck**: at 64 KB per message stdnet is within noise of Asio (26.4 vs 24.7 µs at 16 connections, ratio 1.07, p = 0.45; 30.2 vs 36.5 µs at 256). |

## Ranked improvements

| # | Change | Evidence | Expected effect |
|---|---|---|---|
| 1 | ~~Per-socket reactor state, registered once, edge-triggered (#11)~~ **done** (#17) | 2.0–2.3 allocs/op; ~2 `epoll_ctl`/msg; poll > epoll at 16 conns | achieved: 0 allocations, 0 `epoll_ctl`, lower p99; CPU gain small; epoll ≥ poll not shown |
| 2 | ~~Remove the second scheduling hop of `task` (#12)~~ **done** (`stdnet::task`) | 4.0 vs 2.0 posts/msg; task 1.3–1.5× the CPU of senders (echo, UDP; significant) | achieved: 2.0 vs 2.0 posts/msg; task / senders CPU 1.01 (echo), 0.96 (HTTP), 0.99 (UDP) |
| 3 | ~~Allocation-free timer queue (#13)~~ **done** ([below](#the-timer-queue-13)) | 68.9 vs 23.3 ms to start 100 000 timers (3.0×, partly a run-order artifact) | achieved: 0.69× Asio's start time, 0 allocations per timer, cancellation 0.47× of before |
| 4 | HTTP p99 latency, 2.2× Asio (significant) | explain after #11 | — |
| 5 | Scaling run on ≥ 8 cores, then lock splitting if needed (#14) | inconclusive on 2 cores | — |
| 6 | Bounded inline completion (#15) | 1.75 immediate completions/msg all queued | lower CPU and latency at high load, to be measured |
| 7 | Compile time | 13.2 s vs 6.6 s (Asio) per TU | depends on stdexec / a bundled kernel; tracked, no issue yet |

## Re-running

```sh
python3 bench/run.py --build build-bench/bench --out bench/results/<date>   # ~1.5 min
python3 bench/infographic.py bench/results/<date> docs/infographic.png [<baseline>]
```

The tables and campaigns above come from the long campaign script that this test replaced
(3 s windows, 6 repetitions, about 45 minutes; see the history of `bench/run.py`); their raw
data are in `bench/results/`. `run.py` exits with status 1 if any run is invalid. Each improvement above should land with a
re-run and an update of the figure.

## The second scheduling hop of `task` (#12)

Cause, confirmed in stdexec's `task`: `await_transform` wraps every awaited sender in `affine`, which
appends a `schedule` on the task's start scheduler unless the sender's `get_completion_behavior`
says it already completes on that scheduler (`asynchronous_affine`, a stdexec-specific static
query). An advertised `get_completion_scheduler` equal to the task's scheduler is not enough: a
stdexec-only reproduction (`docs/upstream-task-affinity-repro.cpp`) shows 21 schedules for 10
`co_await`s, with the default type-erased `task_scheduler` and with a concrete start scheduler alike.
Each `co_await` of a stdnet operation (completed on the `io_context`) was thus followed by a second
post to the same `io_context`: 4.0 posts per echo message instead of 2.0.

Fix: `stdnet::task<T>` (`<network>`, `stdnet/task.hpp`) is `std::execution::task<T, task_context>`
whose start scheduler is `bound_scheduler`, a thin wrapper of `io_context::scheduler_type`. The
stdnet I/O and timer senders report `asynchronous_affine` when the awaiting environment's start
scheduler is a `bound_scheduler`, so the coroutine resumes straight from the completion. The claim
is checked at run time: an operation whose `io_context` is not the task's redirects its completion
to the task's context first (one pointer comparison on the fast path; tested). Measured (instrumented
build, 64 connections): 2.0 posts per message for both servers. CPU per message, `task` / senders,
8 interleaved runs: echo 16 connections 1.01, HTTP 0.96, UDP 0.99 (before: 1.3–1.5×). **HTTP p99 re-measured** (64 connections, 20 interleaved runs of each server, `bench/results/task-affinity/http_p99.json`;
`stdnet_coroutines_plain` is `std::execution::task` with the hop): p99 1.72 ms for senders, 1.20 ms for
plain `task`, 1.66 ms for `stdnet::task`. Without the hop the tail equals the senders' (ratio 0.97, CI
0.86–1.07, p = 0.55) and is 1.39× that of plain `task` (CI 1.20–1.58, p < 0.0001); p50 and CPU improve
(p50 0.88×, CPU 0.95× of plain `task`). The earlier "regression" of `task` p99 (2.6 → 7.8 ms) is thus not
caused by the hop: the hop acts as a yield that lets the reactor and other ready operations run between
two steps of a connection, which trades CPU and median for a shorter tail; with it removed the task behaves
like the sender API, whose tail is bounded by the ready-queue order (see section 3 above). Plain
`std::execution::task` keeps working, with the hop.
The mechanism relies on the stdexec `get_completion_behavior` query (not in the C++26 `task`);
the upstream report is drafted in [docs/upstream-task-affinity.md](upstream-task-affinity.md).

## The timer queue (#13)

Cause: each `start_timer` allocated a `std::multimap` node and notified the condition variable
(and the reactor's wake-up descriptor whenever a thread was polling), even though starting a
timer makes nothing ready. Fix: an intrusive binary min-heap (`detail::timer_heap`): a vector of
{deadline, start sequence, operation} entries, each operation recording its index, so that a
cancellation removes it in O(log n); comparisons read only the entries, ties keep the start order
(tested). The vector keeps its capacity, so a steady-state timer does not allocate. Starting a
timer wakes the reactor only when its deadline becomes the earliest while a thread is blocked in
the reactor (tested: the test fails without that wake-up), and never the condition variable.

Measured with `bench_timers` (100 000 timers, deadlines spread over 50 ms), the binary of
`develop` and the new one built from the same benchmark code, 10 interleaved repetitions × both
run orders (`bench/results/2026-10-12-timers/runs.json`):

| | before | after | after / before |
|---|---|---|---|
| start, stdnet | 49.6 ms | 21.5 ms | 0.43 (CI 0.39–0.47) |
| cancel, stdnet | 12.9 ms | 6.1 ms | 0.47 (CI 0.40–0.54) |
| drain, stdnet | 50.4 ms | 50.4 ms | 1.00 (CI 0.996–1.004) |
| allocations while starting 100 000 timers | 100 000 | 18 (vector growth) | Asio: 18 |

**The earlier 3.0× was partly a run-order artifact.** The benchmark ran stdnet first; Asio then
reused the free lists left by the 100 000 freed `multimap` nodes and operation states, and started
its timers in 15.6 ms instead of 30.4 ms when it runs first. With both libraries measured on a cold
allocator, the old queue was 1.7× Asio (52.2 vs 30.4 ms), the new one is **0.69×** (21.3 vs
31.1 ms, CI 0.58–0.80); in either order the new queue is faster than Asio (0.69–0.73×). The
benchmark now takes `asio-first` as a second argument and `run.py` alternates the order. In the
figure run (`bench/results/timer-queue/`, 20 000 timers, 4 repetitions) timer start is 1.7× faster
than Asio and cancellation 4.3× faster.

