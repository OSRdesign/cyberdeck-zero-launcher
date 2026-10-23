// SPDX-License-Identifier: MIT
//
// launcher.sock's server (report 035) against the real client (cp0_app_client.c): mode 0600, the peer's identity, stale
// socket cleanup, a file or a live server in the way, several requests on one connection, the line limit, the client
// limit, idle connections, stop and restart.
#include "app_request_server.hpp"

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

    // a path that cannot be used: no server, no crash
    CHECK(!app_request::start_server(config("")));
    CHECK(!app_request::start_server(config(std::string(200, 'p'))));
    CHECK(!g_logs.empty());

    const std::string cmd = std::string("rm -rf '") + root + "'";
    CHECK(std::system(cmd.c_str()) == 0);
    std::puts("test_app_request_server: PASS");
    return 0;
}
