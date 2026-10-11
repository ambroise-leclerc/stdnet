// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Thin portability layer over BSD sockets / Winsock.
#pragma once

#include "../config.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <system_error>

#if STDNET_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace stdnet::detail {

#if STDNET_WINDOWS
using native_handle_type = SOCKET;
inline constexpr native_handle_type invalid_handle = INVALID_SOCKET;
using socklen_type = int;
using iovec_type = WSABUF;

struct winsock_init {
    winsock_init() {
        WSADATA d;
        ::WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~winsock_init() { ::WSACleanup(); }
};
inline void ensure_init() { static winsock_init init; }

inline std::error_code last_error() noexcept { return {::WSAGetLastError(), std::system_category()}; }
inline bool would_block(const std::error_code& ec) noexcept {
    return ec.value() == WSAEWOULDBLOCK || ec.value() == WSAEINPROGRESS;
}
inline bool interrupted(const std::error_code& ec) noexcept { return ec.value() == WSAEINTR; }
inline int close_handle(native_handle_type h) noexcept { return ::closesocket(h); }
inline std::error_code set_non_blocking(native_handle_type h, bool on) noexcept {
    u_long mode = on ? 1 : 0;
    if (::ioctlsocket(h, FIONBIO, &mode) != 0) return last_error();
    return {};
}
inline void set_iov(iovec_type& v, const void* p, std::size_t n) noexcept {
    v.buf = static_cast<char*>(const_cast<void*>(p));
    v.len = static_cast<ULONG>(n);
}
inline constexpr int send_flags = 0;
#else
using native_handle_type = int;
inline constexpr native_handle_type invalid_handle = -1;
using socklen_type = ::socklen_t;
using iovec_type = ::iovec;

inline void ensure_init() {}
inline std::error_code last_error() noexcept { return {errno, std::system_category()}; }
inline bool would_block(const std::error_code& ec) noexcept {
    return ec.value() == EAGAIN || ec.value() == EWOULDBLOCK || ec.value() == EINPROGRESS;
}
inline bool interrupted(const std::error_code& ec) noexcept { return ec.value() == EINTR; }
inline int close_handle(native_handle_type h) noexcept { return ::close(h); }
inline std::error_code set_non_blocking(native_handle_type h, bool on) noexcept {
    int flags = ::fcntl(h, F_GETFL, 0);
    if (flags < 0) return last_error();
    flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(h, F_SETFL, flags) < 0) return last_error();
    return {};
}
inline void set_iov(iovec_type& v, const void* p, std::size_t n) noexcept {
    v.iov_base = const_cast<void*>(p);
    v.iov_len = n;
}
#if defined(MSG_NOSIGNAL)
inline constexpr int send_flags = MSG_NOSIGNAL;
#else
inline constexpr int send_flags = 0;
#endif
#endif

// Creates a non-blocking, close-on-exec socket. SIGPIPE is suppressed where MSG_NOSIGNAL is unavailable.
inline native_handle_type open_socket(int family, int type, int protocol, std::error_code& ec) noexcept {
    ensure_init();
#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
    native_handle_type h = ::socket(family, type | SOCK_NONBLOCK | SOCK_CLOEXEC, protocol);
    if (h == invalid_handle) {
        ec = last_error();
        return h;
    }
#else
    native_handle_type h = ::socket(family, type, protocol);
    if (h == invalid_handle) {
        ec = last_error();
        return h;
    }
    if (auto e = set_non_blocking(h, true)) {
        close_handle(h);
        ec = e;
        return invalid_handle;
    }
#if !STDNET_WINDOWS
    ::fcntl(h, F_SETFD, FD_CLOEXEC);
#endif
#endif
#if defined(SO_NOSIGPIPE)
    int one = 1;
    ::setsockopt(h, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    ec.clear();
    return h;
}

// Retrieves and clears the pending error of a socket (used after a non-blocking connect).
inline std::error_code socket_error(native_handle_type h) noexcept {
    int err = 0;
    socklen_type len = sizeof err;
    if (::getsockopt(h, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0) return last_error();
    return {err, std::system_category()};
}

// Gathered send / scattered receive.
inline std::ptrdiff_t send_iov(native_handle_type h, iovec_type* iov, std::size_t n, const ::sockaddr* to,
                               socklen_type tolen, std::error_code& ec) noexcept {
#if STDNET_WINDOWS
    DWORD sent = 0;
    int r = to ? ::WSASendTo(h, iov, static_cast<DWORD>(n), &sent, 0, to, tolen, nullptr, nullptr)
               : ::WSASend(h, iov, static_cast<DWORD>(n), &sent, 0, nullptr, nullptr);
    if (r != 0) {
        ec = last_error();
        return -1;
    }
    ec.clear();
    return static_cast<std::ptrdiff_t>(sent);
#else
    ::msghdr msg{};
    msg.msg_name = const_cast<::sockaddr*>(to);
    msg.msg_namelen = to ? tolen : 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = static_cast<decltype(msg.msg_iovlen)>(n);
    for (;;) {
        auto r = ::sendmsg(h, &msg, send_flags);
        if (r < 0) {
            ec = last_error();
            if (interrupted(ec)) continue;
            return -1;
        }
        ec.clear();
        return r;
    }
#endif
}

inline std::ptrdiff_t recv_iov(native_handle_type h, iovec_type* iov, std::size_t n, ::sockaddr* from,
                               socklen_type* fromlen, int flags, std::error_code& ec) noexcept {
#if STDNET_WINDOWS
    DWORD received = 0;
    DWORD fl = static_cast<DWORD>(flags);
    int r = from ? ::WSARecvFrom(h, iov, static_cast<DWORD>(n), &received, &fl, from, fromlen, nullptr, nullptr)
                 : ::WSARecv(h, iov, static_cast<DWORD>(n), &received, &fl, nullptr, nullptr);
    if (r != 0) {
        ec = last_error();
        if (ec.value() == WSAEMSGSIZE) { // truncated datagram: report what was received
            ec.clear();
            return static_cast<std::ptrdiff_t>(received);
        }
        return -1;
    }
    ec.clear();
    return static_cast<std::ptrdiff_t>(received);
#else
    ::msghdr msg{};
    msg.msg_name = from;
    msg.msg_namelen = fromlen ? *fromlen : 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = static_cast<decltype(msg.msg_iovlen)>(n);
    for (;;) {
        auto r = ::recvmsg(h, &msg, flags);
        if (r < 0) {
            ec = last_error();
            if (interrupted(ec)) continue;
            return -1;
        }
        if (fromlen) *fromlen = msg.msg_namelen;
        ec.clear();
        return r;
    }
#endif
}

} // namespace stdnet::detail
