/*
 * SPDX-License-Identifier: MIT
 *
 * Requests from apps to the launcher: the protocol (see app_request.hpp).
 */
#include "app_request.hpp"

#include <cstdlib>
#include <vector>

namespace app_request {

namespace {

std::vector<std::string> words(const std::string &line)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && line[at] == ' ') ++at;
        if (at >= line.size()) break;
        const size_t end = line.find(' ', at);
        out.push_back(line.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos) break;
        at = end;
    }
    return out;
}

/* a whole decimal number 0..100 */
bool percent(const std::string &s, int &value)
{
    if (s.empty() || s.size() > 3) return false;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    if (v > 100) return false;
    value = v;
    return true;
}

/* zero the bytes (the compiler may not drop the stores) and empty the string */
void wipe(std::string &text)
{
    volatile char *data = text.empty() ? nullptr : &text[0];
    for (size_t i = 0; data && i < text.size(); ++i) data[i] = 0;
    text.clear();
}

/* a whole decimal number 1..99999 */
bool count(const std::string &s, size_t &value)
{
    if (s.empty() || s.size() > 5) return false;
    size_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<size_t>(c - '0');
    }
    value = v;
    return true;
}

bool name_ok(const std::string &s)
{
    if (s.empty() || s.size() > kBeepNameMax) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

} // namespace

bool Clipboard::set(const std::string &text, long long now_ms)
{
    if (text.empty() || text.size() > kClipboardMax) return false;
    wipe(text_);
    text_ = text;
    set_at_ = now_ms;
    return true;
}

bool Clipboard::get(std::string &out, long long now_ms)
{
    if (text_.empty()) return false;
    if (now_ms - set_at_ >= kClipboardExpiryMs) {
        wipe(text_);
        return false;
    }
    out = text_;
    return true;
}

void Clipboard::clear()
{
    wipe(text_);
}

bool RateLimiter::allow(int client, const std::string &cls, long long now_ms)
{
    const int gap = min_interval_ms(cls);
    if (gap <= 0) return true;
    const auto key = std::make_pair(client, cls);
    const auto it = last_.find(key);
    if (it != last_.end() && now_ms - it->second < gap) return false;
    if (it == last_.end() && last_.size() > 256) last_.clear(); /* pids come and go: never grows without bound */
    last_[key] = now_ms;
    return true;
}

void RateLimiter::forget(int client)
{
    for (auto it = last_.begin(); it != last_.end();) {
        if (it->first.first == client) it = last_.erase(it);
        else ++it;
    }
}

int min_interval_ms(const std::string &verb)
{
    if (verb == "beep" || verb == "toast") return 1000;
    if (verb == "clipboard") return 50;
    if (verb == "volume" || verb == "brightness" || verb == "dim" || verb == "screen") return 100;
    return 20;
}

bool printable_utf8(const std::string &text)
{
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t extra;
        if (c < 0x20 || c == 0x7f) return false;
        if (c < 0x80) extra = 0;
        else if (c >= 0xc2 && c <= 0xdf) extra = 1;
        else if (c >= 0xe0 && c <= 0xef) extra = 2;
        else if (c >= 0xf0 && c <= 0xf4) extra = 3;
        else return false;
        if (extra > 0 && i + extra >= text.size()) return false;
        for (size_t k = 1; k <= extra; ++k)
            if ((static_cast<unsigned char>(text[i + k]) & 0xc0) != 0x80) return false;
        if (extra >= 1) {
            /* C1 controls (U+0080..U+009F) are not printable */
            if (c == 0xc2 && static_cast<unsigned char>(text[i + 1]) < 0xa0) return false;
        }
        i += extra + 1;
    }
    return true;
}

bool clipboard_text_ok(const std::string &text)
{
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n' || c == '\t') {
            ++i;
            continue;
        }
        /* one character at a time through the printable check: the same UTF-8 rules, a C1 control refused too */
        size_t len = 1;
        if (c >= 0xc2 && c <= 0xdf) len = 2;
        else if (c >= 0xe0 && c <= 0xef) len = 3;
        else if (c >= 0xf0 && c <= 0xf4) len = 4;
        if (i + len > text.size() || !printable_utf8(text.substr(i, len))) return false;
        i += len;
    }
    return true;
}

int payload_length(const std::string &line, size_t &bytes)
{
    const std::vector<std::string> w = words(line);
    if (w.size() < 2 || w[0] != "clipboard" || w[1] != "set-len") return 0;
    size_t n = 0;
    if (w.size() != 3 || !count(w[2], n) || n == 0) return -1;
    if (n > kClipboardMax) return -2;
    bytes = n;
    return 1;
}

std::string cut_chars(const std::string &text, size_t max_chars)
{
    size_t chars = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) {
            if (chars == max_chars) return text.substr(0, i);
            ++chars;
        }
    }
    return text;
}

/* `clipboard set|set-len|get|get-len|clear`. payload: the bytes of set-len (NULL: none given). The log gets a summary
 * only: the verb, its form and a byte count, never the text. */
static std::string clipboard_request(const std::string &line, const std::vector<std::string> &w, const std::string *payload,
                              bool framed, const Peer &peer, Backend &backend, long long now_ms,
                              void (*log)(const std::string &))
{
    Clipboard &clip = backend.clipboard();
    const std::string sub = w.size() >= 2 ? w[1] : "";
    auto note = [&](const std::string &what) {
        if (log) log("[req] " + peer.name + ": clipboard " + what);
    };
    if (sub == "set") {
        /* the text is everything after "clipboard set ", spaces kept as they are */
        const size_t at = std::string("clipboard set ").size();
        if (line.size() < at + 1 || line.compare(0, at, "clipboard set ") != 0) {
            note("set 0 bytes");
            return "err bad";
        }
        const std::string text = line.substr(at);
        note("set " + std::to_string(text.size()) + " bytes");
        if (!clip.set(text, now_ms)) return "err bad";
        return "ok " + std::to_string(text.size());
    }
    if (sub == "set-len") {
        if (!framed || !payload) return "err bad"; /* no payload: the transport has no framing */
        note("set-len " + std::to_string(payload->size()) + " bytes");
        if (!clipboard_text_ok(*payload)) return "err bad";
        if (!clip.set(*payload, now_ms)) return "err bad";
        return "ok " + std::to_string(payload->size());
    }
    if (sub == "get" && w.size() == 2) {
        note("get");
        std::string text;
        if (!clip.get(text, now_ms)) return "err empty";
        const bool one_line = text.find_first_of("\n\t") == std::string::npos && 3 + text.size() <= kLineMax;
        if (!one_line) return "err size"; /* does not fit one reply line: ask with get-len */
        return "ok " + text;
    }
    if (sub == "get-len" && w.size() == 2) {
        note("get-len");
        std::string text;
        if (!clip.get(text, now_ms)) return "err empty";
        return "ok " + std::to_string(text.size()) + "\n" + text;
    }
    if (sub == "clear" && w.size() == 2) {
        note("clear");
        clip.clear();
        return "ok";
    }
    note("?");
    return "err bad";
}

std::string handle_line(const std::string &line, const Peer &peer, Backend &backend, RateLimiter &limiter,
                        long long now_ms, void (*log)(const std::string &))
{
    if (line.size() > kLineMax || !printable_utf8(line)) return "err bad";
    const std::vector<std::string> w = words(line);
    if (w.empty()) return "err bad";
    const std::string &verb = w[0];
    const bool known = verb == "hello" || verb == "caps" || verb == "beep" || verb == "toast" || verb == "volume" ||
                       verb == "brightness" || verb == "dim" || verb == "screen" || verb == "clipboard";
    if (!known) return "err unknown";
    if (verb == "clipboard") {
        /* the text must never reach the log, not even in an error: the line is not logged, a summary is */
        size_t bytes = 0;
        const int framed = payload_length(line, bytes);
        if (framed < 0) return framed == -2 ? "err size" : "err bad"; /* nothing is logged: no text was read */
        if (!limiter.allow(peer.pid, verb, now_ms)) return "err limit";
        return clipboard_request(line, w, nullptr, framed == 1, peer, backend, now_ms, log);
    }
    if (!limiter.allow(peer.pid, verb, now_ms)) return "err limit";
    if (log) log("[req] " + peer.name + ": " + line);

    if (verb == "hello") {
        if (w.size() > 2) return "err bad";
        if (w.size() == 2) {
            int level = 0;
            if (!percent(w[1], level)) return "err bad";
        }
        return backend.hello(peer);
    }
    if (verb == "caps") return w.size() == 1 ? backend.caps() : "err bad";
    if (verb == "beep") {
        if (w.size() > 2) return "err bad";
        const std::string name = w.size() == 2 ? w[1] : "notify";
        if (!name_ok(name) || (name != "notify" && name != "done" && name != "warn")) return "err bad";
        return backend.beep(name);
    }
    if (verb == "toast") {
        /* the text is everything after the verb, spaces kept, ends trimmed */
        size_t at = verb.size();
        while (at < line.size() && line[at] == ' ') ++at;
        size_t end = line.size();
        while (end > at && line[end - 1] == ' ') --end;
        if (at >= end) return "err bad";
        return backend.toast(cut_chars(line.substr(at, end - at), kToastChars));
    }
    if (verb == "volume") {
        if (w.size() == 2 && (w[1] == "get" || w[1] == "up" || w[1] == "down" || w[1] == "mute" || w[1] == "unmute"))
            return backend.volume(w[1], 0);
        int v = 0;
        if (w.size() == 3 && w[1] == "set" && percent(w[2], v)) return backend.volume("set", v);
        return "err bad";
    }
    if (verb == "brightness") {
        if (w.size() == 2 && w[1] == "get") return backend.brightness("get", 0);
        int v = 0;
        if (w.size() == 3 && w[1] == "set" && percent(w[2], v)) return backend.brightness("set", v);
        return "err bad";
    }
    if (verb == "screen") {
        if (w.size() == 2 && (w[1] == "off" || w[1] == "on")) return backend.screen(w[1] == "on", peer);
        return "err bad";
    }
    /* dim */
    if (w.size() == 2 && (w[1] == "allow" || w[1] == "deny")) return backend.dim(w[1] == "allow", peer);
    return "err bad";
}

std::string handle_payload(const std::string &line, const std::string &payload, const Peer &peer, Backend &backend,
                           RateLimiter &limiter, long long now_ms, void (*log)(const std::string &))
{
    size_t bytes = 0;
    if (line.size() > kLineMax || !printable_utf8(line)) return "err bad";
    if (payload_length(line, bytes) != 1 || payload.size() != bytes) return "err bad";
    if (!limiter.allow(peer.pid, "clipboard", now_ms)) return "err limit";
    return clipboard_request(line, words(line), &payload, true, peer, backend, now_ms, log);
}

} // namespace app_request
