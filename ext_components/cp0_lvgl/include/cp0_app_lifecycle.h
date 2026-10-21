/*
 * SPDX-License-Identifier: MIT
 *
 * The app side of the launcher's lifecycle protocol (multitasking, docs/APP-CONTRACT.md section 13, contract 1): an app
 * whose .desktop file says X-Background=run|freeze and that a multitasking launcher started gets
 *   APPLAUNCH_LIFECYCLE=1, APPLAUNCH_CONTROL_FD=<n>   one end of a SOCK_SEQPACKET Unix socket
 * and is told, one ASCII datagram each (at most 64 bytes, a trailing newline or NUL tolerated):
 *   "hide <n>"   stop writing the framebuffer, cancel the press in progress, ignore input; answer "ack <n>" (300 ms)
 *   "show <n>"   drop the input that is still pending, reset the key and pointer state, repaint; answer "ack <n>"
 *   "close <n>"  save and quit (SIGTERM follows 3 s later)
 * and may send "home" (leave to the home grid, staying open) or "switcher" (open the app switcher).
 *
 * Pure C99 + POSIX (sockets, fcntl, getenv), no LVGL, no cp0 runtime: copy this file and src/cp0_app_lifecycle.c into
 * an app that draws by itself (a framebuffer shim), or build in the launcher tree. An app on cp0_lvgl uses
 * cp0_app_lifecycle_lvgl.h, which drives this from its LVGL loop. Unit-tested by tests/test_app_lifecycle.c.
 *
 * Without the variables (an older launcher, multitasking off, started by hand) nothing here is active and the app
 * behaves exactly as before: cp0_app_lifecycle_init() returns 0 and every other call is a no-op.
 */
#ifndef CP0_APP_LIFECYCLE_H
#define CP0_APP_LIFECYCLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_APP_LIFECYCLE_ENV "APPLAUNCH_LIFECYCLE"
#define CP0_APP_LIFECYCLE_FD_ENV "APPLAUNCH_CONTROL_FD"
#define CP0_APP_LIFECYCLE_MSG_MAX 64

typedef enum {
    CP0_APP_LC_NONE = 0, /* anything else: ignored (the protocol's rule for unknown messages) */
    CP0_APP_LC_HIDE = 1,
    CP0_APP_LC_SHOW = 2,
    CP0_APP_LC_CLOSE = 3,
} cp0_app_lc_msg_t;

/* One datagram from the launcher: "hide|show|close <n>", n decimal 0..4294967295, a trailing '\n' or NUL ignored, at
 * most CP0_APP_LIFECYCLE_MSG_MAX bytes. *n is set for a known message. */
cp0_app_lc_msg_t cp0_app_lifecycle_parse(const char *data, size_t len, uint32_t *n);

/* What the app does; each is called on the thread that calls cp0_app_lifecycle_service(), before the answer. */
typedef struct {
    /* hide: from its return on, no pixel is written to the framebuffer; the press in progress is cancelled (no
     * click), input is ignored. The answer "ack <n>" is sent right after (a freeze app is stopped then). */
    void (*hide)(void *ctx, uint32_t n);
    /* show: the launcher has put the app's last frame back and stopped drawing. Drop the input still pending, treat
     * every key as released (modifiers included), reset the pointer, repaint the whole screen; "ack <n>" follows. */
    void (*show)(void *ctx, uint32_t n);
    /* close: save and quit through the app's normal exit (the one of Esc at its root). No answer. */
    void (*close)(void *ctx, uint32_t n);
} cp0_app_lifecycle_ops_t;

typedef struct {
    int fd;                  /* the control socket; -1 = no lifecycle (today's behaviour) */
    int hidden;              /* 1 between a handled hide and the next show */
    int closing;             /* 1 once close was handled (or the launcher went away while the app was hidden) */
    uint32_t last_n;         /* the number of the last message handled */
    unsigned long hides, shows, acks;
    const cp0_app_lifecycle_ops_t *ops;
    void *ctx;
} cp0_app_lifecycle_t;

/* From the environment: APPLAUNCH_LIFECYCLE=1 and APPLAUNCH_CONTROL_FD naming an open socket. The socket is made
 * non-blocking and close-on-exec (helpers the app starts do not inherit it). Returns 1 when active, else 0 (lc->fd
 * -1: everything below does nothing). ops and ctx may be NULL. */
int cp0_app_lifecycle_init(cp0_app_lifecycle_t *lc, const cp0_app_lifecycle_ops_t *ops, void *ctx);

/* The same with a socket the caller has (tests). Returns 1, or 0 for fd < 0. */
int cp0_app_lifecycle_attach(cp0_app_lifecycle_t *lc, int fd, const cp0_app_lifecycle_ops_t *ops, void *ctx);

/* Non-blocking: handles every message waiting, in order (ops, then "ack <n>" for hide and show). A hide while hidden
 * and a show while visible only answer. Returns the number of messages handled (0: none waiting), or -1 when the
 * launcher's end is gone: the socket is closed and the lifecycle ends; when that happens while the app is hidden
 * nobody can bring it back, so ops->close is called (closing = 1). Call it on every turn of the app's main loop. */
int cp0_app_lifecycle_service(cp0_app_lifecycle_t *lc);

/* "home" or "switcher" (any message of at most CP0_APP_LIFECYCLE_MSG_MAX bytes). 0, or -1 (no lifecycle, or the
 * send failed). */
int cp0_app_lifecycle_send(cp0_app_lifecycle_t *lc, const char *msg);

/* Closes the socket (the launcher sees the end of the conversation; it learns of the exit from the process). */
void cp0_app_lifecycle_close(cp0_app_lifecycle_t *lc);

/* ------------------------------------------------------------------------------ keys (pure, LVGL thread or any) */

/* Which keys an app with a lifecycle drops (APP-CONTRACT section 13):
 *   - every key while hidden;
 *   - Tab with Alt held (pressed, repeated or released): Alt+Tab is the launcher's switcher, the app must not also
 *     move its focus;
 *   - after a show, the release or repeat of a key not pressed since that show: a key held when the app was hidden,
 *     whose press the app saw (Alt of the Alt+Tab that hid it), or a release the keyboard backend made up when it let
 *     the keyboards go. code 0 (text without a key: cp0_keyboard_inject_text) is never dropped for that. */
typedef struct {
    unsigned char down[96]; /* bit per key code 0..767 (KEY_CNT): pressed since the last show */
    int after_show;         /* a show was handled: orphan releases are dropped */
} cp0_app_lc_keys_t;

#define CP0_APP_LC_KEY_RELEASED 0
#define CP0_APP_LC_KEY_PRESSED 1
#define CP0_APP_LC_KEY_REPEATED 2
#define CP0_APP_LC_KEY_TAB 15 /* linux/input-event-codes.h KEY_TAB */

/* A show: forget which keys were down, drop orphan releases from now on. */
void cp0_app_lifecycle_keys_shown(cp0_app_lc_keys_t *k);
/* 1 = drop the key. state: CP0_APP_LC_KEY_*; alt: the Alt modifier is held. */
int cp0_app_lifecycle_key_drop(cp0_app_lc_keys_t *k, int hidden, uint32_t code, int state, int alt);

#ifdef __cplusplus
}
#endif

#endif /* CP0_APP_LIFECYCLE_H */
