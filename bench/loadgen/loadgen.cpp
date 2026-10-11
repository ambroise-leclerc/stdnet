// Neutral load generator for the stdnet benchmarks: plain non-blocking sockets and epoll,
// one epoll loop per thread, no dependency on any of the libraries being measured.
//
//   loadgen --mode echo  --port P --conns C --size S --duration D [--warmup W] [--threads T]
//   loadgen --mode churn --port P --conns C --size S --duration D
//   loadgen --mode idle  --port P --conns C --active A --size S --duration D
//   loadgen --mode http  --port P --conns C --duration D
//   loadgen --mode udp   --port P --conns C --size S --window N --duration D
//
// Prints one JSON object with operation counts, throughput and round-trip latency percentiles.
//
// Measurement window: the run is [start, start + warmup) warm-up, then [measure_from, end)
// measured. Only operations started inside the window are counted. With --hold 1 the load
// generator prints "MEASURE_START" and "MEASURE_END" on stdout at the window boundaries and,
// after the window, keeps every connection open until it reads a line on stdin. bench/run.py
// uses this to sample the server (CPU, allocations, RSS) over exactly the same window, with the
// connections still established.
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using clk = std::chrono::steady_clock;

namespace {

struct config {
    std::string mode = "echo";
    unsigned short port = 0;
    int conns = 1;
    int size = 64;
    double duration = 5;
    double warmup = 1;
    int threads = 1;
    int active = 0;  // idle mode: number of active connections
    int window = 16; // udp mode: datagrams in flight per socket
    bool hold = false; // print window markers and wait for stdin before closing connections
};

std::atomic<bool> g_release{true};
std::atomic<int> g_ready{0};
std::atomic<bool> g_go{false};

// Called by every worker once its connections are established: the phases start only when
// all workers are ready, so connection setup never overlaps the measurement window.
void wait_start() {
    g_ready.fetch_add(1);
    while (!g_go.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::microseconds(200));
}

// Called by every worker after its measurement loop, before closing its sockets.
void wait_release() {
    while (!g_release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

config parse(int argc, char** argv) {
    config c;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string_view k = argv[i];
        const char* v = argv[i + 1];
        if (k == "--mode") c.mode = v;
        else if (k == "--port") c.port = static_cast<unsigned short>(std::atoi(v));
        else if (k == "--conns") c.conns = std::atoi(v);
        else if (k == "--size") c.size = std::atoi(v);
        else if (k == "--duration") c.duration = std::atof(v);
        else if (k == "--warmup") c.warmup = std::atof(v);
        else if (k == "--threads") c.threads = std::atoi(v);
        else if (k == "--active") c.active = std::atoi(v);
        else if (k == "--window") c.window = std::atoi(v);
        else if (k == "--hold") c.hold = std::atoi(v) != 0;
    }
    return c;
}

sockaddr_in server_addr(unsigned short port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return a;
}

void set_nonblocking(int fd) { ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK); }

int connect_blocking(unsigned short port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    auto a = server_addr(port);
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
            int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            set_nonblocking(fd);
            return fd;
        }
        if (errno != ECONNREFUSED && errno != EAGAIN && errno != EINTR) break;
        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
    }
    ::close(fd);
    return -1;
}

// Per-thread results, merged at the end.
struct results {
    std::uint64_t ops = 0, bytes = 0, errors = 0, lost = 0;
    std::vector<std::uint32_t> rtt_ns; // samples (capped)
    void sample(clk::duration d) {
        if (rtt_ns.size() < 4'000'000)
            rtt_ns.push_back(static_cast<std::uint32_t>(std::min<long long>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(d).count(), 0xFFFFFFFF)));
    }
};

struct phase {
    clk::time_point start, measure_from, end;
    bool measuring(clk::time_point t) const { return t >= measure_from; }
};

// ---- echo / idle: request-response with a fixed message size, depth 1 per connection -----

struct echo_conn {
    int fd = -1;
    std::size_t sent = 0, received = 0;
    clk::time_point t0;
    bool active = true;
};

void run_echo(const config& cfg, int conns, int active, const phase& ph, results& out) {
    int ep = ::epoll_create1(0);
    std::vector<echo_conn> cs(static_cast<std::size_t>(conns));
    std::string msg(static_cast<std::size_t>(cfg.size), 'x');
    std::vector<char> rbuf(std::max(65536, cfg.size));
    for (int i = 0; i < conns; ++i) {
        auto& c = cs[static_cast<std::size_t>(i)];
        c.fd = connect_blocking(cfg.port);
        if (c.fd < 0) {
            ++out.errors;
            continue;
        }
        c.active = i < active;
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.u32 = static_cast<std::uint32_t>(i);
        ::epoll_ctl(ep, EPOLL_CTL_ADD, c.fd, &ev);
    }
    auto start_send = [&](echo_conn& c, std::size_t idx) {
        c.sent = c.received = 0;
        c.t0 = clk::now();
        auto n = ::send(c.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
        if (n > 0) c.sent = static_cast<std::size_t>(n);
        epoll_event ev{};
        ev.events = EPOLLIN | (c.sent < msg.size() ? EPOLLOUT : 0u);
        ev.data.u32 = static_cast<std::uint32_t>(idx);
        ::epoll_ctl(ep, EPOLL_CTL_MOD, c.fd, &ev);
    };
    wait_start();
    for (std::size_t i = 0; i < cs.size(); ++i)
        if (cs[i].fd >= 0 && cs[i].active) start_send(cs[i], i);
        else if (cs[i].fd >= 0) {
            epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.u32 = static_cast<std::uint32_t>(i);
            ::epoll_ctl(ep, EPOLL_CTL_MOD, cs[i].fd, &ev);
        }

    epoll_event evs[256];
    for (;;) {
        auto now = clk::now();
        if (now >= ph.end) break;
        int n = ::epoll_wait(ep, evs, 256, 10);
        for (int k = 0; k < n; ++k) {
            std::size_t idx = evs[k].data.u32;
            auto& c = cs[idx];
            if ((evs[k].events & EPOLLOUT) && c.sent < msg.size()) {
                auto w = ::send(c.fd, msg.data() + c.sent, msg.size() - c.sent, MSG_NOSIGNAL);
                if (w > 0) c.sent += static_cast<std::size_t>(w);
                if (c.sent == msg.size()) {
                    epoll_event ev{};
                    ev.events = EPOLLIN;
                    ev.data.u32 = static_cast<std::uint32_t>(idx);
                    ::epoll_ctl(ep, EPOLL_CTL_MOD, c.fd, &ev);
                }
            }
            if (evs[k].events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
                for (;;) {
                    auto r = ::recv(c.fd, rbuf.data(), rbuf.size(), 0);
                    if (r > 0) {
                        c.received += static_cast<std::size_t>(r);
                        continue;
                    }
                    if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                        ++out.errors;
                        ::epoll_ctl(ep, EPOLL_CTL_DEL, c.fd, nullptr);
                        ::close(c.fd);
                        c.fd = -1;
                    }
                    break;
                }
                if (c.fd >= 0 && c.active && c.received >= msg.size()) {
                    auto t = clk::now();
                    if (ph.measuring(c.t0)) {
                        ++out.ops;
                        out.bytes += 2 * msg.size();
                        out.sample(t - c.t0);
                    }
                    start_send(c, idx);
                }
            }
        }
    }
    wait_release();
    for (auto& c : cs)
        if (c.fd >= 0) ::close(c.fd);
    ::close(ep);
}

// ---- http: GET / with keep-alive, depth 1 ----------------------------------------------------

void run_http(const config& cfg, int conns, const phase& ph, results& out) {
    static constexpr std::string_view req = "GET / HTTP/1.1\r\nHost: bench\r\n\r\n";
    struct conn {
        int fd;
        std::string in;
        clk::time_point t0;
    };
    int ep = ::epoll_create1(0);
    std::vector<conn> cs;
    for (int i = 0; i < conns; ++i) {
        int fd = connect_blocking(cfg.port);
        if (fd < 0) {
            ++out.errors;
            continue;
        }
        cs.push_back({fd, {}, {}});
    }
    for (std::size_t i = 0; i < cs.size(); ++i) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.u32 = static_cast<std::uint32_t>(i);
        ::epoll_ctl(ep, EPOLL_CTL_ADD, cs[i].fd, &ev);
    }
    wait_start();
    auto send_req = [&](conn& c) {
        c.t0 = clk::now();
        ::send(c.fd, req.data(), req.size(), MSG_NOSIGNAL);
    };
    for (auto& c : cs) send_req(c);
    char buf[65536];
    epoll_event evs[256];
    while (clk::now() < ph.end) {
        int n = ::epoll_wait(ep, evs, 256, 10);
        for (int k = 0; k < n; ++k) {
            auto& c = cs[evs[k].data.u32];
            if (c.fd < 0) continue;
            for (;;) {
                auto r = ::recv(c.fd, buf, sizeof buf, 0);
                if (r > 0) {
                    c.in.append(buf, static_cast<std::size_t>(r));
                    continue;
                }
                if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    ++out.errors;
                    ::epoll_ctl(ep, EPOLL_CTL_DEL, c.fd, nullptr);
                    ::close(c.fd);
                    c.fd = -1;
                }
                break;
            }
            // Complete responses: head + Content-Length body.
            while (c.fd >= 0) {
                auto head_end = c.in.find("\r\n\r\n");
                if (head_end == std::string::npos) break;
                auto cl = c.in.find("Content-Length:");
                std::size_t len = cl < head_end ? std::strtoul(c.in.c_str() + cl + 15, nullptr, 10) : 0;
                if (c.in.size() < head_end + 4 + len) break;
                auto total = head_end + 4 + len;
                c.in.erase(0, total);
                if (ph.measuring(c.t0)) {
                    ++out.ops;
                    out.bytes += req.size() + total;
                    out.sample(clk::now() - c.t0);
                }
                send_req(c);
            }
        }
    }
    wait_release();
    for (auto& c : cs)
        if (c.fd >= 0) ::close(c.fd);
    ::close(ep);
}

// ---- churn: connect, one exchange, close ------------------------------------------------------

void run_churn(const config& cfg, int conns, const phase& ph, results& out) {
    std::string msg(static_cast<std::size_t>(cfg.size), 'c');
    std::vector<char> rbuf(static_cast<std::size_t>(cfg.size));
    auto addr = server_addr(cfg.port);
    wait_start();
    // Each worker slot runs sequential blocking cycles; conns slots share this thread with poll.
    struct slot {
        int fd = -1;
        int state = 0; // 0 idle, 1 connecting, 2 waiting echo
        std::size_t got = 0;
        clk::time_point t0;
    };
    std::vector<slot> slots(static_cast<std::size_t>(conns));
    int ep = ::epoll_create1(0);
    auto begin = [&](std::size_t i) {
        auto& s = slots[i];
        s.fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        s.t0 = clk::now();
        s.got = 0;
        int r = ::connect(s.fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        if (r != 0 && errno != EINPROGRESS) {
            ++out.errors;
            ::close(s.fd);
            s.fd = -1;
            s.state = 0;
            return;
        }
        s.state = 1;
        epoll_event ev{};
        ev.events = EPOLLOUT | EPOLLIN;
        ev.data.u32 = static_cast<std::uint32_t>(i);
        ::epoll_ctl(ep, EPOLL_CTL_ADD, s.fd, &ev);
    };
    auto finish = [&](std::size_t i, bool ok) {
        auto& s = slots[i];
        ::epoll_ctl(ep, EPOLL_CTL_DEL, s.fd, nullptr);
        // RST instead of TIME_WAIT on the client side so that ephemeral ports do not run out.
        linger l{1, 0};
        ::setsockopt(s.fd, SOL_SOCKET, SO_LINGER, &l, sizeof l);
        ::close(s.fd);
        s.fd = -1;
        s.state = 0;
        if (ok) {
            if (ph.measuring(s.t0)) {
                ++out.ops;
                out.sample(clk::now() - s.t0);
            }
        } else {
            ++out.errors;
        }
    };
    epoll_event evs[256];
    while (clk::now() < ph.end) {
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].state == 0) begin(i);
        int n = ::epoll_wait(ep, evs, 256, 10);
        for (int k = 0; k < n; ++k) {
            std::size_t i = evs[k].data.u32;
            auto& s = slots[i];
            if (s.state == 1 && (evs[k].events & (EPOLLOUT | EPOLLERR | EPOLLHUP))) {
                int err = 0;
                socklen_t len = sizeof err;
                ::getsockopt(s.fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err) {
                    finish(i, false);
                    continue;
                }
                ::send(s.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
                s.state = 2;
                epoll_event ev{};
                ev.events = EPOLLIN;
                ev.data.u32 = static_cast<std::uint32_t>(i);
                ::epoll_ctl(ep, EPOLL_CTL_MOD, s.fd, &ev);
                continue;
            }
            if (s.state == 2 && (evs[k].events & (EPOLLIN | EPOLLERR | EPOLLHUP))) {
                auto r = ::recv(s.fd, rbuf.data(), rbuf.size(), 0);
                if (r > 0) s.got += static_cast<std::size_t>(r);
                if (s.got >= msg.size()) finish(i, true);
                else if (r == 0 || (r < 0 && errno != EAGAIN)) finish(i, false);
            }
        }
    }
    wait_release();
    for (auto& s : slots)
        if (s.fd >= 0) ::close(s.fd);
    ::close(ep);
}

// ---- udp: window of datagrams in flight per socket ------------------------------------------------

void run_udp(const config& cfg, int conns, const phase& ph, results& out) {
    struct usock {
        int fd;
        int inflight = 0;
        clk::time_point last_progress;
    };
    int ep = ::epoll_create1(0);
    auto addr = server_addr(cfg.port);
    std::vector<usock> ss;
    std::string msg(static_cast<std::size_t>(cfg.size), 'u');
    for (int i = 0; i < conns; ++i) {
        int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        int buf = 4 << 20;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
        ss.push_back({fd, 0, {}});
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.u32 = static_cast<std::uint32_t>(i);
        ::epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
    }
    wait_start();
    auto refill = [&](usock& s) {
        while (s.inflight < cfg.window) {
            if (::send(s.fd, msg.data(), msg.size(), 0) < 0) break;
            ++s.inflight;
        }
        s.last_progress = clk::now();
    };
    for (auto& s : ss) refill(s);
    char buf[65536];
    epoll_event evs[256];
    while (clk::now() < ph.end) {
        int n = ::epoll_wait(ep, evs, 256, 5);
        auto now = clk::now();
        for (int k = 0; k < n; ++k) {
            auto& s = ss[evs[k].data.u32];
            for (;;) {
                auto r = ::recv(s.fd, buf, sizeof buf, 0);
                if (r < 0) break;
                --s.inflight;
                if (ph.measuring(now)) {
                    ++out.ops;
                    out.bytes += 2 * static_cast<std::size_t>(r);
                }
            }
            refill(s);
        }
        for (auto& s : ss)
            if (now - s.last_progress > std::chrono::milliseconds(50)) { // datagrams lost
                out.lost += static_cast<std::uint64_t>(s.inflight);
                s.inflight = 0;
                refill(s);
            }
    }
    wait_release();
    for (auto& s : ss) ::close(s.fd);
    ::close(ep);
}

double percentile(std::vector<std::uint32_t>& v, double p) {
    if (v.empty()) return 0;
    auto idx = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(idx), v.end());
    return v[idx] / 1000.0;
}

} // namespace

int main(int argc, char** argv) {
    auto cfg = parse(argc, argv);
    rlimit r{};
    getrlimit(RLIMIT_NOFILE, &r);
    r.rlim_cur = r.rlim_max;
    setrlimit(RLIMIT_NOFILE, &r);

    int threads = std::max(1, std::min(cfg.threads, cfg.conns));
    std::vector<results> res(static_cast<std::size_t>(threads));
    phase ph; // filled in once every worker has connected (see wait_start)

    g_release = !cfg.hold;
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t) {
        int share = cfg.conns / threads + (t < cfg.conns % threads ? 1 : 0);
        int active_share = cfg.mode == "idle" ? cfg.active / threads + (t < cfg.active % threads ? 1 : 0) : share;
        ts.emplace_back([&, t, share, active_share] {
            auto& out = res[static_cast<std::size_t>(t)];
            if (cfg.mode == "echo" || cfg.mode == "idle") run_echo(cfg, share, active_share, ph, out);
            else if (cfg.mode == "http") run_http(cfg, share, ph, out);
            else if (cfg.mode == "churn") run_churn(cfg, share, ph, out);
            else if (cfg.mode == "udp") run_udp(cfg, share, ph, out);
        });
    }
    while (g_ready.load() < threads) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ph.start = clk::now();
    ph.measure_from = ph.start + std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(cfg.warmup));
    ph.end = ph.measure_from + std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(cfg.duration));
    g_go.store(true, std::memory_order_release);
    if (cfg.hold) {
        std::this_thread::sleep_until(ph.measure_from);
        std::printf("MEASURE_START\n");
        std::fflush(stdout);
        std::this_thread::sleep_until(ph.end);
        // Workers leave their loops at ph.end; give them a moment to stop issuing requests.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::printf("MEASURE_END\n");
        std::fflush(stdout);
        char line[16];
        if (!std::fgets(line, sizeof line, stdin)) {} // the runner has sampled the server
        g_release = true;
    }
    for (auto& t : ts) t.join();

    results all;
    for (auto& r2 : res) {
        all.ops += r2.ops;
        all.bytes += r2.bytes;
        all.errors += r2.errors;
        all.lost += r2.lost;
        all.rtt_ns.insert(all.rtt_ns.end(), r2.rtt_ns.begin(), r2.rtt_ns.end());
    }
    double secs = cfg.duration;
    std::printf("{\"mode\": \"%s\", \"conns\": %d, \"size\": %d, \"seconds\": %.3f, \"ops\": %llu, "
                "\"ops_per_s\": %.1f, \"mb_per_s\": %.2f, \"errors\": %llu, \"lost\": %llu, "
                "\"p50_us\": %.2f, \"p99_us\": %.2f, \"p999_us\": %.2f}\n",
                cfg.mode.c_str(), cfg.conns, cfg.size, secs, static_cast<unsigned long long>(all.ops),
                static_cast<double>(all.ops) / secs, static_cast<double>(all.bytes) / secs / 1e6,
                static_cast<unsigned long long>(all.errors), static_cast<unsigned long long>(all.lost),
                percentile(all.rtt_ns, 0.50), percentile(all.rtt_ns, 0.99), percentile(all.rtt_ns, 0.999));
    return all.ops > 0 && all.errors == 0 ? 0 : 2;
}
