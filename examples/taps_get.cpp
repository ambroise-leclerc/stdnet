// Fetches a page over HTTP with the property-based (TAPS) API: the program states what it needs
// (a reliable stream to host:port, cleartext) and lets the library resolve and pick the transport.
//
//   taps_get localhost 8080 /
#include <network>

#include <cstdio>
#include <format>
#include <string>
#include <thread>

namespace net = stdnet;
namespace ex = stdnet::ex;

namespace {

ex::task<std::string> get(net::io_context::scheduler_type sch, std::string host, net::ip::port_type port,
                          std::string path) {
    net::preconnection pc(net::remote_endpoint().with_hostname(host).with_port(port),
                          net::transport_properties::reliable_stream(),
                          net::security_parameters::disabled()); // TLS is not implemented yet

    net::connection conn = co_await net::initiate(sch, std::move(pc));

    std::string request = std::format("GET {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n", path, host);
    co_await net::async_send(conn, net::buffer(std::string_view(request)));

    std::string response;
    char buf[4096];
    while (std::size_t n = co_await net::async_receive(conn, net::buffer(buf))) response.append(buf, n);
    co_return response;
}

} // namespace

int main(int argc, char** argv) {
    std::string host = argc > 1 ? argv[1] : "localhost";
    auto port = static_cast<net::ip::port_type>(argc > 2 ? std::stoi(argv[2]) : 80);
    std::string path = argc > 3 ? argv[3] : "/";

    net::io_context ctx;
    // Drive the I/O on a second thread while sync_wait runs the coroutine on this one.
    auto guard = ctx.make_work_guard();
    std::thread io([&] { ctx.run(); });
    int rc = 0;
    try {
        auto [response] = ex::sync_wait(get(ctx.get_scheduler(), host, port, path)).value();
        std::fwrite(response.data(), 1, response.size(), stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        rc = 1;
    }
    guard.reset();
    io.join();
    return rc;
}
