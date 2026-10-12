echo, 16 connections, 64 B (k msg/s; speed-up over one thread in brackets; server CPU in µs per message)

| implementation | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---|---|---|---|
| raw_epoll | 76 (1.00×) · 13.3 µs | 141 (1.85×) · 14.2 µs | 188 (2.47×) · 20.4 µs | 137 (1.81×) · 32.3 µs |
| asio_callbacks | 75 (1.00×) · 13.2 µs | 146 (1.94×) · 13.7 µs | 155 (2.05×) · 17.6 µs | 133 (1.77×) · 31.8 µs |
| stdnet_senders | 76 (1.00×) · 13.0 µs | 65 (0.86×) · 20.8 µs | 64 (0.83×) · 21.1 µs | 60 (0.79×) · 19.4 µs |

stdnet mutex counters per message (stats build):

| threads | contended acquisitions | wait on the mutex, ns | condition-variable waits | cores busy |
|---|---|---|---|---|
| 1 | 0.000 | 0 | 0.000 | 0.96 |
| 2 | 0.067 | 15615 | 0.000 | 1.30 |
| 4 | 0.081 | 48516 | 0.000 | 1.30 |
| 8 | 0.056 | 117488 | 0.001 | 1.18 |

echo, 256 connections, 64 B (k msg/s; speed-up over one thread in brackets; server CPU in µs per message)

| implementation | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---|---|---|---|
| raw_epoll | 74 (1.00×) · 13.7 µs | 149 (2.01×) · 13.5 µs | 217 (2.92×) · 17.7 µs | 197 (2.65×) · 27.5 µs |
| asio_callbacks | 74 (1.00×) · 13.7 µs | 154 (2.08×) · 13.0 µs | 190 (2.56×) · 15.4 µs | 157 (2.12×) · 24.5 µs |
| stdnet_senders | 74 (1.00×) · 13.6 µs | 67 (0.91×) · 20.6 µs | 61 (0.83×) · 22.0 µs | 55 (0.75×) · 21.6 µs |

stdnet mutex counters per message (stats build):

| threads | contended acquisitions | wait on the mutex, ns | condition-variable waits | cores busy |
|---|---|---|---|---|
| 1 | 0.000 | 0 | 0.000 | 0.97 |
| 2 | 0.113 | 15383 | 0.000 | 1.32 |
| 4 | 0.128 | 48840 | 0.000 | 1.32 |
| 8 | 0.062 | 127747 | 0.000 | 1.12 |

