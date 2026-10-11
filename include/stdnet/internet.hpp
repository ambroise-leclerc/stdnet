// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [internet] -- IP addresses, endpoints and the tcp/udp protocols.
#pragma once

#include "detail/platform.hpp"
#include "error.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <typeinfo>

namespace stdnet::ip {

using port_type = std::uint_least16_t;
using scope_id_type = std::uint_least32_t;

class address_v4 {
public:
    using uint_type = std::uint_least32_t;
    using bytes_type = std::array<unsigned char, 4>;

    constexpr address_v4() noexcept = default;
    constexpr explicit address_v4(const bytes_type& b) noexcept : bytes_(b) {}
    constexpr explicit address_v4(uint_type v) noexcept
        : bytes_{static_cast<unsigned char>(v >> 24), static_cast<unsigned char>(v >> 16),
                 static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v)} {}

    constexpr bytes_type to_bytes() const noexcept { return bytes_; }
    constexpr uint_type to_uint() const noexcept {
        return (uint_type(bytes_[0]) << 24) | (uint_type(bytes_[1]) << 16) | (uint_type(bytes_[2]) << 8) | bytes_[3];
    }
    constexpr bool is_unspecified() const noexcept { return to_uint() == 0; }
    constexpr bool is_loopback() const noexcept { return bytes_[0] == 127; }
    constexpr bool is_multicast() const noexcept { return (bytes_[0] & 0xF0) == 0xE0; }

    std::string to_string() const {
        return std::format("{}.{}.{}.{}", bytes_[0], bytes_[1], bytes_[2], bytes_[3]);
    }

    static constexpr address_v4 any() noexcept { return address_v4(); }
    static constexpr address_v4 loopback() noexcept { return address_v4(0x7F000001u); }
    static constexpr address_v4 broadcast() noexcept { return address_v4(0xFFFFFFFFu); }

    friend constexpr bool operator==(const address_v4&, const address_v4&) noexcept = default;
    friend constexpr auto operator<=>(const address_v4&, const address_v4&) noexcept = default;

private:
    bytes_type bytes_{};
};

class address_v6 {
public:
    using bytes_type = std::array<unsigned char, 16>;

    constexpr address_v6() noexcept = default;
    constexpr explicit address_v6(const bytes_type& b, scope_id_type scope = 0) noexcept : bytes_(b), scope_(scope) {}

    constexpr bytes_type to_bytes() const noexcept { return bytes_; }
    constexpr scope_id_type scope_id() const noexcept { return scope_; }
    constexpr void scope_id(scope_id_type id) noexcept { scope_ = id; }

    constexpr bool is_unspecified() const noexcept {
        for (auto b : bytes_)
            if (b) return false;
        return true;
    }
    constexpr bool is_loopback() const noexcept {
        for (std::size_t i = 0; i < 15; ++i)
            if (bytes_[i]) return false;
        return bytes_[15] == 1;
    }
    constexpr bool is_multicast() const noexcept { return bytes_[0] == 0xFF; }
    constexpr bool is_v4_mapped() const noexcept {
        for (std::size_t i = 0; i < 10; ++i)
            if (bytes_[i]) return false;
        return bytes_[10] == 0xFF && bytes_[11] == 0xFF;
    }

    std::string to_string() const {
        char buf[INET6_ADDRSTRLEN] = {};
        ::in6_addr a{};
        std::memcpy(&a, bytes_.data(), 16);
        ::inet_ntop(AF_INET6, &a, buf, sizeof buf);
        std::string s = buf;
        if (scope_) s += "%" + std::to_string(scope_);
        return s;
    }

    static constexpr address_v6 any() noexcept { return address_v6(); }
    static constexpr address_v6 loopback() noexcept {
        bytes_type b{};
        b[15] = 1;
        return address_v6(b);
    }

    friend constexpr bool operator==(const address_v6&, const address_v6&) noexcept = default;
    friend constexpr auto operator<=>(const address_v6&, const address_v6&) noexcept = default;

private:
    bytes_type bytes_{};
    scope_id_type scope_ = 0;
};

class address {
public:
    constexpr address() noexcept = default;
    constexpr address(const address_v4& a) noexcept : v6_(false), v4addr_(a) {}
    constexpr address(const address_v6& a) noexcept : v6_(true), v6addr_(a) {}

    constexpr bool is_v4() const noexcept { return !v6_; }
    constexpr bool is_v6() const noexcept { return v6_; }

    address_v4 to_v4() const {
        if (v6_) throw std::bad_cast();
        return v4addr_;
    }
    address_v6 to_v6() const {
        if (!v6_) throw std::bad_cast();
        return v6addr_;
    }
    constexpr bool is_unspecified() const noexcept { return v6_ ? v6addr_.is_unspecified() : v4addr_.is_unspecified(); }
    constexpr bool is_loopback() const noexcept { return v6_ ? v6addr_.is_loopback() : v4addr_.is_loopback(); }
    constexpr bool is_multicast() const noexcept { return v6_ ? v6addr_.is_multicast() : v4addr_.is_multicast(); }
    std::string to_string() const { return v6_ ? v6addr_.to_string() : v4addr_.to_string(); }

    friend constexpr bool operator==(const address& a, const address& b) noexcept {
        if (a.v6_ != b.v6_) return false;
        return a.v6_ ? a.v6addr_ == b.v6addr_ : a.v4addr_ == b.v4addr_;
    }
    friend constexpr std::strong_ordering operator<=>(const address& a, const address& b) noexcept {
        if (a.v6_ != b.v6_) return a.v6_ <=> b.v6_;
        if (a.v6_) {
            if (a.v6addr_ < b.v6addr_) return std::strong_ordering::less;
            if (b.v6addr_ < a.v6addr_) return std::strong_ordering::greater;
            return std::strong_ordering::equal;
        }
        return a.v4addr_.to_uint() <=> b.v4addr_.to_uint();
    }

private:
    bool v6_ = false;
    address_v4 v4addr_{};
    address_v6 v6addr_{};
};

inline address_v4 make_address_v4(std::string_view s, std::error_code& ec) noexcept {
    detail::ensure_init();
    char buf[INET_ADDRSTRLEN] = {};
    if (s.size() >= sizeof buf) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return {};
    }
    std::memcpy(buf, s.data(), s.size());
    ::in_addr a{};
    if (::inet_pton(AF_INET, buf, &a) != 1) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return {};
    }
    address_v4::bytes_type b;
    std::memcpy(b.data(), &a, 4);
    ec.clear();
    return address_v4(b);
}

inline address_v6 make_address_v6(std::string_view s, std::error_code& ec) noexcept {
    detail::ensure_init();
    scope_id_type scope = 0;
    if (auto pct = s.find('%'); pct != std::string_view::npos) {
        std::string_view zone = s.substr(pct + 1);
        s = s.substr(0, pct);
        bool numeric = !zone.empty();
        scope_id_type v = 0;
        for (char c : zone) {
            if (c < '0' || c > '9') {
                numeric = false;
                break;
            }
            v = v * 10 + scope_id_type(c - '0');
        }
        if (numeric) {
            scope = v;
        } else {
#if STDNET_WINDOWS
            scope = 0; // interface names need iphlpapi on Windows; use a numeric zone instead
#else
            std::string z(zone);
            scope = ::if_nametoindex(z.c_str());
#endif
        }
    }
    char buf[INET6_ADDRSTRLEN] = {};
    if (s.size() >= sizeof buf) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return {};
    }
    std::memcpy(buf, s.data(), s.size());
    ::in6_addr a{};
    if (::inet_pton(AF_INET6, buf, &a) != 1) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return {};
    }
    address_v6::bytes_type b;
    std::memcpy(b.data(), &a, 16);
    ec.clear();
    return address_v6(b, scope);
}

inline address make_address(std::string_view s, std::error_code& ec) noexcept {
    if (auto a4 = make_address_v4(s, ec); !ec) return a4;
    if (auto a6 = make_address_v6(s, ec); !ec) return a6;
    return {};
}

inline address make_address(std::string_view s) {
    std::error_code ec;
    auto a = make_address(s, ec);
    if (ec) throw std::system_error(ec, "make_address");
    return a;
}
inline address_v4 make_address_v4(std::string_view s) {
    std::error_code ec;
    auto a = make_address_v4(s, ec);
    if (ec) throw std::system_error(ec, "make_address_v4");
    return a;
}
inline address_v6 make_address_v6(std::string_view s) {
    std::error_code ec;
    auto a = make_address_v6(s, ec);
    if (ec) throw std::system_error(ec, "make_address_v6");
    return a;
}

// [internet.endpoint] -- an (address, port) pair stored as a native sockaddr.
template <class InternetProtocol>
class basic_endpoint {
public:
    using protocol_type = InternetProtocol;

    constexpr basic_endpoint() noexcept : basic_endpoint(address_v4::any(), 0) {}
    constexpr basic_endpoint(const protocol_type& proto, port_type port) noexcept
        : basic_endpoint(proto.family() == AF_INET6 ? address(address_v6::any()) : address(address_v4::any()), port) {}
    basic_endpoint(const ip::address& addr, port_type port) noexcept {
        std::memset(&storage_, 0, sizeof storage_);
        if (addr.is_v4()) {
            auto& s = reinterpret_cast<::sockaddr_in&>(storage_);
            s.sin_family = AF_INET;
            s.sin_port = htons(port);
            auto b = addr.to_v4().to_bytes();
            std::memcpy(&s.sin_addr, b.data(), 4);
        } else {
            auto& s = reinterpret_cast<::sockaddr_in6&>(storage_);
            s.sin6_family = AF_INET6;
            s.sin6_port = htons(port);
            auto v6 = addr.to_v6();
            auto b = v6.to_bytes();
            std::memcpy(&s.sin6_addr, b.data(), 16);
            s.sin6_scope_id = v6.scope_id();
        }
    }

    protocol_type protocol() const noexcept { return is_v6() ? protocol_type::v6() : protocol_type::v4(); }

    ip::address address() const noexcept {
        if (is_v6()) {
            auto& s = reinterpret_cast<const ::sockaddr_in6&>(storage_);
            address_v6::bytes_type b;
            std::memcpy(b.data(), &s.sin6_addr, 16);
            return address_v6(b, s.sin6_scope_id);
        }
        auto& s = reinterpret_cast<const ::sockaddr_in&>(storage_);
        address_v4::bytes_type b;
        std::memcpy(b.data(), &s.sin_addr, 4);
        return address_v4(b);
    }
    void address(const ip::address& a) noexcept { *this = basic_endpoint(a, port()); }

    port_type port() const noexcept {
        return is_v6() ? ntohs(reinterpret_cast<const ::sockaddr_in6&>(storage_).sin6_port)
                       : ntohs(reinterpret_cast<const ::sockaddr_in&>(storage_).sin_port);
    }
    void port(port_type p) noexcept {
        if (is_v6())
            reinterpret_cast<::sockaddr_in6&>(storage_).sin6_port = htons(p);
        else
            reinterpret_cast<::sockaddr_in&>(storage_).sin_port = htons(p);
    }

    // Extensible-implementation accessors ([socket.reqmts.endpoint]).
    ::sockaddr* data() noexcept { return reinterpret_cast<::sockaddr*>(&storage_); }
    const ::sockaddr* data() const noexcept { return reinterpret_cast<const ::sockaddr*>(&storage_); }
    std::size_t size() const noexcept { return is_v6() ? sizeof(::sockaddr_in6) : sizeof(::sockaddr_in); }
    constexpr std::size_t capacity() const noexcept { return sizeof storage_; }
    void resize(std::size_t) noexcept {}

    std::string to_string() const {
        return is_v6() ? std::format("[{}]:{}", address().to_string(), port())
                       : std::format("{}:{}", address().to_string(), port());
    }

    friend bool operator==(const basic_endpoint& a, const basic_endpoint& b) noexcept {
        return a.address() == b.address() && a.port() == b.port();
    }
    friend std::strong_ordering operator<=>(const basic_endpoint& a, const basic_endpoint& b) noexcept {
        if (auto c = a.address() <=> b.address(); c != 0) return c;
        return a.port() <=> b.port();
    }
    friend std::ostream& operator<<(std::ostream& os, const basic_endpoint& e) { return os << e.to_string(); }

private:
    bool is_v6() const noexcept { return storage_.ss_family == AF_INET6; }
    ::sockaddr_storage storage_{};
};

// [internet.tcp]
class tcp {
public:
    using endpoint = basic_endpoint<tcp>;
    class socket;
    class acceptor;

    static constexpr tcp v4() noexcept { return tcp(AF_INET); }
    static constexpr tcp v6() noexcept { return tcp(AF_INET6); }
    constexpr int family() const noexcept { return family_; }
    constexpr int type() const noexcept { return SOCK_STREAM; }
    constexpr int protocol() const noexcept { return IPPROTO_TCP; }
    friend constexpr bool operator==(const tcp&, const tcp&) noexcept = default;

private:
    constexpr explicit tcp(int f) noexcept : family_(f) {}
    int family_;
};

// [internet.udp]
class udp {
public:
    using endpoint = basic_endpoint<udp>;
    class socket;

    static constexpr udp v4() noexcept { return udp(AF_INET); }
    static constexpr udp v6() noexcept { return udp(AF_INET6); }
    constexpr int family() const noexcept { return family_; }
    constexpr int type() const noexcept { return SOCK_DGRAM; }
    constexpr int protocol() const noexcept { return IPPROTO_UDP; }
    friend constexpr bool operator==(const udp&, const udp&) noexcept = default;

private:
    constexpr explicit udp(int f) noexcept : family_(f) {}
    int family_;
};

} // namespace stdnet::ip

template <class P>
struct std::formatter<stdnet::ip::basic_endpoint<P>> : std::formatter<std::string> {
    auto format(const stdnet::ip::basic_endpoint<P>& e, auto& ctx) const {
        return std::formatter<std::string>::format(e.to_string(), ctx);
    }
};
template <>
struct std::formatter<stdnet::ip::address> : std::formatter<std::string> {
    auto format(const stdnet::ip::address& a, auto& ctx) const {
        return std::formatter<std::string>::format(a.to_string(), ctx);
    }
};
