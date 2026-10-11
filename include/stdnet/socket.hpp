// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [socket] -- sockets and acceptors. Synchronous members are provided in throwing and
// std::error_code forms; asynchronous operations are the sender factories of <stdnet/senders.hpp>.
#pragma once

#include "buffer.hpp"
#include "detail/platform.hpp"
#include "internet.hpp"
#include "io_context.hpp"

#include <array>
#include <chrono>
#include <system_error>
#include <utility>

namespace stdnet {

namespace detail {

// Blocks the calling thread until the handle is ready (used only by synchronous members).
inline std::error_code wait_handle(native_handle_type h, io_dir dir) noexcept {
    ::pollfd p{};
    p.fd = h;
    p.events = dir == io_dir::read ? POLLIN : POLLOUT;
#if STDNET_WINDOWS
    if (::WSAPoll(&p, 1, -1) < 0) return last_error();
#else
    for (;;) {
        if (::poll(&p, 1, -1) >= 0) break;
        auto ec = last_error();
        if (!interrupted(ec)) return ec;
    }
#endif
    return {};
}

template <class Level, class Name>
struct option_id {};

// Boolean and integer socket options ([socket.opt]).
template <int Level, int Name>
class boolean_option {
public:
    constexpr boolean_option() noexcept = default;
    constexpr explicit boolean_option(bool v) noexcept : value_(v ? 1 : 0) {}
    constexpr bool value() const noexcept { return value_ != 0; }
    constexpr explicit operator bool() const noexcept { return value(); }
    template <class P> constexpr int level(const P&) const noexcept { return Level; }
    template <class P> constexpr int name(const P&) const noexcept { return Name; }
    template <class P> void* data(const P&) noexcept { return &value_; }
    template <class P> const void* data(const P&) const noexcept { return &value_; }
    template <class P> constexpr std::size_t size(const P&) const noexcept { return sizeof value_; }

private:
    int value_ = 0;
};

template <int Level, int Name>
class integer_option {
public:
    constexpr integer_option() noexcept = default;
    constexpr explicit integer_option(int v) noexcept : value_(v) {}
    constexpr int value() const noexcept { return value_; }
    template <class P> constexpr int level(const P&) const noexcept { return Level; }
    template <class P> constexpr int name(const P&) const noexcept { return Name; }
    template <class P> void* data(const P&) noexcept { return &value_; }
    template <class P> const void* data(const P&) const noexcept { return &value_; }
    template <class P> constexpr std::size_t size(const P&) const noexcept { return sizeof value_; }

private:
    int value_ = 0;
};

// Collects a buffer sequence into an iovec array. Buffers beyond max_iov are ignored for this call.
inline constexpr std::size_t max_iov = 64;

template <class Buffer, class Seq>
std::size_t fill_iov(const Seq& seq, std::array<iovec_type, max_iov>& iov, std::size_t skip = 0) noexcept {
    std::size_t n = 0;
    for_each_buffer<Buffer>(seq, [&](Buffer b) {
        if (skip >= b.size()) {
            skip -= b.size();
            return;
        }
        b += skip;
        skip = 0;
        if (n < max_iov && b.size() > 0) set_iov(iov[n++], b.data(), b.size());
    });
    return n;
}

} // namespace detail

class socket_base {
public:
    using reuse_address = detail::boolean_option<SOL_SOCKET, SO_REUSEADDR>;
    using keep_alive = detail::boolean_option<SOL_SOCKET, SO_KEEPALIVE>;
    using broadcast = detail::boolean_option<SOL_SOCKET, SO_BROADCAST>;
    using receive_buffer_size = detail::integer_option<SOL_SOCKET, SO_RCVBUF>;
    using send_buffer_size = detail::integer_option<SOL_SOCKET, SO_SNDBUF>;

    enum shutdown_type { shutdown_receive = 0, shutdown_send = 1, shutdown_both = 2 };
    enum wait_type { wait_read = 0, wait_write = 1 };

    static constexpr int max_listen_connections = SOMAXCONN;

protected:
    ~socket_base() = default;
};

namespace ip {
// [internet.tcp.nodelay], [internet.ip.v6only]
using v6_only = detail::boolean_option<IPPROTO_IPV6, IPV6_V6ONLY>;
} // namespace ip

// [socket.basic] -- common base of stream sockets, datagram sockets and acceptors.
template <class Protocol>
class basic_socket : public socket_base {
public:
    using protocol_type = Protocol;
    using endpoint_type = typename Protocol::endpoint;
    using native_handle_type = detail::native_handle_type;

    io_context& context() const noexcept { return *ctx_; }
    native_handle_type native_handle() const noexcept { return handle_; }
    bool is_open() const noexcept { return handle_ != detail::invalid_handle; }

    void open(const protocol_type& p = protocol_type::v4()) { throw_on(open(p, ec_tmp()), "open"); }
    std::error_code open(const protocol_type& p, std::error_code& ec) {
        if (is_open()) return ec = std::make_error_code(std::errc::already_connected);
        handle_ = detail::open_socket(p.family(), p.type(), p.protocol(), ec);
        if (!ec) protocol_ = p;
        return ec;
    }

    // Adopts an already-open native socket. The handle is switched to non-blocking mode.
    std::error_code assign(const protocol_type& p, native_handle_type h, std::error_code& ec) {
        if (is_open()) return ec = std::make_error_code(std::errc::already_connected);
        if ((ec = detail::set_non_blocking(h, true))) return ec;
        handle_ = h;
        protocol_ = p;
        return ec;
    }

    // Aborts pending operations (they complete with errc::operation_canceled) and closes the socket.
    void close() { throw_on(close(ec_tmp()), "close"); }
    std::error_code close(std::error_code& ec) {
        ec.clear();
        if (!is_open()) return ec;
        ctx_->abort_handle(handle_);
        if (detail::close_handle(handle_) != 0) ec = detail::last_error();
        handle_ = detail::invalid_handle;
        return ec;
    }

    // Aborts pending operations without closing the socket.
    void cancel() noexcept {
        if (is_open()) ctx_->abort_handle(handle_);
    }

    // Releases ownership of the native handle after aborting pending operations.
    native_handle_type release() noexcept {
        cancel();
        return std::exchange(handle_, detail::invalid_handle);
    }

    void bind(const endpoint_type& e) { throw_on(bind(e, ec_tmp()), "bind"); }
    std::error_code bind(const endpoint_type& e, std::error_code& ec) {
        if (::bind(handle_, e.data(), static_cast<detail::socklen_type>(e.size())) != 0)
            ec = detail::last_error();
        else
            ec.clear();
        return ec;
    }

    template <class Option>
    void set_option(const Option& o) {
        throw_on(set_option(o, ec_tmp()), "set_option");
    }
    template <class Option>
    std::error_code set_option(const Option& o, std::error_code& ec) {
        if (::setsockopt(handle_, o.level(protocol_), o.name(protocol_), static_cast<const char*>(o.data(protocol_)),
                         static_cast<detail::socklen_type>(o.size(protocol_))) != 0)
            ec = detail::last_error();
        else
            ec.clear();
        return ec;
    }

    template <class Option>
    void get_option(Option& o) const {
        std::error_code ec;
        get_option(o, ec);
        if (ec) throw std::system_error(ec, "get_option");
    }
    template <class Option>
    std::error_code get_option(Option& o, std::error_code& ec) const {
        auto len = static_cast<detail::socklen_type>(o.size(protocol_));
        if (::getsockopt(handle_, o.level(protocol_), o.name(protocol_), static_cast<char*>(o.data(protocol_)), &len) != 0)
            ec = detail::last_error();
        else
            ec.clear();
        return ec;
    }

    endpoint_type local_endpoint() const {
        std::error_code ec;
        auto e = local_endpoint(ec);
        if (ec) throw std::system_error(ec, "local_endpoint");
        return e;
    }
    endpoint_type local_endpoint(std::error_code& ec) const {
        endpoint_type e;
        auto len = static_cast<detail::socklen_type>(e.capacity());
        if (::getsockname(handle_, e.data(), &len) != 0) ec = detail::last_error();
        else ec.clear();
        return e;
    }

    endpoint_type remote_endpoint() const {
        std::error_code ec;
        auto e = remote_endpoint(ec);
        if (ec) throw std::system_error(ec, "remote_endpoint");
        return e;
    }
    endpoint_type remote_endpoint(std::error_code& ec) const {
        endpoint_type e;
        auto len = static_cast<detail::socklen_type>(e.capacity());
        if (::getpeername(handle_, e.data(), &len) != 0) ec = detail::last_error();
        else ec.clear();
        return e;
    }

    void shutdown(shutdown_type what) { throw_on(shutdown(what, ec_tmp()), "shutdown"); }
    std::error_code shutdown(shutdown_type what, std::error_code& ec) {
        if (::shutdown(handle_, static_cast<int>(what)) != 0)
            ec = detail::last_error();
        else
            ec.clear();
        return ec;
    }

    protocol_type protocol() const noexcept { return protocol_; }

protected:
    explicit basic_socket(io_context& ctx) noexcept : ctx_(&ctx) {}
    basic_socket(io_context& ctx, const protocol_type& p) : ctx_(&ctx) { open(p); }
    basic_socket(io_context& ctx, const protocol_type& p, native_handle_type h) : ctx_(&ctx) {
        std::error_code ec;
        assign(p, h, ec);
        throw_on(ec, "assign");
    }
    basic_socket(basic_socket&& o) noexcept
        : ctx_(o.ctx_), handle_(std::exchange(o.handle_, detail::invalid_handle)), protocol_(o.protocol_) {}
    basic_socket& operator=(basic_socket&& o) noexcept {
        if (this != &o) {
            std::error_code ec;
            close(ec);
            ctx_ = o.ctx_;
            handle_ = std::exchange(o.handle_, detail::invalid_handle);
            protocol_ = o.protocol_;
        }
        return *this;
    }
    ~basic_socket() {
        std::error_code ec;
        close(ec);
    }

    static std::error_code& ec_tmp() noexcept {
        thread_local std::error_code ec;
        return ec;
    }
    static void throw_on(const std::error_code& ec, const char* what) {
        if (ec) throw std::system_error(ec, what);
    }

    // Synchronous helpers shared by stream and datagram sockets.
    template <class Seq>
    std::size_t sync_send(const Seq& bufs, const ::sockaddr* to, detail::socklen_type tolen, std::error_code& ec) {
        std::array<detail::iovec_type, detail::max_iov> iov;
        std::size_t n = detail::fill_iov<const_buffer>(bufs, iov);
        for (;;) {
            auto r = detail::send_iov(handle_, iov.data(), n, to, tolen, ec);
            if (r >= 0) return static_cast<std::size_t>(r);
            if (!detail::would_block(ec)) return 0;
            if ((ec = detail::wait_handle(handle_, detail::io_dir::write))) return 0;
        }
    }
    template <class Seq>
    std::size_t sync_receive(const Seq& bufs, ::sockaddr* from, detail::socklen_type* fromlen, std::error_code& ec) {
        std::array<detail::iovec_type, detail::max_iov> iov;
        std::size_t n = detail::fill_iov<mutable_buffer>(bufs, iov);
        for (;;) {
            auto r = detail::recv_iov(handle_, iov.data(), n, from, fromlen, 0, ec);
            if (r >= 0) return static_cast<std::size_t>(r);
            if (!detail::would_block(ec)) return 0;
            if ((ec = detail::wait_handle(handle_, detail::io_dir::read))) return 0;
        }
    }

    io_context* ctx_;
    native_handle_type handle_ = detail::invalid_handle;
    protocol_type protocol_ = protocol_type::v4();
};

// [socket.stream]
template <class Protocol>
class basic_stream_socket : public basic_socket<Protocol> {
    using base = basic_socket<Protocol>;

public:
    using typename base::endpoint_type;
    using typename base::native_handle_type;
    using typename base::protocol_type;

    explicit basic_stream_socket(io_context& ctx) noexcept : base(ctx) {}
    basic_stream_socket(io_context& ctx, const protocol_type& p) : base(ctx, p) {}
    basic_stream_socket(io_context& ctx, const protocol_type& p, native_handle_type h) : base(ctx, p, h) {}
    basic_stream_socket(basic_stream_socket&&) noexcept = default;
    basic_stream_socket& operator=(basic_stream_socket&&) noexcept = default;

    // Synchronous connect; opens the socket if needed.
    void connect(const endpoint_type& e) { base::throw_on(connect(e, base::ec_tmp()), "connect"); }
    std::error_code connect(const endpoint_type& e, std::error_code& ec) {
        if (!this->is_open() && this->open(e.protocol(), ec)) return ec;
        if (::connect(this->handle_, e.data(), static_cast<detail::socklen_type>(e.size())) == 0) return ec = {};
        ec = detail::last_error();
        if (!detail::would_block(ec)) return ec;
        if ((ec = detail::wait_handle(this->handle_, detail::io_dir::write))) return ec;
        return ec = detail::socket_error(this->handle_);
    }

    template <const_buffer_sequence Seq>
    std::size_t send(const Seq& bufs) {
        std::error_code ec;
        auto n = send(bufs, ec);
        base::throw_on(ec, "send");
        return n;
    }
    template <const_buffer_sequence Seq>
    std::size_t send(const Seq& bufs, std::error_code& ec) {
        return this->sync_send(bufs, nullptr, 0, ec);
    }

    // Returns 0 at end of stream.
    template <mutable_buffer_sequence Seq>
    std::size_t receive(const Seq& bufs) {
        std::error_code ec;
        auto n = receive(bufs, ec);
        base::throw_on(ec, "receive");
        return n;
    }
    template <mutable_buffer_sequence Seq>
    std::size_t receive(const Seq& bufs, std::error_code& ec) {
        return this->sync_receive(bufs, nullptr, nullptr, ec);
    }

    template <const_buffer_sequence Seq> std::size_t write_some(const Seq& b) { return send(b); }
    template <const_buffer_sequence Seq> std::size_t write_some(const Seq& b, std::error_code& ec) { return send(b, ec); }
    template <mutable_buffer_sequence Seq> std::size_t read_some(const Seq& b) { return receive(b); }
    template <mutable_buffer_sequence Seq> std::size_t read_some(const Seq& b, std::error_code& ec) { return receive(b, ec); }
};

// [socket.dgram]
template <class Protocol>
class basic_datagram_socket : public basic_socket<Protocol> {
    using base = basic_socket<Protocol>;

public:
    using typename base::endpoint_type;
    using typename base::native_handle_type;
    using typename base::protocol_type;

    explicit basic_datagram_socket(io_context& ctx) noexcept : base(ctx) {}
    basic_datagram_socket(io_context& ctx, const protocol_type& p) : base(ctx, p) {}
    basic_datagram_socket(io_context& ctx, const endpoint_type& e) : base(ctx, e.protocol()) { this->bind(e); }
    basic_datagram_socket(basic_datagram_socket&&) noexcept = default;
    basic_datagram_socket& operator=(basic_datagram_socket&&) noexcept = default;

    // Sets the default destination of send()/async_send().
    void connect(const endpoint_type& e) { base::throw_on(connect(e, base::ec_tmp()), "connect"); }
    std::error_code connect(const endpoint_type& e, std::error_code& ec) {
        if (!this->is_open() && this->open(e.protocol(), ec)) return ec;
        if (::connect(this->handle_, e.data(), static_cast<detail::socklen_type>(e.size())) != 0)
            ec = detail::last_error();
        else
            ec.clear();
        return ec;
    }

    template <const_buffer_sequence Seq>
    std::size_t send_to(const Seq& bufs, const endpoint_type& to) {
        std::error_code ec;
        auto n = send_to(bufs, to, ec);
        base::throw_on(ec, "send_to");
        return n;
    }
    template <const_buffer_sequence Seq>
    std::size_t send_to(const Seq& bufs, const endpoint_type& to, std::error_code& ec) {
        return this->sync_send(bufs, to.data(), static_cast<detail::socklen_type>(to.size()), ec);
    }

    template <mutable_buffer_sequence Seq>
    std::size_t receive_from(const Seq& bufs, endpoint_type& from) {
        std::error_code ec;
        auto n = receive_from(bufs, from, ec);
        base::throw_on(ec, "receive_from");
        return n;
    }
    template <mutable_buffer_sequence Seq>
    std::size_t receive_from(const Seq& bufs, endpoint_type& from, std::error_code& ec) {
        auto len = static_cast<detail::socklen_type>(from.capacity());
        return this->sync_receive(bufs, from.data(), &len, ec);
    }

    template <const_buffer_sequence Seq>
    std::size_t send(const Seq& bufs, std::error_code& ec) { return this->sync_send(bufs, nullptr, 0, ec); }
    template <mutable_buffer_sequence Seq>
    std::size_t receive(const Seq& bufs, std::error_code& ec) { return this->sync_receive(bufs, nullptr, nullptr, ec); }
};

// [socket.acceptor]
template <class Protocol>
class basic_socket_acceptor : public basic_socket<Protocol> {
    using base = basic_socket<Protocol>;

public:
    using typename base::endpoint_type;
    using typename base::native_handle_type;
    using typename base::protocol_type;
    using socket_type = typename Protocol::socket;

    explicit basic_socket_acceptor(io_context& ctx) noexcept : base(ctx) {}
    basic_socket_acceptor(io_context& ctx, const protocol_type& p) : base(ctx, p) {}

    // Opens, sets reuse_address (if requested), binds and listens.
    basic_socket_acceptor(io_context& ctx, const endpoint_type& e, bool reuse_addr = true) : base(ctx, e.protocol()) {
        if (reuse_addr) this->set_option(socket_base::reuse_address(true));
        this->bind(e);
        listen();
    }
    basic_socket_acceptor(basic_socket_acceptor&&) noexcept = default;
    basic_socket_acceptor& operator=(basic_socket_acceptor&&) noexcept = default;

    void listen(int backlog = socket_base::max_listen_connections) {
        base::throw_on(listen(backlog, base::ec_tmp()), "listen");
    }
    std::error_code listen(int backlog, std::error_code& ec) {
        if (::listen(this->handle_, backlog) != 0)
            ec = detail::last_error();
        else
            ec.clear();
        return ec;
    }

    // Synchronous accept. The returned socket is associated with the acceptor's io_context.
    socket_type accept() {
        std::error_code ec;
        auto s = accept(ec);
        base::throw_on(ec, "accept");
        return s;
    }
    socket_type accept(std::error_code& ec) {
        for (;;) {
            auto h = try_accept(ec);
            if (!ec) return socket_type(*this->ctx_, this->protocol_, h);
            if (!detail::would_block(ec)) return socket_type(*this->ctx_);
            if ((ec = detail::wait_handle(this->handle_, detail::io_dir::read))) return socket_type(*this->ctx_);
        }
    }

    // Implementation detail used by async_accept: a single non-blocking accept attempt.
    native_handle_type try_accept(std::error_code& ec) noexcept {
        for (;;) {
#if defined(__linux__)
            native_handle_type h = ::accept4(this->handle_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
            native_handle_type h = ::accept(this->handle_, nullptr, nullptr);
#endif
            if (h == detail::invalid_handle) {
                ec = detail::last_error();
                if (detail::interrupted(ec)) continue;
#if !STDNET_WINDOWS
                if (ec.value() == ECONNABORTED) continue;
#endif
                return h;
            }
#if !defined(__linux__)
            detail::set_non_blocking(h, true);
#if defined(SO_NOSIGPIPE)
            int one = 1;
            ::setsockopt(h, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
#endif
            ec.clear();
            return h;
        }
    }
};

namespace ip {
class tcp::socket : public basic_stream_socket<tcp> {
public:
    using basic_stream_socket<tcp>::basic_stream_socket;
};
class tcp::acceptor : public basic_socket_acceptor<tcp> {
public:
    using basic_socket_acceptor<tcp>::basic_socket_acceptor;
};
class udp::socket : public basic_datagram_socket<udp> {
public:
    using basic_datagram_socket<udp>::basic_datagram_socket;
};
} // namespace ip

namespace ip {
// TCP_NODELAY (declared after tcp is complete to keep tcp an aggregate-like protocol tag)
using tcp_no_delay = detail::boolean_option<IPPROTO_TCP, TCP_NODELAY>;
} // namespace ip

} // namespace stdnet
