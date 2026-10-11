// Baseline 1: blocking sockets, one thread per connection (echo, http) or one thread (udp).
#include "../common/bench_common.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

int listen_on(unsigned short port, int type) {
    int fd = ::socket(AF_INET, type, 0);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) std::_Exit(2);
    if (type == SOCK_STREAM) ::listen(fd, SOMAXCONN);
    return fd;
}

bool send_all(int fd, const char* p, std::size_t n) {
    while (n) {
        auto w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

void echo_session(int fd) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    char buf[65536];
    for (;;) {
        auto n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0 || !send_all(fd, buf, static_cast<std::size_t>(n))) break;
    }
    ::close(fd);
}

void http_session(int fd) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    std::string in;
    char buf[65536];
    for (;;) {
        auto n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        in.append(buf, static_cast<std::size_t>(n));
        std::string out;
        while (auto end = bench::request_end(in.data(), in.size())) {
            in.erase(0, end);
            out += bench::http_response;
        }
        if (!out.empty() && !send_all(fd, out.data(), out.size())) break;
    }
    ::close(fd);
}

} // namespace

int main(int argc, char** argv) {
    auto opt = bench::parse(argc, argv);
    bench::raise_fd_limit();
    auto sigs = bench::block_stop_signals();
    if (opt.mode == "udp") {
        int fd = listen_on(opt.port, SOCK_DGRAM);
        std::thread([fd] {
            char buf[65536];
            for (;;) {
                sockaddr_in from{};
                socklen_t len = sizeof from;
                auto n = ::recvfrom(fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
                if (n >= 0) ::sendto(fd, buf, static_cast<std::size_t>(n), 0, reinterpret_cast<sockaddr*>(&from), len);
            }
        }).detach();
    } else {
        int lfd = listen_on(opt.port, SOCK_STREAM);
        bool http = opt.mode == "http";
        std::thread([lfd, http] {
            for (;;) {
                int fd = ::accept(lfd, nullptr, nullptr);
                if (fd < 0) continue;
                std::thread(http ? http_session : echo_session, fd).detach();
            }
        }).detach();
    }
    bench::announce(opt.port);
    bench::wait_stop_signal(sigs);
    bench::print_report();
    std::_Exit(0);
}
