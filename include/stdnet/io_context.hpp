// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [io_context] -- the event loop driving asynchronous network operations.
#pragma once

#include "config.hpp"
#include "detail/platform.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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
    std::size_t heap_index_ = 0; // position in the context's timer heap while registered_
    bool registered_ = false;
};

// Binary min-heap of pending timers, ordered by deadline then by start order (timers with the
// same deadline complete in the order they were started). Each operation records its position,
// so a cancellation removes it in O(log n). The deadline is copied into the entry: comparisons
// never touch the operation states. The vector keeps its capacity, so a steady-state timer
// does not allocate.
class timer_heap {
public:
    bool empty() const noexcept { return heap_.empty(); }
    clock_type::time_point earliest() const noexcept { return heap_.front().when; }
    timer_op_base* top() const noexcept { return heap_.front().op; }

    // Returns true when the timer became the earliest one.
    bool push(timer_op_base* op) {
        heap_.push_back({op->when_, seq_++, op});
        return sift_up(heap_.size() - 1) == 0;
    }

    void remove(timer_op_base* op) noexcept {
        std::size_t i = op->heap_index_;
        std::size_t last = heap_.size() - 1;
        if (i != last) {
            move(i, heap_[last]);
            heap_.pop_back();
            if (i > 0 && before(heap_[i], heap_[(i - 1) / 2]))
                sift_up(i);
            else
                sift_down(i);
        } else {
            heap_.pop_back();
        }
    }

private:
    struct entry {
        clock_type::time_point when;
        std::uint64_t seq;
        timer_op_base* op;
    };
    static bool before(const entry& a, const entry& b) noexcept {
        return a.when < b.when || (a.when == b.when && a.seq < b.seq);
    }
    void move(std::size_t i, const entry& e) noexcept {
        heap_[i] = e;
        e.op->heap_index_ = i;
    }
    std::size_t sift_up(std::size_t i) noexcept {
        entry e = heap_[i];
        while (i > 0) {
            std::size_t parent = (i - 1) / 2;
            if (!before(e, heap_[parent])) break;
            move(i, heap_[parent]);
            i = parent;
        }
        move(i, e);
        return i;
    }
    void sift_down(std::size_t i) noexcept {
        entry e = heap_[i];
        const std::size_t n = heap_.size();
        for (;;) {
            std::size_t child = 2 * i + 1;
            if (child >= n) break;
            if (child + 1 < n && before(heap_[child + 1], heap_[child])) ++child;
            if (!before(heap_[child], e)) break;
            move(i, heap_[child]);
            i = child;
        }
        move(i, e);
    }

    std::vector<entry> heap_;
    std::uint64_t seq_ = 0;
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
    void push_front(op_base* op) noexcept {
        op->next_ = head;
        head = op;
        if (!tail) tail = op;
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
// benchmarks can relate throughput to the work done per operation. When disabled there is no
// counter, no member in io_context and stats() returns zeros without locking.
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
    unsigned long long lock_waits = 0;      // mutex acquisitions that found it held
    unsigned long long lock_wait_ns = 0;    // time spent waiting for the mutex in those
    unsigned long long idle_in = 0;         // readable events for a descriptor with no pending receive
    unsigned long long idle_out = 0;        // writable events for a descriptor with no pending send
};

#if defined(STDNET_ENABLE_STATS)
#define STDNET_DETAIL_STAT(field) (++stats_.field)
#else
#define STDNET_DETAIL_STAT(field) ((void)0)
#endif

#if !defined(STDNET_REACTOR_INTERVAL)
#define STDNET_REACTOR_INTERVAL 256 // see docs/PERF.md ("Reactor starvation")
#endif

// [io_context.io_context]
class io_context {
public:
    class scheduler_type;

    // Snapshot of the instrumentation counters (all zero unless STDNET_ENABLE_STATS is defined).
    io_context_stats stats() const noexcept {
#if defined(STDNET_ENABLE_STATS)
        auto lk = lock();
        auto st = stats_;
        st.lock_waits = lock_waits_.load(std::memory_order_relaxed);
        st.lock_wait_ns = lock_wait_ns_.load(std::memory_order_relaxed);
        return st;
#else
        return {};
#endif
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
            auto lk = lock();
            stopped_ = true;
        }
        cv_.notify_all();
        wakeup_.signal();
    }
    bool stopped() const noexcept {
        auto lk = lock();
        return stopped_;
    }
    void restart() noexcept {
        auto lk = lock();
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
            auto lk = lock();
            STDNET_DETAIL_STAT(posts);
            ready_.push(op);
        }
        notify();
    }

    // Registers work that completes later through post_counted() (e.g. a lookup on another thread).
    void add_work() noexcept { outstanding_.fetch_add(1); }
    void remove_work() noexcept {
        if (outstanding_.fetch_sub(1) == 1) {
            { auto lk = lock(); }
            cv_.notify_all();
            wakeup_.signal();
        }
    }
    void post_counted(detail::op_base* op) noexcept {
        {
            auto lk = lock();
            ready_.push(op);
        }
        notify();
    }

    // Starts an I/O operation: attempts it immediately, or waits for readiness. The system call
    // is made without holding the mutex (see attempt()).
    void start_io(detail::io_op_base* op) noexcept {
        outstanding_.fetch_add(1);
        {
            auto lk = lock();
            if (op->cancel_requested_) {
                op->status_ = detail::op_base::status::cancelled;
                ready_.push(op);
            } else {
                STDNET_DETAIL_STAT(io_started);
                // The entry outlives the operation: it is created by the first operation on the
                // descriptor and destroyed by abort_handle(), so a steady-state operation neither
                // allocates nor touches the epoll interest set.
                auto& entry = fds_[op->fd_];
                const int d = static_cast<int>(op->dir_);
                if (entry.busy[d] || !entry.queues[d].empty()) {
                    // Behind an operation that is being attempted, or that waits for readiness:
                    // the one in front runs it when it is done.
                    op->registered_ = true;
                    entry.queues[d].push(op);
                    if (!entry.busy[d]) add_interest(op->fd_, entry, d == 1);
                } else {
                    entry.busy[d] = true;
                    attempt(lk, op->fd_, d, op, true);
                }
            }
        }
        notify();
    }

    void start_timer(detail::timer_op_base* op) noexcept {
        outstanding_.fetch_add(1);
        bool cancelled, earliest = false;
        {
            auto lk = lock();
            cancelled = op->cancel_requested_;
            if (cancelled) {
                op->status_ = detail::op_base::status::cancelled;
                ready_.push(op);
            } else {
                op->registered_ = true;
                earliest = timers_.push(op);
            }
        }
        // The operation may already be complete and destroyed here: only locals are used.
        if (cancelled) {
            notify();
        } else if (earliest && polling_.load(std::memory_order_acquire)) {
            // Nothing became ready, so no run() caller needs waking, except the one blocked in
            // the reactor when the new deadline is earlier than the one it waits for (its
            // timeout was computed from the earliest deadline under the mutex, and polling_ is
            // set before the mutex is released).
            wakeup_.signal();
        }
    }

    // Cancels a pending operation (stop request). Has no effect if it is already completing.
    void cancel(detail::io_op_base* op) noexcept {
        {
            auto lk = lock();
            if (!op->registered_) {
                if (op->status_ == detail::op_base::status::pending) op->cancel_requested_ = true;
                return;
            }
            auto it = fds_.find(op->fd_);
            if (it != fds_.end()) {
                auto& q = it->second.queues[static_cast<int>(op->dir_)];
                unlink(q, op);
            }
            op->registered_ = false;
            op->status_ = detail::op_base::status::cancelled;
            ready_.push(op);
        }
        notify();
    }

    void cancel(detail::timer_op_base* op) noexcept {
        {
            auto lk = lock();
            if (!op->registered_) {
                if (op->status_ == detail::op_base::status::pending) op->cancel_requested_ = true;
                return;
            }
            timers_.remove(op);
            op->registered_ = false;
            op->status_ = detail::op_base::status::cancelled;
            ready_.push(op);
        }
        notify();
    }

    // Aborts every operation pending on a handle; they complete with errc::operation_canceled,
    // and the reactor forgets the handle. Called by socket close()/cancel(). Must be called
    // before the handle is closed, so that a reused descriptor number never meets stale state.
    void abort_handle(detail::native_handle_type fd) noexcept {
        {
            auto lk = lock();
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
        bool registered = false; // in the epoll interest set (always false with the poll backend)
        bool out_armed = false;  // EPOLLOUT requested (a write is blocked)
        // A thread is attempting the system call of an operation of this direction, outside the
        // mutex; the operation is then in no queue. Operations started meanwhile queue behind it,
        // and readiness reported meanwhile sets retry[], so that the attempt is made again.
        bool busy[2] = {false, false};
        bool retry[2] = {false, false};
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

    // Registers the descriptor once, edge-triggered, for reading. Operations always attempt the
    // system call before waiting, which covers edges delivered while nothing was pending.
    // Writability is armed only while a write is blocked (a MOD that also reports the current
    // state) and disarmed once it is not: an always-armed EPOLLOUT edge fires after every send
    // (when the peer's ACK frees buffer space), one wake-up and one lookup per message for
    // nothing. The poll backend builds its set at each wait instead.
    void add_interest([[maybe_unused]] detail::native_handle_type fd, [[maybe_unused]] fd_entry& e,
                      [[maybe_unused]] bool want_write) noexcept {
#if STDNET_EPOLL
        if (e.registered && (!want_write || e.out_armed)) return;
        STDNET_DETAIL_STAT(interest_changes);
        ::epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET | (want_write ? EPOLLOUT : 0);
        ev.data.fd = fd;
        ::epoll_ctl(epfd_, e.registered ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, fd, &ev);
        e.registered = true;
        e.out_armed = want_write;
#endif
    }

    // Stops reporting writability once no write is pending any more.
    void disarm_write([[maybe_unused]] detail::native_handle_type fd, [[maybe_unused]] fd_entry& e) noexcept {
#if STDNET_EPOLL
        if (!e.out_armed) return;
        STDNET_DETAIL_STAT(interest_changes);
        ::epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fd;
        ::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
        e.out_armed = false;
#endif
    }

    void remove_interest([[maybe_unused]] detail::native_handle_type fd, fd_entry& e) noexcept {
#if STDNET_EPOLL
        if (e.registered) {
            STDNET_DETAIL_STAT(interest_changes);
            ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
        }
#endif
        e.registered = false;
        e.out_armed = false;
    }

    // Attempts operations of one direction of a descriptor, in order, until one has to wait for
    // readiness or none is left. Called with the mutex held and entry.busy[d] set by the caller,
    // which makes this thread the only one to touch the descriptor in that direction. `op` is the
    // operation to attempt first (in no queue), or null to take the head of the queue. The mutex
    // is released around every system call and held again on return: with all threads sharing one
    // mutex, system calls under it serialized every thread (#14).
    void attempt(std::unique_lock<std::mutex>& lk, detail::native_handle_type fd, int d, detail::io_op_base* op,
                 [[maybe_unused]] bool first) noexcept {
        for (;;) {
            auto it = fds_.find(fd);
            if (it == fds_.end()) return; // aborted: abort_handle() completed what was queued
            auto& e = it->second;
            if (!op) {
                op = static_cast<detail::io_op_base*>(e.queues[d].pop());
                if (!op) {
                    e.busy[d] = false;
                    if (d == 1) disarm_write(fd, e); // no write left waiting
                    return;
                }
                op->registered_ = false;
            }
            e.retry[d] = false;
            lk.unlock();
            const bool done = op->perform();
            lk = lock();
            if (!first) STDNET_DETAIL_STAT(io_retries);
            if (done) {
                if (first) STDNET_DETAIL_STAT(io_immediate);
                STDNET_DETAIL_STAT(posts);
                op->status_ = detail::op_base::status::done;
                ready_.push(op);
                op = nullptr;
                first = false;
                continue;
            }
            STDNET_DETAIL_STAT(io_would_block);
            it = fds_.find(fd);
            if (it == fds_.end()) { // aborted while the system call was made
                op->ec_ = std::make_error_code(std::errc::operation_canceled);
                op->status_ = detail::op_base::status::done;
                ready_.push(op);
                return;
            }
            auto& e2 = it->second;
            if (op->cancel_requested_) { // stop requested while the system call was made
                op->status_ = detail::op_base::status::cancelled;
                ready_.push(op);
                op = nullptr;
                first = false;
                continue;
            }
            if (e2.retry[d]) { // became ready while the system call was made
                first = false;
                continue;
            }
            op->registered_ = true;
            e2.queues[d].push_front(op);
            e2.busy[d] = false;
            add_interest(fd, e2, d == 1);
#if !STDNET_EPOLL
            if (polling_.load(std::memory_order_acquire)) wakeup_.signal(); // the poll set changed
#endif
            return;
        }
    }

    // Drives one operation of the ready queue, polling the reactor when it is empty.
    std::size_t run_one_impl(std::chrono::milliseconds max_wait) {
        auto lk = lock();
        for (;;) {
            if (stopped_) return 0;
            // Completions that finish immediately feed the ready queue faster than it drains, so
            // a busy context could otherwise never look at the reactor and the connections
            // waiting for readiness would starve (a tail-latency problem, not a throughput one).
            // Harvest readiness without blocking every `reactor_interval` completions.
            if (since_poll_ >= reactor_interval && !polling_.load(std::memory_order_acquire) && outstanding_.load() != 0)
                poll_reactor(lk, std::chrono::milliseconds(0));
            if (auto* op = ready_.pop()) {
                ++since_poll_;
                lk.unlock();
                op->complete();
                remove_work(); // wakes idle run() callers when the last work item completes
                return 1;
            }
            if (outstanding_.load() == 0) return 0;
            if (polling_.load()) {
                // Another thread owns the reactor: wait for it to hand over completions.
                if (max_wait.count() == 0) return 0;
                STDNET_DETAIL_STAT(cv_waits);
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
            auto first = timers_.earliest();
            int t = first <= now ? 0 : static_cast<int>(ceil<milliseconds>(first - now).count());
            if (timeout_ms < 0 || t < timeout_ms) timeout_ms = t;
        }

        since_poll_ = 0;
        auto& events = events_;
        events.clear();

#if STDNET_EPOLL
        polling_.store(true, std::memory_order_release);
        lk.unlock();
        ::epoll_event evs[128];
        int n = ::epoll_wait(epfd_, evs, 128, timeout_ms);
        lk = lock();
        polling_.store(false, std::memory_order_release);
        STDNET_DETAIL_STAT(reactor_waits);
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
        auto& pfds = pfds_;
        pfds.clear();
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
        lk = lock();
        polling_.store(false, std::memory_order_release);
        STDNET_DETAIL_STAT(reactor_waits);
        if (pfds[0].revents) wakeup_.drain();
        for (std::size_t i = 1; i < pfds.size(); ++i) {
            auto r = pfds[i].revents;
            if (!r) continue;
            stats_add_events(1);
            bool err = (r & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            events.push_back({pfds[i].fd, err || (r & POLLIN) != 0, err || (r & POLLOUT) != 0});
        }
#endif

        // Claim the descriptors that have operations waiting; the system calls are made outside
        // the mutex by attempt(). A direction somebody is already attempting is only flagged.
        thread_local std::vector<std::pair<detail::native_handle_type, int>> work;
        work.clear();
        for (auto& ev : events) {
            auto it = fds_.find(ev.fd);
            if (it == fds_.end()) continue;
            auto& e = it->second;
            const bool ready[2] = {ev.readable, ev.writable};
            for (int d = 0; d < 2; ++d) {
                if (!ready[d]) continue;
                if (e.busy[d]) {
                    e.retry[d] = true;
                } else if (e.queues[d].empty()) {
                    if (d == 0) {
                        STDNET_DETAIL_STAT(idle_in);
                    } else {
                        STDNET_DETAIL_STAT(idle_out);
                        disarm_write(ev.fd, e);
                    }
                } else {
                    e.busy[d] = true;
                    work.emplace_back(ev.fd, d);
                }
            }
        }
        for (std::size_t i = 0; i < work.size(); ++i) attempt(lk, work[i].first, work[i].second, nullptr, false);

        auto now = detail::clock_type::now();
        while (!timers_.empty() && timers_.earliest() <= now) {
            auto* op = timers_.top();
            timers_.remove(op);
            op->registered_ = false;
            op->status_ = detail::op_base::status::done;
            ready_.push(op);
        }
        cv_.notify_all();
    }

    // Acquires the mutex; the stats build also counts the acquisitions that had to wait, and how long.
    std::unique_lock<std::mutex> lock() const {
#if defined(STDNET_ENABLE_STATS)
        std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
        if (!lk.owns_lock()) {
            auto t0 = detail::clock_type::now();
            lk.lock();
            lock_waits_.fetch_add(1, std::memory_order_relaxed);
            lock_wait_ns_.fetch_add(static_cast<unsigned long long>(
                                        std::chrono::duration_cast<std::chrono::nanoseconds>(detail::clock_type::now() - t0).count()),
                                    std::memory_order_relaxed);
        }
        return lk;
#else
        return std::unique_lock<std::mutex>(mutex_);
#endif
    }

    void stats_add_events([[maybe_unused]] unsigned n) noexcept {
#if defined(STDNET_ENABLE_STATS)
        stats_.reactor_events += n;
#endif
    }

    struct ready_event {
        detail::native_handle_type fd;
        bool readable, writable;
    };

    static constexpr unsigned reactor_interval = STDNET_REACTOR_INTERVAL;
    unsigned since_poll_ = 0; // completions run since the reactor was last polled (under mutex_)
    mutable std::mutex mutex_;
#if defined(STDNET_ENABLE_STATS)
    io_context_stats stats_{};
    mutable std::atomic<unsigned long long> lock_waits_{0}, lock_wait_ns_{0}; // see lock()
#endif
    std::condition_variable cv_;
    detail::op_queue ready_;
    std::unordered_map<detail::native_handle_type, fd_entry> fds_;
    std::vector<ready_event> events_; // reactor scratch space; only the thread that polled uses it
#if !STDNET_EPOLL
    std::vector<::pollfd> pfds_;
#endif
    detail::timer_heap timers_;
    std::atomic<std::size_t> outstanding_{0};
    std::atomic<bool> polling_{false};
    bool stopped_ = false;
    detail::wakeup wakeup_;
#if STDNET_EPOLL
    int epfd_ = -1;
#endif
};

} // namespace stdnet

#undef STDNET_DETAIL_STAT
