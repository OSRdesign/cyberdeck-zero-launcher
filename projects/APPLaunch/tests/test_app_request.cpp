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
    std::string screen(bool on, const app_request::Peer &p) override
    {
        calls.push_back(std::string(on ? "screen on " : "screen off ") + std::to_string(p.pid));
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
    CHECK(go("screen off") == "ok" && b.calls.back() == "screen off 7"); // the pid is the socket peer's
    CHECK(go("screen on") == "ok" && b.calls.back() == "screen on 7");
    {
        const size_t before = b.calls.size();
        CHECK(go("screen") == "err bad" && go("screen dark") == "err bad" && go("screen off now") == "err bad");
        CHECK(go("screen OFF") == "err bad" && go("screen 0") == "err bad" && go("screen get") == "err bad");
        CHECK(b.calls.size() == before); // a bad argument never reaches the backend
    }

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

    // ---- the clipboard: the one-line form
    {
        app_request::RateLimiter cr;
        Fake cf;
        long long ct = 1000000;
        auto cgo = [&](const std::string &line) {
            ct += 1000; // beyond the 50 ms class limit
            return run(cf, cr, line, ct);
        };
        app_request::Clipboard &clip = cf.clipboard();
        std::string got;
        CHECK(cgo("clipboard get") == "err empty" && cgo("clipboard get-len") == "err empty");
        CHECK(cgo("clipboard set hello world") == "ok 11");
        CHECK(clip.get(got, ct) && got == "hello world");
        CHECK(cgo("clipboard get") == "ok hello world");
        CHECK(cgo("clipboard get-len") == "ok 11\nhello world");
        CHECK(cgo("clipboard set  two  spaces ") == "ok 13"); // spaces after the first one are the text
        CHECK(cgo("clipboard get") == "ok  two  spaces ");
        CHECK(cgo("clipboard set \xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80") == "ok 9"); // 2 + 3 + 4 bytes
        CHECK(cgo("clipboard get-len") == "ok 9\n\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
        CHECK(cgo("clipboard clear") == "ok" && cgo("clipboard get") == "err empty" && clip.size() == 0);
        CHECK(cgo("clipboard clear") == "ok"); // twice is fine
        // bad forms never store
        CHECK(cgo("clipboard set") == "err bad" && cgo("clipboard set ") == "err bad" && cgo("clipboard") == "err bad");
        CHECK(cgo("clipboard paste") == "err bad" && cgo("clipboard get now") == "err bad" && cgo("clipboard clear 1") == "err bad");
        CHECK(cgo("clipboard set a\tb") == "err bad" && cgo("clipboard set a\xff") == "err bad"); // control, bad UTF-8
        CHECK(clip.size() == 0);
        // the longest one-line text: the line limit less "clipboard set "
        const std::string longest(app_request::kLineMax - 14, 'x');
        CHECK(cgo("clipboard set " + longest) == "ok " + std::to_string(longest.size()));
        CHECK(cgo("clipboard get") == "ok " + longest);
        CHECK(cgo("clipboard set " + longest + "x") == "err bad"); // over the line limit
        CHECK(cgo("clipboard get") == "ok " + longest);            // a refused set leaves the old text
        // a text that does not fit a reply line is read with get-len
        CHECK(clip.set("line one\nline two", ct) && cgo("clipboard get") == "err size");
        CHECK(cgo("clipboard get-len") == "ok 17\nline one\nline two");
        CHECK(clip.set(std::string(300, 'y'), ct) && cgo("clipboard get") == "err size");
        CHECK(cgo("clipboard get-len") == "ok 300\n" + std::string(300, 'y'));
    }

    // ---- the clipboard: expiry, 10 minutes after the last set; a read does not extend it; a set restarts it
    {
        app_request::Clipboard clip;
        std::string got;
        CHECK(clip.set("token", 1000));
        CHECK(clip.get(got, 1000 + app_request::kClipboardExpiryMs - 1) && got == "token");
        CHECK(clip.get(got, 1000 + app_request::kClipboardExpiryMs - 1) && got == "token"); // reading changes nothing
        CHECK(!clip.get(got, 1000 + app_request::kClipboardExpiryMs) && clip.size() == 0);    // gone, and zeroed
        CHECK(clip.set("a", 5000) && clip.set("b", 5000 + app_request::kClipboardExpiryMs - 10)); // a set restarts it
        CHECK(clip.get(got, 5000 + app_request::kClipboardExpiryMs + 100) && got == "b");
        CHECK(!clip.get(got, 5000 + 2 * app_request::kClipboardExpiryMs));
        CHECK(!clip.set("", 0) && !clip.set(std::string(app_request::kClipboardMax + 1, 'z'), 0));
        CHECK(clip.set(std::string(app_request::kClipboardMax, 'z'), 0)); // 4096 is the largest
        clip.clear();
        CHECK(!clip.get(got, 0));
        // through the verbs: an expired text answers "err empty"
        app_request::RateLimiter cr;
        Fake cf;
        CHECK(run(cf, cr, "clipboard set secret", 0) == "ok 6");
        CHECK(run(cf, cr, "clipboard get", app_request::kClipboardExpiryMs - 1) == "ok secret");
        CHECK(run(cf, cr, "clipboard get", app_request::kClipboardExpiryMs, 8) == "err empty"); // another client: no 50 ms limit
    }

    // ---- the clipboard: the length-prefixed form
    {
        size_t n = 0;
        CHECK(app_request::payload_length("clipboard set-len 5", n) == 1 && n == 5);
        CHECK(app_request::payload_length("clipboard set-len 4096", n) == 1 && n == 4096);
        CHECK(app_request::payload_length("clipboard set-len 4097", n) == -2);
        CHECK(app_request::payload_length("clipboard set-len 99999", n) == -2);
        CHECK(app_request::payload_length("clipboard set-len 0", n) == -1 && app_request::payload_length("clipboard set-len", n) == -1);
        CHECK(app_request::payload_length("clipboard set-len x", n) == -1 && app_request::payload_length("clipboard set-len -3", n) == -1);
        CHECK(app_request::payload_length("clipboard set-len 5 6", n) == -1 && app_request::payload_length("clipboard set-len 1e3", n) == -1);
        CHECK(app_request::payload_length("clipboard set-len 100000", n) == -1); // not a number of up to 5 digits
        CHECK(app_request::payload_length("clipboard get", n) == 0 && app_request::payload_length("toast set-len 4", n) == 0);
        CHECK(app_request::payload_length("clipboard set set-len", n) == 0 && app_request::payload_length("", n) == 0);

        app_request::RateLimiter cr;
        Fake cf;
        long long ct = 2000000;
        auto pgo = [&](const std::string &line, const std::string &payload) {
            ct += 3000; // the direct run() calls below sit at ct + 1000 ... ct + 4000
            app_request::Peer peer;
            peer.pid = 7;
            peer.name = "myapp";
            return app_request::handle_payload(line, payload, peer, cf, cr, ct, log_it);
        };
        const std::string multi = "first line\n\tindented \xc3\xa9\nlast";
        CHECK(pgo("clipboard set-len " + std::to_string(multi.size()), multi) == "ok " + std::to_string(multi.size()));
        std::string got;
        CHECK(cf.clipboard().get(got, ct) && got == multi);
        CHECK(run(cf, cr, "clipboard get-len", ct + 1000) == "ok " + std::to_string(multi.size()) + "\n" + multi);
        const std::string big(app_request::kClipboardMax, 'b');
        CHECK(pgo("clipboard set-len 4096", big) == "ok 4096");
        CHECK(cf.clipboard().size() == 4096);
        // refused payloads leave the text as it was
        CHECK(pgo("clipboard set-len 3", "a\rb") == "err bad");                      // CR is not allowed
        CHECK(pgo("clipboard set-len 3", std::string("a\0b", 3)) == "err bad");      // NUL
        CHECK(pgo("clipboard set-len 2", "\xc3\x28") == "err bad");                  // bad UTF-8
        CHECK(pgo("clipboard set-len 3", "\xc2\x85z") == "err bad");                 // a C1 control
        CHECK(pgo("clipboard set-len 4", "abc") == "err bad");                       // not the announced length
        CHECK(pgo("clipboard set-len 4097", std::string(4097, 'q')) == "err bad");
        CHECK(pgo("clipboard set-len x", "abc") == "err bad" && pgo("clipboard get", "") == "err bad");
        CHECK(cf.clipboard().get(got, ct) && got == big);
        // without framing (the line alone, as a transport without payloads would hand it over)
        CHECK(run(cf, cr, "clipboard set-len 5", ct + 1000) == "err bad");
        CHECK(run(cf, cr, "clipboard set-len 4097", ct + 2000) == "err size");
        CHECK(run(cf, cr, "clipboard set-len 0", ct + 3000) == "err bad" && run(cf, cr, "clipboard set-len z", ct + 4000) == "err bad");
        CHECK(cf.clipboard().get(got, ct) && got == big);
    }

    // ---- the clipboard: flood control (the class limit is 50 ms per client)
    {
        app_request::RateLimiter cr;
        Fake cf;
        CHECK(app_request::min_interval_ms("clipboard") == 50);
        CHECK(run(cf, cr, "clipboard set a", 100000, 1) == "ok 1");
        CHECK(run(cf, cr, "clipboard set b", 100049, 1) == "err limit");
        CHECK(run(cf, cr, "clipboard get", 100049, 1) == "err limit"); // reads share the class
        CHECK(run(cf, cr, "clipboard get", 100050, 1) == "ok a");      // the refused set changed nothing
        CHECK(run(cf, cr, "clipboard get", 100049, 2) == "ok a");      // another client is its own
        app_request::Peer peer;
        peer.pid = 1;
        CHECK(app_request::handle_payload("clipboard set-len 1", "z", peer, cf, cr, 100060, log_it) == "err limit");
        std::string got;
        CHECK(cf.clipboard().get(got, 100060) && got == "a");
    }

    // ---- the clipboard is never logged: not the text, not in an error, not in the summary
    {
        const std::string secret = "hunter2-SECRET-token";
        g_log.clear();
        app_request::RateLimiter cr;
        Fake cf;
        long long ct = 3000000;
        auto cgo = [&](const std::string &line) {
            ct += 1000;
            return run(cf, cr, line, ct);
        };
        auto pgo = [&](const std::string &line, const std::string &payload) {
            ct += 1000;
            app_request::Peer peer;
            peer.name = "myapp";
            return app_request::handle_payload(line, payload, peer, cf, cr, ct, log_it);
        };
        CHECK(cgo("clipboard set " + secret) == "ok " + std::to_string(secret.size()));
        CHECK(cgo("clipboard get") == "ok " + secret);
        CHECK(cgo("clipboard get-len") == "ok 20\n" + secret);
        CHECK(pgo("clipboard set-len 20", secret) == "ok 20");
        CHECK(pgo("clipboard set-len 21", secret + "\r") == "err bad");        // refused payload
        CHECK(pgo("clipboard set-len 22", secret + "\xff\xfe") == "err bad");
        CHECK(cgo("clipboard set " + secret + "\x01") == "err bad");           // refused line (control)
        CHECK(cgo("clipboard set " + secret + std::string(300, 'x')) == "err bad"); // over the line limit
        CHECK(cgo("clipboard sett " + secret) == "err bad");                   // a typo of the sub-verb
        CHECK(cgo("clipboard set-len " + secret) == "err bad");                // bad announcement
        CHECK(cgo("clipboard get") == "ok " + secret);
        CHECK(run(cf, cr, "clipboard set " + secret, ct) == "err limit");      // limited: the same instant
        CHECK(cgo("clipboard clear") == "ok");
        CHECK(!g_log.empty()); // the requests are logged, as summaries
        for (const std::string &l : g_log) {
            CHECK(l.find("hunter2") == std::string::npos && l.find("SECRET") == std::string::npos);
            CHECK(l.find("token") == std::string::npos);
        }
        bool saw_set = false, saw_len = false, saw_get = false, saw_clear = false;
        for (const std::string &l : g_log) {
            saw_set = saw_set || l == "[req] myapp: clipboard set 20 bytes";
            saw_len = saw_len || l == "[req] myapp: clipboard set-len 20 bytes";
            saw_get = saw_get || l == "[req] myapp: clipboard get";
            saw_clear = saw_clear || l == "[req] myapp: clipboard clear";
        }
        CHECK(saw_set && saw_len && saw_get && saw_clear);
        g_log.clear();
    }


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
    CHECK(run(f, rl, "screen off", 50000, 1) == "ok" && run(f, rl, "screen on", 50050, 1) == "err limit");
    CHECK(run(f, rl, "screen on", 50100, 1) == "ok" && run(f, rl, "screen off", 50050, 2) == "ok"); // per client
    CHECK(app_request::min_interval_ms("screen") == 100);
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
