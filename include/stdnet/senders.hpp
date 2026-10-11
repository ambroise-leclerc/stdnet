// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [net.sender] -- sender factories for asynchronous socket operations.
//
// Common contract of every sender in this header:
//  * the I/O object and the memory designated by the buffers must outlive the operation state;
//  * completion always happens on a thread running the I/O object's io_context, never inline in start();
//  * a stop request on the receiver's stop token cancels a pending operation, which then completes
//    with set_stopped();
//  * failures are reported with set_error(std::error_code); closing or cancel()-ing the I/O object
//    completes pending operations with set_error(std::errc::operation_canceled);
//  * end of stream is not an error: receive operations complete with set_value(0).
#pragma once

#include "buffer.hpp"
#include "config.hpp"
#include "io_context.hpp"
#include "scheduler.hpp"
#include "socket.hpp"

#include <system_error>
#include <utility>

namespace stdnet {

namespace detail {

template <class Derived, class Rcvr>
struct io_operation : io_op_base {
    io_context* ctx;
    Rcvr rcvr;

    struct on_stop {
        io_operation* self;
        void operator()() const noexcept { self->ctx->cancel(static_cast<io_op_base*>(self)); }
    };
    stop_hook<Rcvr, on_stop> hook;

    io_operation(io_context& c, native_handle_type fd, io_dir dir, Rcvr r) : ctx(&c), rcvr(std::move(r)) {
        fd_ = fd;
        dir_ = dir;
        perform_fn_ = &do_perform;
        complete_fn_ = &do_complete;
    }
    io_operation(io_operation&&) = delete;

    // Hook for operations that must do work on the calling thread before registration.
    std::error_code prepare() noexcept { return {}; }

    void start() & noexcept {
        if (auto ec = static_cast<Derived*>(this)->prepare()) {
            ec_ = ec;
            status_ = status::done;
            ctx->post(this);
            return;
        }
        if (fd_ == invalid_handle) {
            ec_ = std::make_error_code(std::errc::bad_file_descriptor);
            status_ = status::done;
            ctx->post(this);
            return;
        }
        if (hook.stop_requested(rcvr))
            cancel_requested_ = true;
        else
            hook.arm(rcvr, on_stop{this});
        ctx->start_io(this);
    }

    static bool do_perform(io_op_base* b) noexcept { return static_cast<Derived*>(b)->perform_impl(); }

    static void do_complete(op_base* b) noexcept {
        auto* self = static_cast<Derived*>(b);
        self->hook.disarm();
        if (self->status_ == status::cancelled)
            ex::set_stopped(std::move(self->rcvr));
        else if (self->ec_)
            ex::set_error(std::move(self->rcvr), self->ec_);
        else
            self->deliver();
    }

    // perform helper: classifies the result of a system call.
    // Returns false (keep waiting) when the call would block.
    bool finish_with(const std::error_code& ec) noexcept {
        if (would_block(ec)) return false;
        ec_ = ec;
        return true;
    }
};

using io_env = io_context::scheduler_type::env;

template <class... Values>
using io_completions = ex::completion_signatures<ex::set_value_t(Values...), ex::set_error_t(std::error_code),
                                                 ex::set_stopped_t()>;

// ---- accept

template <class Acceptor, class Rcvr>
struct accept_op : io_operation<accept_op<Acceptor, Rcvr>, Rcvr> {
    using base = io_operation<accept_op, Rcvr>;
    Acceptor* acc;
    native_handle_type accepted = invalid_handle;

    accept_op(Acceptor& a, Rcvr r) : base(a.context(), a.native_handle(), io_dir::read, std::move(r)), acc(&a) {}
    ~accept_op() {
        if (accepted != invalid_handle) close_handle(accepted);
    }

    bool perform_impl() noexcept {
        std::error_code ec;
        accepted = acc->try_accept(ec);
        if (!ec) return true;
        return this->finish_with(ec);
    }
    void deliver() noexcept {
        using socket_type = typename Acceptor::socket_type;
        try {
            socket_type s(acc->context(), acc->protocol(), std::exchange(accepted, invalid_handle));
            ex::set_value(std::move(this->rcvr), std::move(s));
        } catch (...) {
            ex::set_error(std::move(this->rcvr), std::make_error_code(std::errc::not_enough_memory));
        }
    }
};

template <class Acceptor>
struct accept_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = io_completions<typename Acceptor::socket_type>;
    Acceptor* acc;
    template <class Rcvr>
    accept_op<Acceptor, Rcvr> connect(Rcvr r) const {
        return {*acc, std::move(r)};
    }
    io_env get_env() const noexcept { return {&acc->context()}; }
};

// ---- connect

template <class Socket, class Rcvr>
struct connect_op : io_operation<connect_op<Socket, Rcvr>, Rcvr> {
    using base = io_operation<connect_op, Rcvr>;
    using endpoint_type = typename Socket::endpoint_type;
    Socket* sock;
    endpoint_type ep;
    bool in_progress = false;

    connect_op(Socket& s, const endpoint_type& e, Rcvr r)
        : base(s.context(), s.native_handle(), io_dir::write, std::move(r)), sock(&s), ep(e) {}

    std::error_code prepare() noexcept {
        std::error_code ec;
        if (!sock->is_open()) sock->open(ep.protocol(), ec);
        this->fd_ = sock->native_handle();
        return ec;
    }
    bool perform_impl() noexcept {
        if (in_progress) {
            this->ec_ = socket_error(this->fd_);
            return true;
        }
        if (::connect(this->fd_, ep.data(), static_cast<socklen_type>(ep.size())) == 0) return true;
        auto ec = last_error();
        if (would_block(ec)) {
            in_progress = true;
            return false;
        }
        this->ec_ = ec;
        return true;
    }
    void deliver() noexcept { ex::set_value(std::move(this->rcvr)); }
};

template <class Socket>
struct connect_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = io_completions<>;
    Socket* sock;
    typename Socket::endpoint_type ep;
    template <class Rcvr>
    connect_op<Socket, Rcvr> connect(Rcvr r) const {
        return {*sock, ep, std::move(r)};
    }
    io_env get_env() const noexcept { return {&sock->context()}; }
};

// ---- receive / send (single system call: "some" semantics)

template <class Socket, class Seq, class Rcvr>
struct receive_op : io_operation<receive_op<Socket, Seq, Rcvr>, Rcvr> {
    using base = io_operation<receive_op, Rcvr>;
    Seq bufs;
    std::size_t n = 0;

    receive_op(Socket& s, const Seq& b, Rcvr r)
        : base(s.context(), s.native_handle(), io_dir::read, std::move(r)), bufs(b) {}

    bool perform_impl() noexcept {
        std::array<iovec_type, max_iov> iov;
        std::size_t cnt = fill_iov<mutable_buffer>(bufs, iov);
        if (cnt == 0) return true; // empty buffer: completes with 0
        std::error_code ec;
        auto r = recv_iov(this->fd_, iov.data(), cnt, nullptr, nullptr, 0, ec);
        if (r >= 0) {
            n = static_cast<std::size_t>(r);
            return true;
        }
        return this->finish_with(ec);
    }
    void deliver() noexcept { ex::set_value(std::move(this->rcvr), n); }
};

template <class Socket, class Seq, class Rcvr>
struct send_op : io_operation<send_op<Socket, Seq, Rcvr>, Rcvr> {
    using base = io_operation<send_op, Rcvr>;
    Seq bufs;
    std::size_t n = 0;

    send_op(Socket& s, const Seq& b, Rcvr r) : base(s.context(), s.native_handle(), io_dir::write, std::move(r)), bufs(b) {}

    bool perform_impl() noexcept {
        std::array<iovec_type, max_iov> iov;
        std::size_t cnt = fill_iov<const_buffer>(bufs, iov);
        if (cnt == 0) return true;
        std::error_code ec;
        auto r = send_iov(this->fd_, iov.data(), cnt, nullptr, 0, ec);
        if (r >= 0) {
            n = static_cast<std::size_t>(r);
            return true;
        }
        return this->finish_with(ec);
    }
    void deliver() noexcept { ex::set_value(std::move(this->rcvr), n); }
};

// ---- read / write (composed: transfer the whole buffer sequence)

template <class Socket, class Seq, class Rcvr>
struct read_op : io_operation<read_op<Socket, Seq, Rcvr>, Rcvr> {
    using base = io_operation<read_op, Rcvr>;
    Seq bufs;
    std::size_t total = 0;
    std::size_t wanted;

    read_op(Socket& s, const Seq& b, Rcvr r)
        : base(s.context(), s.native_handle(), io_dir::read, std::move(r)), bufs(b), wanted(buffer_size(b)) {}

    bool perform_impl() noexcept {
        while (total < wanted) {
            std::array<iovec_type, max_iov> iov;
            std::size_t cnt = fill_iov<mutable_buffer>(bufs, iov, total);
            std::error_code ec;
            auto r = recv_iov(this->fd_, iov.data(), cnt, nullptr, nullptr, 0, ec);
            if (r < 0) return this->finish_with(ec);
            if (r == 0) return true; // end of stream: short read, not an error
            total += static_cast<std::size_t>(r);
        }
        return true;
    }
    void deliver() noexcept { ex::set_value(std::move(this->rcvr), total); }
};

template <class Socket, class Seq, class Rcvr>
struct write_op : io_operation<write_op<Socket, Seq, Rcvr>, Rcvr> {
    using base = io_operation<write_op, Rcvr>;
    Seq bufs;
    std::size_t total = 0;
    std::size_t wanted;

    write_op(Socket& s, const Seq& b, Rcvr r)
        : base(s.context(), s.native_handle(), io_dir::write, std::move(r)), bufs(b), wanted(buffer_size(b)) {}

    bool perform_impl() noexcept {
        while (total < wanted) {
            std::array<iovec_type, max_iov> iov;
            std::size_t cnt = fill_iov<const_buffer>(bufs, iov, total);
            std::error_code ec;
            auto r = send_iov(this->fd_, iov.data(), cnt, nullptr, 0, ec);
            if (r < 0) return this->finish_with(ec);
            total += static_cast<std::size_t>(r);
        }
        return true;
    }
    void deliver() noexcept { ex::set_value(std::move(this->rcvr), total); }
};

template <template <class, class, class> class Op, class Socket, class Seq>
struct buffers_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = io_completions<std::size_t>;
    Socket* sock;
    Seq bufs;
    template <class Rcvr>
    Op<Socket, Seq, Rcvr> connect(Rcvr r) const {
        return {*sock, bufs, std::move(r)};
    }
    io_env get_env() const noexcept { return {&sock->context()}; }
};

// ---- datagrams

template <class Socket, class Seq, class Rcvr>
struct receive_from_op : io_operation<receive_from_op<Socket, Seq, Rcvr>, Rcvr> {
    using base = io_operation<receive_from_op, Rcvr>;
    using endpoint_type = typename Socket::endpoint_type;
    Seq bufs;
    std::size_t n = 0;
    endpoint_type from;

    receive_from_op(Socket& s, const Seq& b, Rcvr r)
        : base(s.context(), s.native_handle(), io_dir::read, std::move(r)), bufs(b) {}

    bool perform_impl() noexcept {
        std::array<iovec_type, max_iov> iov;
        std::size_t cnt = fill_iov<mutable_buffer>(bufs, iov);
        std::error_code ec;
        auto len = static_cast<socklen_type>(from.capacity());
        auto r = recv_iov(this->fd_, iov.data(), cnt, from.data(), &len, 0, ec);
        if (r >= 0) {
            n = static_cast<std::size_t>(r);
            return true;
        }
        return this->finish_with(ec);
    }
    void deliver() noexcept { ex::set_value(std::move(this->rcvr), n, from); }
};

template <class Socket, class Seq>
struct receive_from_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = io_completions<std::size_t, typename Socket::endpoint_type>;
    Socket* sock;
    Seq bufs;
    template <class Rcvr>
    receive_from_op<Socket, Seq, Rcvr> connect(Rcvr r) const {
        return {*sock, bufs, std::move(r)};
    }
    io_env get_env() const noexcept { return {&sock->context()}; }
};

template <class Socket, class Seq, class Rcvr>
struct send_to_op : io_operation<send_to_op<Socket, Seq, Rcvr>, Rcvr> {
    using base = io_operation<send_to_op, Rcvr>;
    using endpoint_type = typename Socket::endpoint_type;
    Seq bufs;
    endpoint_type to;
    std::size_t n = 0;

    send_to_op(Socket& s, const Seq& b, const endpoint_type& e, Rcvr r)
        : base(s.context(), s.native_handle(), io_dir::write, std::move(r)), bufs(b), to(e) {}

    bool perform_impl() noexcept {
        std::array<iovec_type, max_iov> iov;
        std::size_t cnt = fill_iov<const_buffer>(bufs, iov);
        std::error_code ec;
        auto r = send_iov(this->fd_, iov.data(), cnt, to.data(), static_cast<socklen_type>(to.size()), ec);
        if (r >= 0) {
            n = static_cast<std::size_t>(r);
            return true;
        }
        return this->finish_with(ec);
    }
    void deliver() noexcept { ex::set_value(std::move(this->rcvr), n); }
};

template <class Socket, class Seq>
struct send_to_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = io_completions<std::size_t>;
    Socket* sock;
    Seq bufs;
    typename Socket::endpoint_type to;
    template <class Rcvr>
    send_to_op<Socket, Seq, Rcvr> connect(Rcvr r) const {
        return {*sock, bufs, to, std::move(r)};
    }
    io_env get_env() const noexcept { return {&sock->context()}; }
};

// ---- readiness wait (for integrating third-party protocol code, e.g. TLS libraries)

template <class Socket, class Rcvr>
struct wait_op : io_operation<wait_op<Socket, Rcvr>, Rcvr> {
    using base = io_operation<wait_op, Rcvr>;

    wait_op(Socket& s, socket_base::wait_type w, Rcvr r)
        : base(s.context(), s.native_handle(), w == socket_base::wait_read ? io_dir::read : io_dir::write,
               std::move(r)) {}

    bool perform_impl() noexcept {
        ::pollfd p{};
        p.fd = this->fd_;
        p.events = this->dir_ == io_dir::read ? POLLIN : POLLOUT;
#if STDNET_WINDOWS
        return ::WSAPoll(&p, 1, 0) > 0;
#else
        return ::poll(&p, 1, 0) > 0;
#endif
    }
    void deliver() noexcept { ex::set_value(std::move(this->rcvr)); }
};

template <class Socket>
struct wait_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = io_completions<>;
    Socket* sock;
    socket_base::wait_type what;
    template <class Rcvr>
    wait_op<Socket, Rcvr> connect(Rcvr r) const {
        return {*sock, what, std::move(r)};
    }
    io_env get_env() const noexcept { return {&sock->context()}; }
};

} // namespace detail

// ---------------------------------------------------------------------------------------------
// Sender factories

// Completes with set_value(socket_type) for the next incoming connection.
template <class Protocol>
auto async_accept(basic_socket_acceptor<Protocol>& acc) noexcept {
    return detail::accept_sender<basic_socket_acceptor<Protocol>>{&acc};
}
inline auto async_accept(ip::tcp::acceptor& acc) noexcept { return detail::accept_sender<ip::tcp::acceptor>{&acc}; }

// Opens the socket if needed and connects it. Completes with set_value().
template <class Socket>
    requires std::derived_from<Socket, basic_socket<typename Socket::protocol_type>>
auto async_connect(Socket& s, const typename Socket::endpoint_type& e) noexcept {
    return detail::connect_sender<Socket>{&s, e};
}

// One receive system call. Completes with set_value(n); n == 0 means end of stream
// (or an empty buffer sequence).
template <class Socket, mutable_buffer_sequence Seq>
auto async_receive(Socket& s, const Seq& bufs) {
    return detail::buffers_sender<detail::receive_op, Socket, Seq>{&s, bufs};
}
template <class Socket, mutable_buffer_sequence Seq>
auto async_read_some(Socket& s, const Seq& bufs) {
    return async_receive(s, bufs);
}

// One send system call. Completes with set_value(n), n possibly smaller than buffer_size(bufs).
template <class Socket, const_buffer_sequence Seq>
auto async_send(Socket& s, const Seq& bufs) {
    return detail::buffers_sender<detail::send_op, Socket, Seq>{&s, bufs};
}
template <class Socket, const_buffer_sequence Seq>
auto async_write_some(Socket& s, const Seq& bufs) {
    return async_send(s, bufs);
}

// Fills the whole buffer sequence. Completes with set_value(n): n < buffer_size(bufs) only when
// the stream ended first. The byte count is never lost on end of stream.
template <class Socket, mutable_buffer_sequence Seq>
auto async_read(Socket& s, const Seq& bufs) {
    return detail::buffers_sender<detail::read_op, Socket, Seq>{&s, bufs};
}

// Sends the whole buffer sequence. Completes with set_value(buffer_size(bufs)) or set_error.
template <class Socket, const_buffer_sequence Seq>
auto async_write(Socket& s, const Seq& bufs) {
    return detail::buffers_sender<detail::write_op, Socket, Seq>{&s, bufs};
}

// Datagram reception. Completes with set_value(n, sender_endpoint).
template <class Protocol, mutable_buffer_sequence Seq>
auto async_receive_from(basic_datagram_socket<Protocol>& s, const Seq& bufs) {
    return detail::receive_from_sender<basic_datagram_socket<Protocol>, Seq>{&s, bufs};
}
template <mutable_buffer_sequence Seq>
auto async_receive_from(ip::udp::socket& s, const Seq& bufs) {
    return detail::receive_from_sender<ip::udp::socket, Seq>{&s, bufs};
}

// Datagram emission. Completes with set_value(n).
template <class Protocol, const_buffer_sequence Seq>
auto async_send_to(basic_datagram_socket<Protocol>& s, const Seq& bufs, const typename Protocol::endpoint& to) {
    return detail::send_to_sender<basic_datagram_socket<Protocol>, Seq>{&s, bufs, to};
}
template <const_buffer_sequence Seq>
auto async_send_to(ip::udp::socket& s, const Seq& bufs, const ip::udp::endpoint& to) {
    return detail::send_to_sender<ip::udp::socket, Seq>{&s, bufs, to};
}

// Waits until the socket is readable or writable. Completes with set_value().
template <class Socket>
auto async_wait(Socket& s, socket_base::wait_type what) noexcept {
    return detail::wait_sender<Socket>{&s, what};
}

} // namespace stdnet
