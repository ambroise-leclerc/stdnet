Run: 2026-10-11 18:48 · AMD Ryzen 9 3900X 12-Core Processor · 24 cores · Linux 7.0.0-38-generic · c++ (GCC) 16.1.0 · 6 repetitions per point

### Echo — throughput (messages/s) and server CPU per message (µs)

| Implementation | 1 conn · 64 B | 1 conn · 4096 B | 1 conn · 65536 B | 16 conn · 64 B | 16 conn · 4096 B | 16 conn · 65536 B | 256 conn · 64 B | 256 conn · 4096 B | 256 conn · 65536 B |
|---|---|---|---|---|---|---|---|---|---|
| raw blocking | 35 354 · 14.7 | 32 581 · 16.1 | 14 916 · 40.3 | 72 164 · 19.2 | 64 927 · 21.3 | 21 506 · 55.7 | 68 702 · 20.6 | 61 771 · 22.8 | 19 157 · 64.8 |
| raw poll (BSD sockets) | 33 513 · 15.6 | 30 621 · 17.1 | 13 631 · 52.6 | 85 580 · 11.6 | 81 007 · 12.3 | 24 609 · 40.4 | 80 059 · 12.5 | 76 774 · 13.0 | 23 053 · 43.4 |
| raw epoll | 33 782 · 16.3 | 31 100 · 17.6 | 14 534 · 51.2 | 86 466 · 11.4 | 79 306 · 12.4 | 24 762 · 40.1 | 85 282 · 11.5 | 76 457 · 12.7 | 23 613 · 42.0 |
| Asio callbacks | 33 044 · 17.4 | 30 654 · 18.6 | 13 464 · 55.5 | 87 387 · 11.4 | 77 131 · 12.9 | 23 898 · 41.7 | 84 339 · 11.8 | 76 732 · 12.9 | 19 555 · 51.2 |
| Asio coroutines | 32 808 · 17.6 | 30 229 · 19.1 | 13 708 · 55.0 | 82 289 · 12.1 | 74 392 · 13.2 | 22 874 · 43.4 | 85 241 · 11.7 | 73 609 · 13.4 | 18 887 · 52.8 |
| stdnet senders | 33 167 · 17.2 | 30 867 · 18.4 | 13 634 · 54.4 | 85 154 · 11.6 | 77 340 · 12.9 | 23 527 · 42.4 | 83 159 · 11.9 | 73 861 · 13.5 | 21 644 · 46.4 |
| stdnet task | 32 003 · 18.7 | 29 278 · 20.2 | 13 159 · 57.3 | 84 635 · 11.8 | 73 087 · 13.7 | 23 007 · 43.3 | 82 001 · 12.2 | 76 852 · 13.0 | 21 399 · 47.0 |

### Throughput and server CPU per operation across scenarios

| Implementation | echo 16×64 B (ops/s · µs CPU/op) | echo 256×64 B (ops/s · µs CPU/op) | HTTP 64 conns (ops/s · µs CPU/op) | UDP 64 B (ops/s · µs CPU/op) | churn (ops/s · µs CPU/op) |
|---|---|---|---|---|---|
| raw blocking | 72 164 · 19.2 | 68 702 · 20.6 | 72 451 · 21.1 | 119 992 · 8.3 | 12 516 · 178.9 |
| raw poll (BSD sockets) | 85 580 · 11.6 | 80 059 · 12.5 | 87 774 · 11.3 | 116 151 · 8.5 | 14 666 · 46.8 |
| raw epoll | 86 466 · 11.4 | 85 282 · 11.5 | 88 461 · 11.3 | 115 779 · 8.5 | 14 896 · 42.6 |
| Asio callbacks | 87 387 · 11.4 | 84 339 · 11.8 | 86 516 · 11.5 | 104 178 · 9.6 | 14 505 · 45.0 |
| Asio coroutines | 82 289 · 12.1 | 85 241 · 11.7 | 86 151 · 11.6 | 99 340 · 10.0 | 14 152 · 49.5 |
| stdnet senders | 85 154 · 11.6 | 83 159 · 11.9 | 87 481 · 11.4 | 111 219 · 9.0 | 14 536 · 47.0 |
| stdnet task | 84 635 · 11.8 | 82 001 · 12.2 | 86 709 · 11.5 | 97 335 · 10.2 | 14 746 · 50.2 |

### Latency — round trip, 1 connection, 64 B (µs)

| Implementation | p50 | p99 | p99.9 |
|---|---|---|---|
| raw blocking | 26.5 | 43.3 | 58.9 |
| raw poll (BSD sockets) | 27.9 | 46.7 | 63.9 |
| raw epoll | 27.9 | 44.2 | 61.2 |
| Asio callbacks | 28.6 | 46.2 | 64.3 |
| Asio coroutines | 28.8 | 46.2 | 65.8 |
| stdnet senders | 28.4 | 46.4 | 63.4 |
| stdnet task | 29.7 | 48.6 | 66.4 |

### Heap allocations per operation and memory per idle connection

| Implementation | allocs/op echo 16×64 B | allocs/op HTTP | allocs/op UDP | RSS per connection, 10 000 conns (KB) |
|---|---|---|---|---|
| raw blocking | 0.00 | 1.00 | 0.00 | n/a (10 000 threads) |
| raw poll (BSD sockets) | 0.00 | 0.00 | 0.00 | 0.1 |
| raw epoll | 0.00 | 0.00 | 0.00 | 0.1 |
| Asio callbacks | 0.00 | 0.00 | 0.00 | 4.6 |
| Asio coroutines | 0.00 | 0.00 | 0.00 | 5.3 |
| stdnet senders | 0.00 | 0.00 | 0.00 | 4.7 |
| stdnet task | 0.00 | 0.00 | 0.00 | 5.3 |

### Server threads (echo, 64 connections × 64 B, messages/s)

| Implementation | 1 thread | 2 threads |
|---|---|---|
| raw epoll | 86 295 | 82 119 |
| Asio callbacks | 84 461 | 74 392 |
| Asio coroutines | 83 475 | 77 580 |
| stdnet senders | 85 375 | 72 500 |
| stdnet task | 84 954 | 65 159 |

### stdnet backends (echo, 64 B, messages/s · p99 µs)

Measured in their own runs, separately from the echo table above: the same point can differ by the run-to-run noise.

| Backend | 16 connections | 256 connections |
|---|---|---|
| stdnet senders | 85 030 · 357 | 81 422 · 7 556 |
| stdnet senders (poll) | 87 929 · 382 | 85 332 · 6 327 |

### Timers (100 000 concurrent timers; durations in ms)

| | start all | drain (deadlines over 50 ms) | cancel all |
|---|---|---|---|
| stdnet | 43.4 | 50.4 | 9.3 |
| Asio | 13.6 | 49.9 | 21.1 |
| stdnet / Asio | 3.19 (95 % CI 2.90 – 3.49, p = 0.0000) | 1.01 (95 % CI 1.01 – 1.02, p = 0.0015) | 0.44 (95 % CI 0.36 – 0.52, p = 0.0004) |

### Reactor counters per echo message (instrumented stdnet build, measurement window)

| Build | conns · threads | io_started | io_immediate | io_would_block | reactor_waits | interest_changes | posts | cv_waits |
|---|---|---|---|---|---|---|---|---|
| stdnet_senders | 1 · 1 | 2.00 | 1.01 | 0.99 | 0.99 | 0.00 | 2.00 | 0.00 |
| stdnet_senders | 64 · 1 | 2.00 | 1.18 | 0.82 | 0.06 | 0.00 | 2.00 | 0.00 |
| stdnet_senders | 64 · 2 | 2.00 | 1.94 | 0.06 | 0.00 | 0.00 | 2.00 | 0.00 |
| stdnet_coroutines | 1 · 1 | 2.00 | 1.00 | 1.00 | 1.00 | 0.00 | 4.00 | 0.00 |
| stdnet_coroutines | 64 · 1 | 2.00 | 1.80 | 0.20 | 0.00 | 0.00 | 4.00 | 0.00 |
| stdnet_coroutines | 64 · 2 | 2.00 | 2.00 | 0.00 | 0.00 | 0.00 | 4.00 | 0.00 |

### System calls per echo message (strace, 16 connections, measurement window only)

| Implementation | recvfrom | recvmsg | sendto | sendmsg | epoll_wait | epoll_ctl | futex | write | read | total |
|---|---|---|---|---|---|---|---|---|---|---|
| raw epoll | 1.01 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.01 |
| Asio callbacks | 1.00 | 0.00 | 1.00 | 0.00 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 2.13 |
| stdnet senders | 0.00 | 1.00 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.00 |

### Operation-state sizes (bytes)

| async_receive | async_write_1buf | async_write_2bufs | async_accept | async_connect | async_resume_after | receive_then_write | tcp_socket | io_context |
|---|---|---|---|---|---|---|---|---|
| 112 | 120 | 136 | 104 | 232 | 80 | 200 | 16 | 256 |

### Cost over raw BSD sockets: server CPU per operation (µs), stdnet senders minus the raw loop

`raw poll` is the portable model (non-blocking sockets + `poll`, i.e. what Winsock's `WSAPoll` offers); `raw epoll` is the best a Linux readiness loop can do.

| Scenario | raw poll | raw epoll | Asio | stdnet senders | stdnet − raw poll | stdnet − raw epoll |
|---|---|---|---|---|---|---|
| echo 16 conns × 64 B, server CPU/msg | 11.6 | 11.4 | 11.4 | 11.6 | +0.1 (+0 %) | +0.2 (+2 %) |
| echo 256 conns × 64 B, server CPU/msg | 12.5 | 11.5 | 11.8 | 11.9 | -0.5 (-4 %) | +0.4 (+3 %) |
| echo 16 conns × 64 KB, server CPU/msg | 40.4 | 40.1 | 41.7 | 42.4 | +2.0 (+5 %) | +2.3 (+6 %) |
| HTTP 64 conns, server CPU/request | 11.3 | 11.3 | 11.5 | 11.4 | +0.1 (+1 %) | +0.1 (+1 %) |
| UDP 64 B, server CPU/datagram | 8.5 | 8.5 | 9.6 | 9.0 | +0.4 (+5 %) | +0.4 (+5 %) |
| churn, server CPU/connection | 46.8 | 42.6 | 45.0 | 47.0 | +0.2 (+0 %) | +4.3 (+10 %) |

### Statistical significance of the differences (lower ratio = stdnet better)

Ratio of means with its 95 % confidence interval, and Welch's t-test; 32 comparisons, so a difference is called *significant* only if p < 0.05 / 32 = 0.0016 (Bonferroni). *not significant* means the data cannot tell the two apart at this number of repetitions.

| Comparison | stdnet variant | vs | ratio | 95 % CI | p | n | verdict |
|---|---|---|---|---|---|---|---|
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | Asio callbacks | 1.02 | 0.96 – 1.08 | 0.4273 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.00 | 0.93 – 1.08 | 0.8937 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | raw epoll | 1.02 | 0.94 – 1.10 | 0.5396 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet task | Asio coroutines | 0.97 | 0.88 – 1.06 | 0.4941 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | Asio callbacks | 1.01 | 0.92 – 1.10 | 0.8050 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 0.96 | 0.85 – 1.06 | 0.3869 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | raw epoll | 1.03 | 0.95 – 1.11 | 0.3533 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet task | Asio coroutines | 1.04 | 0.95 – 1.13 | 0.3445 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | Asio callbacks | 1.02 | 0.93 – 1.10 | 0.6501 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.05 | 0.97 – 1.13 | 0.1926 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | raw epoll | 1.06 | 0.97 – 1.15 | 0.1696 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet task | Asio coroutines | 1.00 | 0.93 – 1.07 | 0.9574 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | Asio callbacks | 0.99 | 0.91 – 1.07 | 0.8081 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | raw poll (BSD sockets) | 1.02 | 0.97 – 1.07 | 0.4435 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | raw epoll | 1.02 | 0.94 – 1.09 | 0.6019 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet task | Asio coroutines | 1.03 | 0.92 – 1.14 | 0.5017 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | Asio callbacks | 0.99 | 0.92 – 1.07 | 0.8424 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | raw poll (BSD sockets) | 1.01 | 0.94 – 1.08 | 0.8164 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | raw epoll | 1.01 | 0.94 – 1.08 | 0.7123 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet task | Asio coroutines | 1.00 | 0.93 – 1.07 | 0.9309 | 6/6 | not significant |
| HTTP 64 conns, p99 latency | stdnet senders | Asio callbacks | 1.50 | 0.99 – 2.02 | 0.0201 | 6/6 | not significant |
| HTTP 64 conns, p99 latency | stdnet senders | raw poll (BSD sockets) | 1.31 | 0.99 – 1.63 | 0.0423 | 6/6 | not significant |
| HTTP 64 conns, p99 latency | stdnet senders | raw epoll | 1.36 | 1.05 – 1.67 | 0.0273 | 6/6 | not significant |
| HTTP 64 conns, p99 latency | stdnet task | Asio coroutines | 6.47 | 5.51 – 7.43 | 0.0000 | 6/6 | stdnet higher |
| UDP 64 B, server CPU/datagram | stdnet senders | Asio callbacks | 0.94 | 0.88 – 0.99 | 0.0380 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet senders | raw poll (BSD sockets) | 1.05 | 1.00 – 1.10 | 0.0451 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet senders | raw epoll | 1.05 | 1.00 – 1.10 | 0.0466 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet task | Asio coroutines | 1.02 | 0.95 – 1.09 | 0.5404 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | Asio callbacks | 1.04 | 0.94 – 1.15 | 0.3573 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | raw poll (BSD sockets) | 1.00 | 0.90 – 1.10 | 0.9350 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | raw epoll | 1.10 | 1.00 – 1.21 | 0.0490 | 6/6 | not significant |
| churn, server CPU/connection | stdnet task | Asio coroutines | 1.01 | 0.94 – 1.08 | 0.6905 | 6/6 | not significant |

Run-to-run variability (coefficient of variation, sample standard deviation / mean, 6 runs per point): throughput median 5.9 %, max 10.0 %; server CPU per operation median 5.5 %, max 9.8 %.
