# `<network>` — a specification for C++29 networking

Version 0.1 · October 2026 · Implementation: this repository (`stdnet`)

This document specifies the header implemented by stdnet. It is written to be *realistic*: it
only uses vocabulary that is in C++26, follows the one direction WG21 has polled with consensus
(sender/receiver), and adopts the TAPS model that SG4 requested. Where the committee has not
decided, the choice made here is stated with its rationale and listed in §10. The paper
landscape behind these choices is in [RESEARCH.md](RESEARCH.md).

In the standard the namespace would be `std::net`; the implementation uses `stdnet`.

---

## 1. Design principles

| # | Principle | Basis |
|---|-----------|-------|
| D1 | Every asynchronous operation is a **sender** factory; there are no completion handlers, executors or completion tokens. | P2453 polls; P2762 |
| D2 | I/O objects are **bound to an `io_context`** at construction; their operations complete on that context. | P2762 (preferred option); Networking TS practice |
| D3 | Completion **never happens inline in `start()`**: it is delivered by a thread running the context. | Bounded stack depth in loops; predictable completion context |
| D4 | **Cancellation is stop-token based**: a stop request completes a pending operation with `set_stopped()`. Closing an I/O object completes its pending operations with `set_error(errc::operation_canceled)`. | P2300 cancellation model |
| D5 | **No data is lost through the error channel**: byte counts are values; end of stream and short reads are values; errors carry `std::error_code`. | P4007 critique of three-channel I/O |
| D6 | **Secure by default**: a preconnection without explicit security parameters requires a secure transport. | P1860 |
| D7 | Two layers: a **socket layer** (P2762 shape) and a **connection layer** (TAPS: `preconnection`, `initiate`, `listen`, `connection`). | P3185, P3482 |
| D8 | Every synchronous member has a throwing form **and** a `std::error_code&` form. | Networking TS; P4125 field report |

## 2. Header synopsis

```cpp
namespace std::net {
  // §3 errors
  enum class error { host_not_found = 1, service_not_found, try_again,
                     no_candidate, not_supported, wrong_context };
  const error_category& network_category() noexcept;
  error_code make_error_code(error) noexcept;

  // §4 buffers
  class mutable_buffer;  class const_buffer;
  template<class T> concept mutable_buffer_sequence = /* see below */;
  template<class T> concept const_buffer_sequence   = /* see below */;
  mutable_buffer buffer(void*, size_t) noexcept;          // and contiguous-range overloads
  const_buffer   buffer(const void*, size_t) noexcept;
  size_t buffer_size(const const_buffer_sequence auto&) noexcept;
  size_t buffer_copy(const mutable_buffer_sequence auto&, const const_buffer_sequence auto&,
                     size_t max = SIZE_MAX) noexcept;

  // §5 execution context
  class io_context;            // io_context::scheduler_type models execution::scheduler

  // §6 timers
  sender auto async_resume_after(io_context::scheduler_type, chrono::duration);
  sender auto async_resume_at(io_context::scheduler_type, steady_clock::time_point);

  // §7 internet protocol
  namespace ip {
    class address_v4; class address_v6; class address;
    address make_address(string_view); address make_address(string_view, error_code&) noexcept;
    template<class Protocol> class basic_endpoint;
    class tcp;  class udp;           // tcp::socket, tcp::acceptor, udp::socket, ::endpoint
    template<class P> using resolve_results = vector<typename P::endpoint>;
    enum class resolve_flags;
    template<class P> resolve_results<P> resolve(string_view host, string_view service, ...);
    template<class P> sender auto async_resolve(io_context::scheduler_type, string host,
                                                string service, resolve_flags = {});
    using v6_only = /* boolean socket option */;
  }

  // §8 sockets
  class socket_base;
  template<class P> class basic_socket;
  template<class P> class basic_stream_socket;
  template<class P> class basic_datagram_socket;
  template<class P> class basic_socket_acceptor;

  // §8.5 socket senders
  sender auto async_accept(acceptor&);
  sender auto async_connect(socket&, const endpoint&);
  sender auto async_receive(socket&, const mutable_buffer_sequence auto&);   // async_read_some
  sender auto async_send(socket&, const const_buffer_sequence auto&);        // async_write_some
  sender auto async_read(socket&, const mutable_buffer_sequence auto&);
  sender auto async_write(socket&, const const_buffer_sequence auto&);
  sender auto async_receive_from(datagram_socket&, const mutable_buffer_sequence auto&);
  sender auto async_send_to(datagram_socket&, const const_buffer_sequence auto&, const endpoint&);
  sender auto async_wait(socket&, socket_base::wait_type);

  // §9 connection layer (TAPS)
  enum class preference { require, prefer, no_preference, avoid, prohibit };
  struct transport_properties;  class security_parameters;
  class remote_endpoint;        class local_endpoint;
  enum class transport_protocol { tcp, udp };
  class preconnection;          class connection;     class listener;
  sender auto initiate(io_context::scheduler_type, preconnection);
  listener    listen(io_context&, const preconnection&);
  sender auto async_accept(listener&);
  sender auto async_send(connection&, const const_buffer_sequence auto&);
  sender auto async_receive(connection&, const mutable_buffer_sequence auto&);

  // §3.2 adaptor
  inline constexpr /* unspecified */ as_expected;
}
```

## 3. Error model

### 3.1 Channels

All I/O senders have the completion signatures

```
set_value_t(Values...), set_error_t(error_code), set_stopped_t()
```

* **Value channel** — the operation completed. For transfers, the value is the byte count.
  A *short* count is a value, never an error.
* **Error channel** — an `error_code` from the operating system (`system_category()`,
  comparable with `errc`) or from `network_category()`.
* **Stopped channel** — the receiver's stop token was triggered before the operation completed.

Operations that may complete successfully even when a stop is requested (because the result
arrived first) do so; a stop request is a request, not a guarantee.

### 3.2 `as_expected`

`sndr | as_expected` turns `set_value(vs...)` into `set_value(expected<V, error_code>)` and
`set_error(error_code)` into `set_value(unexpected(ec))`. `V` is `void`, the single value type,
or a `tuple` of the value types. Exceptions and stop requests are forwarded unchanged. This
lets coroutine code handle routine I/O failures without exceptions:

```cpp
auto r = co_await (net::async_receive(s, net::buffer(buf)) | net::as_expected);
if (!r) log(r.error());
```

### 3.3 End of stream

End of stream **is not an error**. `async_receive` on a stream socket completes with
`set_value(0)` when the peer has shut down its sending side. `async_read` completes with the
number of bytes actually read, which is smaller than `buffer_size(bufs)` only when the stream
ended. *Rationale*: the Networking TS reports `error::eof`, which loses the byte count of a
composed read (P4007); POSIX `recv` already reports end of stream as 0.

## 4. Buffers

`mutable_buffer` and `const_buffer` are trivially copyable views (`data()`, `size()`,
`operator+=`, `bytes()` returning a `span<byte>`). A *buffer sequence* is either a single
buffer or a forward range whose references convert to the buffer type.

`buffer(r)` accepts any contiguous sized range of trivially copyable objects; constness of the
elements selects `const_buffer` or `mutable_buffer`. `buffer(b, n)` truncates.

Senders **copy the buffer sequence object** into their operation state; the memory it
designates must remain valid until completion.

## 5. `io_context`

```cpp
class io_context {
public:
  class scheduler_type;                 // models execution::scheduler
  class work_guard;                     // keeps run() alive while it exists
  io_context();
  scheduler_type get_scheduler() noexcept;
  work_guard make_work_guard() noexcept;

  size_t run();                         // until no outstanding work, or stop()
  size_t run_one();
  size_t run_for(const chrono::duration&);
  size_t run_until(const chrono::time_point&);
  size_t poll();                        // ready completions only, never blocks
  void stop() noexcept;  bool stopped() const noexcept;  void restart() noexcept;
  size_t outstanding_work() const noexcept;
};
```

* *Outstanding work* is the number of operations started on the context and not yet
  completed, plus live `work_guard`s. `run()` returns when it reaches zero.
* Several threads may call `run()` concurrently; completions are then distributed among them.
* `scheduler_type::schedule()` completes with `set_value()` on a thread running the context.
  It can complete with `set_stopped()` only when the receiver's stop token is stoppable, so
  the scheduler is *infallible* in the C++26 sense and can be used as a `task` scheduler.
* Every sender in this header exposes `get_completion_scheduler<set_value_t>` returning the
  scheduler of the I/O object's context.

## 6. Timers

`async_resume_after(sch, d)` completes with `set_value()` no earlier than `d` after the
operation is **started** (not when the sender is created). `async_resume_at(sch, tp)` uses an
absolute `steady_clock` deadline. Both complete with `set_stopped()` on a stop request.
Timers are senders rather than objects: a timeout is expressed by composition (e.g. a
`when_any`/`stop_when` algorithm) rather than by a `cancel()` member.

## 7. Internet protocol

As in the Networking TS (`[internet]`), with these adjustments:

* `address`, `address_v4`, `address_v6` are regular, `constexpr` where possible, and
  `formatter`-enabled; `basic_endpoint` is `formatter`-enabled (`[::1]:443`).
* `make_address(string_view, error_code&)` is `noexcept`; IPv6 zone identifiers (`%eth0`,
  `%3`) are accepted.
* Resolution returns `resolve_results<P>`, a `vector` of endpoints with duplicates removed.
  `async_resolve` never completes with an empty result: no address is `error::host_not_found`.
* Resolution is blocking on every mainstream platform; `async_resolve` runs it outside the
  context and completes on it. A stop request completes the sender immediately with
  `set_stopped()`; the platform lookup is abandoned, not interrupted.

## 8. Sockets

### 8.1 Common members (`basic_socket<P>`)

`context()`, `native_handle()`, `is_open()`, `open(P)`, `assign(P, native_handle)`, `close()`,
`cancel()`, `release()`, `bind(endpoint)`, `set_option(o)`, `get_option(o)`, `local_endpoint()`,
`remote_endpoint()`, `shutdown(shutdown_type)`, `protocol()` — each fallible member in a
throwing form and an `error_code&` form.

Sockets are always in non-blocking mode internally. Synchronous members block the calling
thread (and only it).

`close()` and `cancel()` complete pending operations with `set_error(errc::operation_canceled)`.
Destroying a socket with pending operations is undefined behaviour (as for any object
referenced by an operation state).

### 8.2 Options

`socket_base::reuse_address`, `keep_alive`, `broadcast`, `receive_buffer_size`,
`send_buffer_size`; `ip::v6_only`; `ip::tcp_no_delay`. Options follow the Networking TS
extensible-option requirements (`level`, `name`, `data`, `size`).

### 8.3 Stream sockets, datagram sockets, acceptors

As in the Networking TS: `connect`, `send`, `receive`, `read_some`, `write_some` (stream);
`send_to`, `receive_from` (datagram); `listen`, `accept` (acceptor). An acceptor constructed
from an endpoint opens, sets `reuse_address`, binds and listens.

### 8.4 Requirements common to the senders

* The I/O object and the buffer memory must outlive the operation state.
* `connect`-ing the sender does not perform I/O; `start()` does.
* At most one operation of each direction (read-like: accept, receive; write-like: connect,
  send) is serviced at a time per socket; concurrent operations of the same direction are
  queued in start order.

### 8.5 Socket senders

| Factory | Value completion | Notes |
|---|---|---|
| `async_accept(acceptor)` | `set_value(socket_type)` | The socket is bound to the acceptor's context. The peer endpoint is available through `remote_endpoint()`. |
| `async_connect(socket, ep)` | `set_value()` | Opens the socket for `ep.protocol()` if needed. |
| `async_receive(socket, bufs)` / `async_read_some` | `set_value(size_t n)` | One system call. `n == 0`: end of stream (or empty buffers). |
| `async_send(socket, bufs)` / `async_write_some` | `set_value(size_t n)` | One system call; `n` may be short. |
| `async_read(socket, bufs)` | `set_value(size_t n)` | Fills `bufs`; `n < buffer_size(bufs)` only at end of stream. |
| `async_write(socket, bufs)` | `set_value(size_t n)` | `n == buffer_size(bufs)`. |
| `async_receive_from(dsock, bufs)` | `set_value(size_t n, endpoint from)` | One datagram. |
| `async_send_to(dsock, bufs, ep)` | `set_value(size_t n)` | One datagram. |
| `async_wait(socket, wait_type)` | `set_value()` | Readiness, for integrating third-party protocol engines (e.g. TLS libraries). |

All of them also complete with `set_error_t(error_code)` and `set_stopped_t()`.

*Deviation from P2762*: P2762 makes these operations pipeable sender **adaptors** taking an
upstream sender of buffers. They are plain factories here; `let_value` composes them. Making
them adaptors is a compatible extension (§10).

## 9. Connection layer (TAPS)

### 9.1 Properties

`transport_properties` holds a `preference` for `reliability`, `preserve_order`,
`preserve_msg_boundaries`, `congestion_control`, `multistreaming` (RFC 9622 defaults:
reliable, ordered, congestion-controlled stream). `reliable_stream()` and
`unreliable_datagram()` are named presets.

`security_parameters{}` means *a secure transport is required*;
`security_parameters::disabled()` explicitly requests cleartext. `alpn(...)` records ALPN
identifiers for a future TLS binding.

### 9.2 Endpoints and preconnection

`remote_endpoint` / `local_endpoint` are builders: `with_hostname`, `with_address`, `with_port`,
`with_service`. A `preconnection` aggregates local and/or remote endpoint specifiers, transport
properties and security parameters. `preconnection::select(error_code&)` performs candidate
selection: every candidate protocol stack violating a `require` or `prohibit` is discarded,
the remaining ones are ranked by satisfied `prefer`/`avoid` preferences.
`error::no_candidate` reports contradictory properties; `error::not_supported` reports
properties the implementation cannot satisfy (including any secure transport today).

### 9.3 Establishment

* `initiate(sch, preconnection)` → `set_value(connection)`. Resolves the remote endpoint if
  it names a host, then attempts the candidate addresses in order until one succeeds; on
  failure completes with the last error.
* `listen(ctx, preconnection)` returns a `listener` (dual-stack when no local address is
  given). `async_accept(listener&)` → `set_value(connection)`.

### 9.4 `connection`

Move-only; `protocol()`, `context()`, `native_handle()`, `local_/remote_address()`,
`local_/remote_port()`, `close()` (graceful), `abort()` (abortive), and access to the
underlying socket. `async_send(connection&, bufs)` sends one *message*: the whole sequence on a
stream, one datagram otherwise. `async_receive(connection&, bufs)` returns available bytes on a
stream (0 at end of stream) or one datagram.

## 10. Open questions and planned extensions

1. **Coroutine-native alternative (P4003/P4100).** If WG21 adopts an `IoAwaitable` protocol,
   the reactor here can serve both front-ends (P4126 shows the bridge). The public sender API
   would not change.
2. **Pipeable adaptors** (P2762 form): `just(buf) | async_send(sock)`.
3. **Listener as a sequence sender** (P3482): needs sequence senders in the standard first.
4. **TLS and QUIC** behind `security_parameters`; certificate and trust callbacks as senders.
5. **Framers / message contexts** (TAPS) for message-oriented protocols over streams.
6. **Completion-based backends** (io_uring, IOCP). The operation model (perform on readiness)
   would gain a "submit" path; nothing in the public API assumes readiness.
7. **Happy Eyeballs** (RFC 8305) racing of candidates in `initiate`, instead of sequential tries.
8. **Allocator propagation** for operation states that allocate (resolution, buffer copies).
9. **`wrong_context`**: using an I/O object with another context's scheduler is currently not
   diagnosed, because I/O objects carry their own context.

## 11. Implementation status (stdnet 0.1)

| Area | Status |
|---|---|
| Buffers, addresses, endpoints, options | Done |
| `io_context`: epoll (Linux), poll (other POSIX), WSAPoll (Windows) | Done — tested on Linux (epoll and poll); macOS and Windows code paths written and covered by the CI matrix, not yet verified |
| Timers | Done |
| TCP, UDP sockets and all senders of §8.5 | Done |
| Resolver (sync and async) | Done |
| TAPS layer: selection, initiate, listen, connection (TCP, UDP) | Done |
| TLS, QUIC, framers, sequence-sender listener | Not started (§10) |
| `std::execution` backend | stdexec (NVIDIA) today; switches to `<execution>` when `__cpp_lib_senders` is defined |
