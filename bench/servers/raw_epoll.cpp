// Baseline 2: hand-written non-blocking epoll loop, one loop per thread with SO_REUSEPORT.
// Edge-triggered, one registration per socket, no allocation per operation: an upper bound for
// what a readiness reactor can achieve.
#include "../common/bench_common.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

int open_socket(unsigned short port, int type) {
    int fd = ::socket(AF_INET, type | SOCK_NONBLOCK, 0);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) std::_Exit(2);
    if (type == SOCK_STREAM) ::listen(fd, SOMAXCONN);
    return fd;
}

struct conn {
    int fd = -1;
    std::string in;  // http: unparsed input
    std::string out; // pending output
};

void tcp_loop(unsigned short port, bool http) {
    int lfd = open_socket(port, SOCK_STREAM);
    int ep = ::epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.ptr = nullptr; // listener
    ::epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev);
    std::vector<epoll_event> evs(256);
    char buf[65536];

    auto close_conn = [&](conn* c) {
        ::close(c->fd);
        delete c;
    };
    auto flush = [&](conn* c) -> bool {
        while (!c->out.empty()) {
            auto w = ::send(c->fd, c->out.data(), c->out.size(), MSG_NOSIGNAL);
            if (w < 0) return errno == EAGAIN;
            c->out.erase(0, static_cast<std::size_t>(w));
        }
        return true;
    };

    for (;;) {
        int n = ::epoll_wait(ep, evs.data(), static_cast<int>(evs.size()), -1);
        for (int i = 0; i < n; ++i) {
            if (evs[static_cast<std::size_t>(i)].data.ptr == nullptr) {
                for (;;) {
                    int fd = ::accept4(lfd, nullptr, nullptr, SOCK_NONBLOCK);
                    if (fd < 0) break;
                    int one = 1;
                    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                    auto* c = new conn{fd, {}, {}};
                    epoll_event cev{};
                    cev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                    cev.data.ptr = c;
                    ::epoll_ctl(ep, EPOLL_CTL_ADD, fd, &cev);
                }
                continue;
            }
            auto* c = static_cast<conn*>(evs[static_cast<std::size_t>(i)].data.ptr);
            bool dead = false;
            for (;;) {
                auto r = ::recv(c->fd, buf, sizeof buf, 0);
                if (r > 0) {
                    if (http) {
                        c->in.append(buf, static_cast<std::size_t>(r));
                        while (auto end = bench::request_end(c->in.data(), c->in.size())) {
                            c->in.erase(0, end);
                            c->out += bench::http_response;
                        }
                    } else if (c->out.empty()) {
                        // Fast path: echo straight from the stack buffer.
                        auto w = ::send(c->fd, buf, static_cast<std::size_t>(r), MSG_NOSIGNAL);
                        if (w < 0 && errno != EAGAIN) {
                            dead = true;
                            break;
                        }
                        if (w < r) c->out.append(buf + (w > 0 ? w : 0), static_cast<std::size_t>(r - (w > 0 ? w : 0)));
                    } else {
                        c->out.append(buf, static_cast<std::size_t>(r));
                    }
                    continue;
                }
                if (r == 0 || errno != EAGAIN) dead = true;
                break;
            }
            if (!dead && !flush(c)) dead = true;
            if (dead) close_conn(c);
        }
    }
}

void udp_loop(unsigned short port) {
    int fd = open_socket(port, SOCK_DGRAM);
    int ep = ::epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ::epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
    char buf[65536];
    epoll_event evs[8];
    for (;;) {
        ::epoll_wait(ep, evs, 8, -1);
        for (;;) {
            sockaddr_in from{};
            socklen_t len = sizeof from;
            auto n = ::recvfrom(fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
            if (n < 0) break;
            ::sendto(fd, buf, static_cast<std::size_t>(n), 0, reinterpret_cast<sockaddr*>(&from), len);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    auto opt = bench::parse(argc, argv);
    bench::raise_fd_limit();
    auto sigs = bench::block_stop_signals();
    for (int t = 0; t < opt.threads; ++t) {
        if (opt.mode == "udp")
            std::thread(udp_loop, opt.port).detach();
        else
            std::thread(tcp_loop, opt.port, opt.mode == "http").detach();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // listeners bound
    bench::announce(opt.port);
    bench::wait_stop_signal(sigs);
    bench::print_report();
    std::_Exit(0);
}
