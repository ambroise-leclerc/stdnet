// Timer scenario: N concurrent timers with deadlines spread over 50 ms.
// Measures (1) the cost of starting them, (2) time to drain them all, and (3) the cost of
// cancelling N pending timers. stdnet (stop tokens) vs Asio (steady_timer::cancel).
#define BENCH_NO_NEW_OVERRIDE
#include "../common/bench_common.hpp"

#include <network>
#include <asio.hpp>

#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

namespace net = stdnet;
namespace ex = stdnet::ex;
using clk = std::chrono::steady_clock;

namespace {

struct counter_rcv {
    using receiver_concept = ex::receiver_t;
    int* fired;
    int* stopped;
    ex::inplace_stop_token token;
    void set_value() && noexcept { ++*fired; }
    void set_stopped() && noexcept { ++*stopped; }
    struct env {
        ex::inplace_stop_token t;
        ex::inplace_stop_token query(ex::get_stop_token_t) const noexcept { return t; }
    };
    env get_env() const noexcept { return {token}; }
};

using timer_sndr = decltype(net::async_resume_after(std::declval<net::io_context::scheduler_type>(), std::chrono::milliseconds(1)));
using timer_op_t = ex::connect_result_t<timer_sndr, counter_rcv>;
// Operation states are not movable: construct them in place from a factory.
struct timer_op {
    timer_op_t op;
    template <class F>
    explicit timer_op(F f) : op(f()) {}
};

double ms_since(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

struct result {
    double start_ms, drain_ms, cancel_ms;
};

result bench_stdnet(int n, const std::vector<int>& delays_us) {
    result r{};
    {
        net::io_context ctx;
        int fired = 0, stopped = 0;
        ex::inplace_stop_source src;
        std::vector<std::unique_ptr<timer_op>> ops;
        ops.reserve(static_cast<std::size_t>(n));
        auto t0 = clk::now();
        for (int i = 0; i < n; ++i) {
            ops.push_back(std::make_unique<timer_op>([&] {
                return ex::connect(
                    net::async_resume_after(ctx.get_scheduler(), std::chrono::microseconds(delays_us[static_cast<std::size_t>(i)])),
                    counter_rcv{&fired, &stopped, src.get_token()});
            }));
            ex::start(ops.back()->op);
        }
        r.start_ms = ms_since(t0);
        auto t1 = clk::now();
        ctx.run();
        r.drain_ms = ms_since(t1);
        if (fired != n) std::fprintf(stderr, "stdnet: fired %d/%d\n", fired, n);
    }
    {
        net::io_context ctx;
        int fired = 0, stopped = 0;
        ex::inplace_stop_source src;
        std::vector<std::unique_ptr<timer_op>> ops;
        ops.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            ops.push_back(std::make_unique<timer_op>([&] {
                return ex::connect(net::async_resume_after(ctx.get_scheduler(), std::chrono::seconds(60)),
                                   counter_rcv{&fired, &stopped, src.get_token()});
            }));
            ex::start(ops.back()->op);
        }
        ctx.poll();
        auto t0 = clk::now();
        src.request_stop();
        ctx.run();
        r.cancel_ms = ms_since(t0);
        if (stopped != n) std::fprintf(stderr, "stdnet: stopped %d/%d\n", stopped, n);
    }
    return r;
}

result bench_asio(int n, const std::vector<int>& delays_us) {
    result r{};
    {
        asio::io_context ctx(1);
        int fired = 0;
        std::vector<std::unique_ptr<asio::steady_timer>> timers;
        timers.reserve(static_cast<std::size_t>(n));
        auto t0 = clk::now();
        for (int i = 0; i < n; ++i) {
            timers.push_back(std::make_unique<asio::steady_timer>(ctx, std::chrono::microseconds(delays_us[static_cast<std::size_t>(i)])));
            timers.back()->async_wait([&](std::error_code ec) {
                if (!ec) ++fired;
            });
        }
        r.start_ms = ms_since(t0);
        auto t1 = clk::now();
        ctx.run();
        r.drain_ms = ms_since(t1);
        if (fired != n) std::fprintf(stderr, "asio: fired %d/%d\n", fired, n);
    }
    {
        asio::io_context ctx(1);
        int cancelled = 0;
        std::vector<std::unique_ptr<asio::steady_timer>> timers;
        timers.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            timers.push_back(std::make_unique<asio::steady_timer>(ctx, std::chrono::seconds(60)));
            timers.back()->async_wait([&](std::error_code ec) {
                if (ec) ++cancelled;
            });
        }
        ctx.poll();
        auto t0 = clk::now();
        for (auto& t : timers) t->cancel();
        ctx.run();
        r.cancel_ms = ms_since(t0);
        if (cancelled != n) std::fprintf(stderr, "asio: cancelled %d/%d\n", cancelled, n);
    }
    return r;
}

} // namespace

int main(int argc, char** argv) {
    int n = argc > 1 ? std::atoi(argv[1]) : 100000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> d(0, 50000);
    std::vector<int> delays(static_cast<std::size_t>(n));
    for (auto& x : delays) x = d(rng);
    auto s = bench_stdnet(n, delays);
    auto a = bench_asio(n, delays);
    std::printf("{\"timers\": %d, \"stdnet\": {\"start_ms\": %.2f, \"drain_ms\": %.2f, \"cancel_ms\": %.2f}, "
                "\"asio\": {\"start_ms\": %.2f, \"drain_ms\": %.2f, \"cancel_ms\": %.2f}}\n",
                n, s.start_ms, s.drain_ms, s.cancel_ms, a.start_ms, a.drain_ms, a.cancel_ms);
}
