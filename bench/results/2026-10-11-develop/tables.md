Run: 2026-10-11 18:02 · AMD Ryzen 9 3900X 12-Core Processor · 24 cores · Linux 7.0.0-38-generic · c++ (GCC) 16.1.0 · 6 repetitions per point

### Echo — throughput (messages/s) and server CPU per message (µs)

| Implementation | 1 conn · 64 B | 1 conn · 4096 B | 1 conn · 65536 B | 16 conn · 64 B | 16 conn · 4096 B | 16 conn · 65536 B | 256 conn · 64 B | 256 conn · 4096 B | 256 conn · 65536 B |
|---|---|---|---|---|---|---|---|---|---|
| raw blocking | 35 108 · 14.9 | 31 954 · 16.4 | 14 346 · 41.8 | 73 088 · 18.9 | 63 186 · 21.7 | 20 984 · 56.9 | 69 813 · 20.2 | 61 076 · 23.5 | 18 991 · 64.9 |
| raw poll (BSD sockets) | 32 432 · 16.1 | 31 443 · 16.6 | 13 781 · 52.3 | 84 854 · 11.7 | 78 080 · 12.8 | 24 582 · 40.7 | 83 641 · 11.9 | 73 377 · 13.5 | 22 442 · 44.4 |
| raw epoll | 34 021 · 16.1 | 31 688 · 17.3 | 14 434 · 51.3 | 85 689 · 11.5 | 77 628 · 12.6 | 24 999 · 39.7 | 82 090 · 11.9 | 75 715 · 12.9 | 22 726 · 43.5 |
| Asio callbacks | 34 284 · 16.6 | 30 774 · 18.5 | 13 309 · 55.8 | 86 251 · 11.5 | 79 357 · 12.5 | 24 096 · 41.0 | 82 708 · 12.0 | 76 888 · 12.9 | 19 399 · 51.7 |
| Asio coroutines | 32 224 · 18.1 | 30 355 · 18.9 | 13 204 · 56.8 | 85 004 · 11.7 | 76 605 · 12.9 | 23 001 · 43.2 | 83 037 · 12.0 | 74 263 · 13.4 | 19 287 · 52.0 |
| stdnet senders | 31 502 · 19.6 | 29 834 · 20.6 | 13 367 · 56.6 | 80 567 · 12.4 | 73 137 · 13.6 | 23 479 · 42.4 | 83 624 · 11.9 | 75 002 · 13.3 | 21 713 · 46.1 |
| stdnet task | 30 580 · 21.1 | 28 557 · 22.4 | 12 866 · 59.8 | 78 977 · 12.7 | 72 910 · 13.7 | 22 732 · 43.7 | 82 949 · 12.1 | 74 044 · 13.5 | 22 024 · 45.6 |

### Throughput and server CPU per operation across scenarios

| Implementation | echo 16×64 B (ops/s · µs CPU/op) | echo 256×64 B (ops/s · µs CPU/op) | HTTP 64 conns (ops/s · µs CPU/op) | UDP 64 B (ops/s · µs CPU/op) | churn (ops/s · µs CPU/op) |
|---|---|---|---|---|---|
| raw blocking | 73 088 · 18.9 | 69 813 · 20.2 | 72 785 · 21.0 | 119 777 · 8.3 | 12 496 · 174.7 |
| raw poll (BSD sockets) | 84 854 · 11.7 | 83 641 · 11.9 | 88 893 · 11.2 | 120 964 · 8.2 | 14 806 · 46.3 |
| raw epoll | 85 689 · 11.5 | 82 090 · 11.9 | 87 089 · 11.4 | 117 200 · 8.5 | 14 449 · 44.0 |
| Asio callbacks | 86 251 · 11.5 | 82 708 · 12.0 | 87 709 · 11.3 | 104 330 · 9.6 | 14 705 · 44.8 |
| Asio coroutines | 85 004 · 11.7 | 83 037 · 12.0 | 87 197 · 11.4 | 100 365 · 9.9 | 14 034 · 50.2 |
| stdnet senders | 80 567 · 12.4 | 83 624 · 11.9 | 87 503 · 11.4 | 111 711 · 8.9 | 14 655 · 49.3 |
| stdnet task | 78 977 · 12.7 | 82 949 · 12.1 | 83 183 · 12.0 | 96 808 · 10.2 | 14 712 · 53.0 |

### Latency — round trip, 1 connection, 64 B (µs)

| Implementation | p50 | p99 | p99.9 |
|---|---|---|---|
| raw blocking | 26.8 | 43.0 | 60.7 |
| raw poll (BSD sockets) | 29.0 | 48.0 | 73.7 |
| raw epoll | 27.9 | 44.2 | 60.3 |
| Asio callbacks | 27.7 | 43.2 | 58.8 |
| Asio coroutines | 29.2 | 47.5 | 63.1 |
| stdnet senders | 29.7 | 49.5 | 68.1 |
| stdnet task | 30.5 | 51.7 | 75.1 |

### Heap allocations per operation and memory per idle connection

| Implementation | allocs/op echo 16×64 B | allocs/op HTTP | allocs/op UDP | RSS per connection, 10 000 conns (KB) |
|---|---|---|---|---|
| raw blocking | 0.00 | 1.00 | 0.00 | n/a (10 000 threads) |
| raw poll (BSD sockets) | 0.00 | 0.00 | 0.00 | 0.1 |
| raw epoll | 0.00 | 0.00 | 0.00 | 0.1 |
| Asio callbacks | 0.00 | 0.00 | 0.00 | 4.6 |
| Asio coroutines | 0.00 | 0.00 | 0.00 | 5.3 |
| stdnet senders | 2.18 | 2.03 | 2.00 | 4.7 |
| stdnet task | 2.09 | 2.00 | 2.00 | 5.3 |

### Server threads (echo, 64 connections × 64 B, messages/s)

| Implementation | 1 thread | 2 threads |
|---|---|---|
| raw epoll | 87 148 | 78 329 |
| Asio callbacks | 83 936 | 77 589 |
| Asio coroutines | 86 360 | 77 342 |
| stdnet senders | 83 640 | 71 994 |
| stdnet task | 86 230 | 63 704 |

### stdnet backends (echo, 64 B, messages/s · p99 µs)

Measured in their own runs, separately from the echo table above: the same point can differ by the run-to-run noise.

| Backend | 16 connections | 256 connections |
|---|---|---|
| stdnet senders | 83 122 · 515 | 84 041 · 11 429 |
| stdnet senders (poll) | 88 585 · 473 | 84 697 · 8 683 |

### Timers (100 000 concurrent timers; durations in ms)

| | start all | drain (deadlines over 50 ms) | cancel all |
|---|---|---|---|
| stdnet | 45.4 | 50.5 | 10.8 |
| Asio | 15.8 | 49.9 | 23.9 |
| stdnet / Asio | 2.86 (95 % CI 2.03 – 3.70, p = 0.0000) | 1.01 (95 % CI 1.01 – 1.02, p = 0.0011) | 0.45 (95 % CI 0.38 – 0.52, p = 0.0001) |

### Reactor counters per echo message (instrumented stdnet build, measurement window)

| Build | conns · threads | io_started | io_immediate | io_would_block | reactor_waits | interest_changes | posts | cv_waits |
|---|---|---|---|---|---|---|---|---|
| stdnet_senders | 1 · 1 | 2.00 | 1.01 | 0.99 | 0.99 | 1.97 | 2.00 | 0.00 |
| stdnet_senders | 64 · 1 | 2.00 | 1.65 | 0.35 | 0.01 | 0.70 | 2.00 | 0.00 |
| stdnet_senders | 64 · 2 | 2.00 | 1.98 | 0.02 | 0.00 | 0.05 | 2.00 | 0.00 |
| stdnet_coroutines | 1 · 1 | 2.00 | 1.00 | 1.00 | 1.00 | 2.00 | 4.00 | 0.00 |
| stdnet_coroutines | 64 · 1 | 2.00 | 1.94 | 0.06 | 0.00 | 0.13 | 4.00 | 0.00 |
| stdnet_coroutines | 64 · 2 | 2.00 | 2.00 | 0.00 | 0.00 | 0.01 | 4.00 | 0.00 |

### System calls per echo message (strace, 16 connections, measurement window only)

| Implementation | recvfrom | recvmsg | sendto | sendmsg | epoll_wait | epoll_ctl | futex | write | read | total |
|---|---|---|---|---|---|---|---|---|---|---|
| raw epoll | 1.01 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.01 |
| Asio callbacks | 1.00 | 0.00 | 1.00 | 0.00 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 2.13 |
| stdnet senders | 0.00 | 1.00 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.00 |

### Operation-state sizes (bytes)

| async_receive | async_write_1buf | async_write_2bufs | async_accept | async_connect | async_resume_after | receive_then_write | tcp_socket | io_context |
|---|---|---|---|---|---|---|---|---|
| 112 | 120 | 136 | 104 | 232 | 80 | 200 | 16 | 232 |

### Cost over raw BSD sockets: server CPU per operation (µs), stdnet senders minus the raw loop

`raw poll` is the portable model (non-blocking sockets + `poll`, i.e. what Winsock's `WSAPoll` offers); `raw epoll` is the best a Linux readiness loop can do.

| Scenario | raw poll | raw epoll | Asio | stdnet senders | stdnet − raw poll | stdnet − raw epoll |
|---|---|---|---|---|---|---|
| echo 16 conns × 64 B, server CPU/msg | 11.7 | 11.5 | 11.5 | 12.4 | +0.7 (+6 %) | +0.9 (+8 %) |
| echo 256 conns × 64 B, server CPU/msg | 11.9 | 11.9 | 12.0 | 11.9 | +0.0 (+0 %) | +0.0 (+0 %) |
| echo 16 conns × 64 KB, server CPU/msg | 40.7 | 39.7 | 41.0 | 42.4 | +1.7 (+4 %) | +2.7 (+7 %) |
| HTTP 64 conns, server CPU/request | 11.2 | 11.4 | 11.3 | 11.4 | +0.2 (+2 %) | +0.0 (+0 %) |
| UDP 64 B, server CPU/datagram | 8.2 | 8.5 | 9.6 | 8.9 | +0.7 (+8 %) | +0.4 (+5 %) |
| churn, server CPU/connection | 46.3 | 44.0 | 44.8 | 49.3 | +3.0 (+7 %) | +5.3 (+12 %) |

### Statistical significance of the differences (lower ratio = stdnet better)

Ratio of means with its 95 % confidence interval, and Welch's t-test; 32 comparisons, so a difference is called *significant* only if p < 0.05 / 32 = 0.0016 (Bonferroni). *not significant* means the data cannot tell the two apart at this number of repetitions.

| Comparison | stdnet variant | vs | ratio | 95 % CI | p | n | verdict |
|---|---|---|---|---|---|---|---|
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | Asio callbacks | 1.08 | 0.99 – 1.16 | 0.0662 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.06 | 0.98 – 1.14 | 0.1210 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet senders | raw epoll | 1.08 | 1.00 – 1.16 | 0.0377 | 6/6 | not significant |
| echo 16 conns × 64 B, server CPU/msg | stdnet task | Asio coroutines | 1.08 | 1.00 – 1.17 | 0.0483 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | Asio callbacks | 0.99 | 0.91 – 1.08 | 0.8894 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.00 | 0.91 – 1.10 | 0.9514 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet senders | raw epoll | 1.00 | 0.91 – 1.10 | 0.9629 | 6/6 | not significant |
| echo 256 conns × 64 B, server CPU/msg | stdnet task | Asio coroutines | 1.00 | 0.93 – 1.07 | 0.9323 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | Asio callbacks | 1.03 | 0.98 – 1.09 | 0.2042 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | raw poll (BSD sockets) | 1.04 | 0.95 – 1.13 | 0.3005 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet senders | raw epoll | 1.07 | 1.00 – 1.14 | 0.0544 | 6/6 | not significant |
| echo 16 conns × 64 KB, server CPU/msg | stdnet task | Asio coroutines | 1.01 | 0.95 – 1.08 | 0.6872 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | Asio callbacks | 1.07 | 1.02 – 1.13 | 0.0152 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | raw poll (BSD sockets) | 1.02 | 0.93 – 1.12 | 0.5719 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet senders | raw epoll | 1.06 | 0.99 – 1.14 | 0.0810 | 6/6 | not significant |
| echo 1 conn × 64 B, p50 latency | stdnet task | Asio coroutines | 1.05 | 0.98 – 1.12 | 0.1636 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | Asio callbacks | 1.00 | 0.94 – 1.07 | 0.8939 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | raw poll (BSD sockets) | 1.02 | 0.96 – 1.07 | 0.5130 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet senders | raw epoll | 1.00 | 0.91 – 1.09 | 0.9935 | 6/6 | not significant |
| HTTP 64 conns, server CPU/request | stdnet task | Asio coroutines | 1.05 | 0.96 – 1.14 | 0.2274 | 6/6 | not significant |
| HTTP 64 conns, p99 latency | stdnet senders | Asio callbacks | 2.65 | 2.16 – 3.15 | 0.0000 | 6/6 | stdnet higher |
| HTTP 64 conns, p99 latency | stdnet senders | raw poll (BSD sockets) | 2.24 | 1.97 – 2.51 | 0.0000 | 6/6 | stdnet higher |
| HTTP 64 conns, p99 latency | stdnet senders | raw epoll | 2.09 | 1.55 – 2.62 | 0.0000 | 6/6 | stdnet higher |
| HTTP 64 conns, p99 latency | stdnet task | Asio coroutines | 2.24 | 0.74 – 3.75 | 0.0809 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet senders | Asio callbacks | 0.93 | 0.89 – 0.97 | 0.0055 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet senders | raw poll (BSD sockets) | 1.08 | 1.03 – 1.13 | 0.0049 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet senders | raw epoll | 1.05 | 0.99 – 1.11 | 0.1077 | 6/6 | not significant |
| UDP 64 B, server CPU/datagram | stdnet task | Asio coroutines | 1.03 | 0.97 – 1.09 | 0.2587 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | Asio callbacks | 1.10 | 1.02 – 1.19 | 0.0211 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | raw poll (BSD sockets) | 1.07 | 0.98 – 1.15 | 0.0969 | 6/6 | not significant |
| churn, server CPU/connection | stdnet senders | raw epoll | 1.12 | 1.02 – 1.22 | 0.0184 | 6/6 | not significant |
| churn, server CPU/connection | stdnet task | Asio coroutines | 1.06 | 0.96 – 1.15 | 0.2245 | 6/6 | not significant |

Run-to-run variability (coefficient of variation, sample standard deviation / mean, 6 runs per point): throughput median 6.1 %, max 9.5 %; server CPU per operation median 5.4 %, max 11.9 %.
