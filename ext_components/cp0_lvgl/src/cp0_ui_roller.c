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

    /* the on-screen Esc: TARGET wide, TARGET high but inside the bar's margins (status_top above and below, like the
     * clock pill), centred on the pill's line, right-aligned SPACE_M left of the strip's Bluetooth icon */
    p.esc_w = m->tok[CP0_TOK_TARGET];
    p.esc_h = imax(1, imin(m->tok[CP0_TOK_TARGET], p.header_h - 2 * s->status_top));
    p.esc_y = (p.header_h - p.esc_h) / 2;
    p.esc_x = imax(p.title_x, p.strip_left - space_m - p.esc_w);
    p.esc_radius = s->tb_radius;
    p.esc_text_px = s->tb_text_px;
    p.title_max_w = imax(0, p.esc_x - space_m - p.title_x);

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

    /* status line: over the top left of the body, clear of the centred chevron */
    p.caption_px = m->text_px[CP0_TEXT_CAPTION];
    p.caption_x = p.title_x;
    p.caption_y = p.body_y + m->tok[CP0_TOK_SPACE_S];
    p.caption_w = imax(0, m->w / 2 - p.chevron_px - space_m - p.caption_x);
    *out = p;
    return 0;
}
