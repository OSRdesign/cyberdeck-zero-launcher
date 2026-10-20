/*
 * SPDX-License-Identifier: MIT
 *
 * The lifecycle control protocol (see lifecycle_protocol.hpp).
 */
#include "lifecycle_protocol.hpp"

#include <cstring>

namespace multitask {
namespace protocol {

namespace {

struct Word {
    const char *text;
    Kind kind;
    bool needs_n;
};

constexpr Word kWords[] = {
    {"hide", Kind::Hide, true},   {"show", Kind::Show, true}, {"close", Kind::Close, true},
    {"ack", Kind::Ack, true},     {"home", Kind::Home, false}, {"switcher", Kind::Switcher, false},
};

bool parse_number(const char *p, std::size_t len, std::uint32_t &out)
{
    if (len == 0 || len > 10) return false;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < len; ++i) {
        if (p[i] < '0' || p[i] > '9') return false;
        value = value * 10 + static_cast<std::uint64_t>(p[i] - '0');
    }
    if (value > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

} // namespace

const char *kind_name(Kind kind)
{
    for (const Word &w : kWords)
        if (w.kind == kind) return w.text;
    return "unknown";
}

std::string format(Kind kind, std::uint32_t n)
{
    std::string text = kind_name(kind);
    if (kind == Kind::Hide || kind == Kind::Show || kind == Kind::Close || kind == Kind::Ack)
        text += " " + std::to_string(n);
    return text;
}

Message parse(const char *data, std::size_t len)
{
    Message message;
    if (!data || len == 0 || len > kMaxMessage) return message;
    while (len > 0 && (data[len - 1] == '\n' || data[len - 1] == '\0')) --len;
    if (len == 0) return message;
    std::size_t word_len = 0;
    while (word_len < len && data[word_len] != ' ') ++word_len;
    for (const Word &w : kWords) {
        if (std::strlen(w.text) != word_len || std::memcmp(w.text, data, word_len) != 0) continue;
        const bool has_rest = word_len < len;
        if (w.needs_n) {
            if (!has_rest) return message;
            std::uint32_t n = 0;
            if (!parse_number(data + word_len + 1, len - word_len - 1, n)) return message;
            message.kind = w.kind;
            message.n = n;
            message.has_n = true;
            return message;
        }
        if (has_rest) {
            std::uint32_t n = 0;
            if (!parse_number(data + word_len + 1, len - word_len - 1, n)) return message;
        }
        message.kind = w.kind;
        return message;
    }
    return message;
}

std::string env_exports(int fd)
{
    return std::string("export ") + kEnvLifecycle + "=1 " + kEnvControlFd + "=" + std::to_string(fd) + "; ";
}

} // namespace protocol
} // namespace multitask
