# `task`: an awaited sender that already completes on the task's scheduler still costs a reschedule

Draft of the report for the stdexec issue tracker and for the WG21 discussion of the C++26 `task`
(P4007 / P4126, as cited in stdnet issue #12). Not posted yet.

## Summary

`std::execution::task` (stdexec `__task.hpp`) wraps every awaited sender in `affine`, which appends
`schedule(start_scheduler)` unless the sender's `get_completion_behavior` already says
`asynchronous_affine` or `inline_completion`. That query is static (a type-level answer), and the
default start scheduler of `task` is the type-erased `task_scheduler`. A sender whose completion
scheduler is the very scheduler the task runs on cannot say so for a type-erased scheduler, so the
task always re-schedules: **every `co_await` of such a sender costs one extra trip through the
scheduler's queue**.

## Evidence

stdnet (an io_context with a reactor; senders advertise `get_completion_scheduler<set_value_t>`),
echo server, 64 connections, instrumented counters (exact): **4.0 completions posted per message with
`task`, 2.0 with sender algorithms** for identical I/O. CPU per message `task` / senders: 1.3–1.5×
(echo, UDP, HTTP; significant). With a `task` whose start scheduler type is a concrete wrapper the
senders recognise: 2.0 posts, ratio 0.96–1.01 (stdnet PR #20, `docs/PERF.md`).

## Reproduction

`affine_t::transform_sender` in `stdexec/__detail/__affine.hpp`, selected from
`__task::__promise::await_transform` (`__task.hpp`): `__completes_where_it_starts` is false for
`task_scheduler`'s schedule sender, and the child's behavior is `asynchronous`, so the
`__finally_(child, unstoppable(schedule(sch)))` branch is taken. Any scheduler-backed sender of a
single-threaded context shows it: count the number of enqueues per `co_await`.

## Why it matters

* The cost is paid by exactly the senders `task` is meant to await (I/O and timers of the
  scheduler the task runs on) and is invisible in the code.
* The hop also changes scheduling: it acts as a yield (HTTP p99 1.20 ms with the hop against 1.66 ms
  without, p50 and CPU worse with it). Whether this is wanted should be a property the user can see.

## Possible directions

1. **Runtime affinity check for type-erased schedulers.** `task_scheduler` already has an
   `operator==`; `affine` could ask the awaited sender for its completion scheduler at connect time
   and skip the hop when it compares equal to the start scheduler (needs the sender's completion
   scheduler convertible to / comparable with `task_scheduler`, e.g. through the erased backend).
2. **A concrete start scheduler** (what stdnet does): `task<T, Ctx>` with `Ctx::start_scheduler_type` a
   concrete scheduler, plus senders answering `asynchronous_affine` for it. Works today, but the
   claim is static, so the sender must itself guard against a different instance (stdnet redirects
   at completion when the contexts differ).
3. **Document it** in the `task` design: scheduler affinity is paid per `co_await` whenever the
   scheduler is type-erased, and name the customization (2) in the wording.

## Question

Is the extra hop intended for type-erased schedulers (a conservative fallback), or is a runtime
equality check in `affine` acceptable? stdnet can supply a test case (two-line echo coroutine plus a
queue-length counter).
