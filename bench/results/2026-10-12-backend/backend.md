| connections | epoll µs/msg | poll µs/msg | epoll / poll (95 % CI, paired) | verdict |
|---|---|---|---|---|
| 16 | 11.21 | 11.02 | 1.018 (0.994–1.042), n = 30 | non-inferior (upper 1.04 < 1.1) |
| 256 | 11.38 | 10.95 | 1.039 (1.016–1.063), n = 30 | non-inferior (upper 1.06 < 1.1) |
| 2000 idle | 10.97 | 19.51 | 0.563 (0.549–0.578), n = 30 | epoll better (CI below 1) |
| 10000 idle | 11.14 | 47.36 | 0.236 (0.227–0.245), n = 30 | epoll better (CI below 1) |
