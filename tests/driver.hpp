// Test helpers: run a sender to completion on the calling thread by driving an io_context.
#pragma once

#include <network>

#include <exception>
#include <optional>
#include <system_error>
#include <tuple>
#include <variant>

namespace test {

namespace ex = stdnet::ex;

struct outcome_base {
    std::optional<std::error_code> error;
    std::exception_ptr exception;
    bool stopped = false;
    bool done = false;
};

template <class Values>
struct outcome : outcome_base {
    std::optional<Values> value;
};

struct stop_env {
    ex::inplace_stop_token token;
    ex::inplace_stop_token query(ex::get_stop_token_t) const noexcept { return token; }
};

template <class Values>
struct receiver {
    using receiver_concept = ex::receiver_t;
    outcome<Values>* out;
    ex::inplace_stop_token token;

    template <class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        out->value.emplace(std::forward<Vs>(vs)...);
        out->done = true;
    }
    void set_error(std::error_code ec) && noexcept {
        out->error = ec;
        out->done = true;
    }
    void set_error(std::exception_ptr e) && noexcept {
        out->exception = e;
        out->done = true;
    }
    void set_stopped() && noexcept {
        out->stopped = true;
        out->done = true;
    }
    stop_env get_env() const noexcept { return {token}; }
};

template <class Sndr>
using values_of = ex::value_types_of_t<Sndr, stop_env, std::tuple, std::type_identity_t>;

// Holds a started operation so that tests can interleave several of them.
template <class Sndr>
struct running {
    using values = values_of<Sndr>;
    outcome<values> out;
    ex::inplace_stop_source stop;
    decltype(ex::connect(std::declval<Sndr>(), std::declval<receiver<values>>())) op;

    explicit running(Sndr s) : op(ex::connect(std::move(s), receiver<values>{&out, stop.get_token()})) {
        ex::start(op);
    }
    running(running&&) = delete;
};

// Drives ctx until the predicate holds (or there is no more work). Returns true if the predicate holds.
template <class Pred>
bool drive_until(stdnet::io_context& ctx, Pred pred, std::chrono::milliseconds limit = std::chrono::seconds(10)) {
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        if (ctx.run_for(std::chrono::milliseconds(50)) == 0 && ctx.outstanding_work() == 0 && !pred()) return false;
    }
    return true;
}

// Starts the sender, drives ctx until it completes, and returns its outcome.
template <class Sndr>
auto run(stdnet::io_context& ctx, Sndr s) {
    auto r = std::make_unique<running<Sndr>>(std::move(s));
    drive_until(ctx, [&] { return r->out.done; });
    return std::move(r->out);
}

} // namespace test
