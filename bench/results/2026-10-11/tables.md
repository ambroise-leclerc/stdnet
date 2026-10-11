Run: 2026-10-11 11:27 · Intel(R) Xeon(R) Processor @ 2.80GHz · 2 cores · Linux 6.18.44-fc-v114 · c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 · 3 repetitions per point

### Echo — throughput (messages/s) and server CPU per message (µs)

| Implementation | 1 conn · 64 B | 1 conn · 4096 B | 1 conn · 65536 B | 16 conn · 64 B | 16 conn · 4096 B | 16 conn · 65536 B | 256 conn · 64 B | 256 conn · 4096 B | 256 conn · 65536 B |
|---|---|---|---|---|---|---|---|---|---|
| raw blocking | 21 202 · 22.0 | 20 798 · 22.8 | 11 716 · 42.7 | 82 790 · 10.4 | 72 315 · 11.8 | 36 179 · 25.4 | 75 713 · 12.0 | 71 990 · 12.6 | 26 848 · 34.6 |
| raw epoll | 18 559 · 24.9 | 19 059 · 24.7 | 12 643 · 42.2 | 139 536 · 6.2 | 118 235 · 7.4 | 42 360 · 21.7 | 149 224 · 5.9 | 138 944 · 6.7 | 34 969 · 25.0 |
| Asio callbacks | 19 137 · 25.8 | 19 512 · 25.8 | 13 291 · 43.2 | 160 561 · 5.9 | 140 749 · 6.6 | 37 589 · 25.9 | 183 275 · 5.3 | 135 488 · 6.9 | 33 203 · 29.8 |
| Asio coroutines | 19 338 · 26.3 | 18 271 · 28.0 | 11 933 · 47.5 | 140 803 · 6.5 | 126 340 · 7.5 | 35 241 · 27.5 | 136 002 · 7.0 | 123 295 · 7.6 | 29 447 · 33.6 |
| stdnet senders | 18 642 · 28.3 | 18 318 · 28.8 | 12 544 · 46.2 | 127 744 · 7.7 | 119 355 · 8.0 | 38 522 · 25.4 | 165 617 · 6.0 | 133 181 · 7.5 | 38 626 · 25.7 |
| stdnet task | 18 057 · 30.8 | 17 785 · 31.7 | 11 928 · 50.7 | 121 190 · 8.1 | 105 414 · 9.2 | 37 452 · 25.2 | 144 940 · 6.8 | 115 592 · 8.6 | 36 449 · 27.1 |

### Throughput and server CPU per operation across scenarios

| Implementation | echo 16×64 B (ops/s · µs CPU/op) | echo 256×64 B (ops/s · µs CPU/op) | HTTP 64 conns (ops/s · µs CPU/op) | UDP 64 B (ops/s · µs CPU/op) | churn (ops/s · µs CPU/op) |
|---|---|---|---|---|---|
| raw blocking | 82 790 · 10.4 | 75 713 · 12.0 | 83 209 · 10.2 | 281 977 · 3.4 | 10 890 · 117.9 |
| raw epoll | 139 536 · 6.2 | 149 224 · 5.9 | 155 562 · 6.0 | 275 460 · 3.4 | 21 013 · 24.8 |
| Asio callbacks | 160 561 · 5.9 | 183 275 · 5.3 | 157 090 · 6.0 | 171 555 · 5.7 | 23 268 · 23.9 |
| Asio coroutines | 140 803 · 6.5 | 136 002 · 7.0 | 133 530 · 7.0 | 161 968 · 6.1 | 24 370 · 28.1 |
| stdnet senders | 127 744 · 7.7 | 165 617 · 6.0 | 152 555 · 6.5 | 212 739 · 4.5 | 23 519 · 26.6 |
| stdnet task | 121 190 · 8.1 | 144 940 · 6.8 | 132 801 · 7.5 | 140 707 · 7.0 | 31 231 · 26.4 |

### Latency — round trip, 1 connection, 64 B (µs)

| Implementation | p50 | p99 | p99.9 |
|---|---|---|---|
| raw blocking | 49.3 | 105.1 | 307.2 |
| raw epoll | 51.5 | 112.6 | 297.4 |
| Asio callbacks | 49.7 | 120.5 | 305.7 |
| Asio coroutines | 49.6 | 106.8 | 294.4 |
| stdnet senders | 51.6 | 107.8 | 279.1 |
| stdnet task | 52.8 | 114.0 | 380.1 |

### Heap allocations per operation and memory per idle connection

| Implementation | allocs/op echo 16×64 B | allocs/op HTTP | allocs/op UDP | RSS per connection, 10 000 conns (KB) |
|---|---|---|---|---|
| raw blocking | 0.00 | 1.00 | 0.00 | n/a (10 000 threads) |
| raw epoll | 0.00 | 0.00 | 0.00 | 0.1 |
| Asio callbacks | 0.00 | 0.00 | 0.00 | 4.6 |
| Asio coroutines | 0.00 | 0.00 | 0.00 | 5.4 |
| stdnet senders | 2.21 | 2.04 | 2.00 | 4.7 |
| stdnet task | 2.11 | 2.03 | 2.00 | 5.3 |

### Server threads (echo, 64 connections × 64 B, messages/s)

| Implementation | 1 thread | 2 threads |
|---|---|---|
| raw epoll | 138 041 | 139 082 |
| Asio callbacks | 138 016 | 109 352 |
| Asio coroutines | 167 916 | 93 287 |
| stdnet senders | 135 485 | 99 461 |
| stdnet task | 120 340 | 69 356 |

### stdnet backends (echo, 64 B, messages/s · p99 µs)

| Backend | 16 connections | 256 connections |
|---|---|---|
| stdnet senders | 123 314 · 529 | 126 568 · 19 903 |
| stdnet senders (poll) | 146 285 · 482 | 140 049 · 9 412 |

### Timers (100 000 concurrent timers; durations in ms)

| | start all | drain (deadlines over 50 ms) | cancel all |
|---|---|---|---|
| stdnet | 67.5 | 50.7 | 15.0 |
| Asio | 20.2 | 55.5 | 37.9 |

### Reactor counters per echo message (instrumented stdnet build, measurement window)

| Build | conns · threads | io_started | io_immediate | io_would_block | reactor_waits | interest_changes | posts | cv_waits |
|---|---|---|---|---|---|---|---|---|
| stdnet_senders | 1 · 1 | 2.00 | 1.15 | 0.85 | 0.85 | 1.70 | 2.00 | 0.00 |
| stdnet_senders | 64 · 1 | 2.00 | 1.53 | 0.47 | 0.05 | 0.94 | 2.00 | 0.00 |
| stdnet_senders | 64 · 2 | 2.00 | 1.58 | 0.42 | 0.06 | 0.85 | 2.00 | 0.01 |
| stdnet_coroutines | 1 · 1 | 1.99 | 1.10 | 0.88 | 0.88 | 1.77 | 3.97 | 0.00 |
| stdnet_coroutines | 64 · 1 | 2.00 | 1.82 | 0.18 | 0.01 | 0.36 | 4.00 | 0.00 |
| stdnet_coroutines | 64 · 2 | 2.00 | 1.72 | 0.28 | 0.03 | 0.56 | 4.00 | 0.01 |

### System calls per echo message (strace, 16 connections, measurement window only)

| Implementation | recvfrom | recvmsg | sendto | sendmsg | epoll_wait | epoll_ctl | futex | write | read | total |
|---|---|---|---|---|---|---|---|---|---|---|
| raw epoll | 1.01 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.02 |
| Asio callbacks | 1.00 | 0.00 | 1.00 | 0.00 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 2.13 |
| stdnet senders | 0.00 | 1.00 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.01 |

### Operation-state sizes (bytes)

| async_receive | async_write_1buf | async_write_2bufs | async_accept | async_connect | async_resume_after | receive_then_write | tcp_socket | io_context |
|---|---|---|---|---|---|---|---|---|
| 112 | 120 | 136 | 104 | 232 | 80 | 200 | 16 | 304 |

### Build cost (one server translation unit, -O2)

| Implementation | compile time (s) | .text (KB) |
|---|---|---|
| raw blocking | 1.5 | 17 |
| raw epoll | 1.5 | 20 |
| Asio callbacks | 6.5 | 118 |
| Asio coroutines | 7.2 | 140 |
| stdnet senders | 12.2 | 102 |
| stdnet task | 9.6 | 207 |

Run-to-run variability (coefficient of variation of throughput): median 6.7 %, max 23.2 %.
