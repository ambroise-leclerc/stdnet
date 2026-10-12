Run: 2026-10-11 20:21 · Intel(R) Xeon(R) Processor @ 2.80GHz · 2 cores · Linux 6.18.44-fc-v114 · c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 · 6 repetitions per point

### Echo — throughput (messages/s) and server CPU per message (µs)

| Implementation | 1 conn · 64 B | 1 conn · 4096 B | 1 conn · 65536 B | 16 conn · 64 B | 16 conn · 4096 B | 16 conn · 65536 B | 256 conn · 64 B | 256 conn · 4096 B | 256 conn · 65536 B |
|---|---|---|---|---|---|---|---|---|---|
| raw blocking | 19 034 · 24.3 | 16 681 · 28.5 | 11 297 · 47.6 | 73 816 · 11.9 | 68 906 · 12.3 | 30 656 · 29.5 | 59 858 · 14.2 | 58 555 · 14.5 | 22 128 · 39.4 |
| raw poll (BSD sockets) | 19 491 · 24.7 | 16 619 · 28.3 | 11 275 · 46.6 | 149 801 · 6.1 | 141 158 · 6.7 | 39 827 · 23.4 | 155 011 · 6.3 | 137 507 · 7.1 | 35 835 · 27.3 |
| raw epoll | 18 719 · 25.6 | 18 926 · 25.4 | 11 841 · 45.2 | 117 030 · 6.8 | 111 561 · 7.6 | 39 552 · 23.5 | 114 219 · 7.7 | 95 537 · 8.7 | 28 920 · 29.9 |
| Asio callbacks | 17 155 · 28.9 | 17 304 · 28.9 | 11 575 · 47.9 | 135 666 · 6.6 | 120 605 · 7.5 | 33 716 · 28.3 | 99 439 · 9.0 | 112 833 · 8.3 | 26 267 · 37.1 |
| Asio coroutines | 17 104 · 29.3 | 18 728 · 27.3 | 11 376 · 49.7 | 115 918 · 7.7 | 117 784 · 8.0 | 34 258 · 27.8 | 115 984 · 7.9 | 91 907 · 10.2 | 23 114 · 42.3 |
| stdnet senders | 17 867 · 27.8 | 16 706 · 29.6 | 11 189 · 50.1 | 138 949 · 6.9 | 103 868 · 8.8 | 32 718 · 29.4 | 112 312 · 8.2 | 107 458 · 9.0 | 25 473 · 38.9 |
| stdnet task | 17 014 · 31.1 | 16 543 · 32.1 | 12 586 · 47.3 | 114 803 · 8.3 | 108 714 · 8.8 | 33 892 · 28.9 | 133 110 · 7.3 | 106 719 · 9.0 | 27 531 · 36.3 |

### Throughput and server CPU per operation across scenarios

| Implementation | echo 16×64 B (ops/s · µs CPU/op) | echo 256×64 B (ops/s · µs CPU/op) | HTTP 64 conns (ops/s · µs CPU/op) | UDP 64 B (ops/s · µs CPU/op) | churn (ops/s · µs CPU/op) |
|---|---|---|---|---|---|
| raw blocking | 73 816 · 11.9 | 59 858 · 14.2 | 89 942 · 10.1 | 254 853 · 3.6 | 9 138 · 139.0 |
| raw poll (BSD sockets) | 149 801 · 6.1 | 155 011 · 6.3 | 177 842 · 5.5 | 258 885 · 3.5 | 21 226 · 25.2 |
| raw epoll | 117 030 · 6.8 | 114 219 · 7.7 | 136 926 · 6.4 | 230 414 · 4.1 | 19 394 · 25.8 |
| Asio callbacks | 135 666 · 6.6 | 99 439 · 9.0 | 150 076 · 6.2 | 147 360 · 6.6 | 20 503 · 26.5 |
| Asio coroutines | 115 918 · 7.7 | 115 984 · 7.9 | 142 219 · 6.6 | 133 250 · 7.3 | 21 866 · 31.4 |
| stdnet senders | 138 949 · 6.9 | 112 312 · 8.2 | 153 217 · 6.2 | 175 865 · 5.5 | 22 481 · 26.6 |
| stdnet task | 114 803 · 8.3 | 133 110 · 7.3 | 130 680 · 7.4 | 120 117 · 8.0 | 24 468 · 30.7 |

### Latency — round trip, 1 connection, 64 B (µs)

| Implementation | p50 | p99 | p99.9 |
|---|---|---|---|
| raw blocking | 51.6 | 129.3 | 312.4 |
| raw poll (BSD sockets) | 52.0 | 120.0 | 332.8 |
| raw epoll | 52.5 | 116.9 | 322.4 |
| Asio callbacks | 55.8 | 133.3 | 290.0 |
| Asio coroutines | 54.7 | 124.9 | 377.9 |
| stdnet senders | 53.0 | 117.2 | 309.9 |
| stdnet task | 55.8 | 131.7 | 372.9 |

### Heap allocations per operation and memory per idle connection

| Implementation | allocs/op echo 16×64 B | allocs/op HTTP | allocs/op UDP | RSS per connection, 10 000 conns (KB) |
|---|---|---|---|---|
| raw blocking | 0.00 | 1.00 | 0.00 | n/a (10 000 threads) |
| raw poll (BSD sockets) | 0.00 | 0.00 | 0.00 | 0.1 |
| raw epoll | 0.00 | 0.00 | 0.00 | 0.1 |
| Asio callbacks | 0.00 | 0.00 | 0.00 | 4.6 |
| Asio coroutines | 0.00 | 0.00 | 0.00 | 5.4 |
| stdnet senders | 0.00 | 0.00 | 0.00 | 4.7 |
| stdnet task | 0.00 | 0.00 | 0.00 | 5.3 |

### Server threads (echo, 64 connections × 64 B, messages/s)

| Implementation | 1 thread | 2 threads |
|---|---|---|
| raw epoll | 109 703 | 126 426 |
| Asio callbacks | 140 300 | 94 162 |
| Asio coroutines | 123 827 | 96 412 |
| stdnet senders | 136 596 | 75 641 |
| stdnet task | 111 336 | 71 105 |

### stdnet backends (echo, 64 B, messages/s · p99 µs)

Measured in their own runs, separately from the echo table above: the same point can differ by the run-to-run noise.

| Backend | 16 connections | 256 connections |
|---|---|---|
| stdnet senders | 126 083 · 552 | 136 733 · 10 058 |
| stdnet senders (poll) | 118 147 · 549 | 112 216 · 8 114 |

### Timers (100 000 concurrent timers; durations in ms)

| | start all | drain (deadlines over 50 ms) | cancel all |
|---|---|---|---|
| stdnet | 73.7 | 50.2 | 16.0 |
| Asio | 20.5 | 66.9 | 43.6 |
| stdnet / Asio | 3.60 (95 % CI 3.26 – 3.95, p = 0.0000) | 0.75 (95 % CI 0.72 – 0.78, p = 0.0000) | 0.37 (95 % CI 0.31 – 0.42, p = 0.0000) |

### Reactor counters per echo message (instrumented stdnet build, measurement window)

| Build | conns · threads | io_started | io_immediate | io_would_block | reactor_waits | interest_changes | posts | cv_waits |
|---|---|---|---|---|---|---|---|---|
| stdnet_senders | 1 · 1 | 2.00 | 1.01 | 0.99 | 0.99 | 0.00 | 2.00 | 0.00 |
| stdnet_senders | 64 · 1 | 2.00 | 1.57 | 0.43 | 0.08 | 0.00 | 2.00 | 0.00 |
| stdnet_senders | 64 · 2 | 2.00 | 1.23 | 0.77 | 0.20 | 0.00 | 2.00 | 0.04 |
| stdnet_coroutines | 1 · 1 | 2.00 | 1.03 | 0.96 | 0.98 | 0.00 | 3.99 | 0.00 |
| stdnet_coroutines | 64 · 1 | 2.00 | 1.66 | 0.33 | 0.07 | 0.00 | 3.99 | 0.00 |
| stdnet_coroutines | 64 · 2 | 2.00 | 1.51 | 0.49 | 0.12 | 0.00 | 4.00 | 0.03 |

### System calls per echo message (strace, 16 connections, measurement window only)

| Implementation | recvfrom | recvmsg | sendto | sendmsg | epoll_wait | epoll_ctl | futex | write | read | total |
|---|---|---|---|---|---|---|---|---|---|---|
| raw epoll | 1.01 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.01 |
| Asio callbacks | 1.01 | 0.00 | 1.00 | 0.00 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 2.14 |
| stdnet senders | 0.00 | 1.01 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.01 |

### Operation-state sizes (bytes)

| async_receive | async_write_1buf | async_write_2bufs | async_accept | async_connect | async_resume_after | receive_then_write | tcp_socket | io_context |
|---|---|---|---|---|---|---|---|---|
| 112 | 120 | 136 | 104 | 232 | 80 | 200 | 16 | 256 |

### Build cost (one server translation unit, -O2)

| Implementation | compile time (s) | .text (KB) |
|---|---|---|
| raw blocking | 1.6 | 18 |
| raw poll (BSD sockets) | 1.9 | 23 |
| raw epoll | 1.5 | 20 |
| Asio callbacks | 7.2 | 118 |
| Asio coroutines | 7.9 | 140 |
| stdnet senders | 12.9 | 103 |
| stdnet task | 10.3 | 206 |

### Cost over raw BSD sockets: server CPU per operation (µs), stdnet senders minus the raw loop

`raw poll` is the portable model (non-blocking sockets + `poll`, i.e. what Winsock's `WSAPoll` offers); `raw epoll` is the best a Linux readiness loop can do.

| Scenario | raw poll | raw epoll | Asio | stdnet senders | stdnet − raw poll | stdnet − raw epoll |
|---|---|---|---|---|---|---|
| echo 16 conns × 64 B, server CPU/msg | 6.1 | 6.8 | 6.6 | 6.9 | +0.7 (+12 %) | +0.1 (+2 %) |
| echo 256 conns × 64 B, server CPU/msg | 6.3 | 7.7 | 9.0 | 8.2 | +1.9 (+30 %) | +0.5 (+7 %) |
| echo 16 conns × 64 KB, server CPU/msg | 23.4 | 23.5 | 28.3 | 29.4 | +6.1 (+26 %) | +5.9 (+25 %) |
| HTTP 64 conns, server CPU/request | 5.5 | 6.4 | 6.2 | 6.2 | +0.7 (+13 %) | -0.2 (-4 %) |
| UDP 64 B, server CPU/datagram | 3.5 | 4.1 | 6.6 | 5.5 | +2.1 (+60 %) | +1.5 (+36 %) |
| churn, server CPU/connection | 25.2 | 25.8 | 26.5 | 26.6 | +1.4 (+6 %) | +0.8 (+3 %) |

### Statistical significance of the differences (lower ratio = stdnet better)

Ratio of means with its 95 % confidence interval, and Welch's t-test; 32 comparisons, so a difference is called *significant* only if p < 0.05 / 32 = 0.0016 (Bonferroni). *not significant* means the data cannot tell the two apart at this number of repetitions.

| Comparison | stdnet variant | vs | ratio | 95 % CI | p | n | verdict |
|---|---|---|---|---|---|---|---|
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | Asio callbacks | 1.04 | 0.83 – 1.26 | 0.6406 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.12 | 0.89 – 1.35 | 0.2399 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | raw epoll | 1.02 | 0.81 – 1.22 | 0.8562 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet task | Asio coroutines | 1.08 | 0.89 – 1.26 | 0.3579 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | Asio callbacks | 0.91 | 0.75 – 1.07 | 0.2634 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.30 | 1.09 – 1.52 | 0.0065 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | raw epoll | 1.07 | 0.90 – 1.23 | 0.3573 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet task | Asio coroutines | 0.92 | 0.81 – 1.02 | 0.1187 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | Asio callbacks | 1.04 | 0.91 – 1.17 | 0.4832 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.26 | 1.09 – 1.43 | 0.0027 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | raw epoll | 1.25 | 1.05 – 1.46 | 0.0099 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet task | Asio coroutines | 1.04 | 0.95 – 1.13 | 0.3382 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | Asio callbacks | 0.95 | 0.91 – 0.99 | 0.0300 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | raw poll (BSD sockets) | 1.02 | 0.93 – 1.10 | 0.6270 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | raw epoll | 1.01 | 0.95 – 1.06 | 0.7358 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet task | Asio coroutines | 1.02 | 0.96 – 1.08 | 0.4752 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | Asio callbacks | 0.99 | 0.91 – 1.07 | 0.8329 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | raw poll (BSD sockets) | 1.13 | 0.99 – 1.27 | 0.0528 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | raw epoll | 0.96 | 0.84 – 1.09 | 0.5267 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet task | Asio coroutines | 1.13 | 0.98 – 1.28 | 0.0696 | 6/6 | not significant |
| HTTP 64 conns, p99 latency | stdnet senders | Asio callbacks | 2.23 | 1.97 – 2.49 | 0.0000 | 6/6 | stdnet higher |
| HTTP 64 conns, p99 latency | stdnet senders | raw poll (BSD sockets) | 2.26 | 1.97 – 2.55 | 0.0000 | 6/6 | stdnet higher |
| HTTP 64 conns, p99 latency | stdnet senders | raw epoll | 1.87 | 1.62 – 2.12 | 0.0000 | 6/6 | stdnet higher |
| HTTP 64 conns, p99 latency | stdnet task | Asio coroutines | 2.59 | 1.84 – 3.34 | 0.0001 | 6/6 | stdnet higher |
| UDP 64 B, server CPU/datagram | stdnet senders | Asio callbacks | 0.84 | 0.71 – 0.98 | 0.0408 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet senders | raw poll (BSD sockets) | 1.60 | 1.35 – 1.85 | 0.0008 | 6/6 | stdnet higher |
| UDP 64 B, server CPU/datagram | stdnet senders | raw epoll | 1.36 | 1.12 – 1.60 | 0.0043 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet task | Asio coroutines | 1.10 | 0.97 – 1.22 | 0.0973 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | Asio callbacks | 1.00 | 0.95 – 1.06 | 0.8744 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | raw poll (BSD sockets) | 1.06 | 1.01 – 1.10 | 0.0228 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | raw epoll | 1.03 | 0.94 – 1.12 | 0.4082 | 6/6 | not significant |
| churn, server CPU/connection | stdnet task | Asio coroutines | 0.98 | 0.83 – 1.13 | 0.7545 | 6/6 | not significant |

Run-to-run variability (coefficient of variation, sample standard deviation / mean, 6 runs per point): throughput median 8.3 %, max 25.2 %; server CPU per operation median 8.1 %, max 20.1 %.
