// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// [net.adaptors] -- moves std::error_code completions from the error channel into the value channel.
//
//   auto r = co_await (stdnet::async_receive(sock, buf) | stdnet::as_expected);
//   if (!r) handle(r.error()); else use(*r);
//
// This answers the concern (P4007) that coroutine users must otherwise catch std::system_error
// for routine I/O failures. Exceptions (std::exception_ptr) and stop requests are forwarded unchanged.
#pragma once

#include "config.hpp"

#if __has_include(<expected>)
#include <expected>
#endif
#include <variant>
#include <exception>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(__cpp_lib_expected)
#define STDNET_HAS_AS_EXPECTED 1

namespace stdnet {

namespace detail {

template <class... Ts>
struct value_of {
    using type = std::tuple<std::decay_t<Ts>...>;
};
template <>
struct value_of<> {
    using type = void;
};
template <class T>
struct value_of<T> {
    using type = std::decay_t<T>;
};

template <class... Ts>
using value_of_t = typename value_of<Ts...>::type;

template <class... Alts>
struct only_one {
    static_assert(sizeof...(Alts) == 1, "as_expected requires a sender with exactly one value completion");
};
template <class T>
struct only_one<T> {
    using type = T;
};
template <class... Alts>
using only_one_t = typename only_one<Alts...>::type;

template <class Sndr>
using expected_value_t =
    ex::value_types_of_t<Sndr, ex::env<>, value_of_t, only_one_t>;

} // namespace detail

struct as_expected_t {
    template <ex::sender Sndr>
    auto operator()(Sndr&& sndr) const {
        using V = detail::expected_value_t<Sndr>;
        using E = std::expected<V, std::error_code>;
        return ex::upon_error(ex::then(std::forward<Sndr>(sndr),
                                       [](auto&&... vs) -> E {
                                           if constexpr (sizeof...(vs) == 0)
                                               return E();
                                           else if constexpr (sizeof...(vs) == 1)
                                               return E(std::in_place, std::forward<decltype(vs)>(vs)...);
                                           else
                                               return E(std::in_place, std::forward<decltype(vs)>(vs)...);
                                       }),
                              [](auto err) -> E {
                                  if constexpr (std::is_same_v<decltype(err), std::error_code>)
                                      return E(std::unexpect, err);
                                  else if constexpr (std::is_same_v<decltype(err), std::exception_ptr>)
                                      std::rethrow_exception(err);
                                  else
                                      throw err;
                              });
    }

    template <ex::sender Sndr>
    friend auto operator|(Sndr&& sndr, const as_expected_t& self) {
        return self(std::forward<Sndr>(sndr));
    }
};

inline constexpr as_expected_t as_expected{};

} // namespace stdnet

#else
#define STDNET_HAS_AS_EXPECTED 0
#endif
