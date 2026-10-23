// SPDX-License-Identifier: MIT
//
// launcher.sock's protocol (report 035): which lines reach the backend and with what, the answers for bad lines, the
// limits (line length, toast length, UTF-8), the rate limits and the log. The socket itself: test_app_request_server.cpp.
#include "app_request.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

namespace {

struct Fake : app_request::Backend {
    std::vector<std::string> calls;
    std::string hello(const app_request::Peer &p) override
    {
        calls.push_back("hello " + p.name);
        return "ok 1";
    }
    std::string caps() override
    {
        calls.push_back("caps");
        return "ok sound=0";
    }
    std::string beep(const std::string &n) override
    {
        calls.push_back("beep " + n);
        return "ok";
    }
    std::string toast(const std::string &t) override
    {
        calls.push_back("toast " + t);
        return "ok queued";
    }
    std::string volume(const std::string &a, int v) override
    {
        calls.push_back("volume " + a + " " + std::to_string(v));
        return "ok 40 muted=0";
    }
    std::string brightness(const std::string &a, int v) override
    {
        calls.push_back("brightness " + a + " " + std::to_string(v));
        return "ok 70 kind=levels";
    }
    std::string dim(bool allow, const app_request::Peer &) override
    {
        calls.push_back(allow ? "dim allow" : "dim deny");
        return "ok";
    }
};

std::vector<std::string> g_log;
void log_it(const std::string &line) { g_log.push_back(line); }

std::string run(Fake &b, app_request::RateLimiter &r, const std::string &line, long long now = 0, int pid = 7)
{
    app_request::Peer peer;
    peer.pid = pid;
    peer.name = "myapp";
    return app_request::handle_line(line, peer, b, r, now, log_it);
}

} // namespace

int main()
{
    Fake b;
    app_request::RateLimiter r;
    long long t = 0;
    auto go = [&](const std::string &line) {
        t += 5000; // far apart: the rate limits are tested on their own below
        return run(b, r, line, t);
    };

    // the verbs and what the backend gets
    CHECK(go("hello") == "ok 1" && b.calls.back() == "hello myapp");
    CHECK(go("hello 1") == "ok 1");
    CHECK(go("hello x") == "err bad" && go("hello 1 2") == "err bad");
    CHECK(go("caps") == "ok sound=0" && go("caps now") == "err bad");
    CHECK(go("beep") == "ok" && b.calls.back() == "beep notify");
    CHECK(go("beep done") == "ok" && b.calls.back() == "beep done");
    CHECK(go("beep warn") == "ok" && b.calls.back() == "beep warn");
    CHECK(go("beep siren") == "err bad" && go("beep notify now") == "err bad" && go("beep ../x") == "err bad");
    CHECK(go("volume get") == "ok 40 muted=0" && b.calls.back() == "volume get 0");
    CHECK(go("volume up") == "ok 40 muted=0" && b.calls.back() == "volume up 0");
    CHECK(go("volume down") == "ok 40 muted=0" && go("volume mute") == "ok 40 muted=0" && go("volume unmute") == "ok 40 muted=0");
    CHECK(go("volume set 55") == "ok 40 muted=0" && b.calls.back() == "volume set 55");
    CHECK(go("volume set 100") != "err bad" && go("volume set 0") != "err bad");
    CHECK(go("volume set 101") == "err bad" && go("volume set -1") == "err bad" && go("volume set abc") == "err bad");
    CHECK(go("volume set") == "err bad" && go("volume") == "err bad" && go("volume set 5 6") == "err bad");
    CHECK(go("volume loud") == "err bad" && go("volume set 1e2") == "err bad" && go("volume set 0x10") == "err bad");
    CHECK(go("brightness get") == "ok 70 kind=levels" && b.calls.back() == "brightness get 0");
    CHECK(go("brightness set 30") == "ok 70 kind=levels" && b.calls.back() == "brightness set 30");
    CHECK(go("brightness set 101") == "err bad" && go("brightness up") == "err bad");
    CHECK(go("dim allow") == "ok" && b.calls.back() == "dim allow");
    CHECK(go("dim deny") == "ok" && b.calls.back() == "dim deny");
    CHECK(go("dim") == "err bad" && go("dim maybe") == "err bad");

    // unknown verbs never reach the backend, and nothing is executed
    const size_t calls = b.calls.size();
    CHECK(go("reboot") == "err unknown" && go("sh -c id") == "err unknown" && go("HELLO") == "err unknown");
    CHECK(go("poweroff now") == "err unknown" && go("../../etc/passwd") == "err unknown");
    CHECK(go("") == "err bad" && go("   ") == "err bad");
    CHECK(b.calls.size() == calls);

    // toast: everything after the verb, spaces inside kept, ends trimmed, 160 characters at most
    CHECK(go("toast hi") == "ok queued" && b.calls.back() == "toast hi");
    CHECK(go("toast   two  words  ") == "ok queued" && b.calls.back() == "toast two  words");
    CHECK(go("toast") == "err bad" && go("toast    ") == "err bad");
    CHECK(go("toast " + std::string(160, 'a')) == "ok queued" && b.calls.back() == "toast " + std::string(160, 'a'));
    CHECK(go("toast " + std::string(200, 'a')) == "ok queued" && b.calls.back() == "toast " + std::string(160, 'a'));
    std::string accents;
    for (int i = 0; i < 200; i++) accents += "\xc3\xa9"; // 200 characters, 400 bytes: over the line limit
    CHECK(go("toast " + accents) == "err bad");
    std::string accents100;
    for (int i = 0; i < 100; i++) accents100 += "\xc3\xa9"; // 100 characters, 200 bytes
    CHECK(go("toast " + accents100) == "ok queued" && b.calls.back() == "toast " + accents100);
    CHECK(app_request::cut_chars(accents, 160).size() == 320 && app_request::cut_chars("abc", 5) == "abc");
    CHECK(app_request::cut_chars("a\xc3\xa9z", 2) == "a\xc3\xa9");

    // line limits and bad bytes
    CHECK(go("toast " + std::string(app_request::kLineMax, 'a')) == "err bad"); // 6 + 254 bytes
    CHECK(go(std::string(300, 'a')) == "err bad");
    CHECK(go(std::string("toast a") + '\x01' + "b") == "err bad");
    CHECK(go(std::string("toast a") + '\t' + "b") == "err bad");
    CHECK(go(std::string("toast a") + '\x7f') == "err bad");
    CHECK(go("toast a\xff") == "err bad" && go("toast \xc3") == "err bad" && go("toast \xc3\x28") == "err bad");
    CHECK(go("toast \xc2\x85") == "err bad"); // a C1 control
    CHECK(go("toast \xe2\x82\xac ok") == "ok queued"); // the euro sign

    // the log has the app's name and the line, once per request that went on
    g_log.clear();
    CHECK(run(b, r, "beep warn", 900000) == "ok");
    CHECK(g_log.size() == 1 && g_log[0] == "[req] myapp: beep warn");
    CHECK(run(b, r, "beep warn", 900001) == "err limit" && g_log.size() == 1); // limited: not logged, not served
    CHECK(run(b, r, "frobnicate", 900002) == "err unknown" && g_log.size() == 1);

    // rate limits: per client and per class
    app_request::RateLimiter rl;
    Fake f;
    CHECK(run(f, rl, "beep", 10000, 1) == "ok");
    CHECK(run(f, rl, "beep", 10500, 1) == "err limit");
    CHECK(run(f, rl, "beep done", 10999, 1) == "err limit");   // the same class whatever the name
    CHECK(run(f, rl, "beep", 10999, 2) == "ok");               // another client is its own
    CHECK(run(f, rl, "beep", 11000, 1) == "ok");               // a second later
    CHECK(run(f, rl, "toast a", 11000, 1) == "ok queued");     // another class
    CHECK(run(f, rl, "toast b", 11500, 1) == "err limit");
    CHECK(run(f, rl, "volume up", 20000, 1) == "ok 40 muted=0");
    CHECK(run(f, rl, "volume up", 20050, 1) == "err limit");
    CHECK(run(f, rl, "volume up", 20100, 1) == "ok 40 muted=0");
    CHECK(run(f, rl, "hello", 30000, 1) == "ok 1" && run(f, rl, "hello", 30010, 1) == "err limit");
    CHECK(run(f, rl, "hello", 30040, 1) == "ok 1");
    CHECK(run(f, rl, "dim allow", 40000, 1) == "ok" && run(f, rl, "dim deny", 40010, 1) == "err limit");
    CHECK(app_request::min_interval_ms("beep") == 1000 && app_request::min_interval_ms("toast") == 1000);
    CHECK(app_request::min_interval_ms("volume") == 100 && app_request::min_interval_ms("hello") == 20);
    // a client that left is forgotten; the table never grows without bound
    rl.forget(1);
    CHECK(run(f, rl, "beep", 11001, 1) == "ok");
    for (int pid = 1000; pid < 1600; pid++) CHECK(run(f, rl, "beep", 50000, pid) == "ok");
    CHECK(rl.clients() <= 300);

    std::puts("test_app_request: PASS");
    return 0;
}
