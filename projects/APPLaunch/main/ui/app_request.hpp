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
constexpr size_t kClipboardMax = 4096;                       /* clipboard text, in bytes (the length-prefixed form) */
constexpr long long kClipboardExpiryMs = 10LL * 60 * 1000;   /* the text is forgotten 10 minutes after the last set */

/* Who is asking: from the socket's peer credentials, never from the request. */
struct Peer {
    int pid = 0;
    std::string name = "unknown"; /* the process name (/proc/<pid>/comm), for the log only */
};

/* The clipboard slot: one plain UTF-8 text, in memory only (never written to a file, never logged), forgotten
 * kClipboardExpiryMs after the last set (reading does not extend it), zeroed when it is replaced, cleared or expired.
 * Time is passed in (monotonic ms), so the expiry is testable. Used from the server thread only. */
class Clipboard {
public:
    ~Clipboard() { clear(); }
    /* False (nothing changed) for an empty text or one over kClipboardMax bytes. */
    bool set(const std::string &text, long long now_ms);
    /* False when empty or expired (an expired text is zeroed here). */
    bool get(std::string &out, long long now_ms);
    void clear();
    size_t size() const { return text_.size(); }

private:
    std::string text_;
    long long set_at_ = 0;
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
    /* on = false: backlight fully off, not saved; on = true: back to the level before. ok | err busy | err absent */
    virtual std::string screen(bool on, const Peer &peer) = 0;
    /* The clipboard slot (the protocol part of the verb is in handle_line: it needs no backend method). */
    virtual Clipboard &clipboard() { return clipboard_; }

private:
    Clipboard clipboard_;
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

/* The minimum interval of a verb's class: beep 1000, toast 1000, volume, brightness, dim and screen 100, clipboard 50,
 * the rest 20. */
int min_interval_ms(const std::string &verb);

/* True for text that is valid UTF-8 without control characters (newline, tab and DEL included). */
bool printable_utf8(const std::string &text);
/* The first `max_chars` characters of valid UTF-8 text. */
std::string cut_chars(const std::string &text, size_t max_chars);

/* True for clipboard text: valid UTF-8, no control character but newline and tab (so also no NUL, CR or DEL). */
bool clipboard_text_ok(const std::string &text);

/* The length-prefixed request form `clipboard set-len <n>`: n bytes of text and one newline follow the request line.
 * Returns 0 when `line` is not such a request, 1 when n bytes (and the newline) follow (1 <= n <= kClipboardMax, set in
 * `bytes`), -1 for bad arguments, -2 for an n over kClipboardMax. For -1 and -2 handle_line gives the reply
 * ("err bad", "err size") and the connection is closed, as nothing says where the request ends. */
int payload_length(const std::string &line, size_t &bytes);

/* One request line (without its newline) to the reply line (without its newline; a reply may hold newlines: the
 * length-prefixed `clipboard get-len` answers "ok <n>", a newline and the n bytes). `log` is called with
 * "[req] <name>: <line>" for every request that goes on to the backend (NULL: no log). The clipboard verb is the
 * exception: its text is never logged, only "clipboard set 37 bytes" / "clipboard get" / "clipboard clear". */
std::string handle_line(const std::string &line, const Peer &peer, Backend &backend, RateLimiter &limiter,
                        long long now_ms, void (*log)(const std::string &) = nullptr);

/* The same for a request that has a payload (payload_length() returned 1): `line` and the `payload` bytes, without the
 * newline that ended each. Only `clipboard set-len` has one. */
std::string handle_payload(const std::string &line, const std::string &payload, const Peer &peer, Backend &backend,
                           RateLimiter &limiter, long long now_ms, void (*log)(const std::string &) = nullptr);

} // namespace app_request
