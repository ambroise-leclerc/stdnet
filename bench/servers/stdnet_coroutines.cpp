// stdnet with std::execution::task coroutines (stdexec's implementation of C++26 task).
#include "../common/bench_common.hpp"

#include <network>

#include <array>
#include <string>
#include <thread>
#include <vector>

namespace net = stdnet;
namespace ex = stdnet::ex;
using net::ip::tcp;
using net::ip::udp;

namespace {

net::task<void> echo(tcp::socket s) {
    std::array<char, 65536> buf;
    for (;;) {
        std::size_t n = co_await net::async_receive(s, net::buffer(buf));
        if (n == 0) co_return;
        co_await net::async_write(s, net::buffer(buf.data(), n));
    }
}

net::task<void> http(tcp::socket s) {
    std::array<char, 65536> buf;
    std::string in, out;
    for (;;) {
        std::size_t n = co_await net::async_receive(s, net::buffer(buf));
        if (n == 0) co_return;
        in.append(buf.data(), n);
        out.clear();
        while (auto end = bench::request_end(in.data(), in.size())) {
            in.erase(0, end);
            out += bench::http_response;
        }
        co_await net::async_write(s, net::buffer(std::string_view(out)));
    }
}

template <class Sndr>
auto quiet(Sndr&& s) {
    return ex::upon_error(std::forward<Sndr>(s), [](auto&&) noexcept {});
}

net::task<void> listener(tcp::acceptor& acc, ex::counting_scope& scope, net::io_context::scheduler_type sch, bool is_http) {
    for (;;) {
        tcp::socket s = co_await net::async_accept(acc);
        s.set_option(net::ip::tcp_no_delay(true));
        if (is_http)
            ex::spawn(ex::starts_on(sch, quiet(http(std::move(s)))), scope.get_token());
        else
            ex::spawn(ex::starts_on(sch, quiet(echo(std::move(s)))), scope.get_token());
    }
}

net::task<void> udp_echo(udp::socket& s) {
    std::array<char, 65536> buf;
    for (;;) {
        auto [n, from] = co_await net::async_receive_from(s, net::buffer(buf));
        co_await net::async_send_to(s, net::buffer(buf.data(), n), from);
    }
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
    if (opt.mode == "udp") {
        us = std::make_unique<udp::socket>(ctx, udp::endpoint(loopback, opt.port));
        ex::spawn(ex::starts_on(sch, quiet(udp_echo(*us))), scope.get_token());
    } else {
        acc = std::make_unique<tcp::acceptor>(ctx, tcp::endpoint(loopback, opt.port));
        ex::spawn(ex::starts_on(sch, quiet(listener(*acc, scope, sch, opt.mode == "http"))), scope.get_token());
    }
    std::vector<std::thread> ts;
    for (int t = 0; t < opt.threads; ++t) ts.emplace_back([&] { ctx.run(); });
    bench::announce(opt.port);
    bench::wait_stop_signal(sigs, [&ctx] {
#if defined(STDNET_ENABLE_STATS)
        auto s = ctx.stats();
        char buf[640];
        std::snprintf(buf, sizeof buf,
                      "\"stats\": {\"io_started\": %llu, \"io_immediate\": %llu, \"io_retries\": %llu, \"io_would_block\": %llu, "
                      "\"reactor_waits\": %llu, \"reactor_events\": %llu, \"interest_changes\": %llu, \"posts\": %llu, \"cv_waits\": %llu, \"idle_in\": %llu, \"idle_out\": %llu}",
                      s.io_started, s.io_immediate, s.io_retries, s.io_would_block, s.reactor_waits, s.reactor_events,
                      s.interest_changes, s.posts, s.cv_waits, s.idle_in, s.idle_out);
        return std::string(buf);
#else
        (void)ctx;
        return std::string();
#endif
    });
    bench::print_report();
    std::_Exit(0);
}
