# State of C++ networking standardization (October 2026)

This note records the publications that shaped the stdnet specification. It is a survey of
where WG21 stands, not a position paper. Last reviewed: 2026-10-11.

## Short version

* **Nothing is in the C++29 working draft yet.** Networking was not part of C++26, and the
  first C++29 meeting (Brno, June 2026) adopted no networking paper. The next full meeting is
  Búzios (November 2026).
* **The only direction ever polled with consensus is sender/receiver.** In 2021 the committee
  stopped pursuing the Networking TS/Asio executor model and stated that networking should be
  built on `std::execution` (P2453). SG4 confirmed this in 2023 (P2762 line).
* **SG4 then asked for a higher-level, property-based API (IETF TAPS)** instead of exposing
  BSD-sockets-shaped operations publicly (Tokyo, February 2024; P3185, P3482).
* **In 2026 a competing coroutine-native proposal appeared** (the "Network Endeavor",
  P4100 and companions), which argues that senders are a poor fit for byte-stream I/O used
  from coroutines. It explicitly "asks for nothing" yet and targets LEWG reviews through 2028.
* **`std::execution` (P2300) and `std::execution::task` are in C++26**, so anything built on
  senders today targets a shipped standard; coroutine-native I/O would need new vocabulary.

## Chronology

| Year | Document | What it says | Weight for stdnet |
|------|----------|--------------|-------------------|
| 2021 | **P2453R0** (LEWG polls, Oct. 2021) | Networking TS loses consensus; sender/receiver judged a good basis for networking. | Foundational: senders are the API surface. |
| 2019–21 | **P1860** "C++ networking must be secure by default" | Standard networking should default to secure transports. | Security parameters default to *secure*; cleartext is an explicit opt-out. |
| 2023 | **P2762R0–R2** (Dietmar Kühl) "Sender/Receiver Interface for Networking" | Sender factories (`async_accept`, `async_connect`, `async_receive`, `async_send`, `async_resume_after`, …) over Networking-TS-like sockets bound to an `io_context`; errors as `set_error(error_code)`. | Socket layer and operation names follow P2762. |
| 2024 | **P3185R0** (Thomas Rodgers) | Proposes IETF TAPS as the model for C++ networking. | Preconnection layer. |
| 2024 | **P3482R0/R1** (Rodgers, Kühl) | TAPS design for C++: `preconnection`, endpoint/transport/security properties, `initiate`/`listen`/`rendezvous`, senders for send/receive; asks to adopt as much of P2762 as applicable. | Names and property model of `<stdnet/preconnection.hpp>`. |
| 2024– | **beman.net** (Beman project) | Reference implementation of P2762R2 on `beman.execution`; header-only, `poll(2)` backend; notes the SG4 TAPS recommendation. | Evidence that the P2762 surface is implementable; stdnet is an independent implementation. |
| 2025 | **P2300R10 in C++26**; `std::execution::task` | Senders, receivers, `counting_scope`, `spawn`, `task`. | stdnet senders follow the C++26 protocol; works with stdexec until standard libraries ship it. |
| 2026-02 | **P4003R0 / P4007R0** (Falco, Gill, Gerbino) | Coroutine I/O; four gaps where senders meet coroutines (partial-result errors, `with_error`, frame allocators, symmetric transfer). | Error model: end of stream and short reads are values, not errors; `as_expected` adaptor. |
| 2026-04 | **P4100R0** "Coroutine-Native I/O for C++29 (The Network Endeavor)" | 13-paper roadmap (buffers, streams, timers, TCP/UDP/DNS, TLS) on Capy/Corosio; LEWG reviews 2026–2028. | Competing direction; tracked as an open question. |
| 2026-04 | **P4125R0** | Field report (3 engineers, one exchange) on porting from Asio to Capy/Corosio; notes the lack of `error_code` overloads as friction. | Every synchronous member has an `error_code` overload. |
| 2026-05 | **P4003R3** "A Minimal Coroutine Execution Model" | `IoAwaitable` protocol (`io_env`: executor, stop token, frame allocator). | Not adopted; stdnet stays on C++26 vocabulary. |
| 2026-05 | **P4126R1** "A Universal Continuation Model" | Lets sender pipelines drive IoAwaitables without frame allocation. | Suggests the two models can share one I/O core; reinforces keeping the reactor independent of the front-end. |
| 2026-04/05 | **P4097R0/R1** | Revisits the 2021 poll evidence for "senders are good for networking". | Context: the sender basis is contested, not settled. |

## What a realistic C++29 header looks like, given the above

1. A **sender-based** core, because it is the only polled direction and it reuses C++26
   vocabulary (`std::execution`, `task`, `counting_scope`) with no new language machinery.
2. A **TAPS-style connection API** on top, because SG4 asked for it — with the lower socket
   layer kept public for the cases TAPS does not cover yet (and because every TAPS
   implementation needs one underneath).
3. An error model that **does not lose data** (P4007's strongest point): byte counts travel in
   the value channel, end of stream is a value, and `error_code`s can be lifted into
   `std::expected` for coroutine code that does not want exceptions.
4. **Secure by default** (P1860): cleartext must be requested.
5. Room left for TLS, QUIC, sequence senders for listeners, and completion-based backends
   (io_uring, IOCP) without changing the public surface.

## Sources

* P2453R0 — https://wg21.link/p2453r0
* P1860R0 — https://wg21.link/p1860r0
* P2762R0 — https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2023/p2762r0.pdf
* P2762R2 — https://open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2762r2.pdf
* P3185R0 — https://wg21.link/P3185R0
* P3482R0 — https://open-std.org/JTC1/SC22/WG21/docs/papers/2024/p3482r0.html
* P3482R1 — https://open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3482r1.html
* beman.net — https://github.com/bemanproject/net
* P4003R3 — https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4003r3.pdf
* P4007R0 — https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2026/p4007r0.pdf
* P4097R1 — https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4097r1.pdf
* P4100R0 — https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4100r0.pdf
* P4125R0 — https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2026/p4125r0.pdf
* P4126R1 — https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4126r1.pdf
* Brno 2026 agenda — https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/n5045
