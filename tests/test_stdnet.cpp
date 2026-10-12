#include "check.hpp"
#include "driver.hpp"

#include <network>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace net = stdnet;
namespace ex = stdnet::ex;
using namespace std::chrono_literals;
using net::ip::tcp;
using net::ip::udp;

namespace {

// A listening acceptor on an ephemeral loopback port.
tcp::acceptor make_listener(net::io_context& ctx) {
    return tcp::acceptor(ctx, tcp::endpoint(net::ip::address_v4::loopback(), 0));
}

} // namespace

// ---------------------------------------------------------------------------------------------
TEST("buffer: creation, arithmetic and copy") {
    std::string s = "hello";
    auto b = net::buffer(s);
    CHECK(b.size() == 5);
    CHECK((b + 2).size() == 3);
    const std::string cs = "abc";
    net::const_buffer cb = net::buffer(cs);
    CHECK(cb.size() == 3);
    std::array<char, 4> dst{};
    std::vector<net::const_buffer> src{net::buffer(std::string_view("ab")), net::buffer(std::string_view("cde"))};
    CHECK(net::buffer_size(src) == 5);
    CHECK(net::buffer_copy(net::buffer(dst), src) == 4);
    CHECK(std::string_view(dst.data(), 4) == "abcd");
    static_assert(net::const_buffer_sequence<std::vector<net::const_buffer>>);
    static_assert(net::mutable_buffer_sequence<net::mutable_buffer>);
    static_assert(!net::mutable_buffer_sequence<net::const_buffer>);
}

TEST("address: parsing, formatting and endpoints") {
    auto a = net::ip::make_address("192.168.1.20");
    CHECK(a.is_v4());
    CHECK(a.to_string() == "192.168.1.20");
    auto b = net::ip::make_address("::1");
    CHECK(b.is_v6() && b.is_loopback());
    std::error_code ec;
    net::ip::make_address("not an address", ec);
    CHECK(ec == std::errc::invalid_argument);
    tcp::endpoint e(a, 8080);
    CHECK(e.port() == 8080);
    CHECK(e.address() == a);
    CHECK(e.to_string() == "192.168.1.20:8080");
    CHECK(std::format("{}", tcp::endpoint(b, 443)) == "[::1]:443");
    CHECK(e.protocol() == tcp::v4());
}

// ---------------------------------------------------------------------------------------------
TEST("scheduler: schedule completes on run()") {
    net::io_context ctx;
    bool ran = false;
    auto r = test::run(ctx, ex::then(ctx.get_scheduler().schedule(), [&] { ran = true; }));
    CHECK(r.value.has_value());
    CHECK(ran);
}

TEST("scheduler: completion never happens inline in start()") {
    net::io_context ctx;
    test::running op(ctx.get_scheduler().schedule());
    CHECK(!op.out.done); // start() returned without completing
    ctx.run();
    CHECK(op.out.done);
}

TEST("scheduler: an operation that succeeds at once completes inline from a completion, to a bounded depth") {
    net::io_context ctx;
    udp::socket a(ctx, udp::endpoint(net::ip::address_v4::loopback(), 0));
    udp::socket b(ctx, udp::endpoint(net::ip::address_v4::loopback(), 0));
    const auto to = b.local_endpoint();

    // Each send starts the next one from its own completion: a chain that would recurse without limit.
    struct chain {
        struct recv {
            using receiver_concept = ex::receiver_t;
            chain* c;
            void set_value(std::size_t) && noexcept { c->next(); }
            void set_error(std::error_code) && noexcept { ++c->failed; }
            void set_stopped() && noexcept { ++c->failed; }
            ex::env<> get_env() const noexcept { return {}; }
        };
        struct holder_base { virtual ~holder_base() = default; };
        udp::socket* sock = nullptr;
        udp::endpoint dest;
        int left = 0, depth = 0, max_depth = 0, failed = 0, done = 0;
        std::vector<std::unique_ptr<holder_base>> ops;
        const char payload[1] = {'x'};

        void next() {
            ++done;
            if (left == 0) return;
            --left;
            ++depth;
            max_depth = std::max(max_depth, depth);
            launch();
            --depth;
        }
        void launch() {
            auto sndr = net::async_send_to(*sock, net::buffer(payload, 1), dest);
            using op_t = decltype(ex::connect(std::move(sndr), recv{this}));
            struct h : holder_base {
                op_t op;
                explicit h(decltype(sndr)&& s, chain* c) : op(ex::connect(std::move(s), recv{c})) {}
            };
            auto p = std::make_unique<h>(std::move(sndr), this);
            auto* raw = p.get();
            ops.push_back(std::move(p));
            ex::start(raw->op);
        }
    } c;
    c.sock = &a;
    c.dest = to;
    c.left = 200;

    // Started from outside run(): queued, and the chain then runs inside the completions.
    c.next();
    CHECK(ctx.run() == 200); // inline completions are counted too
    CHECK(c.failed == 0);
    CHECK(c.done == 201);
#if STDNET_INLINE_DEPTH > 0
    CHECK(c.max_depth > 1);  // completions did nest (inline)
#endif
    CHECK(c.max_depth <= STDNET_INLINE_DEPTH + 1);
}

TEST("timer: async_resume_after waits, and ordering is by deadline") {
    net::io_context ctx;
    auto sch = ctx.get_scheduler();
    std::vector<int> order;
    auto t0 = std::chrono::steady_clock::now();
    test::running a(ex::then(net::async_resume_after(sch, 60ms), [&] { order.push_back(2); }));
    test::running b(ex::then(net::async_resume_after(sch, 20ms), [&] { order.push_back(1); }));
    ctx.run();
    auto elapsed = std::chrono::steady_clock::now() - t0;
    CHECK(elapsed >= 60ms);
    CHECK((order == std::vector<int>{1, 2}));
}

TEST("timer: stop request completes with set_stopped") {
    net::io_context ctx;
    test::running t(net::async_resume_after(ctx.get_scheduler(), 10s));
    ctx.poll();
    CHECK(!t.out.done);
    t.stop.request_stop();
    auto t0 = std::chrono::steady_clock::now();
    ctx.run();
    CHECK(t.out.stopped);
    CHECK(std::chrono::steady_clock::now() - t0 < 1s);
}

TEST("timer: stop requested before start") {
    net::io_context ctx;
    using S = decltype(net::async_resume_after(ctx.get_scheduler(), 10s));
    test::outcome<test::values_of<S>> out;
    ex::inplace_stop_source src;
    src.request_stop();
    auto op = ex::connect(net::async_resume_after(ctx.get_scheduler(), 10s),
                          test::receiver<test::values_of<S>>{&out, src.get_token()});
    ex::start(op);
    ctx.run();
    CHECK(out.stopped);
}

TEST("timer: deadline order with ties and cancellations in the middle of the queue") {
    net::io_context ctx;
    auto sch = ctx.get_scheduler();
    auto base = std::chrono::steady_clock::now() + 20ms;
    std::vector<int> order;
    auto deadline = [&](int i) { return base + std::chrono::microseconds((i * 7919) % 97 * 100); };
    auto make = [&](int i) { return ex::then(net::async_resume_at(sch, deadline(i)), [&order, i] { order.push_back(i); }); };
    using R = test::running<decltype(make(0))>;
    std::vector<std::unique_ptr<R>> timers;
    for (int i = 0; i < 300; ++i) timers.push_back(std::make_unique<R>(make(i))); // 97 distinct deadlines: ties
    for (int i = 0; i < 300; i += 3) timers[static_cast<std::size_t>(i)]->stop.request_stop();
    ctx.run();
    std::vector<int> expected;
    for (int i = 0; i < 300; ++i)
        if (i % 3 != 0) expected.push_back(i);
    std::stable_sort(expected.begin(), expected.end(), [&](int a, int b) { return deadline(a) < deadline(b); });
    CHECK(order == expected); // by deadline, then in start order
    for (int i = 0; i < 300; ++i) CHECK(timers[static_cast<std::size_t>(i)]->out.stopped == (i % 3 == 0));
}

TEST("timer: an earlier deadline wakes the thread blocked in the reactor") {
    net::io_context ctx;
    test::running late(net::async_resume_after(ctx.get_scheduler(), 10s));
    std::thread runner([&] { ctx.run(); });
    std::this_thread::sleep_for(50ms); // the runner is now waiting for the 10 s deadline
    std::atomic<bool> fired{false};
    auto t0 = std::chrono::steady_clock::now();
    test::running early(ex::then(net::async_resume_after(ctx.get_scheduler(), 10ms), [&] { fired = true; }));
    while (!fired && std::chrono::steady_clock::now() - t0 < 5s) std::this_thread::sleep_for(1ms);
    auto elapsed = std::chrono::steady_clock::now() - t0;
    late.stop.request_stop();
    runner.join();
    CHECK(fired.load());
    CHECK(elapsed < 1s);
    CHECK(late.out.stopped);
}

// ---------------------------------------------------------------------------------------------
TEST("tcp: accept, connect, write, read, end of stream") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    auto ep = acc.local_endpoint();

    test::running accepted(net::async_accept(acc));
    tcp::socket client(ctx);
    auto c = test::run(ctx, net::async_connect(client, ep));
    REQUIRE(c.value.has_value());
    REQUIRE(test::drive_until(ctx, [&] { return accepted.out.done; }));
    REQUIRE(accepted.out.value.has_value());
    tcp::socket server = std::move(std::get<0>(*accepted.out.value));
    CHECK(server.remote_endpoint() == client.local_endpoint());

    // Scatter/gather write of two buffers, composed read of exactly that many bytes.
    std::string part1 = "Hello, ", part2 = "network!";
    std::array<net::const_buffer, 2> out_bufs{net::buffer(std::string_view(part1)), net::buffer(std::string_view(part2))};
    auto w = test::run(ctx, net::async_write(client, out_bufs));
    REQUIRE(w.value.has_value());
    CHECK(std::get<0>(*w.value) == 15);

    std::string in(15, '\0');
    auto r = test::run(ctx, net::async_read(server, net::buffer(in)));
    REQUIRE(r.value.has_value());
    CHECK(std::get<0>(*r.value) == 15);
    CHECK(in == "Hello, network!");

    // End of stream: shutdown, then a read completes with set_value(0) -- not an error.
    client.shutdown(net::socket_base::shutdown_send);
    char buf[8];
    auto e = test::run(ctx, net::async_receive(server, net::buffer(buf)));
    REQUIRE(e.value.has_value());
    CHECK(std::get<0>(*e.value) == 0);
}

TEST("tcp: async_read reports a short count at end of stream instead of losing data") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    tcp::socket client(ctx);
    client.connect(acc.local_endpoint());
    auto server = acc.accept();
    client.send(net::buffer(std::string_view("abc")));
    client.shutdown(net::socket_base::shutdown_send);
    std::array<char, 10> buf{};
    auto r = test::run(ctx, net::async_read(server, net::buffer(buf)));
    REQUIRE(r.value.has_value());
    CHECK(std::get<0>(*r.value) == 3);
    CHECK(std::string_view(buf.data(), 3) == "abc");
}

TEST("tcp: connection refused is reported on the error channel") {
    net::io_context ctx;
    tcp::endpoint ep;
    {
        auto acc = make_listener(ctx);
        ep = acc.local_endpoint();
    } // closed: nobody listens on ep any more
    tcp::socket s(ctx);
    auto r = test::run(ctx, net::async_connect(s, ep));
    REQUIRE(r.error.has_value());
    CHECK(*r.error == std::errc::connection_refused);
}

TEST("tcp: stop request cancels a pending receive") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    tcp::socket client(ctx);
    client.connect(acc.local_endpoint());
    auto server = acc.accept();
    char buf[16];
    test::running rcv(net::async_receive(server, net::buffer(buf)));
    ctx.poll();
    CHECK(!rcv.out.done);
    rcv.stop.request_stop();
    REQUIRE(test::drive_until(ctx, [&] { return rcv.out.done; }));
    CHECK(rcv.out.stopped);
    // The socket stays usable after a cancelled operation.
    client.send(net::buffer(std::string_view("x")));
    auto again = test::run(ctx, net::async_receive(server, net::buffer(buf)));
    REQUIRE(again.value.has_value());
    CHECK(std::get<0>(*again.value) == 1);
}

TEST("tcp: close() aborts pending operations with operation_canceled") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    test::running a(net::async_accept(acc));
    ctx.poll();
    acc.close();
    REQUIRE(test::drive_until(ctx, [&] { return a.out.done; }));
    REQUIRE(a.out.error.has_value());
    CHECK(*a.out.error == std::errc::operation_canceled);
}

TEST("tcp: large transfer across many readiness events") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    tcp::socket client(ctx);
    client.connect(acc.local_endpoint());
    auto server = acc.accept();
    std::vector<unsigned char> data(8 * 1024 * 1024);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<unsigned char>(i * 131);
    std::vector<unsigned char> received(data.size());
    test::running w(net::async_write(client, net::buffer(std::as_const(data))));
    test::running r(net::async_read(server, net::buffer(received)));
    REQUIRE(test::drive_until(ctx, [&] { return w.out.done && r.out.done; }));
    REQUIRE(r.out.value.has_value());
    CHECK(std::get<0>(*r.out.value) == data.size());
    CHECK(received == data);
}

TEST("tcp: when_all composes a server and a client") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    tcp::socket client(ctx);
    std::optional<tcp::socket> server;
    char buf[5] = {};
    auto server_side = ex::let_value(net::async_accept(acc), [&](tcp::socket& s) {
        server.emplace(std::move(s));
        return net::async_read(*server, net::buffer(buf));
    });
    auto client_side = ex::let_value(net::async_connect(client, acc.local_endpoint()), [&] {
        return net::async_write(client, net::buffer(std::string_view("ping!")));
    });
    auto r = test::run(ctx, ex::when_all(std::move(server_side), std::move(client_side)));
    REQUIRE(r.value.has_value());
    CHECK(std::string_view(buf, 5) == "ping!");
}

// ---------------------------------------------------------------------------------------------
TEST("udp: send_to / receive_from") {
    net::io_context ctx;
    udp::socket a(ctx, udp::endpoint(net::ip::address_v4::loopback(), 0));
    udp::socket b(ctx, udp::endpoint(net::ip::address_v4::loopback(), 0));
    char buf[32];
    test::running rcv(net::async_receive_from(b, net::buffer(buf)));
    auto s = test::run(ctx, net::async_send_to(a, net::buffer(std::string_view("datagram")), b.local_endpoint()));
    REQUIRE(s.value.has_value());
    REQUIRE(test::drive_until(ctx, [&] { return rcv.out.done; }));
    REQUIRE(rcv.out.value.has_value());
    auto [n, from] = *rcv.out.value;
    CHECK(n == 8);
    CHECK(std::string_view(buf, n) == "datagram");
    CHECK(from == a.local_endpoint());
}

// ---------------------------------------------------------------------------------------------
TEST("resolver: localhost and numeric addresses") {
    auto r = net::ip::resolve<tcp>("127.0.0.1", "80");
    REQUIRE(!r.empty());
    CHECK(r.front().address() == net::ip::address(net::ip::address_v4::loopback()));
    CHECK(r.front().port() == 80);

    net::io_context ctx;
    auto a = test::run(ctx, net::async_resolve<tcp>(ctx.get_scheduler(), "localhost", "8080"));
    REQUIRE(a.value.has_value());
    auto& eps = std::get<0>(*a.value);
    REQUIRE(!eps.empty());
    CHECK(eps.front().address().is_loopback());
    CHECK(eps.front().port() == 8080);

    std::error_code ec;
    net::ip::resolve<tcp>("no-such-host.invalid", "80", net::ip::resolve_flags::none, ec);
    CHECK(ec == net::error::host_not_found || ec == net::error::try_again);
}

// ---------------------------------------------------------------------------------------------
TEST("preconnection: candidate selection") {
    using net::preconnection;
    auto remote = net::remote_endpoint().with_hostname("example.com").with_port(80);
    std::error_code ec;

    // Secure by default: no TLS in this implementation yet.
    CHECK(!preconnection(remote).select(ec));
    CHECK(ec == net::error::not_supported);

    auto clear = net::security_parameters::disabled();
    auto p = preconnection(remote, net::transport_properties::reliable_stream(), clear).select(ec);
    CHECK(!ec && p == net::transport_protocol::tcp);

    p = preconnection(remote, net::transport_properties::unreliable_datagram(), clear).select(ec);
    CHECK(!ec && p == net::transport_protocol::udp);

    auto impossible = net::transport_properties::reliable_stream();
    impossible.preserve_msg_boundaries = net::preference::require; // would need SCTP
    CHECK(!preconnection(remote, impossible, clear).select(ec));
    CHECK(ec == net::error::no_candidate);
}

TEST("preconnection: listen / initiate / send / receive over TCP") {
    net::io_context ctx;
    auto clear = net::security_parameters::disabled();
    net::listener l = net::listen(
        ctx, net::preconnection(net::local_endpoint().with_address(net::ip::address_v4::loopback()), {}, clear));
    auto port = l.local_port();
    CHECK(port != 0);

    test::running incoming(net::async_accept(l));
    auto c = test::run(ctx, net::initiate(ctx.get_scheduler(),
                                          net::preconnection(net::remote_endpoint().with_hostname("localhost").with_port(port),
                                                             {}, clear)));
    REQUIRE(c.value.has_value());
    auto conn = std::move(std::get<0>(*c.value));
    CHECK(conn.protocol() == net::transport_protocol::tcp);
    CHECK(conn.remote_port() == port);
    REQUIRE(test::drive_until(ctx, [&] { return incoming.out.done; }));
    REQUIRE(incoming.out.value.has_value());
    auto peer = std::move(std::get<0>(*incoming.out.value));

    auto s = test::run(ctx, net::async_send(conn, net::buffer(std::string_view("over TAPS"))));
    REQUIRE(s.value.has_value());
    char buf[9];
    auto r = test::run(ctx, net::async_read(peer, net::buffer(buf)));
    REQUIRE(r.value.has_value());
    CHECK(std::string_view(buf, 9) == "over TAPS");
    conn.close();
    auto eof = test::run(ctx, net::async_receive(peer, net::buffer(buf)));
    REQUIRE(eof.value.has_value());
    CHECK(std::get<0>(*eof.value) == 0);
}

TEST("preconnection: initiate falls back across candidates and reports the last error") {
    net::io_context ctx;
    tcp::endpoint dead;
    {
        auto acc = make_listener(ctx);
        dead = acc.local_endpoint();
    }
    auto c = test::run(ctx, net::initiate(ctx.get_scheduler(),
                                          net::preconnection(net::remote_endpoint()
                                                                 .with_address(dead.address())
                                                                 .with_port(dead.port()),
                                                             {}, net::security_parameters::disabled())));
    REQUIRE(c.error.has_value());
    CHECK(*c.error == std::errc::connection_refused);
}

TEST("preconnection: UDP connection") {
    net::io_context ctx;
    udp::socket peer(ctx, udp::endpoint(net::ip::address_v4::loopback(), 0));
    auto pc = net::preconnection(net::remote_endpoint()
                                     .with_address(net::ip::address_v4::loopback())
                                     .with_port(peer.local_endpoint().port()),
                                 net::transport_properties::unreliable_datagram(), net::security_parameters::disabled());
    auto c = test::run(ctx, net::initiate(ctx.get_scheduler(), pc));
    REQUIRE(c.value.has_value());
    auto conn = std::move(std::get<0>(*c.value));
    CHECK(conn.protocol() == net::transport_protocol::udp);
    auto s = test::run(ctx, net::async_send(conn, net::buffer(std::string_view("dgram"))));
    REQUIRE(s.value.has_value());
    char buf[16];
    udp::endpoint from;
    auto n = peer.receive_from(net::buffer(buf), from);
    CHECK(std::string_view(buf, n) == "dgram");
}

// ---------------------------------------------------------------------------------------------
namespace {
ex::task<std::string> echo_once(tcp::acceptor& acc) {
    tcp::socket s = co_await net::async_accept(acc);
    char buf[64];
    std::size_t n = co_await net::async_receive(s, net::buffer(buf));
    co_await net::async_write(s, net::buffer(std::string_view(buf, n)));
    co_return std::string(buf, n);
}
} // namespace

TEST("coroutines: senders are awaitable in std::execution::task") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    tcp::socket client(ctx);
    client.connect(acc.local_endpoint());
    client.send(net::buffer(std::string_view("co_await")));
    // The io_context runs on its own thread; the task runs in sync_wait's run_loop and
    // resumes there after each co_await (scheduler affinity of std::execution::task).
    auto guard = ctx.make_work_guard();
    std::thread runner([&] { ctx.run(); });
    auto [echoed] = ex::sync_wait(echo_once(acc)).value();
    CHECK(echoed == "co_await");
    char buf[8];
    CHECK(client.receive(net::buffer(buf)) == 8);
    guard.reset();
    runner.join();
}

namespace {
ex::task<std::size_t> echo_session(tcp::socket& s) {
    char buf[256];
    std::size_t total = 0;
    for (;;) {
        std::size_t n = co_await net::async_receive(s, net::buffer(buf));
        if (n == 0) co_return total;
        co_await net::async_write(s, net::buffer(buf, n));
        total += n;
    }
}
} // namespace

TEST("threads: many connections echoed while four threads run one io_context") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    constexpr int connections = 16, rounds = 300, size = 100;
    std::vector<std::unique_ptr<tcp::socket>> clients, servers;
    for (int i = 0; i < connections; ++i) {
        clients.push_back(std::make_unique<tcp::socket>(ctx));
        clients.back()->connect(acc.local_endpoint());
        servers.push_back(std::make_unique<tcp::socket>(acc.accept()));
    }
    auto guard = ctx.make_work_guard();
    std::vector<std::thread> runners;
    for (int i = 0; i < 4; ++i) runners.emplace_back([&] { ctx.run(); });

    std::atomic<int> bad{0};
    std::atomic<std::size_t> echoed{0};
    std::vector<std::thread> sessions, peers;
    for (int i = 0; i < connections; ++i) {
        sessions.emplace_back([&, i] {
            auto [n] = ex::sync_wait(ex::starts_on(ctx.get_scheduler(), echo_session(*servers[i]))).value();
            echoed += n;
        });
        peers.emplace_back([&, i] {
            char out[size], in[size];
            for (int r = 0; r < rounds; ++r) {
                std::memset(out, 'a' + (i + r) % 26, size);
                clients[i]->send(net::buffer(out));
                std::size_t got = 0;
                while (got < size) got += clients[i]->receive(net::buffer(in + got, size - got));
                if (std::memcmp(in, out, size) != 0) ++bad;
            }
            clients[i]->shutdown(net::socket_base::shutdown_send);
        });
    }
    for (auto& t : peers) t.join();
    for (auto& t : sessions) t.join();
    guard.reset();
    for (auto& t : runners) t.join();
    CHECK(bad == 0);
    CHECK(echoed == std::size_t{connections} * rounds * size);
}

#if STDNET_USE_STDEXEC
namespace {
net::task<std::thread::id> bound_echo_once(tcp::acceptor& acc, std::thread::id& after_receive) {
    tcp::socket s = co_await net::async_accept(acc);
    char buf[64];
    std::size_t n = co_await net::async_receive(s, net::buffer(buf));
    after_receive = std::this_thread::get_id();
    co_await net::async_write(s, net::buffer(std::string_view(buf, n)));
    co_return std::this_thread::get_id();
}

// Awaits an operation of `other` while running on the io_context it is started on.
net::task<std::thread::id> cross_context(tcp::socket& on_other) {
    char buf[64];
    co_await net::async_receive(on_other, net::buffer(buf));
    co_return std::this_thread::get_id();
}
} // namespace

TEST("task: a stdnet::task resumes on its io_context after each co_await") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    tcp::socket client(ctx);
    client.connect(acc.local_endpoint());
    client.send(net::buffer(std::string_view("bound")));
    std::thread::id mid{};
    std::optional<std::thread::id> end;
    auto guard = ctx.make_work_guard();
    std::thread runner([&] {
        auto [id] = ex::sync_wait(ex::starts_on(ctx.get_scheduler(), bound_echo_once(acc, mid)) | ex::then([&](auto id) {
                                      ctx.stop();
                                      return id;
                                  })).value();
        end = id;
    });
    ctx.run();
    runner.join();
    char buf[8];
    CHECK(client.receive(net::buffer(buf)) == 5);
    CHECK(mid == std::this_thread::get_id());
    CHECK(end && *end == std::this_thread::get_id());
}

TEST("task: awaiting an operation of another io_context resumes on the task's context") {
    net::io_context a, b;
    auto acc = make_listener(b);
    tcp::socket client(b);
    client.connect(acc.local_endpoint());
    tcp::socket server = acc.accept();
    std::thread::id resumed{};
    auto guard_a = a.make_work_guard();
    auto guard_b = b.make_work_guard();
    std::thread tb([&] { b.run(); });
    std::thread sender([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        client.send(net::buffer(std::string_view("x")));
    });
    std::thread ta([&] {
        auto [id] = ex::sync_wait(ex::starts_on(a.get_scheduler(), cross_context(server)) | ex::then([&](auto id) {
                                      a.stop();
                                      return id;
                                  })).value();
        resumed = id;
    });
    a.run();
    ta.join();
    sender.join();
    guard_b.reset();
    tb.join();
    CHECK(resumed == std::this_thread::get_id());
}

TEST("task: the completion scheduler seen with a bound_scheduler is the task's context") {
    net::io_context a, b;
    auto timer = net::async_resume_after(b.get_scheduler(), std::chrono::milliseconds(1));
    auto sa = net::bound_scheduler(a.get_scheduler());
    auto env = ex::prop{ex::get_start_scheduler, sa};
    auto attrs = ex::get_env(timer);
    // without an environment: the operation's own context
    CHECK(ex::get_completion_scheduler<ex::set_value_t>(attrs) == b.get_scheduler());
    // with one: where it really completes
    auto with_env = ex::get_completion_scheduler<ex::set_value_t>(attrs, env);
    CHECK(&with_env.context() == &a);
}
#endif

#if STDNET_HAS_AS_EXPECTED
TEST("as_expected: error_code moves to the value channel") {
    net::io_context ctx;
    tcp::endpoint dead;
    {
        auto acc = make_listener(ctx);
        dead = acc.local_endpoint();
    }
    tcp::socket s(ctx);
    auto r = test::run(ctx, net::async_connect(s, dead) | net::as_expected);
    REQUIRE(r.value.has_value());
    auto& e = std::get<0>(*r.value);
    CHECK(!e.has_value());
    CHECK(e.error() == std::errc::connection_refused);
}
#endif

// ---------------------------------------------------------------------------------------------
TEST("io_context: run() returns when there is no more work, and from several threads") {
    net::io_context ctx;
    std::atomic<int> count = 0;
    auto make = [&] { return ex::then(ctx.get_scheduler().schedule(), [&] { ++count; }); };
    using S = decltype(make());
    std::vector<std::unique_ptr<test::running<S>>> started;
    for (int i = 0; i < 1000; ++i) started.push_back(std::make_unique<test::running<S>>(make()));
    std::thread t1([&] { ctx.run(); });
    std::thread t2([&] { ctx.run(); });
    ctx.run();
    t1.join();
    t2.join();
    CHECK(count == 1000);
    CHECK(ctx.outstanding_work() == 0);
}

namespace {

// Keeps the ready queue permanently non-empty: every completion of a scheduled operation starts
// the next one, like a connection whose I/O always succeeds immediately.
struct spinner {
    struct rcv {
        using receiver_concept = ex::receiver_t;
        spinner* s;
        void set_value() && noexcept {
            if (!s->stop) s->kick();
        }
        void set_stopped() && noexcept {}
        void set_error(std::exception_ptr) && noexcept {}
        test::stop_env get_env() const noexcept { return {}; }
    };
    using sndr = decltype(std::declval<net::io_context&>().get_scheduler().schedule());
    using op_t = ex::connect_result_t<sndr, rcv>;
    struct holder {
        op_t op;
        template <class F>
        explicit holder(F f) : op(f()) {}
    };

    net::io_context& ctx;
    bool stop = false;
    int i = 0;
    std::optional<holder> slot[2]; // the operation being completed is never the one being replaced

    explicit spinner(net::io_context& c) : ctx(c) {}
    void kick() {
        i ^= 1;
        slot[i].emplace([&] { return ex::connect(ctx.get_scheduler().schedule(), rcv{this}); });
        ex::start(slot[i]->op);
    }
};

} // namespace

TEST("io_context: a never-empty ready queue does not starve operations waiting for readiness") {
    net::io_context ctx;
    auto acc = make_listener(ctx);
    tcp::socket client(ctx);
    client.connect(acc.local_endpoint());
    auto server = acc.accept();
    char buf[4];
    test::running rcv(net::async_receive(server, net::buffer(buf)));
    ctx.poll();
    CHECK(!rcv.out.done);

    spinner busy(ctx);
    busy.kick();
    std::thread writer([&] {
        std::this_thread::sleep_for(20ms);
        client.send(net::buffer(std::string_view("x")));
    });
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!rcv.out.done && std::chrono::steady_clock::now() < deadline) ctx.run_one();
    const bool completed = rcv.out.done;
    busy.stop = true;
    writer.join();
    while (ctx.outstanding_work() > 0 && ctx.run_one() != 0) {}
    CHECK(completed); // without periodic reactor polling the receive waits until the queue drains
}

TEST("io_context: a blocked write completes when the peer reads, over several rounds") {
    // The epoll backend arms EPOLLOUT only while a write is blocked and disarms it afterwards:
    // each round must arm again, and the reads in between must not be disturbed.
    net::io_context ctx;
    auto acc = make_listener(ctx);
    tcp::socket client(ctx);
    client.connect(acc.local_endpoint());
    auto server = acc.accept();
    const std::size_t total = 8u << 20; // far more than the socket buffers hold
    std::string out(total, 'a'), in(total, '\0');
    for (int round = 0; round < 3; ++round) {
        test::running wr(net::async_write(server, net::buffer(out)));
        ctx.poll();
#if !defined(_WIN32)
        CHECK(!wr.out.done); // nobody reads yet: the write is blocked (Winsock buffers 8 MB on loopback)
#endif
        test::running rd(net::async_read(client, net::buffer(in)));
        // bounded wait (run_for slices): a lost writability notification fails instead of hanging
        test::drive_until(ctx, [&] { return wr.out.done && rd.out.done; }, 10s);
        REQUIRE(wr.out.done);
        REQUIRE(rd.out.done);
        REQUIRE(wr.out.value.has_value());
        CHECK(std::get<0>(*wr.out.value) == total);
        CHECK(in == out);
        // an unrelated read that waits for readiness still works after the write was disarmed
        char one[1];
        test::running pending(net::async_receive(server, net::buffer(one)));
        ctx.poll();
        CHECK(!pending.out.done);
        client.send(net::buffer(std::string_view("x")));
        CHECK(test::drive_until(ctx, [&] { return pending.out.done; }));
    }
}

int main(int argc, char** argv) { return check::run_all(argc, argv); }
