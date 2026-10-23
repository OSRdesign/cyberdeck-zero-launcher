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

bool name_ok(const std::string &s)
{
    if (s.empty() || s.size() > kBeepNameMax) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

} // namespace

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
    if (verb == "volume" || verb == "brightness" || verb == "dim") return 100;
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

std::string handle_line(const std::string &line, const Peer &peer, Backend &backend, RateLimiter &limiter,
                        long long now_ms, void (*log)(const std::string &))
{
    if (line.size() > kLineMax || !printable_utf8(line)) return "err bad";
    const std::vector<std::string> w = words(line);
    if (w.empty()) return "err bad";
    const std::string &verb = w[0];
    const bool known = verb == "hello" || verb == "caps" || verb == "beep" || verb == "toast" || verb == "volume" ||
                       verb == "brightness" || verb == "dim";
    if (!known) return "err unknown";
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
    /* dim */
    if (w.size() == 2 && (w[1] == "allow" || w[1] == "deny")) return backend.dim(w[1] == "allow", peer);
    return "err bad";
}

} // namespace app_request
