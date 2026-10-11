Run: 2026-10-11 12:51 · Intel(R) Xeon(R) Processor @ 2.80GHz · 2 cores · Linux 6.18.44-fc-v114 · c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 · 6 repetitions per point

### Echo — throughput (messages/s) and server CPU per message (µs)

| Implementation | 1 conn · 64 B | 1 conn · 4096 B | 1 conn · 65536 B | 16 conn · 64 B | 16 conn · 4096 B | 16 conn · 65536 B | 256 conn · 64 B | 256 conn · 4096 B | 256 conn · 65536 B |
|---|---|---|---|---|---|---|---|---|---|
| raw blocking | 20 898 · 22.3 | 18 352 · 25.3 | 12 941 · 40.4 | 77 919 · 11.1 | 72 376 · 11.8 | 31 661 · 28.0 | 75 309 · 11.6 | 64 470 · 13.4 | 24 450 · 35.6 |
| raw poll (BSD sockets) | 18 773 · 25.0 | 18 272 · 25.9 | 11 973 · 44.3 | 149 645 · 6.2 | 137 135 · 6.8 | 42 599 · 22.4 | 161 480 · 6.1 | 141 567 · 6.9 | 39 230 · 25.0 |
| raw epoll | 18 789 · 25.2 | 17 491 · 26.9 | 12 236 · 44.4 | 136 833 · 6.3 | 130 564 · 6.7 | 43 295 · 22.0 | 121 887 · 7.1 | 107 914 · 8.2 | 32 860 · 27.0 |
| Asio callbacks | 18 118 · 27.2 | 16 698 · 29.5 | 11 115 · 50.3 | 128 077 · 7.0 | 126 344 · 7.2 | 39 580 · 24.7 | 132 750 · 7.1 | 124 197 · 7.5 | 27 012 · 36.5 |
| Asio coroutines | 18 963 · 26.7 | 17 762 · 28.4 | 11 777 · 48.1 | 132 230 · 6.8 | 125 196 · 7.4 | 33 981 · 28.3 | 126 493 · 7.5 | 116 482 · 8.1 | 26 815 · 36.6 |
| stdnet senders | 18 268 · 29.3 | 18 761 · 28.8 | 11 550 · 48.9 | 117 986 · 8.1 | 116 540 · 8.2 | 37 379 · 26.4 | 141 436 · 6.9 | 119 592 · 8.2 | 32 695 · 30.2 |
| stdnet task | 17 661 · 31.9 | 16 394 · 34.0 | 11 621 · 52.2 | 92 911 · 10.4 | 88 919 · 10.9 | 34 898 · 28.1 | 114 648 · 8.9 | 108 334 · 9.1 | 32 752 · 30.2 |

### Throughput and server CPU per operation across scenarios

| Implementation | echo 16×64 B (ops/s · µs CPU/op) | echo 256×64 B (ops/s · µs CPU/op) | HTTP 64 conns (ops/s · µs CPU/op) | UDP 64 B (ops/s · µs CPU/op) | churn (ops/s · µs CPU/op) |
|---|---|---|---|---|---|
| raw blocking | 77 919 · 11.1 | 75 309 · 11.6 | 84 754 · 10.5 | 253 834 · 3.7 | 10 447 · 120.1 |
| raw poll (BSD sockets) | 149 645 · 6.2 | 161 480 · 6.1 | 159 607 · 6.1 | 258 047 · 3.6 | 20 752 · 24.9 |
| raw epoll | 136 833 · 6.3 | 121 887 · 7.1 | 127 346 · 6.7 | 231 559 · 4.0 | 20 751 · 24.7 |
| Asio callbacks | 128 077 · 7.0 | 132 750 · 7.1 | 127 035 · 7.2 | 161 570 · 6.0 | 21 347 · 25.0 |
| Asio coroutines | 132 230 · 6.8 | 126 493 · 7.5 | 127 595 · 7.6 | 149 105 · 6.5 | 24 476 · 28.3 |
| stdnet senders | 117 986 · 8.1 | 141 436 · 6.9 | 130 645 · 7.5 | 190 418 · 5.1 | 25 634 · 25.8 |
| stdnet task | 92 911 · 10.4 | 114 648 · 8.9 | 101 728 · 9.8 | 129 129 · 7.6 | 26 032 · 30.4 |

### Latency — round trip, 1 connection, 64 B (µs)

| Implementation | p50 | p99 | p99.9 |
|---|---|---|---|
| raw blocking | 48.2 | 110.0 | 333.5 |
| raw poll (BSD sockets) | 50.9 | 112.3 | 342.5 |
| raw epoll | 50.6 | 114.9 | 323.6 |
| Asio callbacks | 53.3 | 119.7 | 369.0 |
| Asio coroutines | 51.5 | 113.5 | 304.0 |
| stdnet senders | 52.5 | 126.1 | 285.5 |
| stdnet task | 54.1 | 117.9 | 285.2 |

### Heap allocations per operation and memory per idle connection

| Implementation | allocs/op echo 16×64 B | allocs/op HTTP | allocs/op UDP | RSS per connection, 10 000 conns (KB) |
|---|---|---|---|---|
| raw blocking | 0.00 | 1.00 | 0.00 | n/a (10 000 threads) |
| raw poll (BSD sockets) | 0.00 | 0.00 | 0.00 | 0.1 |
| raw epoll | 0.00 | 0.00 | 0.00 | 0.1 |
| Asio callbacks | 0.00 | 0.00 | 0.00 | 4.6 |
| Asio coroutines | 0.00 | 0.00 | 0.00 | 5.4 |
| stdnet senders | 2.27 | 2.13 | 2.01 | 4.7 |
| stdnet task | 2.21 | 2.05 | 2.00 | 5.3 |

### Server threads (echo, 64 connections × 64 B, messages/s)

| Implementation | 1 thread | 2 threads |
|---|---|---|
| raw epoll | 148 720 | 135 137 |
| Asio callbacks | 143 919 | 109 700 |
| Asio coroutines | 160 700 | 103 121 |
| stdnet senders | 152 116 | 110 903 |
| stdnet task | 114 457 | 70 942 |

### stdnet backends (echo, 64 B, messages/s · p99 µs)

Measured in their own runs, separately from the echo table above: the same point can differ by the run-to-run noise.

| Backend | 16 connections | 256 connections |
|---|---|---|
| stdnet senders | 120 824 · 544 | 138 381 · 20 469 |
| stdnet senders (poll) | 140 417 · 518 | 147 833 · 8 823 |

### Timers (100 000 concurrent timers; durations in ms)

| | start all | drain (deadlines over 50 ms) | cancel all |
|---|---|---|---|
| stdnet | 68.9 | 50.3 | 15.9 |
| Asio | 23.3 | 65.4 | 42.4 |
| stdnet / Asio | 2.95 (95 % CI 2.27 – 3.64, p = 0.0000) | 0.77 (95 % CI 0.64 – 0.89, p = 0.0148) | 0.37 (95 % CI 0.30 – 0.45, p = 0.0000) |

### Reactor counters per echo message (instrumented stdnet build, measurement window)

| Build | conns · threads | io_started | io_immediate | io_would_block | reactor_waits | interest_changes | posts | cv_waits |
|---|---|---|---|---|---|---|---|---|
| stdnet_senders | 1 · 1 | 2.00 | 1.00 | 0.99 | 0.99 | 1.99 | 2.00 | 0.00 |
| stdnet_senders | 64 · 1 | 2.00 | 1.75 | 0.25 | 0.02 | 0.49 | 2.00 | 0.00 |
| stdnet_senders | 64 · 2 | 2.00 | 1.63 | 0.37 | 0.03 | 0.73 | 2.00 | 0.01 |
| stdnet_coroutines | 1 · 1 | 2.00 | 1.02 | 0.98 | 0.98 | 1.96 | 4.00 | 0.00 |
| stdnet_coroutines | 64 · 1 | 2.00 | 1.84 | 0.16 | 0.02 | 0.32 | 4.00 | 0.00 |
| stdnet_coroutines | 64 · 2 | 2.00 | 1.70 | 0.30 | 0.02 | 0.61 | 4.00 | 0.01 |

### System calls per echo message (strace, 16 connections, measurement window only)

| Implementation | recvfrom | recvmsg | sendto | sendmsg | epoll_wait | epoll_ctl | futex | write | read | total |
|---|---|---|---|---|---|---|---|---|---|---|
| raw epoll | 1.01 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.01 |
| Asio callbacks | 1.01 | 0.00 | 1.00 | 0.00 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 2.14 |
| stdnet senders | 0.00 | 1.00 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.01 |

### Operation-state sizes (bytes)

| async_receive | async_write_1buf | async_write_2bufs | async_accept | async_connect | async_resume_after | receive_then_write | tcp_socket | io_context |
|---|---|---|---|---|---|---|---|---|
| 112 | 120 | 136 | 104 | 232 | 80 | 200 | 16 | 232 |

### Build cost (one server translation unit, -O2)

| Implementation | compile time (s) | .text (KB) |
|---|---|---|
| raw blocking | 1.3 | 18 |
| raw poll (BSD sockets) | 1.5 | 23 |
| raw epoll | 1.5 | 20 |
| Asio callbacks | 6.6 | 118 |
| Asio coroutines | 6.6 | 140 |
| stdnet senders | 13.2 | 102 |
| stdnet task | 10.2 | 207 |

### Cost over raw BSD sockets: server CPU per operation (µs), stdnet senders minus the raw loop

`raw poll` is the portable model (non-blocking sockets + `poll`, i.e. what Winsock's `WSAPoll` offers); `raw epoll` is the best a Linux readiness loop can do.

| Scenario | raw poll | raw epoll | Asio | stdnet senders | stdnet − raw poll | stdnet − raw epoll |
|---|---|---|---|---|---|---|
| echo 16 conns × 64 B, server CPU/msg | 6.2 | 6.3 | 7.0 | 8.1 | +1.9 (+31 %) | +1.8 (+29 %) |
| echo 256 conns × 64 B, server CPU/msg | 6.1 | 7.1 | 7.1 | 6.9 | +0.8 (+14 %) | -0.2 (-3 %) |
| echo 16 conns × 64 KB, server CPU/msg | 22.4 | 22.0 | 24.7 | 26.4 | +4.0 (+18 %) | +4.4 (+20 %) |
| HTTP 64 conns, server CPU/request | 6.1 | 6.7 | 7.2 | 7.5 | +1.4 (+23 %) | +0.8 (+12 %) |
| UDP 64 B, server CPU/datagram | 3.6 | 4.0 | 6.0 | 5.1 | +1.5 (+43 %) | +1.1 (+28 %) |
| churn, server CPU/connection | 24.9 | 24.7 | 25.0 | 25.8 | +0.8 (+3 %) | +1.1 (+4 %) |

### Statistical significance of the differences (lower ratio = stdnet better)

Ratio of means with its 95 % confidence interval, and Welch's t-test; 32 comparisons, so a difference is called *significant* only if p < 0.05 / 32 = 0.0016 (Bonferroni). *not significant* means the data cannot tell the two apart at this number of repetitions.

| Comparison | stdnet variant | vs | ratio | 95 % CI | p | n | verdict |
|---|---|---|---|---|---|---|---|
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | Asio callbacks | 1.15 | 1.01 – 1.30 | 0.0288 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.31 | 1.15 – 1.47 | 0.0004 | 6/6 | stdnet higher |
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | raw epoll | 1.29 | 1.15 – 1.42 | 0.0003 | 6/6 | stdnet higher |
| echo 16 conns × 64 B, server CPU/msg | stdnet task | Asio coroutines | 1.52 | 1.38 – 1.66 | 0.0001 | 6/6 | stdnet higher |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | Asio callbacks | 0.98 | 0.78 – 1.18 | 0.8180 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.14 | 0.90 – 1.37 | 0.1971 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | raw epoll | 0.97 | 0.78 – 1.16 | 0.7223 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet task | Asio coroutines | 1.19 | 0.93 – 1.46 | 0.1173 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | Asio callbacks | 1.07 | 0.86 – 1.28 | 0.4522 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.18 | 0.95 – 1.40 | 0.0880 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | raw epoll | 1.20 | 0.97 – 1.43 | 0.0607 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet task | Asio coroutines | 0.99 | 0.88 – 1.11 | 0.8692 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | Asio callbacks | 0.99 | 0.95 – 1.02 | 0.3644 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | raw poll (BSD sockets) | 1.03 | 0.99 – 1.07 | 0.0959 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | raw epoll | 1.04 | 0.99 – 1.09 | 0.1171 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet task | Asio coroutines | 1.05 | 1.03 – 1.07 | 0.0004 | 6/6 | stdnet higher |
| HTTP 64 conns, server CPU/request | stdnet senders | Asio callbacks | 1.04 | 0.85 – 1.23 | 0.6163 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | raw poll (BSD sockets) | 1.23 | 1.01 – 1.45 | 0.0325 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | raw epoll | 1.12 | 0.91 – 1.32 | 0.2238 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet task | Asio coroutines | 1.29 | 1.00 – 1.59 | 0.0289 | 6/6 | not significant |
| HTTP 64 conns, p99 latency | stdnet senders | Asio callbacks | 2.22 | 1.81 – 2.62 | 0.0002 | 6/6 | stdnet higher |
| HTTP 64 conns, p99 latency | stdnet senders | raw poll (BSD sockets) | 2.51 | 2.00 – 3.02 | 0.0001 | 6/6 | stdnet higher |
| HTTP 64 conns, p99 latency | stdnet senders | raw epoll | 2.19 | 1.78 – 2.60 | 0.0002 | 6/6 | stdnet higher |
| HTTP 64 conns, p99 latency | stdnet task | Asio coroutines | 2.07 | 1.51 – 2.63 | 0.0012 | 6/6 | stdnet higher |
| UDP 64 B, server CPU/datagram | stdnet senders | Asio callbacks | 0.86 | 0.70 – 1.02 | 0.0862 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet senders | raw poll (BSD sockets) | 1.43 | 1.12 – 1.74 | 0.0083 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet senders | raw epoll | 1.28 | 1.00 – 1.56 | 0.0366 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet task | Asio coroutines | 1.18 | 1.05 – 1.30 | 0.0084 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | Asio callbacks | 1.03 | 0.97 – 1.09 | 0.3095 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | raw poll (BSD sockets) | 1.03 | 0.97 – 1.09 | 0.2495 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | raw epoll | 1.04 | 0.97 – 1.11 | 0.1776 | 6/6 | not significant |
| churn, server CPU/connection | stdnet task | Asio coroutines | 1.08 | 0.98 – 1.18 | 0.1141 | 6/6 | not significant |

Run-to-run variability (coefficient of variation, sample standard deviation / mean, 6 runs per point): throughput median 9.4 %, max 22.6 %; server CPU per operation median 8.9 %, max 21.7 %.
