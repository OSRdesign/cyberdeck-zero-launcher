/*
 * SPDX-License-Identifier: MIT
 *
 * Screen state for the processes the launcher starts (see cp0_ui_metrics.h): environment variables and the
 * $XDG_RUNTIME_DIR/applaunch/screen.state file, written by atomic rename like input.state (cp0_input_state.c).
 * Device builds only.
 */

#define _GNU_SOURCE
#include "cp0_ui_metrics.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int ensure_parent_dir(const char *path)
{
    char dir[PATH_MAX];
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) return 0;
    const size_t len = (size_t)(slash - path);
    if (len >= sizeof(dir)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    if (mkdir(dir, 0755) == 0 || errno == EEXIST) return 0;
    return -1;
}

int cp0_ui_state_write(const char *path, const char *text, size_t len)
{
    if (!path || !text) {
        errno = EINVAL;
        return -1;
    }
    char tmp[PATH_MAX];
    const int tmp_len = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid());
    if (tmp_len < 0 || (size_t)tmp_len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (ensure_parent_dir(path) != 0) return -1;

    const int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    size_t written = 0;
    while (written < len) {
        const ssize_t n = write(fd, text + written, len - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            const int saved = errno;
            close(fd);
            unlink(tmp);
            errno = saved ? saved : EIO;
            return -1;
        }
        written += (size_t)n;
    }
    (void)fchmod(fd, 0644); /* whatever the umask, apps of other users may read it */
    if (close(fd) != 0 || rename(tmp, path) != 0) {
        const int saved = errno;
        unlink(tmp);
        errno = saved;
        return -1;
    }
    return 0;
}

/* gen of the file a previous launcher run left, 0 when none */
static unsigned previous_gen(const char *path)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    char text[512];
    size_t len = 0;
    for (;;) {
        const ssize_t n = read(fd, text + len, sizeof(text) - len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        len += (size_t)n;
        if (len == sizeof(text)) break;
    }
    close(fd);
    return cp0_ui_state_parse_gen(text, len);
}

int cp0_ui_metrics_publish(const cp0_ui_metrics_t *m, const char *runtime_dir)
{
    if (!m) {
        errno = EINVAL;
        return -1;
    }
    char value[32];
    snprintf(value, sizeof(value), "%d", m->w);
    setenv("APPLAUNCH_SCREEN_W", value, 1);
    snprintf(value, sizeof(value), "%d", m->h);
    setenv("APPLAUNCH_SCREEN_H", value, 1);
    snprintf(value, sizeof(value), "%d", m->rot);
    setenv("APPLAUNCH_SCREEN_ROTATE", value, 1);
    snprintf(value, sizeof(value), "%d", m->ppmm_x100);
    setenv("APPLAUNCH_SCREEN_PPMM", value, 1);
    setenv("APPLAUNCH_SCREEN_CLASS", cp0_ui_class_name(m->cls), 1);

    char path[PATH_MAX];
    if (cp0_ui_state_path(runtime_dir, path, sizeof(path)) != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    char text[512];
    const int len = cp0_ui_state_format(m, previous_gen(path) + 1, text, sizeof(text));
    if (len < 0) {
        errno = EOVERFLOW;
        return -1;
    }
    return cp0_ui_state_write(path, text, (size_t)len);
}
