// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once

#include <string>
#include <system_error>
#include <type_traits>

namespace stdnet {

// Errors specific to the networking library. Operating-system errors are reported
// with std::system_category() (std::errc values compare equal to them).
enum class error {
    host_not_found = 1,     // name resolution: host does not exist
    service_not_found,      // name resolution: unknown service name
    try_again,              // name resolution: temporary failure
    no_candidate,           // preconnection: no transport satisfies the selection properties
    not_supported,          // requested property or protocol not supported by the implementation
    wrong_context,          // an I/O object was used with a scheduler of another io_context
};

class network_category_impl final : public std::error_category {
public:
    const char* name() const noexcept override { return "stdnet"; }
    std::string message(int ev) const override {
        switch (static_cast<error>(ev)) {
        case error::host_not_found: return "host not found";
        case error::service_not_found: return "service not found";
        case error::try_again: return "temporary failure in name resolution";
        case error::no_candidate: return "no transport candidate satisfies the selection properties";
        case error::not_supported: return "operation or property not supported";
        case error::wrong_context: return "I/O object used with a scheduler of a different io_context";
        }
        return "unknown network error";
    }
};

inline const std::error_category& network_category() noexcept {
    static const network_category_impl instance;
    return instance;
}

inline std::error_code make_error_code(error e) noexcept { return {static_cast<int>(e), network_category()}; }

} // namespace stdnet

template <>
struct std::is_error_code_enum<stdnet::error> : std::true_type {};
