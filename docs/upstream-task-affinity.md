# `task` re-schedules after awaiting a sender that already completes on the task's scheduler

Draft of a report for the stdexec issue tracker (github.com/NVIDIA/stdexec/issues). Not posted.

## Summary

Each `co_await` in `stdexec::task` of a sender whose completion scheduler is the task's own
scheduler is followed by one more `schedule()` on that scheduler. Advertising
`get_completion_scheduler<set_value_t>` equal to the task's scheduler does not avoid it: `affine`
(selected by the promise's `await_transform`) only skips the hop when the sender answers the
`get_completion_behavior` query with `asynchronous_affine` or `inline_completion`. That query is
stdexec-specific and static, so with the default type-erased `task_scheduler` no sender can make
the claim at all.

## Reproduction

[`upstream-task-affinity-repro.cpp`](upstream-task-affinity-repro.cpp), stdexec only, ~50 lines:
a run_loop scheduler that counts the operations started on it, and a task doing 10
`co_await ex::schedule(sch)`.

```
g++-15 -std=c++23 -I<stdexec>/include -pthread upstream-task-affinity-repro.cpp && ./a.out
operations started on the scheduler: 21 (expected 11: 1 to start, 1 per co_await)
```

stdexec 7df6de8 (2026-10-10), GCC 15.2. Same result (21) with `task<void, Ctx>` where
`Ctx::start_scheduler_type` is the concrete `counting_scheduler`.

## Impact

In stdnet (networking on an io_context reactor) the echo server posts 4.0 completions per message
with `task` and 2.0 with sender algorithms; `task` costs 1.3–1.5× the CPU. Answering
`get_completion_behavior` for a concrete start scheduler brings it to 2.0 posts and 0.96–1.01× the
CPU of senders. The hop also acts as a yield (HTTP p99 1.20 ms with it, 1.66 ms without), so its
removal is not purely a gain.

## Questions

1. Should `affine` also treat a sender as affine when its `get_completion_scheduler<Tag>` (queried
   with the receiver's environment) has the start scheduler's type and compares equal to it at
   run time? This uses only standard queries.
2. For type-erased `task_scheduler`, is the hop intended (conservative fallback), or could the
   comparison go through the erased backend?
