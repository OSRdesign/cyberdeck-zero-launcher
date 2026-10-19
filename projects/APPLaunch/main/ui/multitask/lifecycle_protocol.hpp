/*
 * SPDX-License-Identifier: MIT
 *
 * The lifecycle control protocol between the launcher and an app that declares X-Background=run|freeze (task 015,
 * docs/APP-CONTRACT.md section 13): ASCII datagrams of at most 64 bytes on a SOCK_SEQPACKET socket the app inherits
 * as APPLAUNCH_CONTROL_FD.
 *   launcher -> app   "hide <n>"  "show <n>"  "close <n>"
 *   app -> launcher   "ack <n>"   "home"      "switcher"
 * Pure: no system call. Unit-tested by tests/test_lifecycle_protocol.cpp.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace multitask {
namespace protocol {

inline constexpr const char *kEnvLifecycle = "APPLAUNCH_LIFECYCLE";
inline constexpr const char *kEnvControlFd = "APPLAUNCH_CONTROL_FD";
inline constexpr std::size_t kMaxMessage = 64;

enum class Kind : std::uint8_t { Unknown, Hide, Show, Close, Ack, Home, Switcher };

struct Message {
    Kind kind = Kind::Unknown;
    std::uint32_t n = 0;
    bool has_n = false;
};

const char *kind_name(Kind kind);

/* "hide 3"; Home and Switcher carry no number. */
std::string format(Kind kind, std::uint32_t n);

/* One datagram. A trailing '\n' or NUL is ignored. Hide, Show, Close and Ack need exactly one decimal number
 * (0..4294967295); Home and Switcher take none (a number after them is tolerated and ignored). Anything else, or a
 * datagram longer than kMaxMessage, is Unknown. */
Message parse(const char *data, std::size_t len);

/* "export APPLAUNCH_LIFECYCLE=1 APPLAUNCH_CONTROL_FD=<fd>; " for the front of the app's sh -c command line. */
std::string env_exports(int fd);

} // namespace protocol
} // namespace multitask
