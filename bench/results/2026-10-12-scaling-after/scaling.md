echo, 16 connections, 64 B (k msg/s; speed-up over one thread in brackets; server CPU in µs per message)

| implementation | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---|---|---|---|
| raw_epoll | 70 (1.00×) · 14.5 µs | 132 (1.89×) · 15.5 µs | 157 (2.25×) · 23.7 µs | 138 (1.97×) · 34.7 µs |
| asio_callbacks | 70 (1.00×) · 14.4 µs | 136 (1.95×) · 14.7 µs | 153 (2.19×) · 20.4 µs | 128 (1.84×) · 34.6 µs |
| stdnet_senders | 70 (1.00×) · 14.4 µs | 138 (1.97×) · 14.6 µs | 141 (2.02×) · 23.2 µs | 103 (1.47×) · 44.0 µs |

stdnet mutex counters per message (stats build):

| threads | contended acquisitions | wait on the mutex, ns | condition-variable waits | cores busy |
|---|---|---|---|---|
| 1 | 0.000 | 0 | 0.000 | 0.96 |
| 2 | 0.124 | 41 | 0.001 | 1.93 |
| 4 | 0.621 | 2218 | 0.063 | 3.09 |
| 8 | 1.710 | 20342 | 0.185 | 4.74 |

echo, 256 connections, 64 B (k msg/s; speed-up over one thread in brackets; server CPU in µs per message)

| implementation | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---|---|---|---|
| raw_epoll | 67 (1.00×) · 15.6 µs | 142 (2.14×) · 14.0 µs | 180 (2.70×) · 21.8 µs | 204 (3.06×) · 29.7 µs |
| asio_callbacks | 69 (1.00×) · 14.9 µs | 143 (2.06×) · 14.0 µs | 187 (2.70×) · 18.1 µs | 169 (2.45×) · 26.5 µs |
| stdnet_senders | 69 (1.00×) · 14.5 µs | 135 (1.96×) · 14.8 µs | 186 (2.70×) · 19.8 µs | 154 (2.23×) · 32.4 µs |

stdnet mutex counters per message (stats build):

| threads | contended acquisitions | wait on the mutex, ns | condition-variable waits | cores busy |
|---|---|---|---|---|
| 1 | 0.000 | 0 | 0.000 | 0.95 |
| 2 | 0.142 | 87 | 0.000 | 1.94 |
| 4 | 0.459 | 2199 | 0.000 | 3.21 |
| 8 | 1.360 | 14258 | 0.000 | 5.32 |

