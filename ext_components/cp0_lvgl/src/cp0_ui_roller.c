/*
 * SPDX-License-Identifier: MIT
 *
 * Roller maths and the roller page layout of the native Settings (see cp0_ui_roller.h). Pure C: no LVGL.
 */

#include "cp0_ui_roller.h"

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

int cp0_ui_roller_width(cp0_ui_class_t cls, int screen_w, int margin)
{
    const int cap = cls == CP0_UI_LARGE ? CP0_UI_ROLLER_CAP_W_LARGE : CP0_UI_ROLLER_CAP_W;
    return imax(0, imin(cap, screen_w - 2 * imax(0, margin)));
}

int cp0_ui_roller_caption_lines(const cp0_ui_roller_page_t *p, int line_h)
{
    if (!p || line_h <= 0) return 1;
    const int room = p->body_y + p->edge_pad - p->gap - p->caption_y; /* down to the highlight bar, a gap above it */
    return imax(1, imin(CP0_UI_ROLLER_CAPTION_MAX_LINES, room / line_h));
}

void cp0_ui_roller_chevron_size(const cp0_ui_roller_page_t *p, int *w, int *h, int *stroke)
{
    const int cw = p ? imax(8, p->chevron_px) : 0;
    const int s = p ? imax(3, cw / 8) : 0;
    if (w) *w = p ? cw + 2 * s : 0;
    if (h) *h = p ? cw / 2 + 2 * s : 0;
    if (stroke) *stroke = s;
}

void cp0_ui_roller_up_chevron_box(const cp0_ui_roller_page_t *p, int *x, int *y, int *w, int *h)
{
    int cw = 0, ch = 0;
    cp0_ui_roller_chevron_size(p, &cw, &ch, NULL);
    if (x) *x = p ? p->roller_x + (p->roller_w - cw) / 2 : 0; /* LV_ALIGN_TOP_MID in the roller's body */
    if (y) *y = p ? p->body_y + p->gap / 2 : 0;
    if (w) *w = cw;
    if (h) *h = ch;
}

int cp0_ui_roller_caption_clear_w(const cp0_ui_roller_page_t *p)
{
    if (!p) return 0;
    int x = 0;
    cp0_ui_roller_up_chevron_box(p, &x, NULL, NULL, NULL);
    const int clear = x - p->gap - p->caption_x;
    if (clear >= p->caption_w) return p->caption_w;
    if (clear * 3 < p->caption_w) return p->caption_w; /* too little room: the line keeps its width */
    return clear;
}

int cp0_ui_roller_caption_low_y(const cp0_ui_roller_page_t *p)
{
    if (!p) return 0;
    int y = 0, h = 0;
    cp0_ui_roller_up_chevron_box(p, NULL, &y, NULL, &h);
    return imax(p->caption_y, y + h + p->gap / 2);
}

int cp0_ui_roller_font_px(int px)
{
    if (px < CP0_UI_FONT_MIN_PX) return CP0_UI_FONT_MIN_PX;
    if (px > CP0_UI_FONT_MAX_PX) return CP0_UI_FONT_MAX_PX;
    return (px + 1) & ~1; /* the even sizes exist: an odd one takes the next larger */
}

/* round(value * pct / 100) for non-negative values */
static int pct_of(int value, int pct)
{
    return (value * pct + 50) / 100;
}

int cp0_ui_roller_text_px(int centre_px, int distance)
{
    if (centre_px <= 0) return 0;
    if (distance < 0) distance = -distance;
    if (distance > 100) distance = 100;
    const int pct = imax(CP0_UI_ROLLER_SHRINK_MIN_PCT, 100 - CP0_UI_ROLLER_SHRINK_PCT * distance);
    return imin(cp0_ui_roller_font_px(pct_of(centre_px, pct)), cp0_ui_roller_font_px(centre_px));
}

int cp0_ui_roller_opa(int distance)
{
    if (distance < 0) distance = -distance;
    if (distance > 100) distance = 100;
    const int pct = imax(CP0_UI_ROLLER_FADE_MIN_PCT, 100 - CP0_UI_ROLLER_FADE_PCT * distance);
    return pct_of(255, pct);
}

int cp0_ui_roller_edge_pad(int body_h, int row_h)
{
    return imax(0, (body_h - row_h) / 2);
}

int cp0_ui_roller_centre(int scroll_y, int row_h, int count)
{
    if (count <= 0) return -1;
    if (row_h <= 0 || scroll_y <= 0) return 0;
    return imin(count - 1, (scroll_y + row_h / 2) / row_h);
}

int cp0_ui_roller_snap(int scroll_y, int row_h, int count)
{
    if (count <= 0 || row_h <= 0) return 0;
    return cp0_ui_roller_centre(scroll_y, row_h, count) * row_h;
}

int cp0_ui_roller_row_at(int y, int scroll_y, int edge_pad, int row_h, int count)
{
    if (count <= 0 || row_h <= 0) return -1;
    const int content_y = y + scroll_y - edge_pad;
    if (content_y < 0) return -1;
    const int row = content_y / row_h;
    return row < count ? row : -1;
}

int cp0_ui_roller_step(int index, int delta, int count, int wrap)
{
    if (count <= 0) return -1;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    if (wrap) return (int)(((long long)index + delta % count + count) % count);
    return imax(0, imin(count - 1, index + delta));
}

int cp0_ui_roller_page(const cp0_ui_metrics_t *m, int strip_left, cp0_ui_roller_page_t *out)
{
    if (!m || !out || m->w <= 0 || m->h <= 0) return -1;
    const cp0_ui_shell_t *s = &m->shell;
    const int space_m = m->tok[CP0_TOK_SPACE_M];
    cp0_ui_roller_page_t p = {0};
    p.w = m->w;
    p.h = m->h;

    /* the home grid's status bar, status strip and title, unchanged */
    p.header_h = s->bar_h;
    p.strip_w = s->status_w;
    p.strip_x = m->w - s->status_w;
    p.strip_y = 0;
    p.strip_h = s->bar_h;
    p.strip_left = strip_left > p.strip_x && strip_left < m->w ? strip_left : p.strip_x;
    p.title_x = s->title_x;
    p.title_y = s->title_y;
    p.title_px = s->title_px;

    /* the on-screen Esc: a 9 mm toolbar key wide (not TARGET, 7 mm since 2026-10-18: the header Esc keeps its size,
     * the same in the launcher and every app), as high but inside the bar's margins (status_top above and below, like
     * the clock pill), centred on the pill's line, right-aligned SPACE_M left of the strip's leftmost slot (the SSH
     * indicator's, kept free even without a session: owner decision 2026-10-30) */
    const int esc_key = cp0_ui_toolbar_key_px(m->ppmm_x100);
    p.esc_w = esc_key;
    p.esc_h = imax(1, imin(esc_key, p.header_h - 2 * s->status_top));
    p.esc_y = (p.header_h - p.esc_h) / 2;
    p.esc_x = imax(p.title_x, p.strip_left - space_m - p.esc_w);
    p.esc_radius = s->tb_radius;
    p.esc_text_px = s->tb_text_px;
    p.title_max_w = imax(0, p.esc_x - space_m - p.title_x);
    p.title_last_only = cp0_ui_narrow_header(m);
    if (cp0_ui_narrow_portrait(m)) {
        /* 320x480: Esc 44 x 30 (TARGET wide, the bar minus its margins high) at the left edge, SPACE_M from it; the tap
         * area is stretched over the whole bar height; no title (study portrait-320, owner decisions 2026-11-03) */
        p.esc_w = m->tok[CP0_TOK_TARGET];
        p.esc_h = imax(1, p.header_h - 2 * s->status_top);
        p.esc_y = (p.header_h - p.esc_h) / 2;
        p.esc_x = space_m;
        p.esc_hit_pad = p.esc_y;
        p.title_max_w = 0;
        p.title_last_only = 0;
        p.title_hidden = 1;
    }

    /* body */
    p.body_y = p.header_h;
    p.body_h = m->h - p.header_h;
    if (p.body_h <= 0) return -1;
    p.roller_w = cp0_ui_roller_width(m->cls, m->w, space_m);
    p.roller_x = (m->w - p.roller_w) / 2;
    p.row_h = imin(m->tok[CP0_TOK_ROW_H], p.body_h);
    p.edge_pad = cp0_ui_roller_edge_pad(p.body_h, p.row_h);
    p.list_h = p.row_h + 2 * p.edge_pad;
    p.text_px = cp0_ui_roller_font_px(pct_of(p.row_h, CP0_UI_ROLLER_TEXT_PCT));
    p.value_px = cp0_ui_roller_font_px(pct_of(p.text_px, CP0_UI_ROLLER_VALUE_PCT));
    p.inset = m->tok[CP0_TOK_SPACE_L];
    p.gap = space_m;
    p.chevron_px = pct_of(p.row_h, CP0_UI_ROLLER_CHEVRON_PCT);

    /* status line: over the top left of the body, the full content width (a margin of title_x on each side). It sits
     * above the highlight bar (cp0_ui_roller_caption_lines): a line half as wide cut "FantaCitron  192.168.68...." at
     * 640x480 (the address is what the line is for). It shares its band with the up scroll chevron: a wrapping line
     * keeps clear of it (cp0_ui_roller_caption_clear_w). */
    p.caption_px = m->text_px[CP0_TEXT_CAPTION];
    p.caption_x = p.title_x;
    p.caption_y = p.body_y + m->tok[CP0_TOK_SPACE_S];
    p.caption_w = imax(0, m->w - 2 * p.caption_x);
    *out = p;
    return 0;
}
