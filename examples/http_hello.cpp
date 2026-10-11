// A minimal HTTP/1.1 server: one coroutine per connection, all running on one io_context.
//
//   http_hello [port]        then: curl http://localhost:8080/
//
// Shows the intended C++29 programming model: I/O senders awaited from std::execution::task,
// connections spawned into a counting_scope, cancellation through stop tokens.
#include <network>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <exception>
#include <format>
#include <string>
#include <string_view>

namespace net = stdnet;
namespace ex = stdnet::ex;
using net::ip::tcp;

namespace {

ex::task<void> serve(tcp::socket socket) {
    std::string request;
    char buf[4096];
    for (;;) {
        // Read until the end of the request head.
        while (request.find("\r\n\r\n") == std::string::npos) {
            std::size_t n = co_await net::async_receive(socket, net::buffer(buf));
            if (n == 0) co_return; // client closed the connection
            request.append(buf, n);
            if (request.size() > 64 * 1024) co_return; // refuse oversized heads
        }
        auto head_end = request.find("\r\n\r\n") + 4;
        std::string_view line(request.data(), request.find("\r\n"));
        bool keep_alive = request.find("Connection: close") == std::string::npos;

        std::string body = std::format("Hello from stdnet!\nYou asked: {}\n", line);
        std::string head = std::format("HTTP/1.1 200 OK\r\n"
                                       "Content-Type: text/plain; charset=utf-8\r\n"
                                       "Content-Length: {}\r\n"
                                       "Connection: {}\r\n\r\n",
                                       body.size(), keep_alive ? "keep-alive" : "close");
        std::array<net::const_buffer, 2> response{net::buffer(std::string_view(head)), net::buffer(std::string_view(body))};
        co_await net::async_write(socket, response);

        request.erase(0, head_end);
        if (!keep_alive) co_return;
    }
}

using scheduler = net::io_context::scheduler_type;

// spawn() only accepts senders that cannot fail: report and swallow exceptions.
template <class Sndr>
auto log_errors(Sndr&& sndr, const char* what) {
    return ex::upon_error(std::forward<Sndr>(sndr), [what](std::exception_ptr e) noexcept {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& x) {
            std::fprintf(stderr, "%s: %s\n", what, x.what());
        } catch (...) {
            std::fprintf(stderr, "%s: unknown error\n", what);
        }
    });
}

ex::task<void> accept_loop(tcp::acceptor& acceptor, ex::counting_scope& scope, scheduler sch) {
    for (;;) {
        tcp::socket s = co_await net::async_accept(acceptor);
        // Errors of one connection must not stop the server.
        ex::spawn(ex::starts_on(sch, log_errors(serve(std::move(s)), "connection")), scope.get_token());
    }
}
} // namespace

int main(int argc, char** argv) {
    net::ip::port_type port = 8080;
    if (argc > 1) std::from_chars(argv[1], argv[1] + std::strlen(argv[1]), port);

    net::io_context ctx;
    tcp::acceptor acceptor(ctx, tcp::endpoint(net::ip::address_v4::any(), port));
    std::printf("listening on %s\n", acceptor.local_endpoint().to_string().c_str());

    ex::counting_scope scope;
    auto sch = ctx.get_scheduler();
    ex::spawn(ex::starts_on(sch, log_errors(accept_loop(acceptor, scope, sch), "accept")), scope.get_token());
    ctx.run();
}
