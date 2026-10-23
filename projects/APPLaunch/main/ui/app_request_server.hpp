/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher.sock server (report 035): a Unix stream socket in the state directory, served by a thread of its own
 * (independent of LVGL and of the supervisor, so it works in every app mode and while the legacy runner blocks the
 * LVGL thread). One request line, one reply line; a connection may send several requests one after the other.
 *
 *   - mode 0600, owner only; a connection whose peer uid is not the launcher's own (or root) is closed at once;
 *   - a stale socket of an earlier run is removed at start; a live one (another launcher) or a file that is not a
 *     socket is left alone and the server does not start;
 *   - at most kMaxClients connections (more get "err busy" and are closed), an idle one is closed after kIdleMs;
 *   - a request line longer than kLineMax bytes gets "err bad" and the connection is closed.
 * Linux only; elsewhere start() returns false and stop() does nothing.
 */
#pragma once

#include <functional>
#include <string>

#include "app_request.hpp"

namespace app_request {

constexpr int kMaxClients = 8;
constexpr int kIdleMs = 30000;

struct ServerConfig {
    std::string path;   /* the socket, e.g. $XDG_RUNTIME_DIR/applaunch/launcher.sock (no /tmp fallback) */
    /* One request line to its reply line, on the server thread. */
    std::function<std::string(const std::string &line, const Peer &peer)> handler;
    std::function<void(const std::string &)> log; /* start-up and error messages; may be empty */
    int idle_ms = kIdleMs;
};

/* Starts the thread. False (and a log line) when the socket cannot be set up. Twice does nothing. */
bool start_server(ServerConfig config);
/* Stops the thread, closes every connection and removes the socket. */
void stop_server();
bool server_running();

} // namespace app_request
