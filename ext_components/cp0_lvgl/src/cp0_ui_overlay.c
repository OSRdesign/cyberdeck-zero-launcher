/*
 * SPDX-License-Identifier: MIT
 *
 * Overlay sizes of the responsive shell (cp0_ui_overlay.h, task 014 P4).
 */
#include "cp0_ui_overlay.h"

/* round-half-up v * num / den (v >= 0) */
static int sc(int v, int num, int den)
{
    return (v * num + den / 2) / den;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

int cp0_ui_overlay_is_compat_display(int display_w)
{
    return display_w <= CP0_UI_OVERLAY_COMPAT_MAX_W;
}

/* 0: today's numbers (factor 1), 1: pinned DECK (2), 2: pinned COMPACT (3/2), 3: computed. */
static int mode_of(const cp0_ui_metrics_t *m, int display_w)
{
    if (!m || cp0_ui_overlay_is_compat_display(display_w)) return 0;
    if (m->w == 640 && m->h == 480 && m->preset == CP0_UI_PRESET_DECK && display_w == 640) return 1;
    if (m->w == 480 && m->h == 320 && m->preset == CP0_UI_PRESET_COMPACT && display_w == 480) return 2;
    return 3;
}

static void factor_of(int mode, int *num, int *den)
{
    *num = mode == 1 ? 2 : mode == 2 ? 3 : 1;
    *den = mode == 2 ? 2 : 1;
}

void cp0_ui_overlay_toast(const cp0_ui_metrics_t *m, int display_w, int lines, cp0_ui_toast_t *out)
{
    const int mode = mode_of(m, display_w);
    const int multi = lines >= 2;
    if (mode != 3) {
        int num, den;
        factor_of(mode, &num, &den);
        out->w = sc(CP0_UI_RIBBON_W, num, den);
        out->h = sc(multi ? 38 : CP0_UI_RIBBON_H, num, den);
        out->top = sc(CP0_UI_RIBBON_Y, num, den);
        out->radius = sc(6, num, den);
        out->font_px = sc(12, num, den);
        out->label_w = sc(CP0_UI_RIBBON_W - 12, num, den);
        return;
    }
    {
        const int space_s = m->tok[CP0_TOK_SPACE_S], space_m = m->tok[CP0_TOK_SPACE_M];
        const int space_l = m->tok[CP0_TOK_SPACE_L];
        const int font = m->text_px[CP0_TEXT_BODY];
        const int line_h = font + font / 4;
        const int n = lines < 1 ? 1 : lines > 3 ? 3 : lines;
        int w = display_w - 2 * space_l;
        if (m->tok[CP0_TOK_DIALOG_MAX_W] > 0) w = imin(w, m->tok[CP0_TOK_DIALOG_MAX_W]);
        out->w = imax(w, 2 * space_m + font);
        out->h = n * line_h + 2 * space_s;
        out->top = space_s;
        out->radius = imin(m->tok[CP0_TOK_RADIUS_M], out->h / 2);
        out->font_px = font;
        out->label_w = out->w - 2 * space_m;
    }
}

static void orect(cp0_ui_orect_t *r, int x, int y, int w, int h, int num, int den)
{
    r->x = sc(x, num, den);
    r->y = sc(y, num, den);
    r->w = sc(w, num, den);
    r->h = sc(h, num, den);
}

static void osd_fill(cp0_ui_osd_t *o, int num, int den)
{
    o->w = sc(190, num, den);
    o->h = sc(82, num, den);
    o->radius = sc(8, num, den);
    o->border = imax(1, sc(1, num, den));
    o->font_px = sc(12, num, den);
    orect(&o->icon, 14, 10, 30, 24, num, den);
    orect(&o->title, 48, 12, 76, 18, num, den);
    orect(&o->value, 124, 12, 48, 18, num, den);
    orect(&o->bar, 18, 50, 154, 10, num, den);
    o->bar_radius = sc(5, num, den);
    orect(&o->mute_icon, 65, 12, 60, 32, num, den);
    orect(&o->mute_title, 0, 50, 190, 18, num, den);
}

void cp0_ui_overlay_osd(const cp0_ui_metrics_t *m, int display_w, int display_h, cp0_ui_osd_t *out)
{
    const int mode = mode_of(m, display_w);
    int num, den;
    if (mode != 3) {
        factor_of(mode, &num, &den);
        osd_fill(out, num, den);
        out->icon_px = mode == 0 ? 0 : cp0_ui_font_snap(sc(14, num, den));
        return;
    }
    /* the body font over today's 12 px, lowered until the box fits with SPACE_L around it */
    den = 12;
    num = imax(m->text_px[CP0_TEXT_BODY], 12);
    {
        const int room_w = display_w - 2 * m->tok[CP0_TOK_SPACE_L];
        const int room_h = display_h - 2 * m->tok[CP0_TOK_SPACE_L];
        while (num > 12 && (sc(190, num, den) > room_w || sc(82, num, den) > room_h)) num--;
    }
    osd_fill(out, num, den);
    out->icon_px = cp0_ui_font_snap(sc(14, num, den));
}

void cp0_ui_overlay_loading(const cp0_ui_metrics_t *m, int display_w, int display_h, cp0_ui_loading_t *out)
{
    const int mode = mode_of(m, display_w);
    int num, den;
    if (mode != 3) {
        factor_of(mode, &num, &den);
    } else {
        den = 14;
        num = imax(m->text_px[CP0_TEXT_BODY], 14);
        {
            const int room_w = display_w - 2 * m->tok[CP0_TOK_SPACE_L];
            const int room_h = display_h - 2 * m->tok[CP0_TOK_SPACE_L];
            while (num > 14 && (sc(200, num, den) > room_w || sc(40, num, den) > room_h)) num--;
        }
    }
    out->w = sc(200, num, den);
    out->h = sc(40, num, den);
    out->radius = sc(8, num, den);
    out->pad = sc(6, num, den);
    out->gap = sc(8, num, den);
    out->spinner = sc(24, num, den);
    out->arc_w = imax(1, sc(3, num, den));
    out->font_px = sc(14, num, den);
}
