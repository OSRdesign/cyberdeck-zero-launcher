/*
 * SPDX-License-Identifier: MIT
 *
 * The app side of the lifecycle protocol (see cp0_app_lifecycle.h). Pure C99 + POSIX; unit-tested on the PC by
 * tests/test_app_lifecycle.c.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* MSG_NOSIGNAL, MSG_DONTWAIT under -std=c11 */
#endif
#include "cp0_app_lifecycle.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* at most this many messages per service call: the launcher sends one at a time and waits for the answer */
#define LC_BURST 32

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* a system without it (the emulator on macOS) never runs a multitasking launcher */
#endif

static int parse_number(const char *p, size_t len, uint32_t *out)
{
    if (len == 0 || len > 10) return 0;
    uint64_t value = 0;
    for (size_t i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') return 0;
        value = value * 10 + (uint64_t)(p[i] - '0');
    }
    if (value > 0xFFFFFFFFull) return 0;
    *out = (uint32_t)value;
    return 1;
}

cp0_app_lc_msg_t cp0_app_lifecycle_parse(const char *data, size_t len, uint32_t *n)
{
    static const struct {
        const char *word;
        cp0_app_lc_msg_t kind;
    } words[] = {{"hide", CP0_APP_LC_HIDE}, {"show", CP0_APP_LC_SHOW}, {"close", CP0_APP_LC_CLOSE}};
    if (!data || len == 0 || len > CP0_APP_LIFECYCLE_MSG_MAX) return CP0_APP_LC_NONE;
    while (len > 0 && (data[len - 1] == '\n' || data[len - 1] == '\0')) len--;
    size_t word_len = 0;
    while (word_len < len && data[word_len] != ' ') word_len++;
    if (word_len >= len) return CP0_APP_LC_NONE; /* every launcher message carries its number */
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (strlen(words[i].word) != word_len || memcmp(words[i].word, data, word_len) != 0) continue;
        uint32_t value = 0;
        if (!parse_number(data + word_len + 1, len - word_len - 1, &value)) return CP0_APP_LC_NONE;
        if (n) *n = value;
        return words[i].kind;
    }
    return CP0_APP_LC_NONE;
}

int cp0_app_lifecycle_attach(cp0_app_lifecycle_t *lc, int fd, const cp0_app_lifecycle_ops_t *ops, void *ctx)
{
    memset(lc, 0, sizeof(*lc));
    lc->fd = -1;
    lc->ops = ops;
    lc->ctx = ctx;
    if (fd < 0) return 0;
    lc->fd = fd;
    const int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    const int fdfl = fcntl(fd, F_GETFD);
    if (fdfl >= 0) (void)fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC);
    return 1;
}

int cp0_app_lifecycle_init(cp0_app_lifecycle_t *lc, const cp0_app_lifecycle_ops_t *ops, void *ctx)
{
    (void)cp0_app_lifecycle_attach(lc, -1, ops, ctx);
    const char *on = getenv(CP0_APP_LIFECYCLE_ENV);
    const char *fd_text = getenv(CP0_APP_LIFECYCLE_FD_ENV);
    if (!on || strcmp(on, "1") != 0 || !fd_text) return 0;
    uint32_t fd = 0;
    if (!parse_number(fd_text, strlen(fd_text), &fd) || fd > 1048576u) return 0;
    struct stat st;
    if (fstat((int)fd, &st) != 0 || !S_ISSOCK(st.st_mode)) return 0; /* not ours: a stale variable, an fd reused */
    return cp0_app_lifecycle_attach(lc, (int)fd, ops, ctx);
}

static void answer(cp0_app_lifecycle_t *lc, uint32_t n)
{
    char msg[24];
    const int len = snprintf(msg, sizeof(msg), "ack %u", (unsigned)n);
    if (len <= 0) return;
    ssize_t r;
    do {
        r = send(lc->fd, msg, (size_t)len, MSG_NOSIGNAL | MSG_DONTWAIT);
    } while (r < 0 && errno == EINTR);
    if (r == len) lc->acks++;
}

/* The launcher's end is gone (it crashed or closed the socket). */
static int hang_up(cp0_app_lifecycle_t *lc)
{
    close(lc->fd);
    lc->fd = -1;
    if (lc->hidden && !lc->closing) {
        /* hidden, and nobody left to show it again: end as if closed */
        lc->closing = 1;
        if (lc->ops && lc->ops->close) lc->ops->close(lc->ctx, lc->last_n);
    }
    return -1;
}

int cp0_app_lifecycle_service(cp0_app_lifecycle_t *lc)
{
    if (!lc || lc->fd < 0) return 0;
    int handled = 0;
    while (handled < LC_BURST && !lc->closing) {
        char buf[CP0_APP_LIFECYCLE_MSG_MAX * 2];
        const ssize_t r = recv(lc->fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (r <= 0) return hang_up(lc); /* 0: end of file on a SOCK_SEQPACKET socket; < 0: reset */
        handled++;
        uint32_t n = 0;
        const cp0_app_lc_msg_t kind = cp0_app_lifecycle_parse(buf, (size_t)r, &n);
        if (kind == CP0_APP_LC_NONE) continue;
        lc->last_n = n;
        if (kind == CP0_APP_LC_HIDE) {
            if (!lc->hidden) {
                if (lc->ops && lc->ops->hide) lc->ops->hide(lc->ctx, n);
                lc->hidden = 1;
                lc->hides++;
            }
            answer(lc, n);
        } else if (kind == CP0_APP_LC_SHOW) {
            if (lc->hidden) {
                if (lc->ops && lc->ops->show) lc->ops->show(lc->ctx, n);
                lc->hidden = 0;
                lc->shows++;
            }
            answer(lc, n);
        } else {
            lc->closing = 1;
            if (lc->ops && lc->ops->close) lc->ops->close(lc->ctx, n);
        }
    }
    return handled;
}

int cp0_app_lifecycle_send(cp0_app_lifecycle_t *lc, const char *msg)
{
    if (!lc || lc->fd < 0 || !msg) return -1;
    const size_t len = strlen(msg);
    if (len == 0 || len > CP0_APP_LIFECYCLE_MSG_MAX) return -1;
    ssize_t r;
    do {
        r = send(lc->fd, msg, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    } while (r < 0 && errno == EINTR);
    return r == (ssize_t)len ? 0 : -1;
}

void cp0_app_lifecycle_close(cp0_app_lifecycle_t *lc)
{
    if (!lc || lc->fd < 0) return;
    close(lc->fd);
    lc->fd = -1;
}

/* ------------------------------------------------------------------------------ keys */

void cp0_app_lifecycle_keys_shown(cp0_app_lc_keys_t *k)
{
    memset(k->down, 0, sizeof(k->down));
    k->after_show = 1;
}

int cp0_app_lifecycle_key_drop(cp0_app_lc_keys_t *k, int hidden, uint32_t code, int state, int alt)
{
    if (hidden) return 1;
    if (code == CP0_APP_LC_KEY_TAB && alt) return 1;
    if (code == 0 || code >= sizeof(k->down) * 8u) return 0;
    const unsigned char bit = (unsigned char)(1u << (code % 8u));
    unsigned char *byte = &k->down[code / 8u];
    if (state == CP0_APP_LC_KEY_PRESSED) {
        *byte |= bit;
        return 0;
    }
    const int was_down = (*byte & bit) != 0;
    if (state == CP0_APP_LC_KEY_RELEASED) *byte &= (unsigned char)~bit;
    return k->after_show && !was_down;
}
