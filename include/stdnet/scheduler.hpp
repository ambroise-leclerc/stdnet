// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [io_context.scheduler] -- the std::execution scheduler of an io_context, and timers.
#pragma once

#include "config.hpp"
#include "io_context.hpp"

#include <chrono>
#include <optional>
#include <system_error>
#include <utility>

namespace stdnet {

namespace detail {

// Manages the stop callback of an operation state. The callback is only instantiated
// when the receiver's environment provides a stoppable token.
template <class Rcvr, class OnStop>
class stop_hook {
    using token_t = ex::stop_token_of_t<ex::env_of_t<Rcvr>>;
    static constexpr bool stoppable = !ex::unstoppable_token<token_t>;
    struct empty {};
    using callback_t = std::conditional_t<stoppable, std::optional<ex::stop_callback_for_t<token_t, OnStop>>, empty>;

public:
    // Returns true when a stop was already requested.
    bool stop_requested(const Rcvr& r) const noexcept {
        if constexpr (stoppable)
            return ex::get_stop_token(ex::get_env(r)).stop_requested();
        else
            return false;
    }
    void arm(const Rcvr& r, OnStop fn) noexcept {
        if constexpr (stoppable) cb_.emplace(ex::get_stop_token(ex::get_env(r)), std::move(fn));
    }
    void disarm() noexcept {
        if constexpr (stoppable) cb_.reset();
    }

private:
    [[no_unique_address]] callback_t cb_;
};

} // namespace detail

class io_context::scheduler_type {
public:
    explicit scheduler_type(io_context& ctx) noexcept : ctx_(&ctx) {}

    io_context& context() const noexcept { return *ctx_; }

    struct env {
        io_context* ctx;
        template <class Tag>
        scheduler_type query(ex::get_completion_scheduler_t<Tag>) const noexcept {
            return scheduler_type(*ctx);
        }
    };

    template <class Rcvr>
    struct schedule_op : detail::op_base {
        io_context* ctx;
        Rcvr rcvr;

        schedule_op(io_context* c, Rcvr r) : ctx(c), rcvr(std::move(r)) { complete_fn_ = &do_complete; }
        schedule_op(schedule_op&&) = delete;

        void start() & noexcept { ctx->post(this); }

        static void do_complete(detail::op_base* b) noexcept {
            auto* self = static_cast<schedule_op*>(b);
            if constexpr (!ex::unstoppable_token<ex::stop_token_of_t<ex::env_of_t<Rcvr>>>) {
                if (ex::get_stop_token(ex::get_env(self->rcvr)).stop_requested()) {
                    ex::set_stopped(std::move(self->rcvr));
                    return;
                }
            }
            ex::set_value(std::move(self->rcvr));
        }
    };

    struct schedule_sender {
        using sender_concept = ex::sender_t;
        // Infallible: set_stopped() is only possible when the receiver's stop token is stoppable
        // (C++26 dependent completion signatures, P3557).
        template <class Self, class... Env>
        static consteval auto get_completion_signatures() {
            if constexpr (sizeof...(Env) == 1 && (ex::unstoppable_token<ex::stop_token_of_t<Env>> && ...))
                return ex::completion_signatures<ex::set_value_t()>{};
            else
                return ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>{};
        }
        io_context* ctx;

        template <class Rcvr>
        schedule_op<Rcvr> connect(Rcvr r) const {
            return {ctx, std::move(r)};
        }
        env get_env() const noexcept { return {ctx}; }
    };

    schedule_sender schedule() const noexcept { return {ctx_}; }

    friend bool operator==(const scheduler_type&, const scheduler_type&) noexcept = default;

private:
    io_context* ctx_;
};

inline io_context::scheduler_type io_context::get_scheduler() noexcept { return scheduler_type(*this); }

// [timer] -- async_resume_at / async_resume_after complete on the io_context after the
// deadline, or with set_stopped() when a stop is requested first.
namespace detail {

template <class Rcvr>
struct timer_op : timer_op_base {
    io_context* ctx;
    Rcvr rcvr;
    struct on_stop {
        timer_op* self;
        void operator()() const noexcept { self->ctx->cancel(static_cast<timer_op_base*>(self)); }
    };
    stop_hook<Rcvr, on_stop> hook;

    std::optional<clock_type::duration> relative;

    timer_op(io_context* c, clock_type::time_point when, std::optional<clock_type::duration> rel, Rcvr r)
        : ctx(c), rcvr(std::move(r)), relative(rel) {
        when_ = when;
        complete_fn_ = &do_complete;
    }
    timer_op(timer_op&&) = delete;

    void start() & noexcept {
        if (relative) when_ = clock_type::now() + *relative; // relative deadlines run from start()
        if (hook.stop_requested(rcvr)) cancel_requested_ = true;
        else hook.arm(rcvr, on_stop{this});
        ctx->start_timer(this);
    }

    static void do_complete(op_base* b) noexcept {
        auto* self = static_cast<timer_op*>(b);
        self->hook.disarm();
        if (self->status_ == status::cancelled)
            ex::set_stopped(std::move(self->rcvr));
        else
            ex::set_value(std::move(self->rcvr));
    }
};

struct timer_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>;
    io_context* ctx;
    clock_type::time_point when;
    std::optional<clock_type::duration> relative;

    template <class Rcvr>
    timer_op<Rcvr> connect(Rcvr r) const {
        return {ctx, when, relative, std::move(r)};
    }
    io_context::scheduler_type::env get_env() const noexcept { return {ctx}; }
};

} // namespace detail

template <class Duration>
detail::timer_sender async_resume_at(io_context::scheduler_type sch,
                                     std::chrono::time_point<detail::clock_type, Duration> tp) noexcept {
    return {&sch.context(), std::chrono::time_point_cast<detail::clock_type::duration>(tp), std::nullopt};
}

template <class Rep, class Period>
detail::timer_sender async_resume_after(io_context::scheduler_type sch, std::chrono::duration<Rep, Period> d) noexcept {
    return {&sch.context(), {}, std::chrono::ceil<detail::clock_type::duration>(d)};
}

} // namespace stdnet
