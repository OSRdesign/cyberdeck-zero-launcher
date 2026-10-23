/*
 * SPDX-License-Identifier: MIT
 *
 * The app contract's reader and the on-screen keyboard rule (see cp0_app_contract.h). Pure C99 + POSIX
 * (getenv, fopen, clock_gettime); unit-tested on the PC by tests/test_app_contract.c.
 */
#define _POSIX_C_SOURCE 200809L
#include "cp0_app_contract.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

/* ------------------------------------------------------------------ small parsers */

/* Whole string a decimal int (optional '-'); trailing spaces / CR tolerated. 1 when ok. */
static int parse_int(const char *s, size_t len, long long lo, long long hi, long long *out)
{
    size_t i = 0;
    int negative = 0;
    if (i < len && s[i] == '-') {
        negative = 1;
        i++;
    }
    const size_t first = i;
    long long v = 0;
    for (; i < len && s[i] >= '0' && s[i] <= '9'; i++) {
        if (v > (LLONG_MAX - 9) / 10) return 0;
        v = v * 10 + (s[i] - '0');
    }
    if (i == first) return 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) i++;
    if (i != len) return 0;
    if (negative) v = -v;
    if (v < lo || v > hi) return 0;
    *out = v;
    return 1;
}

/* "a<sep>b<sep>c..." of exactly n ints. 1 when ok. */
static int parse_ints(const char *s, size_t len, char sep, int n, int *out)
{
    size_t start = 0;
    for (int k = 0; k < n; k++) {
        size_t end = start;
        while (end < len && s[end] != sep) end++;
        if ((k < n - 1) != (end < len)) return 0;
        long long v;
        if (!parse_int(s + start, end - start, INT_MIN, INT_MAX, &v)) return 0;
        out[k] = (int)v;
        start = end + 1;
    }
    return 1;
}

/* value without trailing spaces / CR, into buf; 1 when it fits (else buf = "") */
static int copy_text(const char *s, size_t len, char *buf, size_t size)
{
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) len--;
    if (len >= size) {
        buf[0] = '\0';
        return 0;
    }
    memcpy(buf, s, len);
    buf[len] = '\0';
    return 1;
}

static int env_int(const char *name, long long lo, long long hi, int *out)
{
    const char *v = getenv(name);
    long long n;
    if (!v || !parse_int(v, strlen(v), lo, hi, &n)) return 0;
    *out = (int)n;
    return 1;
}

static void env_text(const char *name, char *buf, size_t size)
{
    const char *v = getenv(name);
    buf[0] = '\0';
    if (v) (void)copy_text(v, strlen(v), buf, size);
}

/* Calls fn(key, key_len, value, value_len, ctx) for every "key=value" line. */
typedef void (*line_fn)(const char *key, size_t key_len, const char *value, size_t value_len, void *ctx);

static void for_each_line(const char *text, size_t len, line_fn fn, void *ctx)
{
    size_t start = 0;
    while (start < len) {
        size_t end = start;
        while (end < len && text[end] != '\n') end++;
        size_t eq = start;
        while (eq < end && text[eq] != '=') eq++;
        if (eq < end && eq > start) fn(text + start, eq - start, text + eq + 1, end - eq - 1, ctx);
        start = end + 1;
    }
}

static int key_is(const char *key, size_t key_len, const char *name)
{
    return strlen(name) == key_len && memcmp(key, name, key_len) == 0;
}

static int read_file(const char *path, char *buf, size_t size, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    *len = fread(buf, 1, size, f);
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ names */

const char *cp0_app_mode_name(cp0_app_mode_t mode)
{
    switch (mode) {
    case CP0_APP_MODE_COMPAT: return "compat";
    case CP0_APP_MODE_FULLSCREEN: return "fullscreen";
    default: return "";
    }
}

cp0_app_mode_t cp0_app_mode_parse(const char *s)
{
    if (!s) return CP0_APP_MODE_NONE;
    if (strcmp(s, "compat") == 0) return CP0_APP_MODE_COMPAT;
    if (strcmp(s, "fullscreen") == 0) return CP0_APP_MODE_FULLSCREEN;
    return CP0_APP_MODE_NONE;
}

cp0_ui_class_t cp0_app_class_parse(const char *s)
{
    if (!s) return CP0_UI_CLASS__COUNT;
    for (int c = 0; c < CP0_UI_CLASS__COUNT; c++)
        if (strcmp(s, cp0_ui_class_name((cp0_ui_class_t)c)) == 0) return (cp0_ui_class_t)c;
    return CP0_UI_CLASS__COUNT;
}

const char *cp0_app_vkb_name(cp0_app_vkb_t vkb)
{
    switch (vkb) {
    case CP0_APP_VKB_OFF: return "off";
    case CP0_APP_VKB_FORCE: return "force";
    default: return "auto";
    }
}

int cp0_app_vkb_parse(const char *s, cp0_app_vkb_t *out)
{
    if (!s || !out) return -1;
    if (strcmp(s, "auto") == 0) *out = CP0_APP_VKB_AUTO;
    else if (strcmp(s, "off") == 0) *out = CP0_APP_VKB_OFF;
    else if (strcmp(s, "force") == 0) *out = CP0_APP_VKB_FORCE;
    else return -1;
    return 0;
}

/* ------------------------------------------------------------------ environment */

int cp0_app_env_read(cp0_app_env_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->screen_class = CP0_UI_CLASS__COUNT;

    (void)env_int(CP0_APP_ENV_SCREEN_W, 1, 100000, &out->screen_w);
    (void)env_int(CP0_APP_ENV_SCREEN_H, 1, 100000, &out->screen_h);
    (void)env_int(CP0_APP_ENV_SCREEN_ROTATE, 0, 270, &out->screen_rot);
    (void)env_int(CP0_APP_ENV_SCREEN_PPMM, 1, 1000000, &out->screen_ppmm_x100);
    out->screen_class = cp0_app_class_parse(getenv(CP0_APP_ENV_SCREEN_CLASS));
    env_text(CP0_APP_ENV_SCREEN_FB, out->screen_fb, sizeof(out->screen_fb));
    env_text(CP0_APP_ENV_SCREEN_TOUCH, out->screen_touch, sizeof(out->screen_touch));
    env_text(CP0_APP_ENV_STATE_DIR, out->state_dir, sizeof(out->state_dir));

    int ok = env_int(CP0_APP_ENV_CONTRACT, 1, INT_MAX, &out->contract);
    out->mode = cp0_app_mode_parse(getenv(CP0_APP_ENV_MODE));
    ok = ok && out->mode != CP0_APP_MODE_NONE;

    int win[4];
    const char *w = getenv(CP0_APP_ENV_WINDOW);
    if (w && parse_ints(w, strlen(w), ',', 4, win) && win[2] > 0 && win[3] > 0) {
        out->win_x = win[0], out->win_y = win[1], out->win_w = win[2], out->win_h = win[3];
    } else {
        ok = 0;
    }
    int canvas[2];
    const char *c = getenv(CP0_APP_ENV_CANVAS);
    if (c && parse_ints(c, strlen(c), 'x', 2, canvas) && canvas[0] > 0 && canvas[1] > 0) {
        out->canvas_w = canvas[0], out->canvas_h = canvas[1];
    } else {
        ok = 0;
    }
    if (!env_int(CP0_APP_ENV_CANVAS_SCALE, 1, 64, &out->canvas_scale)) ok = 0;
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ state files */

int cp0_app_state_path(const char *file, char *buf, size_t size)
{
    if (!file || !file[0] || !buf || size == 0) return -1;
    const char *dir = getenv(CP0_APP_ENV_STATE_DIR);
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    int n;
    if (dir && dir[0]) n = snprintf(buf, size, "%s/%s", dir, file);
    else if (runtime && runtime[0]) n = snprintf(buf, size, "%s/" CP0_APP_STATE_SUBDIR "/%s", runtime, file);
    else n = snprintf(buf, size, CP0_APP_STATE_FALLBACK_PREFIX "%s", file);
    if (n < 0 || (size_t)n >= size) {
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

typedef struct {
    cp0_app_screen_t *s;
    int version_ok, have_w, have_h;
} screen_ctx_t;

static void screen_line(const char *key, size_t key_len, const char *v, size_t len, void *ctx)
{
    screen_ctx_t *c = ctx;
    cp0_app_screen_t *s = c->s;
    long long n;
    int list[4];
    char word[32];
    if (key_is(key, key_len, "v")) {
        c->version_ok = parse_int(v, len, 0, INT_MAX, &n) && n == CP0_UI_STATE_VERSION;
    } else if (key_is(key, key_len, "w")) {
        if ((c->have_w = parse_int(v, len, 1, 100000, &n))) s->w = (int)n;
    } else if (key_is(key, key_len, "h")) {
        if ((c->have_h = parse_int(v, len, 1, 100000, &n))) s->h = (int)n;
    } else if (key_is(key, key_len, "rot")) {
        if (parse_int(v, len, 0, 270, &n)) s->rot = (int)n;
    } else if (key_is(key, key_len, "ppmm")) {
        if (parse_int(v, len, 0, 1000000, &n)) s->ppmm_x100 = (int)n;
    } else if (key_is(key, key_len, "density")) {
        if (copy_text(v, len, word, sizeof(word)))
            s->density_src = strcmp(word, "panel_mm") == 0 ? CP0_UI_DENSITY_PANEL_MM
                             : strcmp(word, "fbdev") == 0  ? CP0_UI_DENSITY_FBDEV
                                                           : CP0_UI_DENSITY_DEFAULT;
    } else if (key_is(key, key_len, "class")) {
        if (copy_text(v, len, word, sizeof(word))) s->cls = cp0_app_class_parse(word);
    } else if (key_is(key, key_len, "compat")) {
        if (parse_ints(v, len, ',', 4, list))
            s->compat_x = list[0], s->compat_y = list[1], s->compat_w = list[2], s->compat_h = list[3];
    } else if (key_is(key, key_len, "scale")) {
        if (parse_int(v, len, 0, 64, &n)) s->compat_scale = (int)n;
    } else if (key_is(key, key_len, "toolbar")) {
        if (parse_int(v, len, 0, 100000, &n)) s->toolbar_h = (int)n;
    } else if (key_is(key, key_len, "fb")) {
        (void)copy_text(v, len, s->fb, sizeof(s->fb));
    } else if (key_is(key, key_len, "bpp")) {
        if (parse_int(v, len, 0, 64, &n)) s->bpp = (int)n;
    } else if (key_is(key, key_len, "touch")) {
        (void)copy_text(v, len, s->touch, sizeof(s->touch));
    } else if (key_is(key, key_len, "space")) {
        if (parse_ints(v, len, ',', 3, list)) s->space_s = list[0], s->space_m = list[1], s->space_l = list[2];
    } else if (key_is(key, key_len, "radius")) {
        if (parse_ints(v, len, ',', 3, list)) s->radius_s = list[0], s->radius_m = list[1], s->radius_l = list[2];
    } else if (key_is(key, key_len, "header")) {
        if (parse_int(v, len, 0, 100000, &n)) s->header_h = (int)n;
    } else if (key_is(key, key_len, "row")) {
        if (parse_int(v, len, 0, 100000, &n)) s->row_h = (int)n;
    } else if (key_is(key, key_len, "target")) {
        if (parse_int(v, len, 0, 100000, &n)) s->target = (int)n;
    } else if (key_is(key, key_len, "dialog_max_w")) {
        if (parse_int(v, len, 0, 100000, &n)) s->dialog_max_w = (int)n;
    } else if (key_is(key, key_len, "text")) {
        if (parse_ints(v, len, ',', 4, list))
            s->text_title = list[0], s->text_heading = list[1], s->text_body = list[2], s->text_caption = list[3];
    } else if (key_is(key, key_len, "kb_inset")) {
        if (parse_int(v, len, 0, 100000, &n)) s->kb_inset = (int)n;
    } else if (key_is(key, key_len, "gen")) {
        if (parse_int(v, len, 0, UINT_MAX, &n)) s->gen = (unsigned)n;
    }
}

int cp0_app_screen_parse(const char *text, size_t len, cp0_app_screen_t *out)
{
    if (!out) return -1;
    cp0_app_screen_t s;
    memset(&s, 0, sizeof(s));
    s.cls = CP0_UI_CLASS__COUNT;
    memset(out, 0, sizeof(*out));
    out->cls = CP0_UI_CLASS__COUNT;
    if (!text) return -1;
    screen_ctx_t ctx = {&s, 0, 0, 0};
    for_each_line(text, len, screen_line, &ctx);
    if (!ctx.version_ok || !ctx.have_w || !ctx.have_h) return -1;
    if (s.cls == CP0_UI_CLASS__COUNT) s.cls = cp0_ui_classify(s.w, s.h);
    s.from_file = 1;
    *out = s;
    return 0;
}

int cp0_app_screen_read(const char *path, cp0_app_screen_t *out)
{
    char def[CP0_APP_PATH_MAX + 32];
    if (!path) {
        if (cp0_app_state_path(CP0_APP_SCREEN_FILE, def, sizeof(def)) != 0) return cp0_app_screen_parse(NULL, 0, out);
        path = def;
    }
    char text[2048];
    size_t len = 0;
    if (read_file(path, text, sizeof(text), &len) != 0) return cp0_app_screen_parse(NULL, 0, out);
    return cp0_app_screen_parse(text, len, out);
}

void cp0_app_screen_from_metrics(const cp0_ui_metrics_t *m, cp0_app_screen_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->cls = CP0_UI_CLASS__COUNT;
    if (!m) return;
    out->w = m->w, out->h = m->h, out->rot = m->rot, out->ppmm_x100 = m->ppmm_x100;
    out->density_src = m->density_src;
    out->cls = m->cls;
    out->compat_x = m->compat.x, out->compat_y = m->compat.y, out->compat_w = m->compat.w, out->compat_h = m->compat.h;
    out->compat_scale = m->compat.scale, out->toolbar_h = m->compat.toolbar_h;
    const int *t = m->tok;
    out->space_s = t[CP0_TOK_SPACE_S], out->space_m = t[CP0_TOK_SPACE_M], out->space_l = t[CP0_TOK_SPACE_L];
    out->radius_s = t[CP0_TOK_RADIUS_S], out->radius_m = t[CP0_TOK_RADIUS_M], out->radius_l = t[CP0_TOK_RADIUS_L];
    out->header_h = t[CP0_TOK_HEADER_H], out->row_h = t[CP0_TOK_ROW_H], out->target = t[CP0_TOK_TARGET];
    out->dialog_max_w = t[CP0_TOK_DIALOG_MAX_W];
    out->text_title = m->text_px[CP0_TEXT_TITLE], out->text_heading = m->text_px[CP0_TEXT_HEADING];
    out->text_body = m->text_px[CP0_TEXT_BODY], out->text_caption = m->text_px[CP0_TEXT_CAPTION];
}

int cp0_app_screen_compute(int w, int h, int rot, int ppmm_x100, cp0_app_screen_t *out)
{
    if (!out) return -1;
    cp0_ui_metrics_t m;
    const cp0_ui_density_src_t src = ppmm_x100 > 0 ? CP0_UI_DENSITY_PANEL_MM : CP0_UI_DENSITY_DEFAULT;
    if (cp0_ui_metrics_compute(w, h, rot, ppmm_x100, src, 0, &m) != 0) {
        cp0_app_screen_from_metrics(NULL, out);
        return -1;
    }
    cp0_app_screen_from_metrics(&m, out);
    return 0;
}

typedef struct {
    cp0_app_input_t *in;
    int version_ok;
} input_ctx_t;

static void input_line(const char *key, size_t key_len, const char *v, size_t len, void *ctx)
{
    input_ctx_t *c = ctx;
    cp0_app_input_t *in = c->in;
    long long n;
    char word[16];
    if (key_is(key, key_len, "v")) {
        c->version_ok = parse_int(v, len, 0, INT_MAX, &n) && n == 1;
    } else if (key_is(key, key_len, "keyboards")) {
        if (parse_int(v, len, 0, UINT_MAX, &n)) in->keyboards = (unsigned)n;
    } else if (key_is(key, key_len, "bt_keyboards")) {
        if (parse_int(v, len, 0, UINT_MAX, &n)) in->bt_keyboards = (unsigned)n;
    } else if (key_is(key, key_len, "last_seen")) {
        if (parse_int(v, len, LLONG_MIN + 1, LLONG_MAX, &n)) in->last_seen = n;
    } else if (key_is(key, key_len, "kb_gone")) {
        if (parse_int(v, len, 0, LLONG_MAX, &n)) in->kb_gone = n;
    } else if (key_is(key, key_len, "bt_gone")) {
        if (parse_int(v, len, 0, LLONG_MAX, &n)) in->bt_gone = n;
    } else if (key_is(key, key_len, "vkb")) {
        if (copy_text(v, len, word, sizeof(word))) (void)cp0_app_vkb_parse(word, &in->vkb);
    } else if (key_is(key, key_len, "pointer")) {
        if (copy_text(v, len, word, sizeof(word))) {
            if (strcmp(word, "on") == 0) in->pointer_nav = 1;
            else if (strcmp(word, "off") == 0) in->pointer_nav = 0;
        }
    } else if (key_is(key, key_len, "gen")) {
        if (parse_int(v, len, 0, UINT_MAX, &n)) in->gen = (unsigned)n;
    }
}

int cp0_app_input_parse(const char *text, size_t len, cp0_app_input_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->vkb = CP0_APP_VKB_AUTO;
    if (!text) return -1;
    cp0_app_input_t in;
    memset(&in, 0, sizeof(in));
    in.vkb = CP0_APP_VKB_AUTO;
    input_ctx_t ctx = {&in, 0};
    for_each_line(text, len, input_line, &ctx);
    if (!ctx.version_ok) return -1;
    *out = in;
    return 0;
}

int cp0_app_input_read(const char *path, cp0_app_input_t *out)
{
    char def[CP0_APP_PATH_MAX + 32];
    if (!path) {
        if (cp0_app_state_path(CP0_APP_INPUT_FILE, def, sizeof(def)) != 0) return cp0_app_input_parse(NULL, 0, out);
        path = def;
    }
    char text[1024];
    size_t len = 0;
    if (read_file(path, text, sizeof(text), &len) != 0) return cp0_app_input_parse(NULL, 0, out);
    return cp0_app_input_parse(text, len, out);
}

typedef struct {
    cp0_app_status_t *s;
    int version_ok;
} status_ctx_t;

static void status_line(const char *key, size_t key_len, const char *v, size_t len, void *ctx)
{
    status_ctx_t *c = ctx;
    cp0_app_status_t *s = c->s;
    long long n;
    char word[16];
    if (key_is(key, key_len, "v")) {
        c->version_ok = parse_int(v, len, 0, INT_MAX, &n) && n == 1;
    } else if (key_is(key, key_len, "pid")) {
        if (parse_int(v, len, 0, INT_MAX, &n)) s->pid = (int)n;
    } else if (key_is(key, key_len, "wifi")) {
        if (parse_int(v, len, 0, 1, &n)) s->wifi = (int)n;
    } else if (key_is(key, key_len, "wifi_pct")) {
        if (parse_int(v, len, 0, 100, &n)) s->wifi_pct = (int)n;
    } else if (key_is(key, key_len, "eth")) {
        if (parse_int(v, len, 0, 1, &n)) s->eth = (int)n;
    } else if (key_is(key, key_len, "bt")) {
        if (copy_text(v, len, word, sizeof(word)))
            s->bt = strcmp(word, "connected") == 0 ? CP0_APP_BT_CONNECTED
                    : strcmp(word, "on") == 0      ? CP0_APP_BT_ON
                                                   : CP0_APP_BT_OFF;
    } else if (key_is(key, key_len, "battery")) {
        if (parse_int(v, len, -1, 100, &n)) s->battery = (int)n;
    } else if (key_is(key, key_len, "battery_state")) {
        if (copy_text(v, len, s->battery_state, sizeof(s->battery_state)))
            s->battery_charging = strcmp(s->battery_state, "Charging") == 0;
    } else if (key_is(key, key_len, "gen")) {
        if (parse_int(v, len, 0, UINT_MAX, &n)) s->gen = (unsigned)n;
    }
}

int cp0_app_status_parse(const char *text, size_t len, cp0_app_status_t *out)
{
    if (!out) return -1;
    cp0_app_status_t s;
    memset(&s, 0, sizeof(s));
    s.battery = -1;
    memset(out, 0, sizeof(*out));
    out->battery = -1;
    if (!text) return -1;
    status_ctx_t ctx = {&s, 0};
    for_each_line(text, len, status_line, &ctx);
    if (!ctx.version_ok) return -1;
    *out = s;
    return 0;
}

int cp0_app_status_load(const char *path, cp0_app_status_t *out)
{
    char def[CP0_APP_PATH_MAX + 32];
    if (!path) {
        if (cp0_app_state_path(CP0_APP_STATUS_FILE, def, sizeof(def)) != 0) return cp0_app_status_parse(NULL, 0, out);
        path = def;
    }
    char text[512];
    size_t len = 0;
    if (read_file(path, text, sizeof(text), &len) != 0) return cp0_app_status_parse(NULL, 0, out);
    return cp0_app_status_parse(text, len, out);
}

int cp0_app_status_read(const char *path, cp0_app_status_t *out)
{
    if (cp0_app_status_load(path, out) != 0) return -1;
    if (out->pid > 0 && (kill((pid_t)out->pid, 0) == 0 || errno == EPERM)) return 0;   /* EPERM: alive, another user */
    (void)cp0_app_status_parse(NULL, 0, out);
    return -1;
}

/* ------------------------------------------------------------------ on-screen keyboard rule */

int cp0_app_osk_rule(cp0_app_vkb_t vkb, unsigned keyboards, int64_t now_s, int64_t kb_gone_s, int64_t bt_gone_s)
{
    if (vkb == CP0_APP_VKB_OFF) return 0;
    if (vkb == CP0_APP_VKB_FORCE) return 1;
    if (keyboards > 0) return 0;
    if (bt_gone_s > 0 && now_s - bt_gone_s < CP0_APP_OSK_BT_ASLEEP_S) return 0;
    if (kb_gone_s > 0 && now_s - kb_gone_s < CP0_APP_OSK_GRACE_S) return 0;
    return 1;
}

int cp0_app_osk_allowed(const cp0_app_input_t *in, int64_t now_mono_s)
{
    if (!in) return 0;
    return cp0_app_osk_rule(in->vkb, in->keyboards, now_mono_s, in->kb_gone, in->bt_gone);
}

int64_t cp0_app_monotonic_s(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec;
}
