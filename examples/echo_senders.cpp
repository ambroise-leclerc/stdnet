// Echo server and client written with sender algorithms only (no coroutines):
// the same operations compose with let_value / then / when_all like any other sender.
#include <network>

#include <cstdio>
#include <optional>
#include <thread>
#include <string>
#include <string_view>

namespace net = stdnet;
namespace ex = stdnet::ex;
using net::ip::tcp;

int main() {
    net::io_context ctx;
    tcp::acceptor acceptor(ctx, tcp::endpoint(net::ip::address_v4::loopback(), 0));
    tcp::socket client(ctx);
    std::optional<tcp::socket> peer;
    char server_buf[64];
    char client_buf[64];
    const std::string_view message = "echo through senders";

    // Server: accept one connection, read what arrives, send it back.
    auto server = ex::let_value(net::async_accept(acceptor), [&](tcp::socket& s) {
        peer.emplace(std::move(s));
        return ex::let_value(net::async_receive(*peer, net::buffer(server_buf)), [&](std::size_t n) {
            return net::async_write(*peer, net::buffer(server_buf, n));
        });
    });

    // Client: connect, send, then read the echo.
    auto client_side = ex::let_value(net::async_connect(client, acceptor.local_endpoint()), [&] {
        return ex::let_value(net::async_write(client, net::buffer(message)),
                             [&](std::size_t n) { return net::async_read(client, net::buffer(client_buf, n)); });
    });

    // Starting the composition and running the context on one thread.
    auto both = ex::when_all(std::move(server), std::move(client_side));
    auto guard = ctx.make_work_guard();
    std::thread io([&] { ctx.run(); });
    auto [sent, received] = ex::sync_wait(std::move(both)).value();
    guard.reset();
    io.join();

    std::printf("server echoed %zu bytes, client received \"%.*s\"\n", sent, static_cast<int>(received), client_buf);
    return std::string_view(client_buf, received) == message ? 0 : 1;
}
