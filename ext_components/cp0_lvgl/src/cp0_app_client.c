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

int cp0_app_client_call(const char *line, char *reply, size_t size, int timeout_ms)
{
    if (reply && size) reply[0] = '\0';
    if (!line) return -1;
    const size_t len = strlen(line);
    if (len == 0 || len > CP0_APP_CLIENT_LINE_MAX - 2) return -1;
    if (strchr(line, '\n')) return -1;
    struct sockaddr_un addr;
    char path[sizeof(addr.sun_path)];
    if (cp0_app_client_path(path, sizeof(path)) != 0) return -1;
    if (timeout_ms <= 0) timeout_ms = CP0_APP_CLIENT_TIMEOUT_MS;
    const long long deadline = now_ms() + timeout_ms;

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    (void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, sizeof(addr.sun_path)); /* path is NUL-terminated and as long as sun_path */
    int result = -1;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) goto done; /* ENOENT, ECONNREFUSED, EAGAIN: busy */

    char out[CP0_APP_CLIENT_LINE_MAX];
    memcpy(out, line, len);
    out[len] = '\n';
    size_t sent = 0;
    while (sent < len + 1) {
        const ssize_t n = send(fd, out + sent, len + 1 - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (wait_fd(fd, POLLOUT, deadline) != 1) goto done;
        } else {
            goto done;
        }
    }

    char in[CP0_APP_CLIENT_LINE_MAX];
    size_t have = 0;
    for (;;) {
        char *nl = memchr(in, '\n', have);
        if (nl) {
            *nl = '\0';
            have = (size_t)(nl - in);
            break;
        }
        if (have >= sizeof(in) - 1) goto done; /* a reply that long is not ours */
        const ssize_t n = recv(fd, in + have, sizeof(in) - 1 - have, 0);
        if (n > 0) {
            have += (size_t)n;
        } else if (n == 0) {
            goto done;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (wait_fd(fd, POLLIN, deadline) != 1) goto done;
        } else {
            goto done;
        }
    }
    in[have] = '\0';
    if (have > 0 && in[have - 1] == '\r') in[--have] = '\0';
    if (reply && size) snprintf(reply, size, "%s", in);
    if (strncmp(in, "ok", 2) == 0 && (in[2] == '\0' || in[2] == ' '))
        result = 0;
    else if (strncmp(in, "err", 3) == 0 && (in[3] == '\0' || in[3] == ' '))
        result = 1;
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
