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
| `run.py` | Runs the campaign: every scenario × implementation, repetitions, server CPU from `/proc`, reactor counters, `strace -c` profiles, build cost. |
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

`--quick --reps 1` gives a 2-minute functional run (used by CI). Linux only (epoll, `/proc`).

## Method

* Server and load generator run on the same host over loopback; each point is the mean of
  3 runs of 3 s after a 1 s warm-up; the coefficient of variation is recorded.
* **Server CPU per operation** (user + system time read from `/proc/<pid>/stat`, divided by
  completed operations) is the main efficiency metric: unlike throughput it does not depend on
  how much CPU the load generator leaves to the server.
* **Allocations** are counted by replacing the global `operator new` in each server, from the
  moment it prints `READY` until it stops. `BENCH_TRACE_ALLOCS=N` prints the call stacks of the
  first `N` counted allocations (build with `-rdynamic`).
* **Reactor counters**: the `*_stats` builds define `STDNET_ENABLE_STATS`.
* Requests are strictly request/response (one message in flight per connection), so throughput
  at 1 connection measures round-trip latency, and at many connections the per-message cost.

## Adding an implementation

Write `servers/<name>.cpp` following an existing server (print `READY <port>` via
`bench::announce`, stop on SIGINT, print `bench::print_report`), add it to
`CMakeLists.txt`, and to `VARIANTS` in `run.py` and `ORDER`/`LABEL`/`COLOR` in `report.py`.
