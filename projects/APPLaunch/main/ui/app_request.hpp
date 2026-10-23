/*
 * SPDX-License-Identifier: MIT
 *
 * Requests from apps to the launcher (report 035, docs/APP-CONTRACT.md section 14): the protocol of launcher.sock, one
 * request line and one reply line. This file is the pure part (parsing, limits, rate limits, the reply text); the
 * socket is app_request_server.hpp and what the verbs do is the Backend that the launcher supplies. No LVGL, no I/O.
 *
 * Nothing here executes anything the client names: a verb is looked up in a fixed list, arguments are numbers or one of
 * a few fixed words, the only free text (toast) is shown, never interpreted.
 */
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>

namespace app_request {

constexpr int kProtocol = 1;
constexpr size_t kLineMax = 254;       /* request bytes, without the newline */
constexpr size_t kToastChars = 160;    /* toast text, in characters */
constexpr size_t kBeepNameMax = 24;

/* Who is asking: from the socket's peer credentials, never from the request. */
struct Peer {
    int pid = 0;
    std::string name = "unknown"; /* the process name (/proc/<pid>/comm), for the log only */
};

/* What the verbs do. Every method returns the whole reply line without the newline ("ok ..." or "err <word>"), called on
 * the server thread, one request at a time. Arguments are already validated. */
class Backend {
public:
    virtual ~Backend() = default;
    virtual std::string hello(const Peer &peer) = 0;          /* ok 1 launcher=... app=... verbs=... */
    virtual std::string caps() = 0;                           /* ok sound=... backlight=... battery=... verbs=... */
    virtual std::string beep(const std::string &name) = 0;    /* name: notify, done or warn */
    virtual std::string toast(const std::string &text) = 0;   /* text: printable UTF-8, at most kToastChars characters */
    virtual std::string volume(const std::string &action, int value) = 0; /* get, set (value), up, down, mute, unmute */
    virtual std::string brightness(const std::string &action, int value) = 0; /* get, set (value) */
    virtual std::string dim(bool allow, const Peer &peer) = 0;
};

/* Minimum time between two requests of one class from one client (ms), 0 = unlimited. */
class RateLimiter {
public:
    /* True when the request may go on now (and records it). */
    bool allow(int client, const std::string &cls, long long now_ms);
    void forget(int client);
    size_t clients() const { return last_.size(); }

private:
    std::map<std::pair<int, std::string>, long long> last_;
};

/* The minimum interval of a verb's class: beep 1000, toast 1000, volume, brightness and dim 100, the rest 20. */
int min_interval_ms(const std::string &verb);

/* True for text that is valid UTF-8 without control characters (newline, tab and DEL included). */
bool printable_utf8(const std::string &text);
/* The first `max_chars` characters of valid UTF-8 text. */
std::string cut_chars(const std::string &text, size_t max_chars);

/* One request line (without its newline) to the reply line (without its newline). `log` is called with
 * "[req] <name>: <line>" for every request that goes on to the backend (NULL: no log). */
std::string handle_line(const std::string &line, const Peer &peer, Backend &backend, RateLimiter &limiter,
                        long long now_ms, void (*log)(const std::string &) = nullptr);

} // namespace app_request
