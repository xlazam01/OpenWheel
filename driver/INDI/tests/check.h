// Minimal test harness: no external test framework needed on a Raspberry Pi.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check
{
struct Case { const char *name; std::function<void()> fn; };
inline std::vector<Case> &cases() { static std::vector<Case> c; return c; }
inline int &failures() { static int f = 0; return f; }
struct Reg { Reg(const char *n, std::function<void()> f) { cases().push_back({ n, f }); } };

inline int runAll(int argc, char **argv)
{
    std::string filter = argc > 1 ? argv[1] : "";
    int run = 0;
    for (auto &c : cases())
    {
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos) continue;
        int before = failures();
        std::printf("[ RUN  ] %s\n", c.name);
        std::fflush(stdout);
        c.fn();
        std::printf("[ %s ] %s\n", failures() == before ? " OK " : "FAIL", c.name);
        run++;
    }
    std::printf("%d tests, %d failed checks\n", run, failures());
    return failures() == 0 && run > 0 ? 0 : 1;
}
}

#define TEST(name) static void name(); static check::Reg reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) { check::failures()++; std::printf("  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { auto va_ = (a); auto vb_ = (b); if (!(va_ == vb_)) { check::failures()++; \
    std::printf("  %s:%d: CHECK_EQ failed: %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)
#define REQUIRE(cond) do { if (!(cond)) { check::failures()++; std::printf("  %s:%d: REQUIRE failed: %s\n", __FILE__, __LINE__, #cond); return; } } while (0)
