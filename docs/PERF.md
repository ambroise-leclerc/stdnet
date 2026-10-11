# stdnet performance

Status of stdnet against raw sockets and Asio, and the ranked list of improvements it implies.
Suite and method: [bench/README.md](../bench/README.md). Full tables of the run below:
[bench/results/2026-10-11/tables.md](../bench/results/2026-10-11/tables.md); raw data in
`results.json` next to it.

## Run of 2026-10-11

Intel Xeon @ 2.80 GHz, **2 cores**, 8 GB, Linux 6.18, GCC 13.3 `-O2`, Asio 1.38.2, stdexec
`7df6de8`. Loopback; server and load generator share the two cores; 3 repetitions of 3 s per
point after 1 s of warm-up; median coefficient of variation of throughput 5.6 % (max 19 %).
`idle` and `churn` were re-run after a fix in the sender benchmark server (its 64 KB buffer was
zero-filled at each accept).

**Read throughput with care**: with two cores the load generator competes with the server, so
absolute message rates mostly say how CPU is shared. The comparable metric is **server CPU per
operation** (from `/proc`), together with allocations, system calls and reactor counters, which
do not depend on the machine.

## Summary

* **stdnet is in the same band as Asio** on every scenario: CPU per echo message 10.0 µs vs
  8.2–8.5 µs (16 connections), HTTP 7.3 µs vs 7.5 µs, connection churn 34.7 µs vs 31.7 µs, same
  latency within noise (p50 53 µs vs 50 µs at one connection, loopback-dominated), same memory
  per idle connection (4.7 KB vs 4.6 KB).
* **Where stdnet is ahead**: UDP with sender algorithms (222 k datagrams/s, 5.9 µs CPU vs Asio's
  175 k and 7.5 µs), large messages (64 KB echo: 32 µs vs 35 µs per message), timer cancellation
  (12 ms vs 32 ms for 100 000 timers), code size of the sender server (101 KB `.text` vs 117 KB).
* **Where it is behind, with identified causes**:
  1. **2.7–2.9 heap allocations per operation** (Asio: 0) and **2.2 `epoll_ctl` per message**
     at low concurrency — both from the per-descriptor map in `io_context` (#11);
  2. **`std::execution::task` posts every completion twice** (4.6 vs 2.3 per message), costing up
     to 3 µs per operation (#12);
  3. **timer start is 3.3× slower than Asio** (#13);
  4. **compile time is 2× Asio's** (11.5 s vs 5.8 s per translation unit, stdexec headers).
* The single-mutex design could not be judged on two cores (#14).

![Echo throughput](../bench/results/2026-10-11/fig_echo_throughput.png)

![Server CPU per operation](../bench/results/2026-10-11/fig_cpu_per_op.png)

| Implementation | echo 16×64 B | echo 256×64 B | HTTP, 64 conns | UDP 64 B | churn |
|---|---|---|---|---|---|
| raw blocking | 78 824 · 14.7 | 84 913 · 13.6 | 94 701 · 12.1 | 266 664 · 4.6 | 11 210 · 152.9 |
| raw epoll | 118 373 · 9.3 | 130 925 · 8.8 | 159 251 · 7.4 | 284 904 · 4.6 | 21 940 · 30.9 |
| Asio callbacks | 147 303 · 8.5 | 155 093 · 8.2 | 170 547 · 7.5 | 174 999 · 7.5 | 24 475 · 31.7 |
| Asio coroutines | 147 890 · 8.2 | 136 623 · 9.3 | 167 962 · 7.9 | 183 300 · 7.1 | 26 948 · 35.9 |
| stdnet senders | 129 881 · 10.0 | 141 524 · 9.3 | 180 598 · 7.3 | 222 026 · 5.9 | 25 148 · 34.7 |
| stdnet task | 129 808 · 10.0 | 130 561 · 10.1 | 148 560 · 8.8 | 144 079 · 9.0 | 30 887 · 34.9 |

*Operations per second · server CPU µs per operation.*

![Latency](../bench/results/2026-10-11/fig_latency.png)

![Allocations and memory](../bench/results/2026-10-11/fig_memory.png)

## What the diagnostics show

**Allocations.** Tracing the first allocations of the sender server (`BENCH_TRACE_ALLOCS`)
attributes them to `std::unordered_map::operator[]` in `io_context::start_io()`: the entry of a
descriptor is created when an operation starts and erased as soon as no operation is pending,
so each operation allocates (and frees) a 56-byte node. The remainder comes from the event
`std::vector` built at each reactor wait.

**Reactor counters** (instrumented build, per echo message):

| Build | conns · threads | operations started | completed in `start()` | had to wait | `epoll_wait` | `epoll_ctl` | posts |
|---|---|---|---|---|---|---|---|
| senders | 1 · 1 | 2.29 | 1.17 | 1.12 | 1.12 | 2.24 | 2.29 |
| senders | 64 · 1 | 2.31 | 2.00 | 0.30 | 0.04 | 0.61 | 2.31 |
| task | 1 · 1 | 2.30 | 1.18 | 1.12 | 1.12 | 2.24 | 4.60 |
| task | 64 · 1 | 2.36 | 2.18 | 0.18 | 0.01 | 0.36 | 4.72 |

Every operation that has to wait costs an `EPOLL_CTL_ADD` and an `EPOLL_CTL_DEL`. That is why
the portable poll backend, which rebuilds its array without system calls, is *faster* than
epoll (158 k vs 137 k msg/s at 16 connections). With `task`, posts double: the coroutine is
re-scheduled after each `co_await` even though the operation already completed on the right
context.

**System calls** at 16 connections are identical between stdnet, Asio and the raw loop (one
`recvmsg`/`recvfrom` and one `sendmsg`/`sendto` per message): when operations succeed
immediately, nothing else is called. The overheads above are in user space.

**Operation states**: 112 bytes for `async_receive`, 120 for `async_write`, 200 for a
receive-then-write iteration, 80 for a timer. A 64-byte-message server needs no heap beyond
its per-connection state once #11 is fixed.

## Hypotheses of issue #10

| Hypothesis | Verdict |
|---|---|
| Single mutex, system calls under the lock, limits scaling | **Not measurable here** — on 2 cores every implementation slows down with a second server thread; condition-variable waits stay ≤ 0.01 per message. Needs a many-core run (#14). |
| `epoll_ctl(MOD)` at each interest change | **Confirmed, and worse**: ADD + DEL per waiting operation (2.24 per message at one connection); poll beats epoll. (#11) |
| Map lookup per readiness event | **Confirmed, and worse**: one node allocation per operation, the main source of allocations. (#11) |
| Immediate success still goes through the ready queue | **Confirmed in counts** (2.0 of 2.3 operations per message at 64 connections); cost not isolated (#15). |
| Buffer sequences copied; resolver allocations | Single-buffer copies are free; the resolver is not exercised by these scenarios — not measured. |
| iovec array rebuilt at each partial transfer | **Not a bottleneck**: at 64 KB per message stdnet is ahead of Asio (32 vs 35 µs). |

## Ranked improvements

| # | Change | Evidence | Expected effect |
|---|---|---|---|
| 1 | Per-socket reactor state, registered once, edge-triggered (#11) | 2.7–2.9 allocs/op; 2.24 `epoll_ctl`/msg; poll > epoll | 0 allocations; −2 syscalls per waiting operation; epoll ≥ poll |
| 2 | Remove the second scheduling hop of `task` (#12) | 4.6 vs 2.3 posts/msg; task up to 3 µs/op slower | task ≈ senders |
| 3 | Allocation-free timer queue (#13) | 54 vs 17 ms to start 100 000 timers | start within 1.5× of Asio |
| 4 | Scaling run on ≥ 8 cores, then lock splitting if needed (#14) | inconclusive on 2 cores | — |
| 5 | Bounded inline completion (#15) | 2.0 immediate completions/msg all queued | lower CPU and latency at high load, to be measured |
| 6 | Compile time | 11.5 s vs 5.8 s (Asio) per TU | depends on stdexec / a bundled kernel; tracked, no issue yet |

## Re-running

```sh
python3 bench/run.py --build build-bench/bench --out bench/results/<date>
python3 bench/run.py --build build-bench/bench --out bench/results/<date> --only echo,backend   # partial re-run, merged
python3 bench/report.py bench/results/<date>
```

Each improvement above should land with a partial re-run and an update of this page.
