Ratio = after / before (lower is better); 44 comparisons, significant if p < 0.0011 (Bonferroni).

| Point | Implementation | before | after | ratio | 95 % CI | p | verdict |
|---|---|---|---|---|---|---|---|
| echo 1 conn × 64 B | stdnet senders | 29.31 | 27.83 | 0.95 | 0.90 – 0.99 | 0.0347 | not significant |
| echo 1 conn × 64 B | stdnet task | 31.92 | 31.12 | 0.98 | 0.91 – 1.04 | 0.4092 | not significant |
| echo 1 conn × 64 B | Asio callbacks | 27.16 | 28.85 | 1.06 | 0.99 – 1.14 | 0.0831 | not significant |
| echo 1 conn × 64 B | raw epoll | 25.16 | 25.58 | 1.02 | 0.92 – 1.11 | 0.6934 | not significant |
| echo 16 conns × 64 B | stdnet senders | 8.09 | 6.90 | 0.85 | 0.68 – 1.02 | 0.0870 | not significant |
| echo 16 conns × 64 B | stdnet task | 10.39 | 8.31 | 0.80 | 0.68 – 0.92 | 0.0077 | not significant |
| echo 16 conns × 64 B | Asio callbacks | 7.02 | 6.61 | 0.94 | 0.82 – 1.06 | 0.3136 | not significant |
| echo 16 conns × 64 B | raw epoll | 6.28 | 6.79 | 1.08 | 0.97 – 1.19 | 0.1030 | not significant |
| echo 256 conns × 64 B | stdnet senders | 6.91 | 8.19 | 1.19 | 0.99 – 1.38 | 0.0495 | not significant |
| echo 256 conns × 64 B | stdnet task | 8.92 | 7.28 | 0.82 | 0.64 – 0.99 | 0.0735 | not significant |
| echo 256 conns × 64 B | Asio callbacks | 7.06 | 8.99 | 1.27 | 1.01 – 1.54 | 0.0248 | not significant |
| echo 256 conns × 64 B | raw epoll | 7.13 | 7.66 | 1.07 | 0.88 – 1.27 | 0.3932 | not significant |
| echo 16 conns × 64 KB | stdnet senders | 26.43 | 29.43 | 1.11 | 0.93 – 1.30 | 0.1569 | not significant |
| echo 16 conns × 64 KB | stdnet task | 28.07 | 28.87 | 1.03 | 0.93 – 1.13 | 0.5225 | not significant |
| echo 16 conns × 64 KB | Asio callbacks | 24.68 | 28.28 | 1.15 | 0.95 – 1.34 | 0.0960 | not significant |
| echo 16 conns × 64 KB | raw epoll | 22.00 | 23.52 | 1.07 | 0.87 – 1.27 | 0.4462 | not significant |
| HTTP 64 conns | stdnet senders | 7.50 | 6.19 | 0.82 | 0.69 – 0.96 | 0.0375 | not significant |
| HTTP 64 conns | stdnet task | 9.81 | 7.45 | 0.76 | 0.61 – 0.90 | 0.0141 | not significant |
| HTTP 64 conns | Asio callbacks | 7.19 | 6.23 | 0.87 | 0.75 – 0.98 | 0.0496 | not significant |
| HTTP 64 conns | raw epoll | 6.72 | 6.41 | 0.95 | 0.80 – 1.11 | 0.5282 | not significant |
| UDP 64 B | stdnet senders | 5.14 | 5.53 | 1.08 | 0.85 – 1.30 | 0.4487 | not significant |
| UDP 64 B | stdnet task | 7.60 | 8.05 | 1.06 | 0.96 – 1.16 | 0.2046 | not significant |
| UDP 64 B | Asio callbacks | 5.97 | 6.55 | 1.10 | 0.96 – 1.23 | 0.1213 | not significant |
| UDP 64 B | raw epoll | 4.01 | 4.07 | 1.02 | 0.84 – 1.19 | 0.8517 | not significant |
| churn | stdnet senders | 25.76 | 26.58 | 1.03 | 0.98 – 1.08 | 0.1715 | not significant |
| churn | stdnet task | 30.44 | 30.74 | 1.01 | 0.85 – 1.17 | 0.8938 | not significant |
| churn | Asio callbacks | 25.00 | 26.48 | 1.06 | 0.99 – 1.13 | 0.0782 | not significant |
| churn | raw epoll | 24.69 | 25.76 | 1.04 | 0.95 – 1.14 | 0.3304 | not significant |
| 10 000 idle conns | stdnet senders | 11.32 | 12.06 | 1.06 | 0.90 – 1.23 | 0.3787 | not significant |
| 10 000 idle conns | stdnet task | 15.09 | 13.87 | 0.92 | 0.82 – 1.02 | 0.1073 | not significant |
| 10 000 idle conns | Asio callbacks | 10.33 | 9.83 | 0.95 | 0.74 – 1.16 | 0.5730 | not significant |
| 10 000 idle conns | raw epoll | 7.21 | 6.42 | 0.89 | 0.72 – 1.06 | 0.2043 | not significant |
| echo 16 conns × 64 B, p99 (µs) | stdnet senders | 535.05 | 459.53 | 0.86 | 0.61 – 1.11 | 0.2342 | not significant |
| echo 16 conns × 64 B, p99 (µs) | stdnet task | 983.55 | 772.57 | 0.79 | 0.64 – 0.93 | 0.0174 | not significant |
| echo 16 conns × 64 B, p99 (µs) | Asio callbacks | 384.75 | 353.00 | 0.92 | 0.78 – 1.05 | 0.2193 | not significant |
| echo 16 conns × 64 B, p99 (µs) | raw epoll | 385.08 | 407.30 | 1.06 | 0.88 – 1.24 | 0.4366 | not significant |
| echo 256 conns × 64 B, p99 (µs) | stdnet senders | 16621.85 | 14299.91 | 0.86 | 0.43 – 1.29 | 0.4606 | not significant |
| echo 256 conns × 64 B, p99 (µs) | stdnet task | 38425.86 | 26469.18 | 0.69 | 0.54 – 0.84 | 0.0117 | not significant |
| echo 256 conns × 64 B, p99 (µs) | Asio callbacks | 5297.10 | 6509.91 | 1.23 | 0.91 – 1.55 | 0.1138 | not significant |
| echo 256 conns × 64 B, p99 (µs) | raw epoll | 5447.05 | 5508.18 | 1.01 | 0.85 – 1.17 | 0.8758 | not significant |
| HTTP 64 conns, p99 (µs) | stdnet senders | 2664.06 | 2308.18 | 0.87 | 0.71 – 1.02 | 0.1041 | not significant |
| HTTP 64 conns, p99 (µs) | stdnet task | 2473.57 | 3271.29 | 1.32 | 0.98 – 1.66 | 0.0347 | not significant |
| HTTP 64 conns, p99 (µs) | Asio callbacks | 1201.52 | 1035.19 | 0.86 | 0.77 – 0.95 | 0.0151 | not significant |
| HTTP 64 conns, p99 (µs) | raw epoll | 1217.95 | 1233.39 | 1.01 | 0.88 – 1.14 | 0.8322 | not significant |

Allocations per operation (deterministic, no test): echo 1 conn × 64 B senders 2.97 → 0.00, echo 16 conns × 64 B senders 2.27 → 0.00, HTTP 64 conns senders 2.13 → 0.00, UDP 64 B senders 2.01 → 0.00
