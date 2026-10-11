// Asio (standalone) with C++20 coroutines (awaitable / use_awaitable).
#include "../common/bench_common.hpp"

#include <asio.hpp>

#include <string>
#include <thread>
#include <vector>

using asio::awaitable;
using asio::use_awaitable;
using asio::ip::tcp;
using asio::ip::udp;

namespace {

awaitable<void> echo(tcp::socket s) {
    std::array<char, 65536> buf;
    try {
        for (;;) {
            std::size_t n = co_await s.async_read_some(asio::buffer(buf), use_awaitable);
            co_await asio::async_write(s, asio::buffer(buf, n), use_awaitable);
        }
    } catch (const std::exception&) {
    }
}

awaitable<void> http(tcp::socket s) {
    std::array<char, 65536> buf;
    std::string in, out;
    try {
        for (;;) {
            std::size_t n = co_await s.async_read_some(asio::buffer(buf), use_awaitable);
            in.append(buf.data(), n);
            out.clear();
            while (auto end = bench::request_end(in.data(), in.size())) {
                in.erase(0, end);
                out += bench::http_response;
            }
            co_await asio::async_write(s, asio::buffer(out), use_awaitable);
        }
    } catch (const std::exception&) {
    }
}

awaitable<void> listener(tcp::acceptor& acc, bool is_http) {
    for (;;) {
        tcp::socket s = co_await acc.async_accept(use_awaitable);
        s.set_option(tcp::no_delay(true));
        auto ex = acc.get_executor();
        if (is_http)
            asio::co_spawn(ex, http(std::move(s)), asio::detached);
        else
            asio::co_spawn(ex, echo(std::move(s)), asio::detached);
    }
}

awaitable<void> udp_echo(udp::socket& s) {
    std::array<char, 65536> buf;
    udp::endpoint from;
    for (;;) {
        std::size_t n = co_await s.async_receive_from(asio::buffer(buf), from, use_awaitable);
        co_await s.async_send_to(asio::buffer(buf, n), from, use_awaitable);
    }
}

} // namespace

int main(int argc, char** argv) {
    auto opt = bench::parse(argc, argv);
    bench::raise_fd_limit();
    auto sigs = bench::block_stop_signals();
    asio::io_context ctx(opt.threads);
    auto loopback = asio::ip::address_v4::loopback();
    std::unique_ptr<tcp::acceptor> acc;
    std::unique_ptr<udp::socket> us;
    if (opt.mode == "udp") {
        us = std::make_unique<udp::socket>(ctx, udp::endpoint(loopback, opt.port));
        asio::co_spawn(ctx, udp_echo(*us), asio::detached);
    } else {
        acc = std::make_unique<tcp::acceptor>(ctx, tcp::endpoint(loopback, opt.port));
        asio::co_spawn(ctx, listener(*acc, opt.mode == "http"), asio::detached);
    }
    std::vector<std::thread> ts;
    for (int t = 0; t < opt.threads; ++t) ts.emplace_back([&] { ctx.run(); });
    bench::announce(opt.port);
    bench::wait_stop_signal(sigs);
    bench::print_report();
    std::_Exit(0);
}
