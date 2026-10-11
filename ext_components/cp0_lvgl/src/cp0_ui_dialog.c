/*
 * SPDX-License-Identifier: MIT
 *
 * Dialog layout (see cp0_ui_dialog.h). Pure C: no LVGL.
 */

#include "cp0_ui_dialog.h"

#include "cp0_ui_osk.h"

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

typedef struct {
    int pad, gap, title, content, buttons, button_h;
} parts_t;

/* the box height around a body of `body` px */
static int box_h(const parts_t *p, int body)
{
    return 2 * p->pad + p->title + (p->title ? p->gap : 0) + body + (p->buttons ? p->gap + p->button_h : 0);
}

int cp0_ui_dialog_layout(const cp0_ui_metrics_t *m, int top, int inset, int title_line_h, int content_h, int buttons,
                         cp0_ui_dialog_t *out)
{
    if (!m || !out || m->w <= 0 || m->h <= 0) return -1;
    cp0_ui_dialog_t d = {0};
    d.w = m->tok[CP0_TOK_DIALOG_MAX_W] > 0 ? m->tok[CP0_TOK_DIALOG_MAX_W] : m->w;
    d.x = (m->w - d.w) / 2;
    d.gap = m->tok[CP0_TOK_SPACE_S];
    d.radius = m->tok[CP0_TOK_RADIUS_M];
    d.title_px = m->text_px[CP0_TEXT_HEADING];
    d.text_px = m->text_px[CP0_TEXT_BODY];
    d.button_px = m->text_px[CP0_TEXT_BODY];
    d.button_h = m->tok[CP0_TOK_TARGET];
    d.button_gap = d.gap;
    d.content_w = imax(0, d.w - 2 * m->tok[CP0_TOK_SPACE_M]);

    top = imax(0, imin(top, m->h));
    inset = imax(0, imin(inset, m->h));
    parts_t p = {m->tok[CP0_TOK_SPACE_M], d.gap, imax(0, title_line_h), imax(0, content_h),
                 imax(0, imin(buttons, CP0_UI_DIALOG_MAX_BUTTONS)), d.button_h};
    const int bottom = m->h - inset;
    int t = top;
    if (inset > 0) {
        /* 1. the keyboard is up: its Enter and Esc stand for the buttons */
        if (box_h(&p, p.content) > bottom - t) p.buttons = 0;
        /* 2. tighter padding, then the room over the header too */
        if (box_h(&p, p.content) > bottom - t) p.pad = m->tok[CP0_TOK_SPACE_S];
        if (box_h(&p, p.content) > bottom - t) t = 0;
    }
    /* 3. the body keeps what is left and scrolls */
    const int room = imax(0, bottom - t);
    int body = p.content;
    if (box_h(&p, body) > room) {
        body = imax(0, room - box_h(&p, 0));
        d.scrolls = body < p.content;
    }

    d.pad = m->tok[CP0_TOK_SPACE_M];
    d.pad_y = p.pad;
    d.buttons = p.buttons;
    d.h = box_h(&p, body);
    d.y = cp0_ui_osk_dialog_y(m->h, t, inset, d.h);
    d.title_y = p.pad;
    d.body_y = p.pad + p.title + (p.title ? p.gap : 0);
    d.body_h = body;
    d.button_y = p.buttons ? d.body_y + body + p.gap : 0;
    d.button_w = p.buttons ? imax(0, (d.content_w - (p.buttons - 1) * d.button_gap) / p.buttons) : 0;
    d.covers_header = d.y < top;
    *out = d;
    return 0;
}
