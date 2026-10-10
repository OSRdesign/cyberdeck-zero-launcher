/*
 * Input policy file writer and reader (see cp0_input_state.h).
 */

#define _GNU_SOURCE
#include "cp0_input_state.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int cp0_input_state_path(const char *runtime_dir, char *buf, size_t size)
{
    if (!buf || size == 0) return -1;
    int n;
    if (runtime_dir && runtime_dir[0])
        n = snprintf(buf, size, "%s/" CP0_INPUT_STATE_SUBDIR "/" CP0_INPUT_STATE_FILE, runtime_dir);
    else
        n = snprintf(buf, size, "%s", CP0_INPUT_STATE_FALLBACK_PATH);
    if (n < 0 || (size_t)n >= size) {
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

int cp0_input_state_format(const cp0_input_state_t *state, char *buf, size_t size)
{
    if (!state || !buf) return -1;
    const int n = snprintf(buf, size, "v=%d\nkeyboards=%u\nlast_seen=%" PRId64 "\ngen=%u\n",
                           CP0_INPUT_STATE_VERSION, state->keyboards, state->last_seen, state->gen);
    if (n < 0 || (size_t)n >= size) return -1;
    return n;
}

/* value of "key=<decimal>" filling the whole line; returns 1 when found */
static int parse_number(const char *line, size_t len, const char *key, long long *out)
{
    const size_t key_len = strlen(key);
    if (len <= key_len || memcmp(line, key, key_len) != 0 || line[key_len] != '=') return 0;
    long long value = 0;
    size_t i = key_len + 1;
    int negative = 0;
    if (i < len && line[i] == '-') {
        negative = 1;
        i++;
    }
    const size_t first_digit = i;
    for (; i < len && line[i] >= '0' && line[i] <= '9'; i++) {
        if (value > (LLONG_MAX - 9) / 10) return 0;
        value = value * 10 + (line[i] - '0');
    }
    if (i == first_digit) return 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i != len) return 0;
    *out = negative ? -value : value;
    return 1;
}

int cp0_input_state_parse(const char *text, size_t len, cp0_input_state_t *out)
{
    if (!text || !out) return -1;
    cp0_input_state_t state = {0, 0, 0};
    int version_ok = 0;
    size_t start = 0;
    while (start < len) {
        size_t end = start;
        while (end < len && text[end] != '\n') end++;
        const char *line = text + start;
        const size_t line_len = end - start;
        long long value = 0;
        if (parse_number(line, line_len, "v", &value)) version_ok = value == CP0_INPUT_STATE_VERSION;
        else if (parse_number(line, line_len, "keyboards", &value) && value >= 0 && value <= UINT_MAX)
            state.keyboards = (unsigned)value;
        else if (parse_number(line, line_len, "last_seen", &value)) state.last_seen = value;
        else if (parse_number(line, line_len, "gen", &value) && value >= 0 && value <= UINT_MAX)
            state.gen = (unsigned)value;
        start = end + 1;
    }
    if (!version_ok) return -1;
    *out = state;
    return 0;
}

int cp0_input_state_read(const char *path, cp0_input_state_t *out)
{
    if (!path || !out) return -1;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
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
    return cp0_input_state_parse(text, len, out);
}

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

int cp0_input_state_write(const char *path, const cp0_input_state_t *state)
{
    if (!path || !state) {
        errno = EINVAL;
        return -1;
    }
    char text[160];
    const int text_len = cp0_input_state_format(state, text, sizeof(text));
    char tmp[PATH_MAX];
    const int tmp_len = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid());
    if (text_len < 0 || tmp_len < 0 || (size_t)tmp_len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (ensure_parent_dir(path) != 0) return -1;

    const int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    size_t written = 0;
    while (written < (size_t)text_len) {
        const ssize_t n = write(fd, text + written, (size_t)text_len - written);
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

int64_t cp0_input_state_next_last_seen(unsigned prev_keyboards, int64_t prev_last_seen, unsigned keyboards,
                                       int64_t now)
{
    return (keyboards > 0 || prev_keyboards > 0) ? now : prev_last_seen;
}
