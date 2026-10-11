// Size of the operation states of common stdnet sender chains, i.e. the memory a pending
// operation needs. These are static facts of the design, printed as JSON.
#define BENCH_NO_NEW_OVERRIDE
#include "../common/bench_common.hpp"

#include <network>

#include <array>
#include <cstdio>

namespace net = stdnet;
namespace ex = stdnet::ex;
using net::ip::tcp;

namespace {
struct rcv {
    using receiver_concept = ex::receiver_t;
    template <class... A>
    void set_value(A&&...) && noexcept {}
    template <class E>
    void set_error(E&&) && noexcept {}
    void set_stopped() && noexcept {}
};
template <class S>
constexpr std::size_t op_size() {
    return sizeof(ex::connect_result_t<S, rcv>);
}
} // namespace

int main() {
    net::io_context ctx;
    tcp::socket s(ctx);
    tcp::acceptor a(ctx);
    std::array<char, 16> buf;
    std::array<net::const_buffer, 2> two;

    using recv_t = decltype(net::async_receive(s, net::buffer(buf)));
    using write_t = decltype(net::async_write(s, net::buffer(std::as_const(buf))));
    using write2_t = decltype(net::async_write(s, two));
    using accept_t = decltype(net::async_accept(a));
    using connect_t = decltype(net::async_connect(s, tcp::endpoint()));
    using timer_t = decltype(net::async_resume_after(ctx.get_scheduler(), std::chrono::seconds(1)));
    auto echo_iter = [&] {
        return ex::let_value(net::async_receive(s, net::buffer(buf)),
                             [&](std::size_t n) { return net::async_write(s, net::buffer(buf.data(), n)); });
    };
    using echo_iter_t = decltype(echo_iter());

    std::printf("{\"async_receive\": %zu, \"async_write_1buf\": %zu, \"async_write_2bufs\": %zu, \"async_accept\": %zu, "
                "\"async_connect\": %zu, \"async_resume_after\": %zu, \"receive_then_write\": %zu, \"tcp_socket\": %zu, "
                "\"io_context\": %zu}\n",
                op_size<recv_t>(), op_size<write_t>(), op_size<write2_t>(), op_size<accept_t>(), op_size<connect_t>(),
                op_size<timer_t>(), op_size<echo_iter_t>(), sizeof(tcp::socket), sizeof(net::io_context));
}
