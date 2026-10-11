// Minimal self-contained test harness (no third-party dependency).
#pragma once

#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace check {

struct test_case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<test_case>& registry() {
    static std::vector<test_case> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}

struct registrar {
    registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline int run_all(int argc, char** argv) {
    int ran = 0;
    for (auto& t : registry()) {
        if (argc > 1 && std::string(t.name).find(argv[1]) == std::string::npos) continue;
        int before = failures();
        try {
            t.fn();
        } catch (const std::exception& e) {
            std::printf("  exception: %s\n", e.what());
            ++failures();
        } catch (...) {
            std::printf("  unknown exception\n");
            ++failures();
        }
        std::printf("[%s] %s\n", failures() == before ? " OK " : "FAIL", t.name);
        ++ran;
    }
    std::printf("%d test(s), %d failure(s)\n", ran, failures());
    return failures() == 0 ? 0 : 1;
}

} // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name)                                                                              \
    static void CHECK_CAT(test_fn_, __LINE__)();                                                \
    static ::check::registrar CHECK_CAT(test_reg_, __LINE__)(name, &CHECK_CAT(test_fn_, __LINE__)); \
    static void CHECK_CAT(test_fn_, __LINE__)()

#define CHECK(expr)                                                                    \
    do {                                                                               \
        if (!(expr)) {                                                                 \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr);   \
            ++::check::failures();                                                     \
        }                                                                              \
    } while (0)

#define REQUIRE(expr)                                                                  \
    do {                                                                               \
        if (!(expr)) {                                                                 \
            std::printf("  %s:%d: REQUIRE(%s) failed\n", __FILE__, __LINE__, #expr);  \
            ++::check::failures();                                                     \
            return;                                                                    \
        }                                                                              \
    } while (0)
