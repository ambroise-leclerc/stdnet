// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [internet.resolve] -- name resolution.
//
// The platform resolver (getaddrinfo) is blocking, so async_resolve runs it on a library-owned
// worker thread and completes on the io_context. A stop request completes the operation at once
// with set_stopped(); the underlying lookup cannot be interrupted and its result is discarded.
#pragma once

#include "config.hpp"
#include "error.hpp"
#include "internet.hpp"
#include "io_context.hpp"
#include "scheduler.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace stdnet {

namespace ip {

template <class Protocol>
using resolve_results = std::vector<typename Protocol::endpoint>;

enum class resolve_flags : int {
    none = 0,
    passive = AI_PASSIVE,           // results are suitable for bind() (empty host means "any")
    numeric_host = AI_NUMERICHOST,  // the host must be a numeric address
    numeric_service = AI_NUMERICSERV,
    address_configured = AI_ADDRCONFIG, // only families configured on the host
};
constexpr resolve_flags operator|(resolve_flags a, resolve_flags b) noexcept {
    return static_cast<resolve_flags>(static_cast<int>(a) | static_cast<int>(b));
}

namespace detail_resolve {

inline std::error_code map_gai_error(int rc) noexcept {
    switch (rc) {
    case EAI_NONAME: return make_error_code(error::host_not_found);
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
    case EAI_NODATA: return make_error_code(error::host_not_found);
#endif
    case EAI_SERVICE: return make_error_code(error::service_not_found);
    case EAI_AGAIN: return make_error_code(error::try_again);
    case EAI_MEMORY: return std::make_error_code(std::errc::not_enough_memory);
    case EAI_FAMILY: return std::make_error_code(std::errc::address_family_not_supported);
#if defined(EAI_SYSTEM)
    case EAI_SYSTEM: return {errno, std::system_category()};
#endif
    default: return make_error_code(error::host_not_found);
    }
}

template <class Protocol>
resolve_results<Protocol> do_resolve(const std::string& host, const std::string& service, resolve_flags flags,
                                     std::error_code& ec) {
    stdnet::detail::ensure_init();
    ::addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = Protocol::v4().type();
    hints.ai_protocol = Protocol::v4().protocol();
    hints.ai_flags = static_cast<int>(flags);
    if (host.empty()) hints.ai_flags |= AI_PASSIVE;
    ::addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host.empty() ? nullptr : host.c_str(), service.empty() ? nullptr : service.c_str(), &hints,
                           &res);
    resolve_results<Protocol> out;
    if (rc != 0) {
        ec = map_gai_error(rc);
        return out;
    }
    for (auto* p = res; p; p = p->ai_next) {
        typename Protocol::endpoint e;
        if (p->ai_addrlen <= e.capacity() && (p->ai_family == AF_INET || p->ai_family == AF_INET6)) {
            std::memcpy(e.data(), p->ai_addr, p->ai_addrlen);
            if (std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
        }
    }
    ::freeaddrinfo(res);
    ec = out.empty() ? make_error_code(error::host_not_found) : std::error_code{};
    return out;
}

// One process-wide worker thread running blocking lookups.
class worker {
public:
    static worker& instance() {
        static worker w;
        return w;
    }
    void submit(std::function<void()> job) {
        {
            std::lock_guard lk(m_);
            if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
    }
    ~worker() {
        {
            std::lock_guard lk(m_);
            done_ = true;
        }
        cv_.notify_one();
        if (thread_.joinable()) thread_.join();
    }

private:
    void loop() {
        std::unique_lock lk(m_);
        for (;;) {
            cv_.wait(lk, [&] { return done_ || !jobs_.empty(); });
            if (jobs_.empty()) return;
            auto job = std::move(jobs_.front());
            jobs_.pop_front();
            lk.unlock();
            job();
            lk.lock();
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    std::thread thread_;
    bool done_ = false;
};

template <class Protocol, class Rcvr>
struct resolve_op : stdnet::detail::op_base {
    struct shared_state {
        std::mutex m;
        resolve_op* op;
    };
    struct on_stop {
        resolve_op* self;
        void operator()() const noexcept {
            auto sh = self->sh;
            std::lock_guard lk(sh->m);
            if (!sh->op) return;
            sh->op = nullptr;
            self->status_ = status::cancelled;
            self->ctx->post_counted(self);
        }
    };

    io_context* ctx;
    Rcvr rcvr;
    std::string host, service;
    resolve_flags flags;
    resolve_results<Protocol> results;
    std::error_code ec;
    std::shared_ptr<shared_state> sh;
    stdnet::detail::stop_hook<Rcvr, on_stop> hook;

    resolve_op(io_context* c, std::string h, std::string s, resolve_flags f, Rcvr r)
        : ctx(c), rcvr(std::move(r)), host(std::move(h)), service(std::move(s)), flags(f) {
        complete_fn_ = &do_complete;
    }
    resolve_op(resolve_op&&) = delete;

    void start() & noexcept {
        if (hook.stop_requested(rcvr)) {
            status_ = status::cancelled;
            ctx->post(this);
            return;
        }
        try {
            sh = std::make_shared<shared_state>();
            sh->op = this;
            ctx->add_work();
            hook.arm(rcvr, on_stop{this});
            worker::instance().submit([sh = sh, host = host, service = service, flags = flags] {
                std::error_code e;
                auto res = do_resolve<Protocol>(host, service, flags, e);
                std::lock_guard lk(sh->m);
                if (auto* op = sh->op) {
                    sh->op = nullptr;
                    op->results = std::move(res);
                    op->ec = e;
                    op->status_ = status::done;
                    op->ctx->post_counted(op);
                }
            });
        } catch (...) {
            ec = std::make_error_code(std::errc::not_enough_memory);
            status_ = status::done;
            ctx->post(this);
        }
    }

    static void do_complete(stdnet::detail::op_base* b) noexcept {
        auto* self = static_cast<resolve_op*>(b);
        self->hook.disarm();
        if (self->status_ == status::cancelled)
            ex::set_stopped(std::move(self->rcvr));
        else if (self->ec)
            ex::set_error(std::move(self->rcvr), self->ec);
        else
            ex::set_value(std::move(self->rcvr), std::move(self->results));
    }
};

template <class Protocol>
struct resolve_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(resolve_results<Protocol>),
                                                            ex::set_error_t(std::error_code), ex::set_stopped_t()>;
    io_context* ctx;
    std::string host, service;
    resolve_flags flags;

    template <class Rcvr>
    resolve_op<Protocol, Rcvr> connect(Rcvr r) const {
        return {ctx, host, service, flags, std::move(r)};
    }
    io_context::scheduler_type::env get_env() const noexcept { return {ctx}; }
};

} // namespace detail_resolve

// Synchronous resolution.
template <class Protocol>
resolve_results<Protocol> resolve(std::string_view host, std::string_view service, resolve_flags flags,
                                  std::error_code& ec) {
    return detail_resolve::do_resolve<Protocol>(std::string(host), std::string(service), flags, ec);
}
template <class Protocol>
resolve_results<Protocol> resolve(std::string_view host, std::string_view service,
                                  resolve_flags flags = resolve_flags::none) {
    std::error_code ec;
    auto r = resolve<Protocol>(host, service, flags, ec);
    if (ec) throw std::system_error(ec, "resolve");
    return r;
}

// Asynchronous resolution. Completes on sch's io_context with set_value(resolve_results<Protocol>),
// never with an empty result.
template <class Protocol>
detail_resolve::resolve_sender<Protocol> async_resolve(io_context::scheduler_type sch, std::string host,
                                                       std::string service, resolve_flags flags = resolve_flags::none) {
    return {&sch.context(), std::move(host), std::move(service), flags};
}

} // namespace ip

using ip::async_resolve;

} // namespace stdnet
