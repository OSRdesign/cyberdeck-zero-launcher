/*
 * SPDX-License-Identifier: MIT
 *
 * Roller maths and the roller page layout of the native Settings (task 014 P2a, decision D1: today's centre-highlight
 * roller look, full screen, touch native). Pure C, no LVGL: unit-tested on the PC by tests/test_ui_roller.c. The
 * LVGL widgets that draw it are in cp0_ui_widgets.hpp.
 *
 * Every size comes from the layout service (cp0_ui_metrics.h): rows are the ROW token (7 mm), the title and the status
 * strip stay exactly where the home grid draws them (shell title_x / title_y / title_px, status bar, status_top), the
 * header is the home grid's status bar, text sizes are snapped to the font set (even px 8..48, the larger one when a
 * size is missing).
 *
 * Roller look (approved design, Controller review 2026-10-11): black ground; the centre row is highlighted by a dark
 * bar (#2b2b2b) across the roller, its text is 0.43 x the row height and bold, values on the right 0.9 x that; the rows
 * above and below get smaller by 8 % per step (at least 70 %) and fainter by 20 % per step (at least 35 %), so two or
 * three rows each side stay readable and rows reach the top and the bottom of the body; orange chevrons (#ff6a3d,
 * 0.55 x the row height) at the top / bottom while more rows exist that way. The roller width is capped (560 px,
 * 640 px on the Large class) and centred; it fills the screen height under the header.
 */
#pragma once

#include "cp0_ui_metrics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_UI_ROLLER_CAP_W 560        /* roller width cap: Compact, Standard, Wide, Square, Portrait ... */
#define CP0_UI_ROLLER_CAP_W_LARGE 640  /* ... and on the Large class */
#define CP0_UI_ROLLER_TEXT_PCT 43      /* centre row text: 43 % of the row height ... */
#define CP0_UI_ROLLER_VALUE_PCT 90     /* ... values on the right 90 % of that */
#define CP0_UI_ROLLER_SHRINK_PCT 8     /* each step away from the centre: text 8 % smaller ... */
#define CP0_UI_ROLLER_SHRINK_MIN_PCT 70 /* ... never under 70 % ... */
#define CP0_UI_ROLLER_FADE_PCT 20      /* ... and 20 % more transparent ... */
#define CP0_UI_ROLLER_FADE_MIN_PCT 35  /* ... never under 35 % */
#define CP0_UI_ROLLER_CHEVRON_PCT 55   /* chevrons: 55 % of the row height */
#define CP0_UI_ROLLER_BAR_RGB 0x2B2B2B
#define CP0_UI_ROLLER_CHEVRON_RGB 0xFF6A3D

typedef struct {
    int w, h; /* screen (logical canvas) */

    /* header: the home grid's status bar. Title (home place and size), the on-screen Esc, the status strip. */
    int header_h;                            /* = the home grid's status bar height */
    int title_x, title_y, title_px;          /* = the home grid's title */
    int title_max_w;                         /* up to the Esc button */
    int strip_x, strip_y, strip_w, strip_h;  /* the shared status strip canvas, exactly the home grid's */
    int strip_left;                          /* the leftmost pixel it draws (its Bluetooth icon) */
    int esc_x, esc_y, esc_w, esc_h;          /* TARGET wide; TARGET high but not above header_h - 2 status_top;
                                                centred on the clock pill's line; SPACE_M left of strip_left */
    int esc_radius, esc_text_px;             /* the stock-app toolbar keys' look */

    /* one status line, laid over the top left of the body (value pages' messages, refusals) */
    int caption_x, caption_y, caption_w, caption_px;

    /* body: the roller */
    int body_y, body_h;     /* under the header to the bottom of the screen */
    int roller_x, roller_w; /* capped, centred */
    int row_h;              /* ROW token */
    int edge_pad;           /* room above the first / below the last row so that they reach the centre */
    int list_h;             /* row_h + 2 * edge_pad (<= body_h) */
    int text_px;            /* centre row text: 0.43 x row_h, snapped */
    int value_px;           /* centre row value: 0.9 x text_px, snapped */
    int inset;              /* label and value inset inside the roller: SPACE_L */
    int gap;                /* between a label and its value: SPACE_M */
    int chevron_px;         /* chevron width: 0.55 x row_h (drawn as lines, any size) */
} cp0_ui_roller_page_t;

/* The roller page for the metrics m. strip_left: the screen x of the leftmost pixel the shared status strip draws
 * (its Bluetooth icon; the launcher measures it with the strip renderer), <= 0 or past the screen = the strip canvas'
 * left edge. Returns 0, or -1 (out untouched) when m is NULL or the screen has no room. */
int cp0_ui_roller_page(const cp0_ui_metrics_t *m, int strip_left, cp0_ui_roller_page_t *out);

/* Roller width: screen width minus a margin on each side, capped by the class (CP0_UI_ROLLER_CAP_W[_LARGE]). */
int cp0_ui_roller_width(cp0_ui_class_t cls, int screen_w, int margin);

/* A text size the font set has: even, CP0_UI_FONT_MIN_PX..CP0_UI_FONT_MAX_PX, an odd px goes to the larger size. */
int cp0_ui_roller_font_px(int px);

/* Text size of a row `distance` rows away from the centre: centre_px x max(1 - 0.08 x distance, 0.7), rounded, then
 * cp0_ui_roller_font_px (never larger than the centre size once snapped). */
int cp0_ui_roller_text_px(int centre_px, int distance);

/* Opacity (0..255) of a row `distance` rows away from the centre: 255 x max(1 - 0.2 x distance, 0.35), rounded. */
int cp0_ui_roller_opa(int distance);

/* Padding above the first and below the last row: (body_h - row_h) / 2, at least 0. */
int cp0_ui_roller_edge_pad(int body_h, int row_h);

/* The row in the centre (under the highlight bar) for a scroll offset of the list: round(scroll_y / row_h) clamped
 * to 0..count-1 (rows are laid out from edge_pad, so row i is centred at scroll_y = i * row_h). -1 when count <= 0. */
int cp0_ui_roller_centre(int scroll_y, int row_h, int count);

/* Where a scroll comes to rest: the scroll offset of the nearest centred row (centre * row_h). 0 when count <= 0. */
int cp0_ui_roller_snap(int scroll_y, int row_h, int count);

/* The row under the point y of the roller body (0 = its top), or -1 for the empty room above / below the rows. */
int cp0_ui_roller_row_at(int y, int scroll_y, int edge_pad, int row_h, int count);

/* index + delta inside 0..count-1: wraps around when wrap is set (lists), else stops at the ends (value lists).
 * -1 when count <= 0. */
int cp0_ui_roller_step(int index, int delta, int count, int wrap);

#ifdef __cplusplus
}
#endif
