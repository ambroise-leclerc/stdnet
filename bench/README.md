# stdnet benchmark suite

Compares stdnet with raw sockets and Asio on the same workloads (issue #10). Results and
analysis: [docs/PERF.md](../docs/PERF.md).

## Layout

| Path | What |
|---|---|
| `loadgen/loadgen.cpp` | Neutral load generator: plain non-blocking sockets + epoll, no dependency on the libraries under test. Modes `echo`, `idle`, `http`, `churn`, `udp`. |
| `servers/*.cpp` | One server per implementation, each handling `--mode echo|http|udp` and `--threads N`: `raw_blocking` (thread per connection), `raw_epoll` (hand-written edge-triggered loop, `SO_REUSEPORT` per thread), `asio_callbacks`, `asio_coroutines`, `stdnet_senders` (sender algorithms only), `stdnet_coroutines` (`std::execution::task`). |
| `common/bench_common.hpp` | Startup/stop protocol, global `operator new` counting, `BENCH_TRACE_ALLOCS`. |
| `inprocess/` | `timers` (100 000 timers, stdnet vs Asio) and `opstate_sizes`. |
| `run.py` | Runs the campaign: every scenario × implementation, repetitions, server CPU from `/proc`, reactor counters and `strace` profiles over the measurement window, validation of every run, build cost. |
| `report.py` | Turns `results.json` into Markdown tables and charts. |
| `results/<date>/` | Raw results, tables and charts of a campaign. |

## Running

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DSTDNET_BUILD_BENCH=ON \
      -DSTDNET_BUILD_TESTS=OFF -DSTDNET_BUILD_EXAMPLES=OFF
cmake --build build-bench -j
python3 bench/run.py --build build-bench/bench --out bench/results/$(date +%F)   # ~25 min
python3 bench/report.py bench/results/$(date +%F)                                # needs matplotlib
```

`--quick --reps 1` gives a 2-minute functional run (used by CI; it fails on any invalid run).
Linux only (epoll, `/proc`).

## Method

* Server and load generator run on the same host over loopback; each point is the mean of
  3 runs of 3 s after a 1 s warm-up; the coefficient of variation is recorded.
* **One measurement window for everything.** The load generator connects, then runs a warm-up
  and a measured window; it counts only the operations started inside the window and prints
  `MEASURE_START` / `MEASURE_END` at its boundaries (`--hold 1`). At each marker `run.py` reads
  the server's CPU time and RSS from `/proc` and sends it `SIGUSR1` / `SIGUSR2`, to which the
  server answers with a snapshot of its allocation and reactor counters. Every per-operation
  ratio is a difference between the two snapshots divided by the operations of the same window,
  so it does not depend on the warm-up length.
* **Memory per connection** is the RSS at the end of the window minus the RSS before the first
  connection, read while every connection is still open: the load generator keeps them until
  `run.py` has sampled the server. (Kernel socket buffers are not part of the process RSS.)
* **Server CPU per operation** is the main efficiency metric: unlike throughput it does not
  depend on how much CPU the load generator leaves to the server.
* **Allocations** are counted by replacing the global `operator new` in each server.
  `BENCH_TRACE_ALLOCS=N` prints the call stacks of the first `N` counted allocations (build with
  `-rdynamic`).
* **Reactor counters** come from the `*_stats` builds (`STDNET_ENABLE_STATS`). **System calls**
  are counted from an `strace -f -ttt` log, keeping only the calls whose timestamps fall in the
  window; tracing slows the server down a lot, so it shows which calls are made, not how often
  operations have to wait — the counters answer that.
* **Validation.** A run is rejected — and `run.py` exits with status 1 after writing the
  results — if the load generator fails or completes no operation, reports any connection
  error, loses more than 1 % of UDP datagrams, if the server dies or exits abnormally, or if a
  window snapshot is missing. This applies to every network scenario, the diagnostics and the
  timer benchmark (which checks that every timer fired or was cancelled).
* Requests are strictly request/response (one message in flight per connection), so throughput
  at 1 connection measures round-trip latency, and at many connections the per-message cost.

## Adding an implementation

Write `servers/<name>.cpp` following an existing server (print `READY <port>` via
`bench::announce`, serve snapshots and stop through `bench::wait_stop_signal`, print `bench::print_report`), add it to
`CMakeLists.txt`, and to `VARIANTS` in `run.py` and `ORDER`/`LABEL`/`COLOR` in `report.py`.
