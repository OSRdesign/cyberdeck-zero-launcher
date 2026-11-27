// SPDX-License-Identifier: MIT
//
// launcher.sock's server (report 035) against the real client (cp0_app_client.c): mode 0600, the peer's identity, stale
// socket cleanup, a file or a live server in the way, several requests on one connection, the line limit, the client
// limit, idle connections, stop and restart.
#include "app_request_server.hpp"

#include "app_request.hpp"
#include "cp0_app_client.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

namespace {

std::vector<std::string> g_logs;

app_request::ServerConfig config(const std::string &path, int idle_ms = 30000)
{
    app_request::ServerConfig c;
    c.path = path;
    c.idle_ms = idle_ms;
    c.log = [](const std::string &s) { g_logs.push_back(s); };
    c.handler = [](const std::string &line, const app_request::Peer &peer) {
        if (line == "who") return "ok pid=" + std::to_string(peer.pid) + " name=" + peer.name;
        if (line == "slow") {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return std::string("ok slow");
        }
        if (line == "throw") throw 1;
        return "ok " + line;
    };
    return c;
}

int raw_connect(const std::string &path)
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a;
    std::memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    std::snprintf(a.sun_path, sizeof(a.sun_path), "%s", path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// reads one line (up to 2 s); "" on close / timeout
std::string read_line(int fd, int timeout_ms = 2000)
{
    std::string out;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < end) {
        char c;
        timeval tv = {0, 50000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        const ssize_t n = ::recv(fd, &c, 1, 0);
        if (n == 1) {
            if (c == '\n') return out;
            out += c;
        } else if (n == 0) {
            return out;
        }
    }
    return out;
}

bool closed_by_peer(int fd, int timeout_ms = 2000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < end) {
        char c;
        timeval tv = {0, 50000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        const ssize_t n = ::recv(fd, &c, 1, 0);
        if (n == 0) return true;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return true;
    }
    return false;
}

} // namespace

int main()
{
    char root[] = "/tmp/applaunch-reqsrv-XXXXXX";
    CHECK(mkdtemp(root) != nullptr);
    const std::string dir = std::string(root) + "/applaunch";
    const std::string path = dir + "/launcher.sock";
    ::setenv(CP0_APP_CLIENT_ENV_SOCKET, path.c_str(), 1);
    char reply[300];

    // no server yet: the client says no service at once
    CHECK(cp0_app_client_call("hello", reply, sizeof(reply), 0) == -1);

    // a file in the way is not ours to remove
    CHECK(::mkdir(dir.c_str(), 0755) == 0);
    int f = ::open(path.c_str(), O_WRONLY | O_CREAT, 0644);
    CHECK(f >= 0);
    ::close(f);
    CHECK(!app_request::start_server(config(path)));
    CHECK(!app_request::server_running());
    CHECK(::access(path.c_str(), F_OK) == 0);
    ::unlink(path.c_str());

    // a live server in the way is not stolen
    {
        const int live = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a;
        std::memset(&a, 0, sizeof(a));
        a.sun_family = AF_UNIX;
        std::snprintf(a.sun_path, sizeof(a.sun_path), "%s", path.c_str());
        CHECK(::bind(live, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0 && ::listen(live, 1) == 0);
        CHECK(!app_request::start_server(config(path)));
        CHECK(::access(path.c_str(), F_OK) == 0);
        ::close(live);
        // now it is stale (nobody listens): removed and replaced, as after a crash
        CHECK(app_request::start_server(config(path)));
        CHECK(app_request::server_running());
    }

    // mode 0600, owner only
    struct stat st;
    CHECK(::stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode) && (st.st_mode & 0777) == 0600);

    // the client end to end; the peer is this process
    CHECK(cp0_app_client_call("hello 1", reply, sizeof(reply), 0) == 0 && std::strcmp(reply, "ok hello 1") == 0);
    CHECK(cp0_app_client_call("who", reply, sizeof(reply), 0) == 0);
    CHECK(std::string(reply).rfind("ok pid=" + std::to_string(getpid()) + " name=", 0) == 0);
    CHECK(cp0_app_client_call("throw", reply, sizeof(reply), 0) == 1 && std::strcmp(reply, "err busy") == 0);
    CHECK(cp0_app_client_call("slow", reply, sizeof(reply), 1000) == 0 && std::strcmp(reply, "ok slow") == 0);

    // several requests on one connection, even sent together; CRLF tolerated
    int c = raw_connect(path);
    CHECK(c >= 0);
    const char burst[] = "one\ntwo\r\nthree";
    CHECK(::send(c, burst, sizeof(burst) - 1, 0) > 0);
    CHECK(read_line(c) == "ok one" && read_line(c) == "ok two");
    CHECK(::send(c, "\n", 1, 0) == 1); // the third line ends now
    CHECK(read_line(c) == "ok three");
    ::close(c);

    // a line over the limit: err bad, connection closed
    c = raw_connect(path);
    std::string longline(400, 'a');
    CHECK(::send(c, longline.data(), longline.size(), 0) > 0);
    CHECK(read_line(c) == "err bad" && closed_by_peer(c));
    ::close(c);

    // a client that leaves in the middle of a line costs nothing
    c = raw_connect(path);
    CHECK(::send(c, "half", 4, 0) == 4);
    ::close(c);
    CHECK(cp0_app_client_call("after", reply, sizeof(reply), 0) == 0);

    // at most kMaxClients at once: the next one is told, the others unharmed
    std::vector<int> held;
    for (int i = 0; i < app_request::kMaxClients; i++) {
        const int h = raw_connect(path);
        CHECK(h >= 0);
        held.push_back(h);
        CHECK(::send(h, "ping\n", 5, 0) == 5 && read_line(h) == "ok ping"); // accepted and counted
    }
    const int extra = raw_connect(path);
    CHECK(extra >= 0 && read_line(extra) == "err busy");
    ::close(extra);
    for (int h : held) ::close(h);
    CHECK(cp0_app_client_call("room again", reply, sizeof(reply), 1000) == 0);

    // stop removes the socket; start again is clean
    app_request::stop_server();
    CHECK(!app_request::server_running());
    CHECK(::access(path.c_str(), F_OK) != 0);
    CHECK(cp0_app_client_call("hello", reply, sizeof(reply), 0) == -1);
    app_request::stop_server(); // unpaired: nothing

    // idle connections are closed
    CHECK(app_request::start_server(config(path, 300)));
    c = raw_connect(path);
    CHECK(::send(c, "x\n", 2, 0) == 2 && read_line(c) == "ok x");
    CHECK(closed_by_peer(c, 3000));
    ::close(c);
    CHECK(app_request::start_server(config(path, 300))); // twice: does nothing
    app_request::stop_server();

    // ---- the clipboard end to end: the real protocol and the real client on the real server (the framing of
    // `clipboard set-len`), and the log
    {
        struct Quiet : app_request::Backend {
            std::string hello(const app_request::Peer &) override { return "ok 1"; }
            std::string caps() override { return "ok"; }
            std::string beep(const std::string &) override { return "ok"; }
            std::string toast(const std::string &) override { return "ok queued"; }
            std::string volume(const std::string &, int) override { return "ok"; }
            std::string brightness(const std::string &, int) override { return "ok"; }
            std::string dim(bool, const app_request::Peer &) override { return "ok"; }
            std::string screen(bool, const app_request::Peer &) override { return "ok"; }
        };
        static Quiet backend;
        static app_request::RateLimiter limiter;
        static long long fake_now = 1000000;
        g_logs.clear();
        app_request::ServerConfig cc;
        cc.path = path;
        cc.log = [](const std::string &s) { g_logs.push_back(s); };
        cc.handler = [](const std::string &line, const app_request::Peer &peer) {
            fake_now += 1000; // far apart: the 50 ms class limit is tested in test_app_request
            return app_request::handle_line(line, peer, backend, limiter, fake_now,
                                            [](const std::string &s) { g_logs.push_back(s); });
        };
        cc.frame = [](const std::string &line, size_t &bytes) { return app_request::payload_length(line, bytes); };
        cc.payload_handler = [](const std::string &line, const std::string &payload, const app_request::Peer &peer) {
            fake_now += 1000;
            return app_request::handle_payload(line, payload, peer, backend, limiter, fake_now,
                                               [](const std::string &s) { g_logs.push_back(s); });
        };
        CHECK(app_request::start_server(cc));

        const std::string secret = "hunter2-SECRET-token";
        char text[CP0_APP_CLIPBOARD_MAX + 1];
        size_t n = 0;
        // nothing yet: OK with an empty text (the clipboard is empty)
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && n == 0 && text[0] == '\0');
        // short, one line: the one-line request
        CHECK(cp0_app_clipboard_set(secret.c_str(), secret.size(), 0) == CP0_APP_CLIPBOARD_OK);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && n == secret.size() &&
              secret == text);
        CHECK(cp0_app_client_call("clipboard get", reply, sizeof(reply), 0) == 0 && std::string(reply) == "ok " + secret);
        // several lines and tabs: length-prefixed
        const std::string multi = "line one\n\tline two \xc3\xa9\nline three\n";
        CHECK(cp0_app_clipboard_set(multi.data(), multi.size(), 0) == CP0_APP_CLIPBOARD_OK);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && std::string(text, n) == multi);
        // a long single line (over 230 bytes): length-prefixed too
        const std::string longline(231, 'l');
        CHECK(cp0_app_clipboard_set(longline.data(), longline.size(), 0) == CP0_APP_CLIPBOARD_OK);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && std::string(text, n) == longline);
        // the largest: 4096 bytes, with newlines inside, round trip byte for byte
        std::string big;
        while (big.size() < CP0_APP_CLIPBOARD_MAX) big += (big.size() % 61 == 60) ? "\n" : "abcdefghij";
        big.resize(CP0_APP_CLIPBOARD_MAX);
        CHECK(cp0_app_clipboard_set(big.data(), big.size(), 0) == CP0_APP_CLIPBOARD_OK);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && n == big.size() &&
              std::string(text, n) == big);
        // too big or empty: refused by the client, nothing sent, the text stays
        CHECK(cp0_app_clipboard_set(big.data(), big.size() + 1, 0) == CP0_APP_CLIPBOARD_REFUSED);
        CHECK(cp0_app_clipboard_set("", 0, 0) == CP0_APP_CLIPBOARD_REFUSED);
        CHECK(cp0_app_clipboard_set(NULL, 3, 0) == CP0_APP_CLIPBOARD_REFUSED);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && n == big.size());
        // refused by the launcher: a CR, a NUL, bad UTF-8 (both forms)
        CHECK(cp0_app_clipboard_set("a\rb", 3, 0) == CP0_APP_CLIPBOARD_REFUSED);
        CHECK(cp0_app_clipboard_set("a\0b", 3, 0) == CP0_APP_CLIPBOARD_REFUSED);
        CHECK(cp0_app_clipboard_set("a\xff", 2, 0) == CP0_APP_CLIPBOARD_REFUSED);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && std::string(text, n) == big);
        // a caller's buffer smaller than the text: cut at a character boundary, NUL-terminated
        const std::string accents = "ab\xc3\xa9\xc3\xa9";
        CHECK(cp0_app_clipboard_set(accents.data(), accents.size(), 0) == CP0_APP_CLIPBOARD_OK);
        char small[5];
        CHECK(cp0_app_clipboard_get(small, sizeof(small), &n, 0) == CP0_APP_CLIPBOARD_OK && n == 4 &&
              std::string(small) == "ab\xc3\xa9");
        // clear
        CHECK(cp0_app_clipboard_clear(0) == CP0_APP_CLIPBOARD_OK);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && n == 0);

        // raw: the framing as a script would write it
        int rc = raw_connect(path);
        CHECK(rc >= 0);
        const char one[] = "clipboard set-len 11\nhello\nworld\n";
        CHECK(::send(rc, one, sizeof(one) - 1, 0) > 0);
        CHECK(read_line(rc) == "ok 11");
        // a payload that holds a request line is text, not a request; replies come in order on one connection
        const char sneaky[] = "clipboard set-len 15\nclipboard clear\nxx\nclipboard get-len\n";
        CHECK(::send(rc, sneaky, sizeof(sneaky) - 1, 0) > 0);
        CHECK(read_line(rc) == "ok 15" && read_line(rc) == "err unknown"); // the "xx" after the payload is a request
        CHECK(read_line(rc) == "ok 15" && read_line(rc) == "clipboard clear"); // and the payload was stored, not run
        // a payload that arrives in pieces
        CHECK(::send(rc, "clipboard set-len 7\nab", 22, 0) == 22);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        CHECK(::send(rc, "cd\nef", 5, 0) == 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        CHECK(::send(rc, "\n", 1, 0) == 1);
        CHECK(read_line(rc) == "ok 7");
        CHECK(::send(rc, "clipboard get-len\n", 18, 0) == 18);
        CHECK(read_line(rc) == "ok 7" && read_line(rc) == "abcd" && read_line(rc) == "ef");
        ::close(rc);

        // a payload not ended by a newline: err bad, the connection closed (the framing is lost)
        rc = raw_connect(path);
        CHECK(::send(rc, "clipboard set-len 3\nabcd", 24, 0) == 24);
        CHECK(read_line(rc) == "err bad" && closed_by_peer(rc));
        ::close(rc);
        // an announcement over 4096 or not a number: its reply, and closed
        rc = raw_connect(path);
        CHECK(::send(rc, "clipboard set-len 4097\n", 23, 0) == 23);
        CHECK(read_line(rc) == "err size" && closed_by_peer(rc));
        ::close(rc);
        rc = raw_connect(path);
        CHECK(::send(rc, "clipboard set-len lots\n", 23, 0) == 23);
        CHECK(read_line(rc) == "err bad" && closed_by_peer(rc));
        ::close(rc);
        // a client that leaves in the middle of a payload costs nothing and stores nothing
        CHECK(cp0_app_clipboard_set("kept", 4, 0) == CP0_APP_CLIPBOARD_OK);
        rc = raw_connect(path);
        CHECK(::send(rc, "clipboard set-len 100\nhalf", 26, 0) == 26);
        ::close(rc);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && std::string(text, n) == "kept");
        // a payload that never ends: the idle limit applies (here the server's default, so only check it stays alive)
        // and a plain long line is still refused while no payload is awaited
        rc = raw_connect(path);
        std::string toolong(400, 'a');
        CHECK(::send(rc, toolong.data(), toolong.size(), 0) > 0);
        CHECK(read_line(rc) == "err bad" && closed_by_peer(rc));
        ::close(rc);

        // the log: requests are there as summaries, the text nowhere (the secret went through set, get, set-len, errors)
        CHECK(cp0_app_clipboard_set(secret.c_str(), secret.size(), 0) == CP0_APP_CLIPBOARD_OK);
        CHECK(cp0_app_clipboard_set((secret + "\n" + secret).c_str(), 2 * secret.size() + 1, 0) == CP0_APP_CLIPBOARD_OK);
        CHECK(cp0_app_clipboard_set((secret + "\r").c_str(), secret.size() + 1, 0) == CP0_APP_CLIPBOARD_REFUSED);
        CHECK(cp0_app_client_call(("clipboard set " + secret + "\x01").c_str(), reply, sizeof(reply), 0) == 1);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_OK && n == 2 * secret.size() + 1);
        CHECK(cp0_app_clipboard_clear(0) == CP0_APP_CLIPBOARD_OK);
        bool saw_request = false;
        for (const std::string &l : g_logs) {
            CHECK(l.find("hunter2") == std::string::npos && l.find("SECRET") == std::string::npos);
            CHECK(l.find("kept") == std::string::npos && l.find("abcd") == std::string::npos);
            saw_request = saw_request || l.find("clipboard set-len") != std::string::npos;
        }
        CHECK(saw_request);
        app_request::stop_server();

        // an older launcher (no clipboard verb, no framing): every form answers "err unknown"; the client says so
        g_logs.clear();
        app_request::ServerConfig old = config(path);
        old.handler = [](const std::string &, const app_request::Peer &) { return std::string("err unknown"); };
        CHECK(app_request::start_server(old));
        CHECK(cp0_app_clipboard_set("x", 1, 0) == CP0_APP_CLIPBOARD_UNSUPPORTED);
        CHECK(cp0_app_clipboard_set(multi.data(), multi.size(), 0) == CP0_APP_CLIPBOARD_UNSUPPORTED);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_UNSUPPORTED && n == 0 && text[0] == '\0');
        CHECK(cp0_app_clipboard_clear(0) == CP0_APP_CLIPBOARD_UNSUPPORTED);
        app_request::stop_server();
        // no launcher at all: NOSERVICE at once, nothing printed
        CHECK(cp0_app_clipboard_set("x", 1, 0) == CP0_APP_CLIPBOARD_NOSERVICE);
        CHECK(cp0_app_clipboard_set(multi.data(), multi.size(), 0) == CP0_APP_CLIPBOARD_NOSERVICE);
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 0) == CP0_APP_CLIPBOARD_NOSERVICE && n == 0);
        CHECK(cp0_app_clipboard_clear(0) == CP0_APP_CLIPBOARD_NOSERVICE);
        // a launcher that is slow to answer: the short timeout gives up (the caller treats it as no clipboard)
        app_request::ServerConfig slow = config(path);
        slow.handler = [](const std::string &, const app_request::Peer &) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            return std::string("ok 0");
        };
        CHECK(app_request::start_server(slow));
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(cp0_app_clipboard_get(text, sizeof(text), &n, 100) == CP0_APP_CLIPBOARD_NOSERVICE);
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(took >= 90 && took < 300);
        app_request::stop_server();
    }

    // a path that cannot be used: no server, no crash
    CHECK(!app_request::start_server(config("")));
    CHECK(!app_request::start_server(config(std::string(200, 'p'))));
    CHECK(!g_logs.empty());

    const std::string cmd = std::string("rm -rf '") + root + "'";
    CHECK(std::system(cmd.c_str()) == 0);
    std::puts("test_app_request_server: PASS");
    return 0;
}
