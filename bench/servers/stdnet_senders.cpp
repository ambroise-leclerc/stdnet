// stdnet with sender algorithms only: each session is a repeat_until loop of
// receive -> write, spawned into a counting_scope. No coroutines, no callbacks.
#include "../common/bench_common.hpp"

#include <network>

#include <exec/repeat_until.hpp>

#include <array>
#include <string>
#include <thread>
#include <vector>

namespace net = stdnet;
namespace ex = stdnet::ex;
using net::ip::tcp;
using net::ip::udp;

namespace {

// The 64 KB receive buffer is held by pointer: sender values are moved between storage
// locations (just -> let_value), and moving a std::array would copy (and touch) all of it.
using buffer_ptr = std::unique_ptr<std::array<char, 65536>>;

struct echo_state {
    tcp::socket socket;
    buffer_ptr buf = std::make_unique_for_overwrite<std::array<char, 65536>>();
};

struct http_state {
    tcp::socket socket;
    buffer_ptr buf = std::make_unique_for_overwrite<std::array<char, 65536>>();
    std::string in, out;
};

// One loop iteration completes with true when the session is over.
auto echo_session(tcp::socket s) {
    return ex::let_value(ex::just(echo_state{std::move(s)}), [](echo_state& st) {
        return exec::repeat_until(ex::let_value(net::async_receive(st.socket, net::buffer(*st.buf)), [&st](std::size_t n) {
                   return ex::then(net::async_write(st.socket, net::buffer(st.buf->data(), n)),
                                   [n](std::size_t) noexcept { return n == 0; });
               })) |
               ex::upon_error([](auto&&) noexcept {});
    });
}

auto http_session(tcp::socket s) {
    return ex::let_value(ex::just(http_state{std::move(s)}), [](http_state& st) {
        return exec::repeat_until(ex::let_value(net::async_receive(st.socket, net::buffer(*st.buf)), [&st](std::size_t n) {
                   st.in.append(st.buf->data(), n);
                   st.out.clear();
                   while (auto end = bench::request_end(st.in.data(), st.in.size())) {
                       st.in.erase(0, end);
                       st.out += bench::http_response;
                   }
                   return ex::then(net::async_write(st.socket, net::buffer(std::string_view(st.out))),
                                   [n](std::size_t) noexcept { return n == 0; });
               })) |
               ex::upon_error([](auto&&) noexcept {});
    });
}

template <class Session>
auto accept_loop(tcp::acceptor& acc, ex::counting_scope& scope, net::io_context::scheduler_type sch, Session session) {
    return exec::repeat_until(ex::then(net::async_accept(acc),
                                       [&scope, sch, session](tcp::socket s) {
                                           s.set_option(net::ip::tcp_no_delay(true));
                                           ex::spawn(ex::starts_on(sch, session(std::move(s)) | ex::upon_error([](auto&&) noexcept {})), scope.get_token());
                                           return false;
                                       })) |
           ex::upon_error([](auto&&) noexcept {});
}

struct udp_state {
    udp::socket socket;
    std::array<char, 65536> buf;
};

auto udp_echo(udp::socket& s, std::array<char, 65536>& buf) {
    return exec::repeat_until(
               ex::let_value(net::async_receive_from(s, net::buffer(buf)),
                             [&](std::size_t n, udp::endpoint from) {
                                 return ex::then(net::async_send_to(s, net::buffer(buf.data(), n), from),
                                                 [](std::size_t) noexcept { return false; });
                             })) |
           ex::upon_error([](auto&&) noexcept {});
}

} // namespace

int main(int argc, char** argv) {
    auto opt = bench::parse(argc, argv);
    bench::raise_fd_limit();
    auto sigs = bench::block_stop_signals();
    net::io_context ctx;
    auto sch = ctx.get_scheduler();
    auto guard = ctx.make_work_guard();
    ex::counting_scope scope;
    auto loopback = net::ip::address_v4::loopback();

    std::unique_ptr<tcp::acceptor> acc;
    std::unique_ptr<udp::socket> us;
    std::array<char, 65536> ubuf;
    if (opt.mode == "udp") {
        us = std::make_unique<udp::socket>(ctx, udp::endpoint(loopback, opt.port));
        ex::spawn(ex::starts_on(sch, udp_echo(*us, ubuf) | ex::upon_error([](auto&&) noexcept {})), scope.get_token());
    } else {
        acc = std::make_unique<tcp::acceptor>(ctx, tcp::endpoint(loopback, opt.port));
        auto quiet = [](auto&& s) { return std::forward<decltype(s)>(s) | ex::upon_error([](auto&&) noexcept {}); };
        if (opt.mode == "http")
            ex::spawn(ex::starts_on(sch, quiet(accept_loop(*acc, scope, sch, [](tcp::socket s) { return http_session(std::move(s)); }))),
                      scope.get_token());
        else
            ex::spawn(ex::starts_on(sch, quiet(accept_loop(*acc, scope, sch, [](tcp::socket s) { return echo_session(std::move(s)); }))),
                      scope.get_token());
    }
    std::vector<std::thread> ts;
    for (int t = 0; t < opt.threads; ++t) ts.emplace_back([&] { ctx.run(); });
    bench::announce(opt.port);
    bench::wait_stop_signal(sigs);
    bench::print_report();
#if defined(STDNET_ENABLE_STATS)
    auto s = ctx.stats();
    std::printf("{\"stats\": {\"io_started\": %llu, \"io_immediate\": %llu, \"io_retries\": %llu, \"io_would_block\": %llu, "
                "\"reactor_waits\": %llu, \"reactor_events\": %llu, \"interest_changes\": %llu, \"posts\": %llu, \"cv_waits\": %llu}}\n",
                s.io_started, s.io_immediate, s.io_retries, s.io_would_block, s.reactor_waits, s.reactor_events,
                s.interest_changes, s.posts, s.cv_waits);
    std::fflush(stdout);
#endif
    std::_Exit(0);
}
