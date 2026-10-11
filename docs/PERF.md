# stdnet performance

Status of stdnet against raw sockets and Asio, and the ranked list of improvements it implies.
Suite and method: [bench/README.md](../bench/README.md). Full tables of the run below:
[bench/results/2026-10-11/tables.md](../bench/results/2026-10-11/tables.md); raw data in
`results.json` next to it.

## Run of 2026-10-11

Intel Xeon @ 2.80 GHz, **2 cores**, 8 GB, Linux 6.18, GCC 13.3 `-O2`, Asio 1.38.2, stdexec
`7df6de8`. Loopback; server and load generator share the two cores; 3 repetitions of 3 s per
point after 1 s of warm-up; median coefficient of variation of throughput 6.7 % (max 23 %).
Every one of the 252 runs passed validation (no connection error, no lost datagram, server
alive, counters present).

**Measurement window.** Server CPU, allocations, reactor counters and system calls are sampled
at the start and end of the load generator's measured window, and divided by the operations of
that same window; memory per connection is read at the end of the window with every connection
still open. *(A first version of this report divided whole-run counters, warm-up included, by
the operations of the measured window only, which overstated every per-operation figure; it has
been replaced by this run.)*

**Read throughput with care**: with two cores the load generator competes with the server, so
absolute message rates mostly say how CPU is shared. The comparable metric is **server CPU per
operation**, together with allocations, system calls and reactor counters, which do not depend
on the machine.

## Summary

* **stdnet is close to Asio, with a measurable gap on small messages**: CPU per 64-byte echo
  message 7.7 µs vs 5.9 µs at 16 connections (+30 %), 6.0 vs 5.3 µs at 256 connections; HTTP
  6.5 vs 6.0 µs; connection churn 26.6 vs 23.9 µs. Latency is the same within noise
  (p50 51.6 vs 49.7 µs at one connection, loopback-dominated) and so is the memory of an idle
  connection (4.7 KB vs 4.6 KB).
* **Where stdnet is ahead**: UDP with sender algorithms (213 k datagrams/s and 4.5 µs per
  datagram vs Asio's 172 k and 5.7 µs); 64 KB messages at 256 connections (25.7 vs 29.8 µs per
  message); timer cancellation (15 ms vs 38 ms for 100 000 timers); code size of the sender
  server (102 KB `.text` vs 118 KB).
* **Where it is behind, with identified causes**:
  1. **2.0–2.2 heap allocations per operation** (Asio: 0) and **1.7 `epoll_ctl` per message** at
     one connection — both from the per-descriptor map in `io_context` (#11);
  2. **`std::execution::task` posts every completion twice** (4.0 vs 2.0 per message), costing
     about 2.5 µs per UDP datagram (#12);
  3. **timer start is 3.3× slower than Asio** (#13);
  4. **compile time is 1.9× Asio's** (12.2 s vs 6.5 s per translation unit, stdexec headers);
  5. HTTP p99 latency at 64 connections is about 3× Asio's (3.1 ms vs 1.1 ms) — not explained
     yet, to be re-measured after #11.
* The single-mutex design could not be judged on two cores (#14).

![Echo throughput](../bench/results/2026-10-11/fig_echo_throughput.png)

![Server CPU per operation](../bench/results/2026-10-11/fig_cpu_per_op.png)

| Implementation | echo 16×64 B | echo 256×64 B | HTTP, 64 conns | UDP 64 B | churn |
|---|---|---|---|---|---|
| raw blocking | 82 790 · 10.4 | 75 713 · 12.0 | 83 209 · 10.2 | 281 977 · 3.4 | 10 890 · 117.9 |
| raw epoll | 139 536 · 6.2 | 149 224 · 5.9 | 155 562 · 6.0 | 275 460 · 3.4 | 21 013 · 24.8 |
| Asio callbacks | 160 561 · 5.9 | 183 275 · 5.3 | 157 090 · 6.0 | 171 555 · 5.7 | 23 268 · 23.9 |
| Asio coroutines | 140 803 · 6.5 | 136 002 · 7.0 | 133 530 · 7.0 | 161 968 · 6.1 | 24 370 · 28.1 |
| stdnet senders | 127 744 · 7.7 | 165 617 · 6.0 | 152 555 · 6.5 | 212 739 · 4.5 | 23 519 · 26.6 |
| stdnet task | 121 190 · 8.1 | 144 940 · 6.8 | 132 801 · 7.5 | 140 707 · 7.0 | 31 231 · 26.4 |

*Operations per second · server CPU µs per operation.*

![Latency](../bench/results/2026-10-11/fig_latency.png)

![Allocations and memory](../bench/results/2026-10-11/fig_memory.png)

## What the diagnostics show

**Allocations.** Tracing the allocations of the sender server (`BENCH_TRACE_ALLOCS`) attributes
them to `std::unordered_map::operator[]` in `io_context::start_io()`: the entry of a descriptor
is created when an operation starts and erased as soon as no operation is pending, so each
operation allocates (and frees) a 56-byte node — 2 operations, 2 allocations per echo message.
The remainder comes from the event `std::vector` built at each reactor wait.

**Reactor counters** (instrumented build, per echo message, measurement window):

| Build | conns · threads | operations started | completed in `start()` | had to wait | `epoll_wait` | `epoll_ctl` | posts |
|---|---|---|---|---|---|---|---|
| senders | 1 · 1 | 2.00 | 1.15 | 0.85 | 0.85 | 1.70 | 2.00 |
| senders | 64 · 1 | 2.00 | 1.53 | 0.47 | 0.05 | 0.94 | 2.00 |
| task | 1 · 1 | 1.99 | 1.10 | 0.88 | 0.88 | 1.77 | 3.97 |
| task | 64 · 1 | 2.00 | 1.82 | 0.18 | 0.01 | 0.36 | 4.00 |

Every operation that has to wait costs an `EPOLL_CTL_ADD` and an `EPOLL_CTL_DEL` (1.70 calls for
0.85 waiting operations). That is why the portable poll backend, which rebuilds its array
without system calls, is *faster* than epoll (146 k vs 123 k msg/s at 16 connections, 140 k vs
127 k at 256). With `task`, posts double: the coroutine is re-scheduled after each `co_await`
even though the operation already completed on the right context.

**System calls** inside the window at 16 connections: one `recvmsg`/`recvfrom` and one
`sendmsg`/`sendto` per message for stdnet, Asio and the raw loop alike. Under `strace` the
server is slowed down so much that almost every operation finds its socket ready, so this
profile shows which calls are made, not how often operations wait — the counters above do.

**Operation states**: 112 bytes for `async_receive`, 120 for `async_write`, 200 for a
receive-then-write iteration, 80 for a timer. A 64-byte-message server needs no heap beyond its
per-connection state once #11 is fixed.

**Memory per connection** (10 000 connections, 100 active, read with the connections open):
0.1 KB for the raw loop, 4.6 KB Asio callbacks, 4.7 KB stdnet senders, 5.3–5.4 KB for the
coroutine variants. Most of it is the touched part of each session's 64 KB receive buffer.
Kernel socket buffers are not part of the process RSS.

## Hypotheses of issue #10

| Hypothesis | Verdict |
|---|---|
| Single mutex, system calls under the lock, limits scaling | **Not measurable here** — on 2 cores every library slows down with a second server thread (the raw loop, with one reactor per thread, stays flat); condition-variable waits stay ≤ 0.01 per message. Needs a many-core run (#14). |
| `epoll_ctl(MOD)` at each interest change | **Confirmed, and worse**: ADD + DEL per waiting operation (1.70 per message at one connection); poll beats epoll. (#11) |
| Map lookup per readiness event | **Confirmed, and worse**: one node allocation per operation, the main source of allocations. (#11) |
| Immediate success still goes through the ready queue | **Confirmed in counts** (1.53 of 2 operations per message at 64 connections); cost not isolated (#15). |
| Buffer sequences copied; resolver allocations | Single-buffer copies are free; the resolver is not exercised by these scenarios — not measured. |
| iovec array rebuilt at each partial transfer | **Not a bottleneck**: at 64 KB per message stdnet matches or beats Asio (25.4 vs 25.9 µs at 16 connections, 25.7 vs 29.8 µs at 256). |

## Ranked improvements

| # | Change | Evidence | Expected effect |
|---|---|---|---|
| 1 | Per-socket reactor state, registered once, edge-triggered (#11) | 2.0–2.2 allocs/op; 1.70 `epoll_ctl`/msg; poll > epoll | 0 allocations; −2 syscalls per waiting operation; epoll ≥ poll |
| 2 | Remove the second scheduling hop of `task` (#12) | 4.0 vs 2.0 posts/msg; task 0.4–2.5 µs/op slower than senders | task ≈ senders |
| 3 | Allocation-free timer queue (#13) | 67.5 vs 20.2 ms to start 100 000 timers | start within 1.5× of Asio |
| 4 | Scaling run on ≥ 8 cores, then lock splitting if needed (#14) | inconclusive on 2 cores | — |
| 5 | Bounded inline completion (#15) | 1.5 immediate completions/msg all queued | lower CPU and latency at high load, to be measured |
| 6 | Compile time | 12.2 s vs 6.5 s (Asio) per TU | depends on stdexec / a bundled kernel; tracked, no issue yet |

## Re-running

```sh
python3 bench/run.py --build build-bench/bench --out bench/results/<date>
python3 bench/run.py --build build-bench/bench --out bench/results/<date> --only echo,backend   # partial re-run, merged
python3 bench/report.py bench/results/<date>
```

`run.py` exits with status 1 if any run is invalid. Each improvement above should land with a
partial re-run and an update of this page.
