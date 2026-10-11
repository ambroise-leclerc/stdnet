// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [net.taps] -- property-based connection establishment, after IETF TAPS (RFC 9621/9622)
// as proposed for C++ in P3185 and P3482.
//
// An application describes *what* it needs (endpoints, transport properties, security
// parameters) in a preconnection; the implementation selects a protocol stack and produces a
// connection. This layer sits on top of the socket layer of <stdnet/socket.hpp>.
//
// Implementation status (see docs/SPEC.md): selection covers TCP and UDP; security is
// "secure by default" (P1860), so a preconnection must either carry security parameters the
// implementation supports or explicitly opt out with security_parameters::disabled(). TLS is not
// implemented yet: initiate()/listen() report error::not_supported unless security is disabled.
#pragma once

#include "config.hpp"
#include "error.hpp"
#include "internet.hpp"
#include "io_context.hpp"
#include "resolver.hpp"
#include "scheduler.hpp"
#include "senders.hpp"
#include "socket.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace stdnet {

// [net.taps.props] -- transport preference levels (RFC 9622 §6.2).
enum class preference { require, prefer, no_preference, avoid, prohibit };

struct transport_properties {
    preference reliability = preference::require;
    preference preserve_order = preference::require;
    preference preserve_msg_boundaries = preference::no_preference;
    preference congestion_control = preference::require;
    preference multistreaming = preference::prefer;

    // Defaults of RFC 9622: a reliable, ordered, congestion-controlled byte stream.
    static constexpr transport_properties reliable_stream() noexcept { return {}; }

    // Unreliable, unordered messages (datagrams).
    static constexpr transport_properties unreliable_datagram() noexcept {
        transport_properties p;
        p.reliability = preference::avoid;
        p.preserve_order = preference::avoid;
        p.preserve_msg_boundaries = preference::require;
        p.congestion_control = preference::no_preference;
        p.multistreaming = preference::no_preference;
        return p;
    }
};

// [net.taps.security]
class security_parameters {
public:
    // Default: a secure transport is required (P1860 "C++ networking must be secure by default").
    security_parameters() = default;

    // Explicitly opts out of transport security (cleartext).
    static security_parameters disabled() noexcept {
        security_parameters s;
        s.disabled_ = true;
        return s;
    }

    security_parameters& alpn(std::vector<std::string> protocols) {
        alpn_ = std::move(protocols);
        return *this;
    }

    bool is_disabled() const noexcept { return disabled_; }
    const std::vector<std::string>& alpn() const noexcept { return alpn_; }

private:
    bool disabled_ = false;
    std::vector<std::string> alpn_;
};

// [net.taps.endpoint] -- endpoint specifiers; any subset of the fields may be set.
template <class Tag>
class endpoint_specifier {
public:
    endpoint_specifier() = default;

    endpoint_specifier& with_hostname(std::string h) {
        hostname_ = std::move(h);
        return *this;
    }
    endpoint_specifier& with_address(const ip::address& a) {
        address_ = a;
        return *this;
    }
    endpoint_specifier& with_port(ip::port_type p) {
        port_ = p;
        return *this;
    }
    endpoint_specifier& with_service(std::string s) {
        service_ = std::move(s);
        return *this;
    }

    const std::optional<std::string>& hostname() const noexcept { return hostname_; }
    const std::optional<ip::address>& address() const noexcept { return address_; }
    const std::optional<ip::port_type>& port() const noexcept { return port_; }
    const std::optional<std::string>& service() const noexcept { return service_; }

private:
    std::optional<std::string> hostname_;
    std::optional<ip::address> address_;
    std::optional<ip::port_type> port_;
    std::optional<std::string> service_;
};

struct remote_endpoint_tag {};
struct local_endpoint_tag {};
using remote_endpoint = endpoint_specifier<remote_endpoint_tag>;
using local_endpoint = endpoint_specifier<local_endpoint_tag>;

// The protocol stacks this implementation can select.
enum class transport_protocol { tcp, udp };

class preconnection {
public:
    preconnection(remote_endpoint r, transport_properties t = {}, security_parameters s = {})
        : remote_(std::move(r)), transport_(t), security_(std::move(s)) {}
    preconnection(local_endpoint l, transport_properties t = {}, security_parameters s = {})
        : local_(std::move(l)), transport_(t), security_(std::move(s)) {}
    preconnection(local_endpoint l, remote_endpoint r, transport_properties t = {}, security_parameters s = {})
        : local_(std::move(l)), remote_(std::move(r)), transport_(t), security_(std::move(s)) {}

    const std::optional<local_endpoint>& local() const noexcept { return local_; }
    const std::optional<remote_endpoint>& remote() const noexcept { return remote_; }
    const transport_properties& transport() const noexcept { return transport_; }
    const security_parameters& security() const noexcept { return security_; }

    // Candidate selection (RFC 9623 §4). Returns the selected protocol or an error:
    // error::no_candidate when the properties are contradictory for the supported stacks,
    // error::not_supported when they require an unimplemented feature (e.g. security).
    std::optional<transport_protocol> select(std::error_code& ec) const noexcept {
        if (!security_.is_disabled()) {
            ec = make_error_code(error::not_supported); // TLS/QUIC not implemented yet
            return std::nullopt;
        }
        auto ok = [](preference p, bool has) {
            return !((p == preference::require && !has) || (p == preference::prohibit && has));
        };
        auto score = [](preference p, bool has) {
            return (p == preference::prefer && has) || (p == preference::avoid && !has) ? 1 : 0;
        };
        struct traits {
            transport_protocol proto;
            bool reliable, ordered, boundaries, congestion;
        };
        constexpr traits stacks[] = {{transport_protocol::tcp, true, true, false, true},
                                     {transport_protocol::udp, false, false, true, false}};
        std::optional<transport_protocol> best;
        int best_score = -1;
        for (const auto& s : stacks) {
            const auto& t = transport_;
            if (!ok(t.reliability, s.reliable) || !ok(t.preserve_order, s.ordered) ||
                !ok(t.preserve_msg_boundaries, s.boundaries) || !ok(t.congestion_control, s.congestion))
                continue;
            int sc = score(t.reliability, s.reliable) + score(t.preserve_order, s.ordered) +
                     score(t.preserve_msg_boundaries, s.boundaries) + score(t.congestion_control, s.congestion);
            if (sc > best_score) {
                best = s.proto;
                best_score = sc;
            }
        }
        if (!best) ec = make_error_code(error::no_candidate);
        else ec.clear();
        return best;
    }

private:
    std::optional<local_endpoint> local_;
    std::optional<remote_endpoint> remote_;
    transport_properties transport_;
    security_parameters security_;
};

// [net.taps.connection] -- an established connection, whatever the selected protocol.
// For TCP, messages are bytes of a stream; for UDP, each send is one datagram and each
// receive returns one datagram.
class connection {
public:
    explicit connection(ip::tcp::socket s) : sock_(std::move(s)) {}
    explicit connection(ip::udp::socket s) : sock_(std::move(s)) {}
    connection(connection&&) noexcept = default;
    connection& operator=(connection&&) noexcept = default;

    transport_protocol protocol() const noexcept {
        return sock_.index() == 0 ? transport_protocol::tcp : transport_protocol::udp;
    }
    io_context& context() const noexcept {
        return std::visit([](auto& s) -> io_context& { return s.context(); }, sock_);
    }
    detail::native_handle_type native_handle() const noexcept {
        return std::visit([](auto& s) { return s.native_handle(); }, sock_);
    }
    bool is_open() const noexcept {
        return std::visit([](auto& s) { return s.is_open(); }, sock_);
    }

    ip::address remote_address() const {
        return std::visit([](auto& s) { return s.remote_endpoint().address(); }, sock_);
    }
    ip::port_type remote_port() const {
        return std::visit([](auto& s) { return s.remote_endpoint().port(); }, sock_);
    }
    ip::address local_address() const {
        return std::visit([](auto& s) { return s.local_endpoint().address(); }, sock_);
    }
    ip::port_type local_port() const {
        return std::visit([](auto& s) { return s.local_endpoint().port(); }, sock_);
    }

    // Graceful close: no more data will be sent; pending operations are aborted.
    void close() noexcept {
        std::visit(
            [](auto& s) {
                std::error_code ec;
                if (s.is_open()) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(s)>, ip::tcp::socket>)
                        s.shutdown(socket_base::shutdown_send, ec);
                    s.close(ec);
                }
            },
            sock_);
    }

    // Abortive close (TCP RST).
    void abort() noexcept {
        std::visit(
            [](auto& s) {
                if (!s.is_open()) return;
                ::linger l{};
                l.l_onoff = 1;
                l.l_linger = 0;
                ::setsockopt(s.native_handle(), SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&l), sizeof l);
                std::error_code ec;
                s.close(ec);
            },
            sock_);
    }

    // Access to the underlying socket, for options not covered by the property model.
    ip::tcp::socket* tcp_socket() noexcept { return std::get_if<ip::tcp::socket>(&sock_); }
    ip::udp::socket* udp_socket() noexcept { return std::get_if<ip::udp::socket>(&sock_); }

private:
    std::variant<ip::tcp::socket, ip::udp::socket> sock_;
};

// Sends one message: the whole buffer sequence (TCP) or one datagram (UDP). set_value(n).
template <const_buffer_sequence Seq>
auto async_send(connection& c, const Seq& bufs) {
    return detail::buffers_sender<detail::write_op, connection, Seq>{&c, bufs};
}

// Receives available bytes (TCP, 0 at end of stream) or one datagram (UDP). set_value(n).
template <mutable_buffer_sequence Seq>
auto async_receive(connection& c, const Seq& bufs) {
    return detail::buffers_sender<detail::receive_op, connection, Seq>{&c, bufs};
}

namespace detail {

// Establishes a connection: resolves the remote endpoint if needed, then tries every
// candidate address in order until one connects.
template <class Protocol, class Rcvr>
struct initiate_impl {
    using socket_type = typename Protocol::socket;
    using endpoint_type = typename Protocol::endpoint;

    struct resolve_rcvr {
        using receiver_concept = ex::receiver_t;
        initiate_impl* self;
        void set_value(ip::resolve_results<Protocol> r) && noexcept {
            self->candidates = std::move(r);
            self->try_next();
        }
        void set_error(std::error_code ec) && noexcept { self->finish_error(ec); }
        void set_stopped() && noexcept { ex::set_stopped(std::move(*self->rcvr)); }
        ex::env_of_t<Rcvr> get_env() const noexcept { return ex::get_env(*self->rcvr); }
    };
    struct connect_rcvr {
        using receiver_concept = ex::receiver_t;
        initiate_impl* self;
        void set_value() && noexcept {
            try {
                ex::set_value(std::move(*self->rcvr), connection(std::move(*self->sock)));
            } catch (...) {
                ex::set_error(std::move(*self->rcvr), std::make_error_code(std::errc::not_enough_memory));
            }
        }
        void set_error(std::error_code ec) && noexcept {
            // The failed attempt's state cannot be destroyed from inside its own completion:
            // the next attempt is started from a fresh completion on the io_context.
            self->last_error = ec;
            self->ctx->post(&self->retry);
        }
        void set_stopped() && noexcept { ex::set_stopped(std::move(*self->rcvr)); }
        ex::env_of_t<Rcvr> get_env() const noexcept { return ex::get_env(*self->rcvr); }
    };

    using resolve_state = decltype(ex::connect(std::declval<ip::detail_resolve::resolve_sender<Protocol>>(),
                                               std::declval<resolve_rcvr>()));
    using connect_state = connect_op<socket_type, connect_rcvr>;

    struct resolving_slot {
        std::optional<resolve_state> state;
    };
    struct retry_node : op_base {
        initiate_impl* self;
        static void run(op_base* b) noexcept { static_cast<retry_node*>(b)->self->try_next(); }
    };

    io_context* ctx;
    Rcvr* rcvr;
    const preconnection* pc;
    ip::resolve_results<Protocol> candidates;
    std::size_t next = 0;
    std::error_code last_error = make_error_code(error::host_not_found);
    std::optional<socket_type> sock;
    resolving_slot resolving;
    std::optional<connect_state> connecting;
    retry_node retry;

    template <class F>
    struct emplacer {
        F f;
        operator std::invoke_result_t<F&>() { return f(); }
    };

    initiate_impl(io_context* c, Rcvr* r, const preconnection* p) : ctx(c), rcvr(r), pc(p) {
        retry.self = this;
        retry.complete_fn_ = &retry_node::run;
    }

    void start() noexcept {
        const auto& remote = *pc->remote();
        ip::port_type port = remote.port().value_or(0);
        if (remote.address()) {
            candidates.push_back(endpoint_type(*remote.address(), port));
            try_next();
            return;
        }
        std::string service = remote.service().value_or(remote.port() ? std::to_string(port) : std::string());
        try {
            resolving.state.emplace(emplacer{[&] {
                return ex::connect(ip::async_resolve<Protocol>(ctx->get_scheduler(), remote.hostname().value_or(""),
                                                               service),
                                   resolve_rcvr{this});
            }});
        } catch (...) {
            finish_error(std::make_error_code(std::errc::not_enough_memory));
            return;
        }
        ex::start(*resolving.state);
    }

    void try_next() noexcept {
        // Each candidate gets a fresh socket; the previous attempt's state is destroyed first.
        connecting.reset();
        if (next >= candidates.size()) {
            finish_error(last_error);
            return;
        }
        endpoint_type ep = candidates[next++];
        if (pc->remote()->port() && !pc->remote()->address()) ep.port(*pc->remote()->port());
        sock.emplace(*ctx);
        if (pc->local()) {
            std::error_code ec;
            sock->open(ep.protocol(), ec);
            if (!ec) sock->set_option(socket_base::reuse_address(true), ec);
            if (!ec) {
                const auto& l = *pc->local();
                ip::address la = l.address().value_or(ep.protocol() == Protocol::v6() ? ip::address(ip::address_v6::any())
                                                                                     : ip::address(ip::address_v4::any()));
                sock->bind(endpoint_type(la, l.port().value_or(0)), ec);
            }
            if (ec) {
                last_error = ec;
                ctx->post(&retry);
                return;
            }
        }
        connecting.emplace(*sock, ep, connect_rcvr{this});
        connecting->start();
    }

    void finish_error(std::error_code ec) noexcept { ex::set_error(std::move(*rcvr), ec); }
};

template <class Rcvr>
struct initiate_op : op_base {
    io_context* ctx;
    Rcvr rcvr;
    preconnection pc;
    std::error_code ec;
    std::variant<std::monostate, initiate_impl<ip::tcp, Rcvr>, initiate_impl<ip::udp, Rcvr>> impl;

    initiate_op(io_context* c, preconnection p, Rcvr r) : ctx(c), rcvr(std::move(r)), pc(std::move(p)) {
        complete_fn_ = &fail;
    }
    initiate_op(initiate_op&&) = delete;

    static void fail(op_base* b) noexcept {
        auto* self = static_cast<initiate_op*>(b);
        ex::set_error(std::move(self->rcvr), self->ec);
    }

    void start() & noexcept {
        auto proto = pc.select(ec);
        if (!ec && !pc.remote()) ec = std::make_error_code(std::errc::destination_address_required);
        if (ec) {
            ctx->post(this); // complete on the context, not inline
            return;
        }
        if (*proto == transport_protocol::tcp)
            impl.template emplace<1>(ctx, &rcvr, &pc).start();
        else
            impl.template emplace<2>(ctx, &rcvr, &pc).start();
    }
};

struct initiate_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(connection), ex::set_error_t(std::error_code),
                                                            ex::set_stopped_t()>;
    io_context* ctx;
    preconnection pc;

    template <class Rcvr>
    initiate_op<Rcvr> connect(Rcvr r) const {
        return {ctx, pc, std::move(r)};
    }
    io_context::scheduler_type::env get_env() const noexcept { return {ctx}; }
};

} // namespace detail

// Actively opens a connection (TAPS "Initiate"). Completes with set_value(connection).
inline detail::initiate_sender initiate(io_context::scheduler_type sch, preconnection pc) {
    return {&sch.context(), std::move(pc)};
}

// [net.taps.listener] -- passive open (TAPS "Listen"). Only stream transports are supported.
class listener {
public:
    listener(io_context& ctx, const preconnection& pc) : acc_(ctx) {
        std::error_code ec;
        open(pc, ec);
        if (ec) throw std::system_error(ec, "listen");
    }
    listener(io_context& ctx, const preconnection& pc, std::error_code& ec) : acc_(ctx) { open(pc, ec); }

    io_context& context() const noexcept { return acc_.context(); }
    ip::port_type local_port() const { return acc_.local_endpoint().port(); }
    ip::address local_address() const { return acc_.local_endpoint().address(); }
    void close() noexcept {
        std::error_code ec;
        acc_.close(ec);
    }
    ip::tcp::acceptor& acceptor() noexcept { return acc_; }

private:
    void open(const preconnection& pc, std::error_code& ec) {
        auto proto = pc.select(ec);
        if (ec) return;
        if (*proto != transport_protocol::tcp) {
            ec = make_error_code(error::not_supported);
            return;
        }
        ip::tcp::endpoint ep(ip::address_v6::any(), 0);
        bool dual_stack = true;
        if (pc.local()) {
            const auto& l = *pc.local();
            ip::port_type port = l.port().value_or(0);
            if (!l.port() && l.service()) {
                auto r = ip::resolve<ip::tcp>("", *l.service(), ip::resolve_flags::passive, ec);
                if (ec) return;
                port = r.front().port();
            }
            if (l.address()) {
                ep = ip::tcp::endpoint(*l.address(), port);
                dual_stack = false;
            } else if (l.hostname()) {
                auto r = ip::resolve<ip::tcp>(*l.hostname(), "", ip::resolve_flags::passive, ec);
                if (ec) return;
                ep = ip::tcp::endpoint(r.front().address(), port);
                dual_stack = false;
            } else {
                ep.port(port);
            }
        }
        if (acc_.open(ep.protocol(), ec)) {
            if (!dual_stack) return;
            // No IPv6 on this host: fall back to IPv4 "any".
            ep = ip::tcp::endpoint(ip::address_v4::any(), ep.port());
            dual_stack = false;
            if (acc_.open(ep.protocol(), ec)) return;
        }
        if (dual_stack) acc_.set_option(ip::v6_only(false), ec);
        if (acc_.set_option(socket_base::reuse_address(true), ec)) return;
        if (acc_.bind(ep, ec)) return;
        acc_.listen(socket_base::max_listen_connections, ec);
    }

    ip::tcp::acceptor acc_;
};

inline listener listen(io_context& ctx, const preconnection& pc) { return listener(ctx, pc); }

// Completes with set_value(connection) for the next incoming connection.
inline auto async_accept(listener& l) noexcept {
    return ex::then(async_accept(l.acceptor()), [](ip::tcp::socket s) { return connection(std::move(s)); });
}

} // namespace stdnet
