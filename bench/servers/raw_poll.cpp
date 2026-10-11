// Baseline: the portable BSD-sockets way — non-blocking sockets and level-triggered poll(2),
// the same model as WSAPoll on Winsock and the same readiness semantics as stdnet's poll
// backend. One loop per thread with SO_REUSEPORT. No allocation per operation.
#include "../common/bench_common.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string>
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
    std::string in;  // http: unparsed input
    std::string out; // pending output
};

void tcp_loop(unsigned short port, bool http) {
    std::vector<pollfd> fds;      // fds[0] is the listener
    std::vector<conn> conns;      // conns[i] belongs to fds[i] (index 0 unused)
    fds.push_back({open_socket(port, SOCK_STREAM), POLLIN, 0});
    conns.emplace_back();
    char buf[65536];

    auto remove_at = [&](std::size_t i) {
        ::close(fds[i].fd);
        fds[i] = fds.back();
        conns[i] = std::move(conns.back());
        fds.pop_back();
        conns.pop_back();
    };

    for (;;) {
        ::poll(fds.data(), static_cast<nfds_t>(fds.size()), -1);
        if (fds[0].revents & POLLIN) {
            for (;;) {
                int fd = ::accept4(fds[0].fd, nullptr, nullptr, SOCK_NONBLOCK);
                if (fd < 0) break;
                int one = 1;
                ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                fds.push_back({fd, POLLIN, 0});
                conns.emplace_back();
            }
        }
        for (std::size_t i = fds.size(); i-- > 1;) {
            short ev = fds[i].revents;
            if (!ev) continue;
            conn& c = conns[i];
            bool dead = (ev & (POLLERR | POLLNVAL)) != 0;
            if (!dead && (ev & (POLLIN | POLLHUP))) {
                auto r = ::recv(fds[i].fd, buf, sizeof buf, 0);
                if (r > 0) {
                    if (http) {
                        c.in.append(buf, static_cast<std::size_t>(r));
                        while (auto end = bench::request_end(c.in.data(), c.in.size())) {
                            c.in.erase(0, end);
                            c.out += bench::http_response;
                        }
                    } else if (c.out.empty()) {
                        auto w = ::send(fds[i].fd, buf, static_cast<std::size_t>(r), MSG_NOSIGNAL);
                        if (w < 0 && errno != EAGAIN) dead = true;
                        else if (w < r) c.out.append(buf + (w > 0 ? w : 0), static_cast<std::size_t>(r - (w > 0 ? w : 0)));
                    } else {
                        c.out.append(buf, static_cast<std::size_t>(r));
                    }
                } else if (r == 0 || errno != EAGAIN) {
                    dead = true;
                }
            }
            if (!dead && !c.out.empty()) {
                auto w = ::send(fds[i].fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
                if (w < 0 && errno != EAGAIN) dead = true;
                else if (w > 0) c.out.erase(0, static_cast<std::size_t>(w));
            }
            if (dead) {
                remove_at(i);
                continue;
            }
            fds[i].events = c.out.empty() ? POLLIN : (POLLIN | POLLOUT);
        }
    }
}

void udp_loop(unsigned short port) {
    pollfd p{open_socket(port, SOCK_DGRAM), POLLIN, 0};
    char buf[65536];
    for (;;) {
        ::poll(&p, 1, -1);
        for (;;) {
            sockaddr_in from{};
            socklen_t len = sizeof from;
            auto n = ::recvfrom(p.fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
            if (n < 0) break;
            ::sendto(p.fd, buf, static_cast<std::size_t>(n), 0, reinterpret_cast<sockaddr*>(&from), len);
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
