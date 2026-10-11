// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [io_context] -- the event loop driving asynchronous network operations.
#pragma once

#include "config.hpp"
#include "detail/platform.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#if STDNET_EPOLL
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif

namespace stdnet {

class io_context;

namespace detail {

// Every asynchronous operation owned by an io_context derives from op_base.
// complete() is always invoked by a thread calling io_context::run*(), never inline
// from start(): this bounds stack depth and gives a well-defined completion context.
struct op_base {
    op_base* next_ = nullptr;
    void (*complete_fn_)(op_base*) noexcept = nullptr;
    enum class status : unsigned char { pending, done, cancelled } status_ = status::pending;
    bool cancel_requested_ = false; // stop requested before the operation reached the context
    void complete() noexcept { complete_fn_(this); }
};

enum class io_dir : unsigned char { read = 0, write = 1 };

// A readiness-driven I/O operation. perform() attempts the non-blocking system call;
// it returns false when the call would block and the operation must wait for readiness.
struct io_op_base : op_base {
    native_handle_type fd_ = invalid_handle;
    io_dir dir_ = io_dir::read;
    bool (*perform_fn_)(io_op_base*) noexcept = nullptr;
    bool registered_ = false;
    io_op_base* fd_next_ = nullptr;
    std::error_code ec_;
    bool perform() noexcept { return perform_fn_(this); }
};

using clock_type = std::chrono::steady_clock;

struct timer_op_base : op_base {
    clock_type::time_point when_;
    std::multimap<clock_type::time_point, timer_op_base*>::iterator pos_;
    bool registered_ = false;
};

struct op_queue {
    op_base* head = nullptr;
    op_base* tail = nullptr;
    bool empty() const noexcept { return head == nullptr; }
    void push(op_base* op) noexcept {
        op->next_ = nullptr;
        if (tail)
            tail->next_ = op;
        else
            head = op;
        tail = op;
    }
    op_base* pop() noexcept {
        op_base* op = head;
        if (op) {
            head = op->next_;
            if (!head) tail = nullptr;
            op->next_ = nullptr;
        }
        return op;
    }
};

// Wakes a thread blocked in the reactor.
class wakeup {
public:
    wakeup() {
#if STDNET_EPOLL
        fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (fd_ < 0) throw std::system_error(last_error(), "eventfd");
        rfd_ = fd_;
#elif STDNET_WINDOWS
        // A loopback UDP socket that sends datagrams to itself.
        std::error_code ec;
        fd_ = open_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, ec);
        if (ec) throw std::system_error(ec, "wakeup socket");
        ::sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd_, reinterpret_cast<::sockaddr*>(&a), sizeof a);
        int len = sizeof a;
        ::getsockname(fd_, reinterpret_cast<::sockaddr*>(&a), &len);
        ::connect(fd_, reinterpret_cast<::sockaddr*>(&a), sizeof a);
        rfd_ = fd_;
#else
        int p[2];
        if (::pipe(p) != 0) throw std::system_error(last_error(), "pipe");
        for (int f : p) {
            set_non_blocking(f, true);
            ::fcntl(f, F_SETFD, FD_CLOEXEC);
        }
        rfd_ = p[0];
        fd_ = p[1];
#endif
    }
    wakeup(const wakeup&) = delete;
    wakeup& operator=(const wakeup&) = delete;
    ~wakeup() {
        close_handle(fd_);
        if (rfd_ != fd_) close_handle(rfd_);
    }

    native_handle_type read_handle() const noexcept { return rfd_; }

    void signal() noexcept {
#if STDNET_EPOLL
        std::uint64_t one = 1;
        [[maybe_unused]] auto r = ::write(fd_, &one, sizeof one);
#elif STDNET_WINDOWS
        char c = 0;
        ::send(fd_, &c, 1, 0);
#else
        char c = 0;
        [[maybe_unused]] auto r = ::write(fd_, &c, 1);
#endif
    }

    void drain() noexcept {
        char buf[64];
#if STDNET_WINDOWS
        while (::recv(rfd_, buf, sizeof buf, 0) > 0) {}
#else
        while (::read(rfd_, buf, sizeof buf) > 0) {}
#endif
    }

private:
    native_handle_type fd_ = invalid_handle;
    native_handle_type rfd_ = invalid_handle;
};

} // namespace detail

// Optional instrumentation (define STDNET_ENABLE_STATS): counts reactor activity so that
// benchmarks can relate throughput to the work done per operation. Zero cost when disabled.
struct io_context_stats {
    unsigned long long io_started = 0;      // I/O operations started
    unsigned long long io_immediate = 0;    // completed by the first attempt in start_io()
    unsigned long long io_retries = 0;      // perform() attempts after a readiness event
    unsigned long long io_would_block = 0;  // attempts that found the socket not ready
    unsigned long long reactor_waits = 0;   // epoll_wait / poll calls
    unsigned long long reactor_events = 0;  // readiness events returned
    unsigned long long interest_changes = 0; // epoll_ctl calls (ADD/MOD/DEL)
    unsigned long long posts = 0;           // completions handed to the ready queue
    unsigned long long cv_waits = 0;        // run() callers that blocked on the condition variable
};

#if defined(STDNET_ENABLE_STATS)
#define STDNET_STAT(field) (++stats_.field)
#else
#define STDNET_STAT(field) ((void)0)
#endif

// [io_context.io_context]
class io_context {
public:
    class scheduler_type;

    // Snapshot of the instrumentation counters (all zero unless STDNET_ENABLE_STATS is defined).
    io_context_stats stats() const noexcept {
        std::lock_guard lk(mutex_);
        return stats_;
    }

    io_context() {
        detail::ensure_init();
#if STDNET_EPOLL
        epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
        if (epfd_ < 0) throw std::system_error(detail::last_error(), "epoll_create1");
        ::epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = wakeup_.read_handle();
        ::epoll_ctl(epfd_, EPOLL_CTL_ADD, wakeup_.read_handle(), &ev);
#endif
    }
    io_context(const io_context&) = delete;
    io_context& operator=(const io_context&) = delete;
    ~io_context() {
#if STDNET_EPOLL
        detail::close_handle(epfd_);
#endif
    }

    scheduler_type get_scheduler() noexcept;

    // Runs completions until there is no outstanding work or stop() is called.
    // Returns the number of completions run.
    std::size_t run() {
        std::size_t n = 0;
        while (run_one_impl(std::chrono::milliseconds(-1))) ++n;
        return n;
    }
    std::size_t run_one() { return run_one_impl(std::chrono::milliseconds(-1)); }

    template <class Rep, class Period>
    std::size_t run_for(std::chrono::duration<Rep, Period> d) {
        return run_until(detail::clock_type::now() + d);
    }
    template <class Clock, class Duration>
    std::size_t run_until(std::chrono::time_point<Clock, Duration> deadline) {
        std::size_t n = 0;
        for (;;) {
            auto now = Clock::now();
            if (now >= deadline) break;
            auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
            if (!run_one_impl(left)) {
                if (stopped() || outstanding_.load() == 0) break;
                continue;
            }
            ++n;
        }
        return n;
    }

    // Runs ready completions without blocking.
    std::size_t poll() {
        std::size_t n = 0;
        while (run_one_impl(std::chrono::milliseconds(0))) ++n;
        return n;
    }

    void stop() noexcept {
        {
            std::lock_guard lk(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
        wakeup_.signal();
    }
    bool stopped() const noexcept {
        std::lock_guard lk(mutex_);
        return stopped_;
    }
    void restart() noexcept {
        std::lock_guard lk(mutex_);
        stopped_ = false;
    }

    // Keeps run() from returning for lack of work while it is alive (e.g. for a server thread).
    class work_guard {
    public:
        explicit work_guard(io_context& ctx) noexcept : ctx_(&ctx) { ctx_->add_work(); }
        work_guard(work_guard&& o) noexcept : ctx_(std::exchange(o.ctx_, nullptr)) {}
        work_guard(const work_guard&) = delete;
        work_guard& operator=(const work_guard&) = delete;
        ~work_guard() { reset(); }
        void reset() noexcept {
            if (auto* c = std::exchange(ctx_, nullptr)) c->remove_work();
        }
        bool owns_work() const noexcept { return ctx_ != nullptr; }

    private:
        io_context* ctx_;
    };
    work_guard make_work_guard() noexcept { return work_guard(*this); }

    // Number of operations started on this context and not yet completed.
    std::size_t outstanding_work() const noexcept { return outstanding_.load(); }

    // ---- Implementation interface used by I/O objects and senders (not part of the proposed API).

    void post(detail::op_base* op) noexcept {
        outstanding_.fetch_add(1);
        {
            std::lock_guard lk(mutex_);
            STDNET_STAT(posts);
            ready_.push(op);
        }
        notify();
    }

    // Registers work that completes later through post_counted() (e.g. a lookup on another thread).
    void add_work() noexcept { outstanding_.fetch_add(1); }
    void remove_work() noexcept {
        if (outstanding_.fetch_sub(1) == 1) {
            { std::lock_guard lk(mutex_); }
            cv_.notify_all();
            wakeup_.signal();
        }
    }
    void post_counted(detail::op_base* op) noexcept {
        {
            std::lock_guard lk(mutex_);
            ready_.push(op);
        }
        notify();
    }

    // Starts an I/O operation: attempts it immediately, or waits for readiness.
    void start_io(detail::io_op_base* op) noexcept {
        outstanding_.fetch_add(1);
        {
            std::lock_guard lk(mutex_);
            if (op->cancel_requested_) {
                op->status_ = detail::op_base::status::cancelled;
                ready_.push(op);
                goto done;
            }
            STDNET_STAT(io_started);
            auto& entry = fds_[op->fd_];
            auto& q = entry.queues[static_cast<int>(op->dir_)];
            if (q.head == nullptr && op->perform()) {
                STDNET_STAT(io_immediate);
                STDNET_STAT(posts);
                op->status_ = detail::op_base::status::done;
                ready_.push(op);
                if (entry.empty()) fds_.erase(op->fd_);
            } else {
                STDNET_STAT(io_would_block);
                op->registered_ = true;
                q.push(op);
                update_interest(op->fd_, entry);
            }
        }
    done:
        notify();
    }

    void start_timer(detail::timer_op_base* op) noexcept {
        outstanding_.fetch_add(1);
        {
            std::lock_guard lk(mutex_);
            if (op->cancel_requested_) {
                op->status_ = detail::op_base::status::cancelled;
                ready_.push(op);
            } else {
                op->pos_ = timers_.emplace(op->when_, op);
                op->registered_ = true;
            }
        }
        notify();
    }

    // Cancels a pending operation (stop request). Has no effect if it is already completing.
    void cancel(detail::io_op_base* op) noexcept {
        {
            std::lock_guard lk(mutex_);
            if (!op->registered_) {
                if (op->status_ == detail::op_base::status::pending) op->cancel_requested_ = true;
                return;
            }
            auto it = fds_.find(op->fd_);
            if (it != fds_.end()) {
                auto& q = it->second.queues[static_cast<int>(op->dir_)];
                unlink(q, op);
                if (it->second.empty()) {
                    remove_interest(op->fd_, it->second);
                    fds_.erase(it);
                } else {
                    update_interest(op->fd_, it->second);
                }
            }
            op->registered_ = false;
            op->status_ = detail::op_base::status::cancelled;
            ready_.push(op);
        }
        notify();
    }

    void cancel(detail::timer_op_base* op) noexcept {
        {
            std::lock_guard lk(mutex_);
            if (!op->registered_) {
                if (op->status_ == detail::op_base::status::pending) op->cancel_requested_ = true;
                return;
            }
            timers_.erase(op->pos_);
            op->registered_ = false;
            op->status_ = detail::op_base::status::cancelled;
            ready_.push(op);
        }
        notify();
    }

    // Aborts every operation pending on a handle; they complete with errc::operation_canceled.
    // Called by socket close()/cancel(). Must be called before the handle is closed.
    void abort_handle(detail::native_handle_type fd) noexcept {
        {
            std::lock_guard lk(mutex_);
            auto it = fds_.find(fd);
            if (it == fds_.end()) return;
            for (auto& q : it->second.queues) {
                while (auto* op = static_cast<detail::io_op_base*>(q.pop())) {
                    op->registered_ = false;
                    op->ec_ = std::make_error_code(std::errc::operation_canceled);
                    op->status_ = detail::op_base::status::done;
                    ready_.push(op);
                }
            }
            remove_interest(fd, it->second);
            fds_.erase(it);
        }
        notify();
    }

private:
    struct fd_entry {
        detail::op_queue queues[2];
        unsigned registered_events = 0; // bit 0: read, bit 1: write
        bool empty() const noexcept { return queues[0].empty() && queues[1].empty(); }
    };

    static void unlink(detail::op_queue& q, detail::op_base* op) noexcept {
        detail::op_base* prev = nullptr;
        for (auto* cur = q.head; cur; prev = cur, cur = cur->next_) {
            if (cur != op) continue;
            if (prev)
                prev->next_ = cur->next_;
            else
                q.head = cur->next_;
            if (q.tail == cur) q.tail = prev;
            cur->next_ = nullptr;
            return;
        }
    }

    void notify() noexcept {
        cv_.notify_one();
        if (polling_.load(std::memory_order_acquire)) wakeup_.signal();
    }

    void update_interest([[maybe_unused]] detail::native_handle_type fd, fd_entry& e) noexcept {
        unsigned want = (e.queues[0].empty() ? 0u : 1u) | (e.queues[1].empty() ? 0u : 2u);
        if (want == e.registered_events) return;
        STDNET_STAT(interest_changes);
#if STDNET_EPOLL
        ::epoll_event ev{};
        ev.events = ((want & 1) ? EPOLLIN : 0u) | ((want & 2) ? EPOLLOUT : 0u);
        ev.data.fd = fd;
        int opcode = e.registered_events == 0 ? EPOLL_CTL_ADD : (want == 0 ? EPOLL_CTL_DEL : EPOLL_CTL_MOD);
        ::epoll_ctl(epfd_, opcode, fd, &ev);
#endif
        e.registered_events = want;
    }

    void remove_interest([[maybe_unused]] detail::native_handle_type fd, fd_entry& e) noexcept {
#if STDNET_EPOLL
        if (e.registered_events) {
            STDNET_STAT(interest_changes);
            ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
        }
#endif
        e.registered_events = 0;
    }

    // Drives one operation of the ready queue, polling the reactor when it is empty.
    std::size_t run_one_impl(std::chrono::milliseconds max_wait) {
        std::unique_lock lk(mutex_);
        for (;;) {
            if (stopped_) return 0;
            if (auto* op = ready_.pop()) {
                lk.unlock();
                op->complete();
                remove_work(); // wakes idle run() callers when the last work item completes
                return 1;
            }
            if (outstanding_.load() == 0) return 0;
            if (polling_.load()) {
                // Another thread owns the reactor: wait for it to hand over completions.
                if (max_wait.count() == 0) return 0;
                STDNET_STAT(cv_waits);
                if (max_wait.count() < 0)
                    cv_.wait(lk);
                else if (cv_.wait_for(lk, max_wait) == std::cv_status::timeout)
                    return 0;
                continue;
            }
            poll_reactor(lk, max_wait);
            if (ready_.empty() && max_wait.count() >= 0) {
                // A bounded wait that produced nothing: let the caller check its deadline.
                return 0;
            }
        }
    }

    void poll_reactor(std::unique_lock<std::mutex>& lk, std::chrono::milliseconds max_wait) {
        using namespace std::chrono;
        int timeout_ms = max_wait.count() < 0 ? -1 : static_cast<int>(max_wait.count());
        if (!timers_.empty()) {
            auto now = detail::clock_type::now();
            auto first = timers_.begin()->first;
            int t = first <= now ? 0 : static_cast<int>(ceil<milliseconds>(first - now).count());
            if (timeout_ms < 0 || t < timeout_ms) timeout_ms = t;
        }

        struct ready_event {
            detail::native_handle_type fd;
            bool readable, writable;
        };
        std::vector<ready_event> events;

#if STDNET_EPOLL
        polling_.store(true, std::memory_order_release);
        lk.unlock();
        ::epoll_event evs[128];
        int n = ::epoll_wait(epfd_, evs, 128, timeout_ms);
        lk.lock();
        polling_.store(false, std::memory_order_release);
        STDNET_STAT(reactor_waits);
        if (n > 0) stats_add_events(static_cast<unsigned>(n));
        for (int i = 0; i < n; ++i) {
            if (evs[i].data.fd == wakeup_.read_handle()) {
                wakeup_.drain();
                continue;
            }
            bool err = (evs[i].events & (EPOLLERR | EPOLLHUP)) != 0;
            events.push_back({evs[i].data.fd, err || (evs[i].events & EPOLLIN) != 0,
                              err || (evs[i].events & EPOLLOUT) != 0});
        }
#else
        std::vector<::pollfd> pfds;
        pfds.reserve(fds_.size() + 1);
        pfds.push_back({wakeup_.read_handle(), POLLIN, 0});
        for (auto& [fd, e] : fds_) {
            short want = static_cast<short>((e.queues[0].empty() ? 0 : POLLIN) | (e.queues[1].empty() ? 0 : POLLOUT));
            if (want) pfds.push_back({fd, want, 0});
        }
        polling_.store(true, std::memory_order_release);
        lk.unlock();
#if STDNET_WINDOWS
        ::WSAPoll(pfds.data(), static_cast<ULONG>(pfds.size()), timeout_ms);
#else
        ::poll(pfds.data(), static_cast<::nfds_t>(pfds.size()), timeout_ms);
#endif
        lk.lock();
        polling_.store(false, std::memory_order_release);
        STDNET_STAT(reactor_waits);
        if (pfds[0].revents) wakeup_.drain();
        for (std::size_t i = 1; i < pfds.size(); ++i) {
            auto r = pfds[i].revents;
            if (!r) continue;
            stats_add_events(1);
            bool err = (r & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            events.push_back({pfds[i].fd, err || (r & POLLIN) != 0, err || (r & POLLOUT) != 0});
        }
#endif

        for (auto& ev : events) {
            auto it = fds_.find(ev.fd);
            if (it == fds_.end()) continue;
            auto& e = it->second;
            const bool ready[2] = {ev.readable, ev.writable};
            for (int d = 0; d < 2; ++d) {
                if (!ready[d]) continue;
                auto& q = e.queues[d];
                while (auto* op = static_cast<detail::io_op_base*>(q.head)) {
                    STDNET_STAT(io_retries);
                    if (!op->perform()) {
                        STDNET_STAT(io_would_block);
                        break;
                    }
                    STDNET_STAT(posts);
                    q.pop();
                    op->registered_ = false;
                    op->status_ = detail::op_base::status::done;
                    ready_.push(op);
                }
            }
            if (e.empty()) {
                remove_interest(ev.fd, e);
                fds_.erase(it);
            } else {
                update_interest(ev.fd, e);
            }
        }

        auto now = detail::clock_type::now();
        while (!timers_.empty() && timers_.begin()->first <= now) {
            auto* op = timers_.begin()->second;
            timers_.erase(timers_.begin());
            op->registered_ = false;
            op->status_ = detail::op_base::status::done;
            ready_.push(op);
        }
        cv_.notify_all();
    }

    void stats_add_events([[maybe_unused]] unsigned n) noexcept {
#if defined(STDNET_ENABLE_STATS)
        stats_.reactor_events += n;
#endif
    }

    mutable std::mutex mutex_;
    io_context_stats stats_{};
    std::condition_variable cv_;
    detail::op_queue ready_;
    std::unordered_map<detail::native_handle_type, fd_entry> fds_;
    std::multimap<detail::clock_type::time_point, detail::timer_op_base*> timers_;
    std::atomic<std::size_t> outstanding_{0};
    std::atomic<bool> polling_{false};
    bool stopped_ = false;
    detail::wakeup wakeup_;
#if STDNET_EPOLL
    int epfd_ = -1;
#endif
};

} // namespace stdnet
