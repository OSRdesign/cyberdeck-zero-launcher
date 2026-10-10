/*
 * SPDX-License-Identifier: MIT
 *
 * Layout service of the responsive shell (see cp0_ui_metrics.h). Pure C: no LVGL, no system call.
 */

#include "cp0_ui_metrics.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ tables */

/* Per-class tokens that do not come from millimetres (report 022 section 3.2). The Standard and Compact columns hold
 * the numbers the deck and the Pi 3A+ draw today (header, pad = SPACE_M, tile radius = RADIUS_L, toolbar key radius
 * = RADIUS_M, tile icon, title and label fonts); tests/test_ui_metrics.c checks that. */
typedef struct {
    int header, status_pct;
    int space[3], radius[3];
    int tile_icon, dialog_max_w;
    int text[4]; /* title, heading, body, caption */
} class_spec_t;

static const class_spec_t k_class[CP0_UI_CLASS__COUNT] = {
    [CP0_UI_COMPACT] = {40, 72, {5, 10, 14}, {6, 14, 20}, 88, 440, {24, 20, 18, 14}},
    [CP0_UI_STANDARD] = {56, 100, {8, 16, 20}, {8, 18, 28}, 128, 560, {32, 28, 24, 18}},
    [CP0_UI_WIDE] = {56, 100, {8, 14, 20}, {8, 16, 24}, 128, 600, {32, 26, 22, 18}},
    [CP0_UI_SQUARE] = {64, 100, {8, 16, 24}, {8, 18, 28}, 128, 600, {32, 28, 24, 20}},
    [CP0_UI_LARGE] = {64, 115, {10, 16, 24}, {10, 20, 28}, 160, 720, {36, 30, 26, 20}},
    /* D12: portrait is not a target; it gets the Standard numbers so a portrait canvas still works */
    [CP0_UI_PORTRAIT] = {56, 100, {8, 16, 20}, {8, 18, 28}, 128, 560, {32, 28, 24, 18}},
};

/* Today's home grid and toolbar (native_ui.cpp before P1b): the deck layout and the compact one (task 013 T3,
 * status bar 40, padding 10, icon and text about 72 % of the deck sizes). Do not change: the render harness goldens
 * at 640x480 and 480x320 are drawn from these. */
static const cp0_ui_shell_t k_shell_deck = {
    .cols = 3, .rows = 2, .bar_h = 56, .pad = 16, .status_pct = 100, .status_top = 8,
    .title_x = 20, .title_y = 10, .title_px = 32,
    .tile_radius = 28, .tile_border = 3, .tile_border_sel = 5, .icon = 128, .icon_top = 14,
    .label_px = 24, .label_bottom = 12, .label_inset = 16,
    .tb_pad_x = 10, .tb_pad_y = 10, .tb_gap = 8, .tb_radius = 18, .tb_text_px = 24, .tb_symbol_px = 32,
};
static const cp0_ui_shell_t k_shell_compact = {
    .cols = 3, .rows = 2, .bar_h = 40, .pad = 10, .status_pct = 72, .status_top = 5,
    .title_x = 14, .title_y = 6, .title_px = 24,
    .tile_radius = 20, .tile_border = 2, .tile_border_sel = 4, .icon = 88, .icon_top = 6,
    .label_px = 18, .label_bottom = 5, .label_inset = 12,
    .tb_pad_x = 6, .tb_pad_y = 5, .tb_gap = 6, .tb_radius = 14, .tb_text_px = 18, .tb_symbol_px = 24,
};

#define SHELL_COMPACT_BELOW_H 400 /* today's switch between the two home layouts (sizes P1c has not designed yet) */
#define LEGACY_SMALL_PANEL_H 400  /* today's compat placement: top-aligned from this height, else centred */
#define LEGACY_SMALL_TOOLBAR 100  /* ... above a 100 px toolbar */
#define PINNED_TOOLBAR_DECK 140   /* the toolbars the two shipped boards draw */
#define PINNED_TOOLBAR_COMPACT 100
#define LARGE_FROM_H 700
#define GRID_PAD_MM_X10 20        /* P1c home grid: gap and margin 2 mm (10..24 px) ... */
#define GRID_TILE_MM_X10 200      /* ... target tile 20 mm ... */
#define GRID_TILE_FLOOR_PX 120    /* ... at least 120 px */
#define MAX_MM_X10 100000         /* 10 m: anything larger is a driver's "unknown" */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ class, density, px */

cp0_ui_class_t cp0_ui_classify(int w, int h)
{
    const long long W = w, H = h;
    if (5 * W < 4 * H) return CP0_UI_PORTRAIT; /* a < 0.8 */
    if (h < 400) return CP0_UI_COMPACT;
    if (4 * W <= 5 * H) return CP0_UI_SQUARE;  /* a <= 1.25 */
    if (h >= LARGE_FROM_H) return CP0_UI_LARGE;
    if (2 * W >= 3 * H) return CP0_UI_WIDE;    /* a >= 1.5 */
    return CP0_UI_STANDARD;
}

const char *cp0_ui_class_name(cp0_ui_class_t cls)
{
    switch (cls) {
    case CP0_UI_COMPACT: return "compact";
    case CP0_UI_STANDARD: return "standard";
    case CP0_UI_WIDE: return "wide";
    case CP0_UI_SQUARE: return "square";
    case CP0_UI_LARGE: return "large";
    case CP0_UI_PORTRAIT: return "portrait";
    default: return "?";
    }
}

const char *cp0_ui_density_src_name(cp0_ui_density_src_t s)
{
    switch (s) {
    case CP0_UI_DENSITY_PANEL_MM: return "panel_mm";
    case CP0_UI_DENSITY_FBDEV: return "fbdev";
    default: return "default";
    }
}

/* "79" or "56.6" -> tenths; *end after it. -1 when malformed. */
static int parse_mm(const char *s, const char **end)
{
    long v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        if (v > MAX_MM_X10) return -1;
        s++;
        digits++;
    }
    if (digits == 0) return -1;
    v *= 10;
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') return -1;
        v += *s - '0';
        s++;
    }
    if (v <= 0 || v > MAX_MM_X10) return -1;
    *end = s;
    return (int)v;
}

int cp0_ui_parse_panel_mm(const char *s, int *w_x10, int *h_x10)
{
    if (!s || !w_x10 || !h_x10) return -1;
    const char *p = s;
    const int w = parse_mm(p, &p);
    if (w < 0 || (*p != 'x' && *p != 'X')) return -1;
    p++;
    const int h = parse_mm(p, &p);
    if (h < 0 || *p != '\0') return -1;
    *w_x10 = w;
    *h_x10 = h;
    return 0;
}

int cp0_ui_ppmm_from_mm(int w, int h, int w_x10, int h_x10)
{
    if (w <= 0 || h <= 0 || w_x10 <= 0 || h_x10 <= 0) return 0;
    /* mean of w*1000/w_x10 and h*1000/h_x10, rounded, in one integer division */
    const long long num = 1000LL * ((long long)w * h_x10 + (long long)h * w_x10);
    const long long den = 2LL * w_x10 * h_x10;
    const long long v = (2 * num + den) / (2 * den);
    return v > INT_MAX ? INT_MAX : (int)v;
}

static int ppmm_usable(int p)
{
    return p >= CP0_UI_PPMM_MIN_X100 && p <= CP0_UI_PPMM_MAX_X100;
}

cp0_ui_density_t cp0_ui_density_resolve(int w, int h, int rot, const char *panel_mm, int fb_mm_w, int fb_mm_h)
{
    cp0_ui_density_t d = {CP0_UI_DEFAULT_PPMM_X100, CP0_UI_DENSITY_DEFAULT, 0};
    if (panel_mm && panel_mm[0]) {
        int mw = 0, mh = 0;
        if (cp0_ui_parse_panel_mm(panel_mm, &mw, &mh) == 0) {
            if (w != h && mw != mh && ((w > h) != (mw > mh))) { /* written portrait: follow the canvas */
                const int t = mw;
                mw = mh;
                mh = t;
            }
            const int p = cp0_ui_ppmm_from_mm(w, h, mw, mh);
            if (ppmm_usable(p)) {
                d.ppmm_x100 = p;
                d.src = CP0_UI_DENSITY_PANEL_MM;
                return d;
            }
        }
        d.panel_mm_rejected = 1;
    }
    if (fb_mm_w > 0 && fb_mm_h > 0 && fb_mm_w <= MAX_MM_X10 / 10 && fb_mm_h <= MAX_MM_X10 / 10) {
        int mw = fb_mm_w * 10, mh = fb_mm_h * 10;
        if (rot == 90 || rot == 270) {
            const int t = mw;
            mw = mh;
            mh = t;
        }
        const int p = cp0_ui_ppmm_from_mm(w, h, mw, mh);
        if (ppmm_usable(p)) {
            d.ppmm_x100 = p;
            d.src = CP0_UI_DENSITY_FBDEV;
        }
    }
    return d;
}

int cp0_ui_mm_px(int ppmm_x100, int mm_x10, int floor_px)
{
    const long long v = ((long long)mm_x10 * ppmm_x100 + 500) / 1000;
    const int px = v > INT_MAX ? INT_MAX : (int)v;
    return px < floor_px ? floor_px : px;
}

int cp0_ui_font_snap(int px)
{
    if (px < CP0_UI_FONT_MIN_PX) return CP0_UI_FONT_MIN_PX;
    if (px > CP0_UI_FONT_MAX_PX) return CP0_UI_FONT_MAX_PX;
    return px & ~1;
}

/* ------------------------------------------------------------------ placement */

static void finish_window(int w, int scale, cp0_ui_compat_t *c)
{
    c->scale = scale;
    c->w = CP0_UI_COMPAT_W * scale;
    c->h = CP0_UI_COMPAT_H * scale;
    c->x = imax(0, (w - c->w) / 2);
}

static int capped(int scale_max, int cap)
{
    return cap > 0 && cap < scale_max ? cap : scale_max;
}

/* The deck and the Pi 3A+: exactly today's placement (largest scale that fits; top-aligned on the 640x480 deck,
 * toolbar 140 px; centred above a 100 px toolbar on 480x320: y 25, 25 px between window and toolbar). */
static void place_pinned(int w, int h, int cap, cp0_ui_compat_t *c)
{
    c->scale_max = imax(1, imin(w / CP0_UI_COMPAT_W, h / CP0_UI_COMPAT_H));
    finish_window(w, capped(c->scale_max, cap), c);
    c->y = h >= LEGACY_SMALL_PANEL_H ? 0 : imax(0, (h - LEGACY_SMALL_TOOLBAR - c->h) / 2);
    c->toolbar_y = imin(h, c->y + c->h + c->y);
    c->toolbar_h = h - c->toolbar_y;
}

/* Report 022 section 3.6 with the toolbar rule of task 014 (2026-10-10): the toolbar has a fixed physical height
 * (toolbar_h: 9 mm keys plus their margins), never "the rest of the screen"; the window takes the largest integer
 * scale that fits above it, centred horizontally and vertically in that area; what is left stays black (D8). Only a
 * canvas too small for the 1x window plus the toolbar shrinks the toolbar. */
static void place_rule(int w, int h, int toolbar_h, int cap, cp0_ui_compat_t *c)
{
    const int sx = w / CP0_UI_COMPAT_W;
    const int sy = h > toolbar_h ? (h - toolbar_h) / CP0_UI_COMPAT_H : 0;
    c->scale_max = imax(1, imin(sx, sy));
    finish_window(w, capped(c->scale_max, cap), c);
    c->toolbar_h = imax(0, imin(h - c->h, toolbar_h));
    c->toolbar_y = h - c->toolbar_h;
    c->y = imax(0, (h - c->toolbar_h - c->h) / 2);
}

static int ratio_dist(int r) { return r > 95 ? r - 95 : 95 - r; }

/* Task 014 "Home grid design rule" (P1c), computed sizes only. Everything in px; the status strip, title and toolbar
 * numbers stay the base preset's (P1b), the grid and what is drawn inside a tile come from the physical tokens:
 *   pad = clamp(2 mm, 10, 24); bar = the class header token; avail = (w - 2 pad) x (h - bar)
 *   target tile = 20 mm (at least 120 px); cols0 = clamp(round((avail_w + pad) / (target + pad)), 2, 8)
 *   tile_w = (avail_w - (cols - 1) pad) / cols; rows = max(2, floor((avail_h - pad) / (0.92 tile_w + pad)))
 *   tile_h = (avail_h - (rows + 1) pad) / rows; when tile_h / tile_w is outside [0.85, 1.2] the rows change by one in
 *   the direction that gets closest to 0.95 (one row is allowed when two are too flat); cols0 and cols0 +- 1 are all
 *   tried: a result inside [0.85, 1.2] with the tile width closest to the target wins, else the aspect closest to 0.95; a tile still taller than 1.2 tile_w is capped there and the grid is
 *   centred vertically (top_extra). A canvas too small for 40 px tiles keeps the base preset's grid. */
static void shell_computed_grid(const class_spec_t *k, int w, int h, int ppmm_x100, cp0_ui_shell_t *s)
{
    const int pad = imax(10, imin(24, cp0_ui_mm_px(ppmm_x100, GRID_PAD_MM_X10, 0)));
    const int bar = k->header;
    const int avail_w = w - 2 * pad, avail_h = h - bar;
    if (avail_w <= 0 || avail_h <= 0) return;
    const int target = cp0_ui_mm_px(ppmm_x100, GRID_TILE_MM_X10, GRID_TILE_FLOOR_PX);
    const int den = target + pad;
    const int cols0 = imax(2, imin(8, (2 * (avail_w + pad) + den) / (2 * den)));

    /* the rule's columns first, then the neighbours; a candidate replaces the best only when strictly closer to 0.95 */
    int cols = 0, tile_w = 0, rows = 0, tile_h = 0, best_score = 0;
    for (int ci = 0; ci < 3; ci++) {
        const int c = ci == 0 ? cols0 : ci == 1 ? cols0 - 1 : cols0 + 1;
        if (c < 2 || c > 8) continue;
        const int tw = (avail_w - (c - 1) * pad) / c;
        if (tw < 40) continue;
        int r = imax(2, (avail_h - pad) / (tw * 92 / 100 + pad));
        int th = (avail_h - (r + 1) * pad) / r;
        int rt = th * 100 / tw;
        if (rt < 85 || rt > 120) {
            for (int r2 = r - 1; r2 <= r + 1; r2 += 2) {
                if (r2 < 1) continue; /* one row is allowed when two would be too flat (short, wide canvases) */
                const int th2 = (avail_h - (r2 + 1) * pad) / r2;
                if (th2 >= 40 && ratio_dist(th2 * 100 / tw) < ratio_dist(rt)) {
                    r = r2;
                    th = th2;
                    rt = th2 * 100 / tw;
                }
            }
        }
        if (th < 40) continue;
        /* candidates inside [0.85, 1.2] beat those outside; among them the tile width closest to the target wins,
         * among the others the aspect closest to 0.95; the first of equals (cols0) is kept */
        const int in = rt >= 85 && rt <= 120;
        const int score = in ? imax(tw - target, target - tw) : 100000 + ratio_dist(rt > 120 ? 120 : rt);
        if (cols == 0 || score < best_score) {
            best_score = score;
            cols = c;
            tile_w = tw;
            rows = r;
            tile_h = th;
        }
    }
    if (cols == 0) return;
    int top_extra = 0;
    if (tile_h * 100 > 120 * tile_w) {
        tile_h = tile_w * 120 / 100;
        top_extra = imax(0, (avail_h - rows * tile_h - (rows + 1) * pad) / 2);
    }

    const int min_side = imin(tile_w, tile_h);
    const int body = k->text[2];
    const int label_px = cp0_ui_font_snap(imax(14, imin(body, tile_w / 7)));
    const int label_bottom = imax(5, imin(14, tile_h / 16));
    int icon = (min_side / 2) & ~7; /* half the short side, a multiple of 8 */
    icon = imax(32, imin(icon, tile_h - label_bottom - label_px * 5 / 4 - 2 * s->tile_border_sel - 4));

    const int base_bar = s->bar_h;
    s->cols = cols;
    s->rows = rows;
    s->bar_h = bar;
    s->pad = pad;
    s->tile_w = tile_w;
    s->tile_h = tile_h;
    s->top_extra = top_extra;
    s->tile_radius = k->radius[2];
    s->icon = icon;
    s->label_px = label_px;
    s->label_bottom = label_bottom;
    s->label_inset = pad;
    s->icon_top = imax(s->tile_border_sel + 2, (tile_h - label_bottom - label_px * 5 / 4 - icon) / 2);
    /* the status strip and the title keep their size; they centre in a taller bar */
    s->status_top += (bar - base_bar) / 2;
    s->title_y += (bar - base_bar) / 2;
}

static void shell_fill(cp0_ui_preset_t preset, int w, int h, cp0_ui_shell_t *s)
{
    *s = preset == CP0_UI_PRESET_COMPACT ? k_shell_compact : k_shell_deck;
    s->tile_w = (w - 2 * s->pad - (s->cols - 1) * s->pad) / s->cols;
    s->tile_h = (h - s->bar_h - (s->rows + 1) * s->pad) / s->rows;
    s->status_w = CP0_UI_COMPAT_W * s->status_pct / 100;
}

/* ------------------------------------------------------------------ compute */

int cp0_ui_metrics_compute(int w, int h, int rot, int ppmm_x100, cp0_ui_density_src_t density_src, int compat_cap,
                           cp0_ui_metrics_t *out)
{
    if (!out || w <= 0 || h <= 0) return -1;
    cp0_ui_metrics_t m;
    memset(&m, 0, sizeof(m)); /* padding too: equal inputs give equal bytes */
    m.w = w;
    m.h = h;
    m.rot = rot;
    m.ppmm_x100 = ppmm_x100 > 0 ? ppmm_x100 : CP0_UI_DEFAULT_PPMM_X100;
    m.density_src = ppmm_x100 > 0 ? density_src : CP0_UI_DENSITY_DEFAULT;
    m.cls = cp0_ui_classify(w, h);
    m.preset = (w == 640 && h == 480)   ? CP0_UI_PRESET_DECK
               : (w == 480 && h == 320) ? CP0_UI_PRESET_COMPACT
                                        : CP0_UI_PRESET_COMPUTED;

    const class_spec_t *k = &k_class[m.cls];
    m.tok[CP0_TOK_SPACE_S] = k->space[0];
    m.tok[CP0_TOK_SPACE_M] = k->space[1];
    m.tok[CP0_TOK_SPACE_L] = k->space[2];
    m.tok[CP0_TOK_RADIUS_S] = k->radius[0];
    m.tok[CP0_TOK_RADIUS_M] = k->radius[1];
    m.tok[CP0_TOK_RADIUS_L] = k->radius[2];
    m.tok[CP0_TOK_HEADER_H] = k->header;
    m.tok[CP0_TOK_STATUS_PCT] = k->status_pct;
    m.tok[CP0_TOK_ROW_H] = cp0_ui_mm_px(m.ppmm_x100, CP0_UI_ROW_MM_X10, CP0_UI_ROW_FLOOR_PX);
    m.tok[CP0_TOK_TARGET] = cp0_ui_mm_px(m.ppmm_x100, CP0_UI_TARGET_MM_X10, CP0_UI_TARGET_FLOOR_PX);
    m.tok[CP0_TOK_TILE_ICON] = k->tile_icon;
    m.tok[CP0_TOK_DIALOG_MAX_W] = imax(0, imin(k->dialog_max_w, w - 2 * k->space[1]));

    m.text_px[CP0_TEXT_TITLE] = cp0_ui_font_snap(k->text[0]);
    m.text_px[CP0_TEXT_HEADING] = cp0_ui_font_snap(k->text[1]);
    m.text_px[CP0_TEXT_BODY] = cp0_ui_font_snap(k->text[2]);
    m.text_px[CP0_TEXT_CAPTION] = cp0_ui_font_snap(k->text[3]);
    m.text_px[CP0_TEXT_SYMBOL] = m.text_px[CP0_TEXT_TITLE];

    m.shell_preset = m.preset != CP0_UI_PRESET_COMPUTED ? m.preset
                     : h < SHELL_COMPACT_BELOW_H        ? CP0_UI_PRESET_COMPACT
                                                        : CP0_UI_PRESET_DECK;
    shell_fill(m.shell_preset, w, h, &m.shell);
    if (m.preset == CP0_UI_PRESET_COMPUTED) shell_computed_grid(&k_class[m.cls], w, h, m.ppmm_x100, &m.shell);

    if (m.preset != CP0_UI_PRESET_COMPUTED) {
        m.tok[CP0_TOK_TOOLBAR_H] = m.preset == CP0_UI_PRESET_DECK ? PINNED_TOOLBAR_DECK : PINNED_TOOLBAR_COMPACT;
        place_pinned(w, h, compat_cap, &m.compat);
    } else {
        /* the toolbar keys (toolbar height minus the shell's vertical padding) are exactly the 9 mm target */
        m.tok[CP0_TOK_TOOLBAR_H] = m.tok[CP0_TOK_TARGET] + 2 * m.shell.tb_pad_y;
        place_rule(w, h, m.tok[CP0_TOK_TOOLBAR_H], compat_cap, &m.compat);
    }
    *out = m;
    return 0;
}

static cp0_ui_metrics_t g_current;
static int g_has_current;

void cp0_ui_metrics_set_current(const cp0_ui_metrics_t *m)
{
    if (!m) {
        g_has_current = 0;
        return;
    }
    g_current = *m;
    g_has_current = 1;
}

const cp0_ui_metrics_t *cp0_ui_metrics_current(void)
{
    return g_has_current ? &g_current : NULL;
}

/* ------------------------------------------------------------------ state file text */

int cp0_ui_state_path(const char *runtime_dir, char *buf, size_t size)
{
    if (!buf || size == 0) return -1;
    int n;
    if (runtime_dir && runtime_dir[0])
        n = snprintf(buf, size, "%s/" CP0_UI_STATE_SUBDIR "/" CP0_UI_STATE_FILE, runtime_dir);
    else
        n = snprintf(buf, size, "%s", CP0_UI_STATE_FALLBACK_PATH);
    if (n < 0 || (size_t)n >= size) {
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

int cp0_ui_state_format(const cp0_ui_metrics_t *m, unsigned gen, char *buf, size_t size)
{
    if (!m || !buf) return -1;
    const int n = snprintf(buf, size,
                           "v=%d\nw=%d\nh=%d\nrot=%d\nppmm=%d\ndensity=%s\nclass=%s\ncompat=%d,%d,%d,%d\nscale=%d\n"
                           "toolbar=%d\nkb_inset=0\ngen=%u\n",
                           CP0_UI_STATE_VERSION, m->w, m->h, m->rot, m->ppmm_x100,
                           cp0_ui_density_src_name(m->density_src), cp0_ui_class_name(m->cls), m->compat.x, m->compat.y,
                           m->compat.w, m->compat.h, m->compat.scale, m->compat.toolbar_h, gen);
    if (n < 0 || (size_t)n >= size) return -1;
    return n;
}

/* "key=<unsigned decimal>" filling the line; 1 when found */
static int line_number(const char *line, size_t len, const char *key, unsigned long long *out)
{
    const size_t key_len = strlen(key);
    if (len <= key_len || memcmp(line, key, key_len) != 0 || line[key_len] != '=') return 0;
    size_t i = key_len + 1;
    unsigned long long v = 0;
    const size_t first = i;
    for (; i < len && line[i] >= '0' && line[i] <= '9'; i++) {
        if (v > (UINT_MAX - 9) / 10) return 0;
        v = v * 10 + (unsigned)(line[i] - '0');
    }
    if (i == first) return 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i != len) return 0;
    *out = v;
    return 1;
}

unsigned cp0_ui_state_parse_gen(const char *text, size_t len)
{
    if (!text) return 0;
    int version_ok = 0;
    unsigned gen = 0;
    size_t start = 0;
    while (start < len) {
        size_t end = start;
        while (end < len && text[end] != '\n') end++;
        unsigned long long v = 0;
        if (line_number(text + start, end - start, "v", &v)) version_ok = v == CP0_UI_STATE_VERSION;
        else if (line_number(text + start, end - start, "gen", &v)) gen = (unsigned)v;
        start = end + 1;
    }
    return version_ok ? gen : 0;
}

int cp0_ui_metrics_describe(const cp0_ui_metrics_t *m, char *buf, size_t size)
{
    if (!m || !buf) return -1;
    const cp0_ui_compat_t *c = &m->compat;
    const cp0_ui_shell_t *s = &m->shell;
    const int *t = m->tok;
    const int n = snprintf(
        buf, size,
        "screen %dx%d rot %d class %s (%s)\n"
        "density %d.%02d px/mm (%s)\n"
        "tokens  space %d/%d/%d  radius %d/%d/%d  header %d  status %d%%  row %d  target %d  toolbar %d  "
        "tile_icon %d  dialog_max_w %d\n"
        "text    title %d  heading %d  body %d  caption %d  symbol %d\n"
        "compat  scale %d (rule max %d)  window %dx%d at (%d,%d)  toolbar y %d h %d\n"
        "shell   %s layout: grid %dx%d bar %d pad %d tile %dx%d top %d icon %d title %d label %d status %d%% (%d px) "
        "toolbar pad %d/%d gap %d radius %d text %d symbol %d\n",
        m->w, m->h, m->rot, cp0_ui_class_name(m->cls),
        m->preset == CP0_UI_PRESET_DECK      ? "pinned deck"
        : m->preset == CP0_UI_PRESET_COMPACT ? "pinned 480x320"
                                             : "computed",
        m->ppmm_x100 / 100, m->ppmm_x100 % 100, cp0_ui_density_src_name(m->density_src), t[CP0_TOK_SPACE_S],
        t[CP0_TOK_SPACE_M], t[CP0_TOK_SPACE_L], t[CP0_TOK_RADIUS_S], t[CP0_TOK_RADIUS_M], t[CP0_TOK_RADIUS_L],
        t[CP0_TOK_HEADER_H], t[CP0_TOK_STATUS_PCT], t[CP0_TOK_ROW_H], t[CP0_TOK_TARGET], t[CP0_TOK_TOOLBAR_H],
        t[CP0_TOK_TILE_ICON], t[CP0_TOK_DIALOG_MAX_W], m->text_px[CP0_TEXT_TITLE], m->text_px[CP0_TEXT_HEADING],
        m->text_px[CP0_TEXT_BODY], m->text_px[CP0_TEXT_CAPTION], m->text_px[CP0_TEXT_SYMBOL], c->scale, c->scale_max,
        c->w, c->h, c->x, c->y, c->toolbar_y, c->toolbar_h,
        m->shell_preset == CP0_UI_PRESET_COMPACT ? "compact" : "deck", s->cols, s->rows, s->bar_h, s->pad, s->tile_w, s->tile_h,
        s->top_extra, s->icon, s->title_px, s->label_px, s->status_pct, s->status_w, s->tb_pad_x, s->tb_pad_y, s->tb_gap,
        s->tb_radius, s->tb_text_px, s->tb_symbol_px);
    if (n < 0 || (size_t)n >= size) return -1;
    return n;
}
