// Minimal zero-dependency test framework: TEST, CHECK, CHECK_EQ, CHECK_THROWS_AS.
#pragma once

#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

namespace minitest {

struct Test {
    const char* name;
    void (*fn)();
};

inline std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}

inline int failures = 0;
inline const char* currentTest = "";

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures;
    std::printf("FAIL %s (%s:%d): %s\n", currentTest, file, line, msg.c_str());
}

inline std::string repr(const std::string& s) { return "\"" + s + "\""; }
inline std::string repr(const char* s) { return repr(std::string(s)); }
template <class T>
std::string repr(T v) requires std::is_arithmetic_v<T> {
    return std::to_string(v);
}

inline int run() {
    for (const auto& t : registry()) {
        currentTest = t.name;
        t.fn();
    }
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("All %zu tests passed\n", registry().size());
    return 0;
}

}  // namespace minitest

#define TEST(name)                                                      \
    static void test_##name();                                          \
    static ::minitest::Registrar registrar_##name(#name, &test_##name); \
    static void test_##name()

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) ::minitest::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        const auto va = (actual);                                             \
        const auto vb = (expected);                                           \
        if (!(va == vb))                                                      \
            ::minitest::fail(__FILE__, __LINE__,                              \
                             std::string("CHECK_EQ(" #actual ", " #expected   \
                                         "): ") +                             \
                                 ::minitest::repr(va) + " != " +              \
                                 ::minitest::repr(vb));                       \
    } while (0)

#define CHECK_THROWS_AS(expr, ExType)                                       \
    do {                                                                     \
        bool caught = false;                                                 \
        try {                                                                \
            (void)(expr);                                                    \
        } catch (const ExType&) {                                            \
            caught = true;                                                   \
        } catch (...) {                                                      \
        }                                                                    \
        if (!caught)                                                         \
            ::minitest::fail(__FILE__, __LINE__,                             \
                             "expected " #ExType " from: " #expr);           \
    } while (0)
