Run: 2026-10-11 09:38 · Intel(R) Xeon(R) Processor @ 2.80GHz · 2 cores · Linux 6.18.44-fc-v114 · c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 · 3 repetitions per point

### Echo — throughput (messages/s) and server CPU per message (µs)

| Implementation | 1 conn · 64 B | 1 conn · 4096 B | 1 conn · 65536 B | 16 conn · 64 B | 16 conn · 4096 B | 16 conn · 65536 B | 256 conn · 64 B | 256 conn · 4096 B | 256 conn · 65536 B |
|---|---|---|---|---|---|---|---|---|---|
| raw blocking | 24 856 · 25.3 | 17 139 · 36.3 | 12 669 · 56.8 | 78 824 · 14.7 | 76 226 · 16.3 | 32 802 · 34.5 | 84 913 · 13.6 | 73 589 · 15.6 | 26 583 · 44.7 |
| raw epoll | 19 141 · 32.9 | 18 933 · 33.3 | 12 599 · 56.0 | 118 373 · 9.3 | 127 110 · 9.1 | 45 639 · 28.3 | 130 925 · 8.8 | 140 706 · 8.7 | 44 309 · 28.0 |
| Asio callbacks | 19 277 · 34.0 | 18 760 · 35.2 | 13 363 · 54.7 | 147 303 · 8.5 | 143 091 · 8.4 | 36 787 · 35.4 | 155 093 · 8.2 | 159 177 · 7.9 | 30 022 · 43.9 |
| Asio coroutines | 18 711 · 35.9 | 18 835 · 36.3 | 12 811 · 58.4 | 147 890 · 8.2 | 113 218 · 10.9 | 37 319 · 34.3 | 136 623 · 9.3 | 118 257 · 10.6 | 26 436 · 49.9 |
| stdnet senders | 18 090 · 39.2 | 17 587 · 40.0 | 13 139 · 59.3 | 129 881 · 10.0 | 115 044 · 11.3 | 40 435 · 32.1 | 141 524 · 9.3 | 126 216 · 10.6 | 39 297 · 33.9 |
| stdnet task | 17 550 · 42.7 | 17 479 · 43.1 | 11 368 · 69.9 | 129 808 · 10.0 | 102 073 · 12.6 | 34 228 · 38.8 | 130 561 · 10.1 | 105 791 · 12.6 | 33 653 · 39.0 |

### Throughput and server CPU per operation across scenarios

| Implementation | echo 16×64 B (ops/s · µs CPU/op) | echo 256×64 B (ops/s · µs CPU/op) | HTTP 64 conns (ops/s · µs CPU/op) | UDP 64 B (ops/s · µs CPU/op) | churn (ops/s · µs CPU/op) |
|---|---|---|---|---|---|
| raw blocking | 78 824 · 14.7 | 84 913 · 13.6 | 94 701 · 12.1 | 266 664 · 4.6 | 11 210 · 152.9 |
| raw epoll | 118 373 · 9.3 | 130 925 · 8.8 | 159 251 · 7.4 | 284 904 · 4.6 | 21 940 · 30.9 |
| Asio callbacks | 147 303 · 8.5 | 155 093 · 8.2 | 170 547 · 7.5 | 174 999 · 7.5 | 24 475 · 31.7 |
| Asio coroutines | 147 890 · 8.2 | 136 623 · 9.3 | 167 962 · 7.9 | 183 300 · 7.1 | 26 948 · 35.9 |
| stdnet senders | 129 881 · 10.0 | 141 524 · 9.3 | 180 598 · 7.3 | 222 026 · 5.9 | 25 148 · 34.7 |
| stdnet task | 129 808 · 10.0 | 130 561 · 10.1 | 148 560 · 8.8 | 144 079 · 9.0 | 30 887 · 34.9 |

### Latency — round trip, 1 connection, 64 B (µs)

| Implementation | p50 | p99 | p99.9 |
|---|---|---|---|
| raw blocking | 46.6 | 104.1 | 299.5 |
| raw epoll | 50.3 | 107.4 | 228.1 |
| Asio callbacks | 50.1 | 102.4 | 236.5 |
| Asio coroutines | 51.4 | 118.5 | 255.7 |
| stdnet senders | 52.9 | 112.3 | 249.4 |
| stdnet task | 54.3 | 111.6 | 249.0 |

### Heap allocations per operation and memory per idle connection

| Implementation | allocs/op echo 16×64 B | allocs/op HTTP | allocs/op UDP | RSS per connection, 10 000 conns (KB) |
|---|---|---|---|---|
| raw blocking | 0.00 | 1.34 | 0.00 | n/a (10 000 threads) |
| raw epoll | 0.00 | 0.00 | 0.00 | 0.1 |
| Asio callbacks | 0.00 | 0.00 | 0.00 | 4.6 |
| Asio coroutines | 0.00 | 0.00 | 0.00 | 5.4 |
| stdnet senders | 2.93 | 2.75 | 2.75 | 4.7 |
| stdnet task | 2.77 | 2.73 | 2.66 | 5.3 |

### Server threads (echo, 64 connections × 64 B, messages/s)

| Implementation | 1 thread | 2 threads |
|---|---|---|
| raw epoll | 175 128 | 154 743 |
| Asio callbacks | 159 091 | 102 718 |
| Asio coroutines | 153 069 | 99 375 |
| stdnet senders | 160 102 | 113 921 |
| stdnet task | 117 165 | 71 750 |

### stdnet backends (echo, 64 B, messages/s · p99 µs)

| Backend | 16 connections | 256 connections |
|---|---|---|
| stdnet senders | 137 169 · 470 | 137 744 · 13 890 |
| stdnet senders (poll) | 158 472 · 399 | 160 720 · 8 253 |

### Timers (100 000 concurrent timers; durations in ms)

| | start all | drain (deadlines over 50 ms) | cancel all |
|---|---|---|---|
| stdnet | 54.4 | 50.0 | 12.4 |
| Asio | 16.6 | 51.9 | 31.7 |

### Reactor counters per echo message (instrumented stdnet build)

| Build | conns · threads | io_started | io_immediate | io_would_block | reactor_waits | interest_changes | posts | cv_waits |
|---|---|---|---|---|---|---|---|---|
| stdnet_senders | 1 · 1 | 2.29 | 1.17 | 1.12 | 1.12 | 2.24 | 2.29 | 0.00 |
| stdnet_senders | 64 · 1 | 2.31 | 2.00 | 0.30 | 0.04 | 0.61 | 2.31 | 0.00 |
| stdnet_senders | 64 · 2 | 2.21 | 1.79 | 0.42 | 0.03 | 0.85 | 2.21 | 0.01 |
| stdnet_coroutines | 1 · 1 | 2.30 | 1.18 | 1.12 | 1.12 | 2.24 | 4.60 | 0.00 |
| stdnet_coroutines | 64 · 1 | 2.36 | 2.18 | 0.18 | 0.01 | 0.36 | 4.72 | 0.00 |
| stdnet_coroutines | 64 · 2 | 2.24 | 1.93 | 0.31 | 0.02 | 0.62 | 4.48 | 0.00 |

### System calls per echo message (strace -c, 16 connections)

| Implementation | recvfrom | recvmsg | sendto | sendmsg | epoll_wait | epoll_ctl | futex | write | read | total |
|---|---|---|---|---|---|---|---|---|---|---|
| raw epoll | 1.00 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.02 |
| Asio callbacks | 1.00 | 0.00 | 1.00 | 0.00 | 0.13 | 0.00 | 0.00 | 0.00 | 0.00 | 2.15 |
| stdnet senders | 0.00 | 1.00 | 0.00 | 1.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 2.02 |

### Operation-state sizes (bytes)

| async_receive | async_write_1buf | async_write_2bufs | async_accept | async_connect | async_resume_after | receive_then_write | tcp_socket | io_context |
|---|---|---|---|---|---|---|---|---|
| 112 | 120 | 136 | 104 | 232 | 80 | 200 | 16 | 304 |

### Build cost (one server translation unit, -O2)

| Implementation | compile time (s) | .text (KB) |
|---|---|---|
| raw blocking | 1.0 | 16 |
| raw epoll | 1.3 | 19 |
| Asio callbacks | 5.8 | 117 |
| Asio coroutines | 6.5 | 139 |
| stdnet senders | 11.5 | 101 |
| stdnet task | 8.8 | 206 |

Run-to-run variability (coefficient of variation of throughput): median 5.9 %, max 19.4 %.
