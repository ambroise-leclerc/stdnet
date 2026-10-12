// stdnet -- an implementation of a C++29 <network> header, built on std::execution.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once

// Selection of the std::execution implementation.
//
// The library is written against the C++26 sender/receiver protocol (P2300R10 as
// adopted into C++26). When the standard library provides it (__cpp_lib_senders),
// std::execution is used. Otherwise the NVIDIA reference implementation (stdexec)
// is used; it exposes the same names in namespace ::stdexec.
//
// Define STDNET_USE_STDEXEC=1 to force stdexec even when <execution> has senders.

#if __has_include(<version>)
#include <version>
#endif

#if !defined(STDNET_USE_STDEXEC)
#if defined(__cpp_lib_senders)
#define STDNET_USE_STDEXEC 0
#else
#define STDNET_USE_STDEXEC 1
#endif
#endif

#if STDNET_USE_STDEXEC
#include <stdexec/execution.hpp>
#include <exec/completion_behavior.hpp>
namespace stdnet::ex {
using namespace ::stdexec;
}
#else
#include <execution>
namespace stdnet::ex {
using namespace ::std::execution;
using ::std::stop_token;
using ::std::inplace_stop_token;
using ::std::never_stop_token;
using ::std::stoppable_token;
using ::std::unstoppable_token;
using ::std::stop_callback_for_t;
} // namespace stdnet::ex
#endif

#if defined(_WIN32)
#define STDNET_WINDOWS 1
#else
#define STDNET_WINDOWS 0
#endif

#if defined(__linux__) && !defined(STDNET_FORCE_POLL)
#define STDNET_EPOLL 1
#else
#define STDNET_EPOLL 0
#endif
