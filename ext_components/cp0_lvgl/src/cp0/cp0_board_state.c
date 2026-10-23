/*
 * Board facts writer (see cp0_board_state.h).
 */

#define _GNU_SOURCE
#include "cp0_board_state.h"

#include "cp0_ui_metrics.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

int cp0_board_state_path(const char *runtime_dir, char *buf, size_t size)
{
    if (!buf || size == 0) return -1;
    int n;
    if (runtime_dir && runtime_dir[0])
        n = snprintf(buf, size, "%s/" CP0_APP_STATE_SUBDIR "/" CP0_APP_BOARD_FILE, runtime_dir);
    else
        n = snprintf(buf, size, "%s", CP0_BOARD_STATE_FALLBACK_PATH);
    if (n < 0 || (size_t)n >= size) {
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

/* appends "key=value\n" (the value with control characters as spaces); 0 when it does not fit */
static int put_text(char *buf, size_t size, size_t *at, const char *key, const char *value)
{
    if (!value[0]) return 1;
    const size_t k = strlen(key), v = strlen(value);
    if (*at + k + 1 + v + 1 >= size) return 0;
    memcpy(buf + *at, key, k);
    *at += k;
    buf[(*at)++] = '=';
    for (size_t i = 0; i < v; i++) buf[(*at)++] = (unsigned char)value[i] < 0x20 ? ' ' : value[i];
    buf[(*at)++] = '\n';
    buf[*at] = '\0';
    return 1;
}

static int put_int(char *buf, size_t size, size_t *at, const char *key, int value)
{
    if (value < 0) return 1;
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    return put_text(buf, size, at, key, text);
}

int cp0_board_state_format(const cp0_app_board_t *b, char *buf, size_t size)
{
    if (!b || !buf || size < 64) return -1;
    size_t at = 0;
    buf[0] = '\0';
    char head[48];
    snprintf(head, sizeof(head), "%u", b->gen);
    int ok = put_text(buf, size, &at, "v", "1") && put_text(buf, size, &at, "gen", head);
    snprintf(head, sizeof(head), "%d", b->pid);
    ok = ok && put_text(buf, size, &at, "pid", head);
    ok = ok && put_text(buf, size, &at, "launcher", b->launcher) && put_text(buf, size, &at, "board", b->board) &&
         put_text(buf, size, &at, "arch", b->arch) && put_int(buf, size, &at, "ram_mb", b->ram_mb) &&
         put_int(buf, size, &at, "battery", b->battery) &&
         put_text(buf, size, &at, "battery_source", b->battery_source) &&
         put_text(buf, size, &at, "backlight", b->backlight) &&
         put_int(buf, size, &at, "backlight_steps", b->backlight_steps) && put_int(buf, size, &at, "sound", b->sound) &&
         put_text(buf, size, &at, "sound_kind", b->sound_kind) && put_int(buf, size, &at, "sound_guess", b->sound_guess) &&
         put_text(buf, size, &at, "requests", b->requests) && put_text(buf, size, &at, "battery_note", b->battery_note) &&
         put_text(buf, size, &at, "backlight_note", b->backlight_note) &&
         put_text(buf, size, &at, "sound_note", b->sound_note);
    return ok ? (int)at : -1;
}

int cp0_board_state_differs(const cp0_app_board_t *a, const cp0_app_board_t *b)
{
    cp0_app_board_t x = *a, y = *b;
    x.gen = y.gen = 0;
    x.pid = y.pid = 0;
    return memcmp(&x, &y, sizeof(x)) != 0;
}

int cp0_board_state_write(const char *path, const cp0_app_board_t *board)
{
    if (!path || !board) {
        errno = EINVAL;
        return -1;
    }
    char text[1536];
    const int len = cp0_board_state_format(board, text, sizeof(text));
    if (len < 0) {
        errno = EOVERFLOW;
        return -1;
    }
    return cp0_ui_state_write(path, text, (size_t)len);
}
