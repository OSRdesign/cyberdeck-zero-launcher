/*
 * SPDX-License-Identifier: MIT
 *
 * The app side of launcher.sock (report 035, docs/APP-CONTRACT.md section 14): one request line, one reply line, to the
 * launcher that started the app (or to the launcher of the same user, from ssh). Pure C99 + POSIX sockets, no LVGL, no
 * cp0 runtime: copy this file and src/cp0_app_client.c into an app (or build in the launcher tree).
 *
 * The socket is $XDG_RUNTIME_DIR/applaunch/launcher.sock (owner only, mode 0600). There is no /tmp fallback: without
 * XDG_RUNTIME_DIR there is no service. $APPLAUNCH_SOCKET replaces the path (tests).
 *
 * Degrade rules: a missing socket, a refused connection (the launcher is gone), an older launcher, a slow launcher
 * all give -1 within timeout_ms (default 200); nothing blocks longer, nothing is printed, nothing is retried. The app
 * hides what depends on the service and carries on. Never call it from a UI thread that must not stall for 200 ms:
 * use a worker thread (the toolkit wrapper does).
 *
 * Verbs (protocol level 1; the launcher answers `err unknown` to the others, board.state `requests=` lists what this
 * board answers):
 *   hello [1]                 ok 1 launcher=<version> app=<name> verbs=<list>
 *   beep [notify|done|warn]   ok | err nosound
 *   toast <text>              ok | err limit        (the launcher's toast; shown when the launcher is in front)
 *   volume get|set N|up|down|mute|unmute      ok <0..100> muted=<0|1> | err nosound
 *   brightness get|set N      ok <0..100> kind=levels|onoff | err absent
 *   dim allow|deny            ok | err absent       (idle dimming while this app is in front; battery boards only)
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_APP_CLIENT_SOCKET_FILE "launcher.sock"
#define CP0_APP_CLIENT_ENV_SOCKET "APPLAUNCH_SOCKET"
#define CP0_APP_CLIENT_TIMEOUT_MS 200
#define CP0_APP_CLIENT_LINE_MAX 256 /* request and reply, with the newline */
#define CP0_APP_CLIENT_PROTOCOL 1

/* The socket path: $APPLAUNCH_SOCKET, else $XDG_RUNTIME_DIR/applaunch/launcher.sock. 0, or -1 (no service possible:
 * no runtime directory, or buf too small). */
int cp0_app_client_path(char *buf, size_t size);

/* Sends `line` (no newline; at most 254 bytes) and reads the reply line into `reply` (no newline; NUL-terminated, cut to
 * `size`; may be NULL). timeout_ms <= 0: CP0_APP_CLIENT_TIMEOUT_MS. Returns 0 for a reply starting with "ok", 1 for
 * "err ...", -1 for no service, I/O error, timeout or a malformed reply. */
int cp0_app_client_call(const char *line, char *reply, size_t size, int timeout_ms);

/* One-shot helpers. name NULL: "notify". 0 sent and accepted, 1 refused (no sound output, rate limit), -1 no service. */
int cp0_app_beep(const char *name);
int cp0_app_toast(const char *text);

#ifdef __cplusplus
}
#endif
