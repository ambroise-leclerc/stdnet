// Asio (standalone) with completion handlers: the Networking TS programming model.
#include "../common/bench_common.hpp"

#include <asio.hpp>

#include <memory>
#include <string>
#include <thread>
#include <vector>

using asio::ip::tcp;
using asio::ip::udp;

namespace {

class echo_session : public std::enable_shared_from_this<echo_session> {
public:
    explicit echo_session(tcp::socket s) : socket_(std::move(s)) {}
    void start() { read(); }

private:
    void read() {
        socket_.async_read_some(asio::buffer(buf_), [self = shared_from_this()](std::error_code ec, std::size_t n) {
            if (ec) return;
            asio::async_write(self->socket_, asio::buffer(self->buf_, n),
                              [self](std::error_code ec2, std::size_t) {
                                  if (!ec2) self->read();
                              });
        });
    }
    tcp::socket socket_;
    std::array<char, 65536> buf_;
};

class http_session : public std::enable_shared_from_this<http_session> {
public:
    explicit http_session(tcp::socket s) : socket_(std::move(s)) {}
    void start() { read(); }

private:
    void read() {
        socket_.async_read_some(asio::buffer(buf_), [self = shared_from_this()](std::error_code ec, std::size_t n) {
            if (ec) return;
            self->in_.append(self->buf_.data(), n);
            self->out_.clear();
            while (auto end = bench::request_end(self->in_.data(), self->in_.size())) {
                self->in_.erase(0, end);
                self->out_ += bench::http_response;
            }
            asio::async_write(self->socket_, asio::buffer(self->out_), [self](std::error_code ec2, std::size_t) {
                if (!ec2) self->read();
            });
        });
    }
    tcp::socket socket_;
    std::array<char, 65536> buf_;
    std::string in_, out_;
};

void accept_loop(tcp::acceptor& acc, bool http) {
    acc.async_accept([&acc, http](std::error_code ec, tcp::socket s) {
        if (!ec) {
            s.set_option(tcp::no_delay(true));
            if (http)
                std::make_shared<http_session>(std::move(s))->start();
            else
                std::make_shared<echo_session>(std::move(s))->start();
        }
        accept_loop(acc, http);
    });
}

struct udp_echo {
    udp::socket socket;
    udp::endpoint from;
    std::array<char, 65536> buf;
    void receive() {
        socket.async_receive_from(asio::buffer(buf), from, [this](std::error_code ec, std::size_t n) {
            if (ec) return;
            socket.async_send_to(asio::buffer(buf, n), from, [this](std::error_code, std::size_t) { receive(); });
        });
    }
};

} // namespace

int main(int argc, char** argv) {
    auto opt = bench::parse(argc, argv);
    bench::raise_fd_limit();
    auto sigs = bench::block_stop_signals();
    asio::io_context ctx(opt.threads);
    auto loopback = asio::ip::address_v4::loopback();
    std::unique_ptr<tcp::acceptor> acc;
    std::unique_ptr<udp_echo> uecho;
    if (opt.mode == "udp") {
        uecho.reset(new udp_echo{udp::socket(ctx, udp::endpoint(loopback, opt.port)), {}, {}});
        uecho->receive();
    } else {
        acc = std::make_unique<tcp::acceptor>(ctx, tcp::endpoint(loopback, opt.port));
        accept_loop(*acc, opt.mode == "http");
    }
    std::vector<std::thread> ts;
    for (int t = 0; t < opt.threads; ++t) ts.emplace_back([&] { ctx.run(); });
    bench::announce(opt.port);
    bench::wait_stop_signal(sigs);
    bench::print_report();
    std::_Exit(0);
}
