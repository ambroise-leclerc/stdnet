// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [net.task] -- stdnet::task, a std::execution::task bound to an io_context.
//
// std::execution::task keeps its scheduler type-erased (task_scheduler), so after each co_await it
// cannot tell that the awaited stdnet sender already completed on its scheduler and schedules itself
// again: two completions are posted to the ready queue per I/O operation instead of one. A
// stdnet::task has an io_context::scheduler_type-like start scheduler (bound_scheduler) which the
// stdnet senders recognise; the coroutine then resumes directly from the completion.
//
// The task runs on the io_context it is started on (use ex::starts_on(ctx.get_scheduler(), task)).
// Awaiting an operation of another io_context is still correct: the operation is redirected to the
// task's context before the coroutine resumes.
#pragma once

#include "config.hpp"
#include "scheduler.hpp"

#if STDNET_USE_STDEXEC

namespace stdnet {

struct task_context {
    using start_scheduler_type = bound_scheduler;
};

template <class T = void>
using task = ex::task<T, task_context>;

} // namespace stdnet

#endif
