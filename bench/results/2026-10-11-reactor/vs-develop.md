Ratio = after / before (lower is better); 44 comparisons, significant if p < 0.0011 (Bonferroni).

| Point | Implementation | before | after | ratio | 95 % CI | p | verdict |
|---|---|---|---|---|---|---|---|
| echo 1 conn × 64 B | stdnet senders | 19.62 | 17.22 | 0.88 | 0.81 – 0.95 | 0.0038 | not significant |
| echo 1 conn × 64 B | stdnet task | 21.10 | 18.72 | 0.89 | 0.79 – 0.98 | 0.0269 | not significant |
| echo 1 conn × 64 B | Asio callbacks | 16.64 | 17.43 | 1.05 | 0.96 – 1.13 | 0.2191 | not significant |
| echo 1 conn × 64 B | raw epoll | 16.12 | 16.26 | 1.01 | 0.91 – 1.10 | 0.8434 | not significant |
| echo 16 conns × 64 B | stdnet senders | 12.41 | 11.65 | 0.94 | 0.87 – 1.00 | 0.0644 | not significant |
| echo 16 conns × 64 B | stdnet task | 12.66 | 11.76 | 0.93 | 0.86 – 1.00 | 0.0499 | not significant |
| echo 16 conns × 64 B | Asio callbacks | 11.54 | 11.41 | 0.99 | 0.92 – 1.06 | 0.7280 | not significant |
| echo 16 conns × 64 B | raw epoll | 11.49 | 11.40 | 0.99 | 0.91 – 1.07 | 0.8240 | not significant |
| echo 256 conns × 64 B | stdnet senders | 11.93 | 11.95 | 1.00 | 0.92 – 1.09 | 0.9786 | not significant |
| echo 256 conns × 64 B | stdnet task | 12.06 | 12.18 | 1.01 | 0.92 – 1.10 | 0.8088 | not significant |
| echo 256 conns × 64 B | Asio callbacks | 12.00 | 11.83 | 0.99 | 0.90 – 1.08 | 0.7275 | not significant |
| echo 256 conns × 64 B | raw epoll | 11.91 | 11.55 | 0.97 | 0.88 – 1.05 | 0.4517 | not significant |
| echo 16 conns × 64 KB | stdnet senders | 42.36 | 42.41 | 1.00 | 0.93 – 1.08 | 0.9725 | not significant |
| echo 16 conns × 64 KB | stdnet task | 43.74 | 43.33 | 0.99 | 0.93 – 1.05 | 0.7467 | not significant |
| echo 16 conns × 64 KB | Asio callbacks | 41.01 | 41.67 | 1.02 | 0.95 – 1.09 | 0.6098 | not significant |
| echo 16 conns × 64 KB | raw epoll | 39.68 | 40.11 | 1.01 | 0.93 – 1.09 | 0.7664 | not significant |
| HTTP 64 conns | stdnet senders | 11.39 | 11.41 | 1.00 | 0.93 – 1.07 | 0.9522 | not significant |
| HTTP 64 conns | stdnet task | 12.00 | 11.52 | 0.96 | 0.88 – 1.04 | 0.3103 | not significant |
| HTTP 64 conns | Asio callbacks | 11.34 | 11.49 | 1.01 | 0.94 – 1.08 | 0.6994 | not significant |
| HTTP 64 conns | raw epoll | 11.38 | 11.28 | 0.99 | 0.90 – 1.08 | 0.8129 | not significant |
| UDP 64 B | stdnet senders | 8.89 | 8.97 | 1.01 | 0.96 – 1.06 | 0.6915 | not significant |
| UDP 64 B | stdnet task | 10.25 | 10.22 | 1.00 | 0.93 – 1.06 | 0.9179 | not significant |
| UDP 64 B | Asio callbacks | 9.56 | 9.59 | 1.00 | 0.94 – 1.06 | 0.9169 | not significant |
| UDP 64 B | raw epoll | 8.50 | 8.54 | 1.00 | 0.95 – 1.06 | 0.8558 | not significant |
| churn | stdnet senders | 49.30 | 46.96 | 0.95 | 0.86 – 1.05 | 0.3022 | not significant |
| churn | stdnet task | 53.01 | 50.17 | 0.95 | 0.87 – 1.02 | 0.1364 | not significant |
| churn | Asio callbacks | 44.78 | 44.99 | 1.00 | 0.93 – 1.08 | 0.8868 | not significant |
| churn | raw epoll | 44.03 | 42.62 | 0.97 | 0.89 – 1.04 | 0.3683 | not significant |
| 10 000 idle conns | stdnet senders | 19.80 | 19.60 | 0.99 | 0.89 – 1.10 | 0.8347 | not significant |
| 10 000 idle conns | stdnet task | 21.83 | 21.02 | 0.96 | 0.88 – 1.05 | 0.3291 | not significant |
| 10 000 idle conns | Asio callbacks | 20.80 | 20.48 | 0.98 | 0.90 – 1.07 | 0.6877 | not significant |
| 10 000 idle conns | raw epoll | 13.29 | 13.38 | 1.01 | 0.85 – 1.16 | 0.9142 | not significant |
| echo 16 conns × 64 B, p99 (µs) | stdnet senders | 527.59 | 377.10 | 0.71 | 0.53 – 0.90 | 0.0106 | not significant |
| echo 16 conns × 64 B, p99 (µs) | stdnet task | 880.97 | 606.34 | 0.69 | 0.60 – 0.78 | 0.0003 | faster |
| echo 16 conns × 64 B, p99 (µs) | Asio callbacks | 358.62 | 339.56 | 0.95 | 0.80 – 1.09 | 0.4401 | not significant |
| echo 16 conns × 64 B, p99 (µs) | raw epoll | 379.04 | 370.75 | 0.98 | 0.83 – 1.12 | 0.7455 | not significant |
| echo 256 conns × 64 B, p99 (µs) | stdnet senders | 10941.26 | 7058.12 | 0.65 | 0.40 – 0.89 | 0.0186 | not significant |
| echo 256 conns × 64 B, p99 (µs) | stdnet task | 42058.89 | 18032.79 | 0.43 | 0.38 – 0.47 | 0.0000 | faster |
| echo 256 conns × 64 B, p99 (µs) | Asio callbacks | 6072.37 | 5967.48 | 0.98 | 0.86 – 1.11 | 0.7678 | not significant |
| echo 256 conns × 64 B, p99 (µs) | raw epoll | 5515.40 | 4723.76 | 0.86 | 0.56 – 1.15 | 0.3457 | not significant |
| HTTP 64 conns, p99 (µs) | stdnet senders | 3202.59 | 1896.01 | 0.59 | 0.47 – 0.72 | 0.0001 | faster |
| HTTP 64 conns, p99 (µs) | stdnet task | 2633.01 | 7803.11 | 2.96 | 1.05 – 4.87 | 0.0005 | slower |
| HTTP 64 conns, p99 (µs) | Asio callbacks | 1207.56 | 1262.69 | 1.05 | 0.69 – 1.40 | 0.7745 | not significant |
| HTTP 64 conns, p99 (µs) | raw epoll | 1535.49 | 1396.05 | 0.91 | 0.66 – 1.16 | 0.4461 | not significant |

Allocations per operation (deterministic, no test): echo 1 conn × 64 B senders 2.98 → 0.00, echo 16 conns × 64 B senders 2.18 → 0.00, HTTP 64 conns senders 2.03 → 0.00, UDP 64 B senders 2.00 → 0.00
