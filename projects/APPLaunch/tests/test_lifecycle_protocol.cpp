// SPDX-License-Identifier: MIT
//
// Lifecycle control protocol (task 015, APP-CONTRACT section 13): formatting and tolerant-but-strict parsing.
#include "multitask/lifecycle_protocol.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace multitask::protocol;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(const std::string &what, bool ok)
{
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s\n", what.c_str());
}

Message p(const std::string &s)
{
    return parse(s.data(), s.size());
}

} // namespace

int main()
{
    check("format hide", format(Kind::Hide, 3) == "hide 3");
    check("format show", format(Kind::Show, 0) == "show 0");
    check("format close", format(Kind::Close, 4294967295u) == "close 4294967295");
    check("format ack", format(Kind::Ack, 12) == "ack 12");
    check("format home", format(Kind::Home, 9) == "home");
    check("format switcher", format(Kind::Switcher, 9) == "switcher");

    check("ack 7", p("ack 7").kind == Kind::Ack && p("ack 7").n == 7 && p("ack 7").has_n);
    check("ack with newline", p("ack 7\n").kind == Kind::Ack && p("ack 7\n").n == 7);
    {
        const char nul[] = {'a', 'c', 'k', ' ', '9', '\0'};
        check("ack with NUL", parse(nul, sizeof(nul)).kind == Kind::Ack && parse(nul, sizeof(nul)).n == 9);
    }
    check("hide 1", p("hide 1").kind == Kind::Hide);
    check("show 2", p("show 2").kind == Kind::Show);
    check("close 3", p("close 3").kind == Kind::Close);
    check("home", p("home").kind == Kind::Home && !p("home").has_n);
    check("home 5 tolerated", p("home 5").kind == Kind::Home);
    check("switcher", p("switcher").kind == Kind::Switcher);
    check("max u32", p("ack 4294967295").n == 4294967295u);

    check("ack without n", p("ack").kind == Kind::Unknown);
    check("ack x", p("ack x").kind == Kind::Unknown);
    check("ack -1", p("ack -1").kind == Kind::Unknown);
    check("ack too big", p("ack 4294967296").kind == Kind::Unknown);
    check("ack two spaces", p("ack  1").kind == Kind::Unknown);
    check("ack trailing word", p("ack 1 2").kind == Kind::Unknown);
    check("ACK upper case", p("ACK 1").kind == Kind::Unknown);
    check("home junk", p("home x").kind == Kind::Unknown);
    check("homes", p("homes").kind == Kind::Unknown);
    check("empty", p("").kind == Kind::Unknown);
    check("newline only", p("\n").kind == Kind::Unknown);
    check("null data", parse(nullptr, 3).kind == Kind::Unknown);
    check("65 bytes", p(std::string("ack 1") + std::string(60, '\n')).kind == Kind::Unknown);
    check("64 bytes ok", p(std::string("ack 1") + std::string(59, '\n')).kind == Kind::Ack);

    check("env exports", env_exports(7) == "export APPLAUNCH_LIFECYCLE=1 APPLAUNCH_CONTROL_FD=7; ");
    check("kind names", std::strcmp(kind_name(Kind::Unknown), "unknown") == 0 &&
                            std::strcmp(kind_name(Kind::Switcher), "switcher") == 0);
    if (g_failures) {
        std::printf("test_lifecycle_protocol: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("test_lifecycle_protocol: %d checks passed\n", g_checks);
    return 0;
}
