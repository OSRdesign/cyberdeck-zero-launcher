/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher.sock server (see app_request_server.hpp).
 */
#include "app_request_server.hpp"

#if defined(__linux__)

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace app_request {
namespace {

long long now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Client {
    int fd = -1;
    Peer peer;
    std::string in;
    long long last = 0;
};

struct Server {
    ServerConfig config;
    int listen_fd = -1;
    int wake[2] = {-1, -1};
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::vector<Client> clients;
    RateLimiter limiter;
    std::mutex mutex; /* start / stop */
};

Server &server()
{
    static Server instance;
    return instance;
}

void say(const Server &s, const std::string &text)
{
    if (s.config.log) s.config.log(text);
}

std::string process_name(int pid)
{
    char path[48];
    std::snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    std::FILE *f = std::fopen(path, "r");
    if (!f) return "unknown";
    char name[40] = {0};
    if (!std::fgets(name, sizeof(name), f)) name[0] = '\0';
    std::fclose(f);
    std::string out;
    for (const char *p = name; *p && *p != '\n'; ++p) out += (static_cast<unsigned char>(*p) < 0x20) ? '?' : *p;
    return out.empty() ? "unknown" : out;
}

void send_line(int fd, const std::string &reply)
{
    const std::string text = reply + "\n";
    size_t sent = 0;
    while (sent < text.size()) {
        const ssize_t n = ::send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) sent += static_cast<size_t>(n);
        else if (n < 0 && errno == EINTR) continue;
        else return; /* the reply is a few bytes: a client that cannot take it is gone or hostile */
    }
}

void drop(Server &s, size_t index)
{
    s.limiter.forget(s.clients[index].peer.pid);
    ::close(s.clients[index].fd);
    s.clients.erase(s.clients.begin() + static_cast<std::ptrdiff_t>(index));
}

void accept_clients(Server &s)
{
    for (;;) {
        const int fd = ::accept4(s.listen_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        struct ucred cred;
        socklen_t len = sizeof(cred);
        if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 ||
            (cred.uid != ::geteuid() && cred.uid != 0)) {
            ::close(fd);
            continue;
        }
        if (static_cast<int>(s.clients.size()) >= kMaxClients) {
            send_line(fd, "err busy");
            ::close(fd);
            continue;
        }
        Client c;
        c.fd = fd;
        c.peer.pid = static_cast<int>(cred.pid);
        c.peer.name = process_name(c.peer.pid);
        c.last = now_ms();
        s.clients.push_back(std::move(c));
    }
}

/* Reads what the client sent and answers every complete line. False: close the connection. */
bool serve(Server &s, Client &c)
{
    char buf[256];
    for (;;) {
        const ssize_t n = ::recv(c.fd, buf, sizeof(buf), 0);
        if (n > 0) {
            c.in.append(buf, static_cast<size_t>(n));
            c.last = now_ms();
        } else if (n == 0) {
            return false;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else {
            return false;
        }
        if (c.in.size() > 4 * kLineMax) break;
    }
    for (;;) {
        const size_t nl = c.in.find('\n');
        if (nl == std::string::npos) break;
        std::string line = c.in.substr(0, nl);
        c.in.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string reply;
        try {
            reply = s.config.handler(line, c.peer);
        } catch (...) {
            reply = "err busy";
        }
        send_line(c.fd, reply);
    }
    if (c.in.size() > kLineMax) {
        send_line(c.fd, "err bad");
        return false;
    }
    return true;
}

void run(Server &s)
{
    while (!s.stopping.load()) {
        std::vector<pollfd> fds;
        fds.push_back({s.wake[0], POLLIN, 0});
        fds.push_back({s.listen_fd, POLLIN, 0});
        for (const Client &c : s.clients) fds.push_back({c.fd, POLLIN, 0});
        const int r = ::poll(fds.data(), fds.size(), 1000);
        if (r < 0 && errno != EINTR) break;
        if (s.stopping.load()) break;
        if (r > 0) {
            /* the clients first (one that left makes room for the one that waits), newest first so erasing keeps the
             * indices of the others */
            for (size_t i = fds.size() - 1; i >= 2; --i) {
                const size_t index = i - 2;
                if (index >= s.clients.size()) continue;
                if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                    if (!serve(s, s.clients[index])) drop(s, index);
                }
            }
            if (fds[1].revents & POLLIN) accept_clients(s);
        }
        const long long now = now_ms();
        for (size_t i = s.clients.size(); i-- > 0;)
            if (now - s.clients[i].last > s.config.idle_ms) drop(s, i);
    }
    while (!s.clients.empty()) drop(s, s.clients.size() - 1);
}

/* 1: something answers on the path, 0: nothing does */
int path_is_live(const std::string &path)
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    (void)::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    const int ok = ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0 || errno == EAGAIN;
    ::close(fd);
    return ok ? 1 : 0;
}

} // namespace

bool start_server(ServerConfig config)
{
    Server &s = server();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.worker.joinable()) return true;
    s.config = std::move(config);
    if (!s.config.handler) return false;
    const std::string &path = s.config.path;
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
        say(s, "[REQ] socket path missing or too long: launcher.sock not served");
        return false;
    }
    const size_t slash = path.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        const std::string dir = path.substr(0, slash);
        if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
            say(s, "[REQ] cannot create " + dir + ": launcher.sock not served");
            return false;
        }
    }
    struct stat st;
    if (::lstat(path.c_str(), &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            say(s, "[REQ] " + path + " exists and is not a socket: launcher.sock not served");
            return false;
        }
        if (path_is_live(path)) {
            say(s, "[REQ] " + path + " is served by another process: launcher.sock not served");
            return false;
        }
        ::unlink(path.c_str()); /* stale: left by a launcher that did not shut down */
    }
    s.listen_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (s.listen_fd < 0) return false;
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    const mode_t old_umask = ::umask(0177);
    const int bound = ::bind(s.listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    ::umask(old_umask);
    if (bound != 0 || ::chmod(path.c_str(), 0600) != 0 || ::listen(s.listen_fd, kMaxClients) != 0 ||
        ::pipe2(s.wake, O_NONBLOCK | O_CLOEXEC) != 0) {
        say(s, std::string("[REQ] cannot set up ") + path + ": " + std::strerror(errno));
        ::close(s.listen_fd);
        s.listen_fd = -1;
        ::unlink(path.c_str());
        return false;
    }
    s.stopping.store(false);
    s.worker = std::thread([&s] { run(s); });
    say(s, "[REQ] serving " + path);
    return true;
}

void stop_server()
{
    Server &s = server();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.worker.joinable()) return;
    s.stopping.store(true);
    const char byte = 0;
    (void)!::write(s.wake[1], &byte, 1);
    s.worker.join();
    ::close(s.listen_fd);
    ::close(s.wake[0]);
    ::close(s.wake[1]);
    s.listen_fd = s.wake[0] = s.wake[1] = -1;
    ::unlink(s.config.path.c_str());
}

bool server_running()
{
    Server &s = server();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.worker.joinable();
}

} // namespace app_request

#else

namespace app_request {

bool start_server(ServerConfig) { return false; }
void stop_server() {}
bool server_running() { return false; }

} // namespace app_request

#endif
