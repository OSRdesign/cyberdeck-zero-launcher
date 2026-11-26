/*
 * SPDX-License-Identifier: MIT
 *
 * The app side of launcher.sock (see cp0_app_client.h).
 */
#define _POSIX_C_SOURCE 200809L
#include "cp0_app_client.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int cp0_app_client_path(char *buf, size_t size)
{
    if (!buf || size == 0) return -1;
    buf[0] = '\0';
    const char *override = getenv(CP0_APP_CLIENT_ENV_SOCKET);
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    int n;
    if (override && override[0])
        n = snprintf(buf, size, "%s", override);
    else if (runtime && runtime[0])
        n = snprintf(buf, size, "%s/applaunch/" CP0_APP_CLIENT_SOCKET_FILE, runtime);
    else
        return -1;
    if (n < 0 || (size_t)n >= size || (size_t)n >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

static long long now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* waits for `events` on fd until `deadline`; 1 ready, 0 timeout, -1 error */
static int wait_fd(int fd, short events, long long deadline)
{
    for (;;) {
        long long left = deadline - now_ms();
        if (left < 0) left = 0;
        struct pollfd p = {fd, events, 0};
        const int r = poll(&p, 1, (int)left);
        if (r > 0) return (p.revents & (events | POLLHUP)) ? 1 : -1;
        if (r == 0) return 0;
        if (errno != EINTR) return -1;
    }
}

/* A connected non-blocking socket to the launcher, or -1 (no service). */
static int connect_fd(void)
{
    struct sockaddr_un addr;
    char path[sizeof(addr.sun_path)];
    if (cp0_app_client_path(path, sizeof(path)) != 0) return -1;
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    (void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, sizeof(addr.sun_path)); /* path is NUL-terminated and as long as sun_path */
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { /* ENOENT, ECONNREFUSED, EAGAIN: busy */
        close(fd);
        return -1;
    }
    return fd;
}

/* All of buf, until `deadline`. 0 sent, -1 failed or timed out. */
static int send_all(int fd, const char *buf, size_t len, long long deadline)
{
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (wait_fd(fd, POLLOUT, deadline) != 1) return -1;
        } else {
            return -1;
        }
    }
    return 0;
}

/* Reads into in[0..cap) until in holds `want` bytes (want <= cap), or, when want is 0, until it holds a newline. `*have`
 * counts the bytes held. 0 done, -1 closed, full, timeout or error. */
static int fill(int fd, char *in, size_t cap, size_t *have, size_t want, long long deadline)
{
    for (;;) {
        if (want ? *have >= want : memchr(in, '\n', *have) != NULL) return 0;
        if (*have >= cap) return -1;
        const ssize_t n = recv(fd, in + *have, cap - *have, 0);
        if (n > 0) {
            *have += (size_t)n;
        } else if (n == 0) {
            return -1;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (wait_fd(fd, POLLIN, deadline) != 1) return -1;
        } else {
            return -1;
        }
    }
}

/* "ok" or "ok ..." -> 0, "err" or "err ..." -> 1, else -1 */
static int classify(const char *in)
{
    if (strncmp(in, "ok", 2) == 0 && (in[2] == '\0' || in[2] == ' ')) return 0;
    if (strncmp(in, "err", 3) == 0 && (in[3] == '\0' || in[3] == ' ')) return 1;
    return -1;
}

int cp0_app_client_call(const char *line, char *reply, size_t size, int timeout_ms)
{
    if (reply && size) reply[0] = '\0';
    if (!line) return -1;
    const size_t len = strlen(line);
    if (len == 0 || len > CP0_APP_CLIENT_LINE_MAX - 2) return -1;
    if (strchr(line, '\n')) return -1;
    if (timeout_ms <= 0) timeout_ms = CP0_APP_CLIENT_TIMEOUT_MS;
    const long long deadline = now_ms() + timeout_ms;

    const int fd = connect_fd();
    if (fd < 0) return -1;
    int result = -1;
    char out[CP0_APP_CLIENT_LINE_MAX];
    memcpy(out, line, len);
    out[len] = '\n';
    char in[CP0_APP_CLIENT_LINE_MAX];
    size_t have = 0;
    if (send_all(fd, out, len + 1, deadline) != 0) goto done;
    if (fill(fd, in, sizeof(in) - 1, &have, 0, deadline) != 0) goto done; /* a reply that long is not ours */
    {
        char *nl = memchr(in, '\n', have);
        have = (size_t)(nl - in);
        in[have] = '\0';
    }
    if (have > 0 && in[have - 1] == '\r') in[--have] = '\0';
    if (reply && size) snprintf(reply, size, "%s", in);
    result = classify(in);
done:
    close(fd);
    return result;
}

int cp0_app_beep(const char *name)
{
    char line[64];
    if (name && name[0]) {
        if (strlen(name) > 24) return -1;
        snprintf(line, sizeof(line), "beep %s", name);
    } else {
        snprintf(line, sizeof(line), "beep");
    }
    return cp0_app_client_call(line, NULL, 0, 0);
}

int cp0_app_toast(const char *text)
{
    if (!text || !text[0]) return -1;
    char line[CP0_APP_CLIENT_LINE_MAX];
    const int n = snprintf(line, sizeof(line), "toast %s", text);
    if (n < 0) return -1;
    if (n > 250) line[250] = '\0'; /* cut: the launcher cuts the text at 160 characters anyway */
    for (char *p = line; *p; p++)
        if (*p == '\n' || *p == '\r') *p = ' ';
    return cp0_app_client_call(line, NULL, 0, 0);
}

int cp0_app_backlight(int on)
{
    /* its own, longer wait: the launcher answers on its UI loop, which a slow board or a frame being drawn can keep busy for
     * more than the 200 ms default; a "no answer" there would make the app think a request failed that is about to be done */
    return cp0_app_client_call(on ? "screen on" : "screen off", NULL, 0, CP0_APP_BACKLIGHT_TIMEOUT_MS);
}

/* ---- the clipboard (docs/APP-CONTRACT.md section 14) */

/* the one-line form is used for text of one printable line up to this long; anything else goes length-prefixed */
#define CLIP_LINE_BYTES 230

static int clip_result(int r, const char *reply)
{
    if (r == 0) return CP0_APP_CLIPBOARD_OK;
    if (r == 1) return strcmp(reply, "err unknown") == 0 ? CP0_APP_CLIPBOARD_UNSUPPORTED : CP0_APP_CLIPBOARD_REFUSED;
    return CP0_APP_CLIPBOARD_NOSERVICE;
}

int cp0_app_clipboard_set(const char *text, size_t len, int timeout_ms)
{
    if (!text || len == 0 || len > CP0_APP_CLIPBOARD_MAX) return CP0_APP_CLIPBOARD_REFUSED;
    char reply[CP0_APP_CLIENT_LINE_MAX];
    int one_line = len <= CLIP_LINE_BYTES;
    for (size_t i = 0; one_line && i < len; i++)
        if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] == 0x7f) one_line = 0;
    if (one_line) {
        char line[CP0_APP_CLIENT_LINE_MAX];
        memcpy(line, "clipboard set ", 14);
        memcpy(line + 14, text, len);
        line[14 + len] = '\0';
        const int r = cp0_app_client_call(line, reply, sizeof(reply), timeout_ms);
        memset(line, 0, sizeof(line));
        return clip_result(r, reply);
    }
    if (timeout_ms <= 0) timeout_ms = CP0_APP_CLIENT_TIMEOUT_MS;
    const long long deadline = now_ms() + timeout_ms;
    const int fd = connect_fd();
    if (fd < 0) return CP0_APP_CLIPBOARD_NOSERVICE;
    int result = CP0_APP_CLIPBOARD_NOSERVICE;
    char head[48];
    char in[CP0_APP_CLIENT_LINE_MAX];
    size_t have = 0;
    const int hn = snprintf(head, sizeof(head), "clipboard set-len %zu\n", len);
    /* the header line, the text and its newline: one request */
    if (hn <= 0 || send_all(fd, head, (size_t)hn, deadline) != 0 || send_all(fd, text, len, deadline) != 0 ||
        send_all(fd, "\n", 1, deadline) != 0)
        goto done;
    if (fill(fd, in, sizeof(in) - 1, &have, 0, deadline) != 0) goto done;
    {
        char *nl = memchr(in, '\n', have);
        have = (size_t)(nl - in);
        in[have] = '\0';
    }
    if (have > 0 && in[have - 1] == '\r') in[--have] = '\0';
    result = clip_result(classify(in), in);
done:
    close(fd);
    return result;
}

int cp0_app_clipboard_get(char *buf, size_t size, size_t *len, int timeout_ms)
{
    if (len) *len = 0;
    if (buf && size) buf[0] = '\0';
    if (!buf || size == 0) return CP0_APP_CLIPBOARD_REFUSED;
    if (timeout_ms <= 0) timeout_ms = CP0_APP_CLIENT_TIMEOUT_MS;
    const long long deadline = now_ms() + timeout_ms;
    const int fd = connect_fd();
    if (fd < 0) return CP0_APP_CLIPBOARD_NOSERVICE;
    int result = CP0_APP_CLIPBOARD_NOSERVICE;
    char in[CP0_APP_CLIPBOARD_MAX + 64];
    size_t have = 0;
    char head[CP0_APP_CLIENT_LINE_MAX];
    if (send_all(fd, "clipboard get-len\n", 18, deadline) != 0) goto done;
    if (fill(fd, in, sizeof(in), &have, 0, deadline) != 0) goto done;
    {
        const char *nl = memchr(in, '\n', have);
        const size_t head_len = (size_t)(nl - in);
        if (head_len >= sizeof(head)) goto done;
        memcpy(head, in, head_len);
        head[head_len] = '\0';
        if (head_len > 0 && head[head_len - 1] == '\r') head[head_len - 1] = '\0';
        const int r = classify(head);
        if (r < 0) goto done;
        if (r == 1) {
            if (strcmp(head, "err empty") == 0) result = CP0_APP_CLIPBOARD_OK; /* nothing there: *len stays 0 */
            else result = strcmp(head, "err unknown") == 0 ? CP0_APP_CLIPBOARD_UNSUPPORTED : CP0_APP_CLIPBOARD_REFUSED;
            goto done;
        }
        char *end = NULL;
        const unsigned long n = strtoul(head[2] == ' ' ? head + 3 : head + 2, &end, 10);
        if (!end || *end != '\0' || n == 0 || n > CP0_APP_CLIPBOARD_MAX) goto done;
        /* the text and its newline follow the header line; part of them may have come with it */
        const size_t after = head_len + 1;
        if (fill(fd, in, sizeof(in), &have, after + (size_t)n + 1, deadline) != 0) goto done;
        if (in[after + n] != '\n') goto done;
        size_t take = (size_t)n;
        if (take > size - 1) { /* the caller's buffer is smaller: cut at a character boundary */
            take = size - 1;
            while (take > 0 && ((unsigned char)in[after + take] & 0xC0) == 0x80) take--;
        }
        memcpy(buf, in + after, take);
        buf[take] = '\0';
        if (len) *len = take;
        result = CP0_APP_CLIPBOARD_OK;
    }
done:
    memset(in, 0, sizeof(in));
    close(fd);
    return result;
}

int cp0_app_clipboard_clear(int timeout_ms)
{
    char reply[CP0_APP_CLIENT_LINE_MAX];
    const int r = cp0_app_client_call("clipboard clear", reply, sizeof(reply), timeout_ms);
    return clip_result(r, reply);
}
