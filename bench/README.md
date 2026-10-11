# stdnet benchmark suite

Compares stdnet with raw sockets and Asio on the same workloads (issue #10). Results and
analysis: [docs/PERF.md](../docs/PERF.md).

## Layout

| Path | What |
|---|---|
| `loadgen/loadgen.cpp` | Neutral load generator: plain non-blocking sockets + epoll, no dependency on the libraries under test. Modes `echo`, `idle`, `http`, `churn`, `udp`. |
| `servers/*.cpp` | One server per implementation, each handling `--mode echo|http|udp` and `--threads N`: `raw_blocking` (thread per connection), `raw_poll` (portable BSD sockets: non-blocking + `poll(2)`, the model of Winsock's `WSAPoll`), `raw_epoll` (hand-written edge-triggered loop, `SO_REUSEPORT` per thread), `asio_callbacks`, `asio_coroutines`, `stdnet_senders` (sender algorithms only), `stdnet_coroutines` (`std::execution::task`). |
| `common/bench_common.hpp` | Startup/stop protocol, global `operator new` counting, `BENCH_TRACE_ALLOCS`. |
| `inprocess/` | `timers` (100 000 timers, stdnet vs Asio) and `opstate_sizes`. |
| `run.py` | Runs the campaign: every scenario × implementation, repetitions, server CPU from `/proc`, reactor counters and `strace` profiles over the measurement window, validation of every run, build cost. |
| `compare.py` | Before/after comparison of two campaigns made on the same machine, with confidence intervals and Bonferroni-corrected tests. |
| `quick.py` | Two-minute interleaved A/B run (one or two builds, controls, Welch test) for iterating on an optimisation. |
| `instr.py` | Deterministic user-space instructions per operation under callgrind, with the hottest functions; about one minute. Needs valgrind. |
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

## Iterating on an optimisation

The full campaign takes about an hour; use it to publish numbers, not to try ideas.

```sh
# 1. does the change reduce the work done in user space? (deterministic, ~1 min)
python3 bench/instr.py --build before=../base/build/bench --build after=build-bench/bench --top 10
# 2. does it help end to end? (interleaved, with controls, ~2 min)
python3 bench/quick.py --build before=../base/build/bench --build after=build-bench/bench \
        --variants stdnet_senders,stdnet_coroutines,asio_callbacks --points echo16,http,echo256 --reps 5
```

Read them together: on loopback about 90 % of the server's CPU time is the kernel and varies by
10 % from run to run (the Asio control tells how much in each run), so `instr.py` is the only
tool that can resolve a change in the library's own code; `quick.py` tells whether latency,
throughput and CPU moved in practice and catches effects the instruction count cannot see
(batching, wake-ups, tail latency). Build the "before" tree in a `git worktree`
and pass its `bench` build directory.

## Method

* Server and load generator run on the same host over loopback; each point is the mean of
  `--reps` runs (6 in the published campaign) of 3 s after a 1 s warm-up. The sample standard
  deviation (n - 1) and the coefficient of variation are recorded, and the per-run values are
  kept in `results.json`.
* **Significance.** `report.py` compares stdnet with each reference using Welch's t-test and a
  95 % confidence interval of the ratio of means, with a Bonferroni correction over all the
  comparisons listed. A difference smaller than the noise is reported as *not significant*;
  conclusions in `docs/PERF.md` rely only on the significant ones (needs `scipy`).
* **Winsock is not measured.** The suite is Linux-only. `raw_poll` is the portable BSD model
  and the closest stand-in for Winsock; the Windows backend of stdnet is covered by the
  conformance tests in CI, not by performance numbers.
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
  This is a closed-loop generator: latency percentiles describe the server at the offered load
  that the loop produces (at most one message per connection), they say nothing about
  behaviour when the server is saturated by an open-loop arrival process. Coordinated omission
  does not apply to a one-in-flight loop, but the p99/p99.9 values must not be read as a
  saturation curve.
* **Robustness.** Every server and load generator is started with a time budget (a server must
  print `READY` within 20 s, a load generator is killed after a budget derived from its
  duration); a run that cannot be carried out is recorded as a problem instead of stopping the
  campaign, all child processes are killed at exit, and ports are checked to be free (TCP and
  UDP) before use.

## Results in git

`results/<date>/results.json` is a few hundred KB; a campaign adds that to the history each
time. If it becomes a problem, keep only `tables.md` and the figures in git and attach
`results.json` to a release or a CI artifact.

## Adding an implementation

Write `servers/<name>.cpp` following an existing server (print `READY <port>` via
`bench::announce`, serve snapshots and stop through `bench::wait_stop_signal`, print `bench::print_report`), add it to
`CMakeLists.txt`, and to `VARIANTS` in `run.py` and `ORDER`/`LABEL`/`COLOR` in `report.py`.
