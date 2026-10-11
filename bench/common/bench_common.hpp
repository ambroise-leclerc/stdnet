// Shared helpers for the benchmark servers: argument parsing, allocation counting,
// file-descriptor limit, and the stop/report protocol used by bench/run.py.
//
// Protocol: a server prints "READY <port>" on stdout once it listens, then runs until it
// receives SIGINT/SIGTERM; on exit it prints one JSON line {"allocs": N, "alloc_bytes": B} with
// the heap activity counted since "READY".
//
// Measurement window: SIGUSR1 and SIGUSR2 make the server print a snapshot line
// {"snapshot": "start"|"end", "allocs": N, "alloc_bytes": B, ...} with the cumulative counters
// (plus the reactor counters of instrumented builds). bench/run.py sends them when the load
// generator enters and leaves its measurement window, so that every per-operation ratio uses
// the same window for its numerator and its denominator.
#pragma once

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unistd.h>
#include <execinfo.h>

namespace bench {

// ---- allocation counting (global operator new replacement) -----------------------------

inline std::atomic<unsigned long long> g_allocs{0};
inline std::atomic<unsigned long long> g_alloc_bytes{0};
inline std::atomic<bool> g_counting{false};

// BENCH_TRACE_ALLOCS=N prints the call stack of the first N counted allocations to stderr
// (build with -rdynamic or symbolize the addresses with addr2line).
// The budget is initialised by a function-local static (thread-safe, no allocation).
inline std::atomic<long>& trace_left() {
    static std::atomic<long> left{[] {
        const char* e = std::getenv("BENCH_TRACE_ALLOCS");
        return e ? std::atol(e) : 0L;
    }()};
    return left;
}
inline thread_local bool g_in_trace = false;

inline void trace_alloc(std::size_t n) {
    auto& left = trace_left();
    if (g_in_trace || left.load(std::memory_order_relaxed) <= 0 || left.fetch_sub(1) <= 0) return;
    g_in_trace = true;
    void* frames[24];
    int k = backtrace(frames, 24);
    char head[64];
    int len = std::snprintf(head, sizeof head, "--- alloc %zu bytes\n", n);
    [[maybe_unused]] auto w = ::write(2, head, static_cast<std::size_t>(len));
    backtrace_symbols_fd(frames, k, 2);
    g_in_trace = false;
}

inline void* counted_alloc(std::size_t n) {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_allocs.fetch_add(1, std::memory_order_relaxed);
        g_alloc_bytes.fetch_add(n, std::memory_order_relaxed);
        trace_alloc(n);
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}

inline void start_counting() {
    g_allocs = 0;
    g_alloc_bytes = 0;
    g_counting = true;
}

inline void print_report() {
    g_counting = false;
    std::printf("{\"allocs\": %llu, \"alloc_bytes\": %llu}\n", g_allocs.load(), g_alloc_bytes.load());
    std::fflush(stdout);
}

// ---- options ------------------------------------------------------------------------------

struct options {
    unsigned short port = 0;
    int threads = 1;
    std::string mode = "echo"; // echo | udp | http
};

inline options parse(int argc, char** argv) {
    options o;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string_view k = argv[i];
        if (k == "--port") o.port = static_cast<unsigned short>(std::atoi(argv[i + 1]));
        else if (k == "--threads") o.threads = std::atoi(argv[i + 1]);
        else if (k == "--mode") o.mode = argv[i + 1];
    }
    return o;
}

inline void raise_fd_limit() {
    rlimit r{};
    if (getrlimit(RLIMIT_NOFILE, &r) == 0) {
        r.rlim_cur = r.rlim_max;
        setrlimit(RLIMIT_NOFILE, &r);
    }
}

inline void announce(unsigned short port) {
    std::printf("READY %u\n", static_cast<unsigned>(port));
    std::fflush(stdout);
    start_counting();
}

// Blocks the control signals in the calling thread (call before starting worker threads, which
// inherit the mask), so that only wait_stop_signal() receives them.
inline sigset_t block_stop_signals() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
    return set;
}

// Serves snapshot requests (SIGUSR1: window start, SIGUSR2: window end) until SIGINT/SIGTERM.
// `extra` may return additional JSON members (without braces) to include in each snapshot.
inline void wait_stop_signal(const sigset_t& set, const std::function<std::string()>& extra = {}) {
    for (;;) {
        int sig = 0;
        sigwait(&set, &sig);
        if (sig == SIGINT || sig == SIGTERM) return;
        std::string more = extra ? extra() : std::string();
        std::printf("{\"snapshot\": \"%s\", \"allocs\": %llu, \"alloc_bytes\": %llu%s%s}\n",
                    sig == SIGUSR1 ? "start" : "end", g_allocs.load(), g_alloc_bytes.load(), more.empty() ? "" : ", ",
                    more.c_str());
        std::fflush(stdout);
    }
}

// Canned HTTP response used by the http mode of every variant (same bytes everywhere).
inline constexpr std::string_view http_response =
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 13\r\nConnection: keep-alive\r\n\r\n"
    "Hello, world!";

// Returns the length of a complete request head in buf[0..n), or 0 if incomplete.
inline std::size_t request_end(const char* buf, std::size_t n) {
    for (std::size_t i = 3; i < n; ++i)
        if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n') return i + 1;
    return 0;
}

} // namespace bench

// The replacement must be defined in exactly one translation unit: each server is one TU.
#ifndef BENCH_NO_NEW_OVERRIDE
void* operator new(std::size_t n) { return bench::counted_alloc(n); }
void* operator new[](std::size_t n) { return bench::counted_alloc(n); }
void* operator new(std::size_t n, std::align_val_t a) {
    if (bench::g_counting.load(std::memory_order_relaxed)) {
        bench::g_allocs.fetch_add(1, std::memory_order_relaxed);
        bench::g_alloc_bytes.fetch_add(n, std::memory_order_relaxed);
        bench::trace_alloc(n);
    }
    if (void* p = std::aligned_alloc(static_cast<std::size_t>(a), (n + static_cast<std::size_t>(a) - 1) & ~(static_cast<std::size_t>(a) - 1))) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
#endif
