// stdexec::task: each co_await of a sender that completes on the task's own scheduler
// costs a second schedule on that scheduler.
#include <stdexec/execution.hpp>
#include <cstdio>
#include <thread>

namespace ex = stdexec;

// A run_loop scheduler that counts the operations started on it.
struct counting_scheduler {
    using scheduler_concept = ex::scheduler_t;
    ex::run_loop::scheduler inner;
    int* starts;

    struct sender;
    sender schedule() const noexcept;
    bool operator==(const counting_scheduler&) const = default;
};

struct counting_scheduler::sender {
    using sender_concept = ex::sender_t;
    template <class Self, class... Env>
    static consteval auto get_completion_signatures() {
        return ex::get_completion_signatures<ex::schedule_result_t<ex::run_loop::scheduler>, Env...>();
    }
    counting_scheduler sch;

    auto connect(auto rcvr) const {
        ++*sch.starts;
        return ex::connect(ex::schedule(sch.inner), std::move(rcvr));
    }
    struct env {
        counting_scheduler sch;
        counting_scheduler query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept { return sch; }
    };
    env get_env() const noexcept { return {sch}; }
};

auto counting_scheduler::schedule() const noexcept -> sender { return {*this}; }

ex::task<void> coro(counting_scheduler sch) {
    for (int i = 0; i < 10; ++i)
        co_await ex::schedule(sch); // completes on sch, the scheduler the task runs on
}

int main() {
    ex::run_loop loop;
    int starts = 0;
    counting_scheduler sch{loop.get_scheduler(), &starts};
    std::thread t([&] { loop.run(); });
    ex::sync_wait(ex::starts_on(sch, coro(sch)));
    loop.finish();
    t.join();
    std::printf("operations started on the scheduler: %d (expected 11: 1 to start, 1 per co_await)\n", starts);
}
