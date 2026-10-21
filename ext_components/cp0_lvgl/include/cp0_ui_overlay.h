/*
 * SPDX-License-Identifier: MIT
 *
 * Sizes of the shell's overlays (task 014 P4: toast, hold-Esc ribbon, media OSD, "Loading..."), from the layout
 * service's tokens. Pure C, no LVGL: unit-tested by tests/test_ui_overlay.c.
 *
 * The rule is by DISPLAY, never by metrics alone (cp0_ui_metrics_get() falls back to the default display's metrics,
 * so a 320x170 window classifies as a computed Compact screen):
 *   - a display at most CP0_UI_OVERLAY_COMPAT_MAX_W wide (the legacy-app compat window, the SDL
 *     320x170 simulator) or a missing metrics pointer gets exactly the numbers the overlays always had;
 *   - the native display gets: the pinned DECK (640x480) and COMPACT (480x320) tables, which are today's rendering
 *     on those screens (today's numbers x 2 and x 3/2: the old "scaled by width / 320" rule), and for every other
 *     size a size computed from the tokens (toast) or today's box scaled by the body font (OSD, loading), so they
 *     stay readable on 720x720 and 1280x720.
 */
#pragma once

#include "cp0_ui_metrics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_UI_OVERLAY_COMPAT_MAX_W 320

/* The hold-Esc ribbon's rectangle on the compat display (the toast's compat rectangle, 280x22 at the top centre of
 * 320 px): reserved with cp0_display_set_overlay_rect() while the ribbon shows over a stock app. */
#define CP0_UI_RIBBON_X 20
#define CP0_UI_RIBBON_Y 4
#define CP0_UI_RIBBON_W 280
#define CP0_UI_RIBBON_H 22

typedef struct {
    int x, y, w, h;
} cp0_ui_orect_t;

/* 1 when a display this wide keeps today's overlay numbers. */
int cp0_ui_overlay_is_compat_display(int display_w);

typedef struct {
    int w, h;          /* box; centred at the top */
    int top;           /* distance from the top edge */
    int radius;
    int font_px;       /* FreeType (AlibabaPuHuiTi bold) size */
    int label_w;       /* wrapping width of the text */
} cp0_ui_toast_t;

/* lines: 1, or 2 and more (a newline in the text). m may be NULL (compat numbers). */
void cp0_ui_overlay_toast(const cp0_ui_metrics_t *m, int display_w, int lines, cp0_ui_toast_t *out);

typedef struct {
    int w, h;          /* box; centred on the display */
    int radius, border;
    int font_px;       /* FreeType size of the title and the value */
    int icon_px;       /* Montserrat size of the icon glyph; 0 = LV_FONT_DEFAULT (compat) */
    cp0_ui_orect_t icon, title, value;      /* level: icon, title on the left, value on the right */
    cp0_ui_orect_t bar;
    int bar_radius;
    cp0_ui_orect_t mute_icon, mute_title;   /* mute: big icon, centred title under it */
} cp0_ui_osd_t;

void cp0_ui_overlay_osd(const cp0_ui_metrics_t *m, int display_w, int display_h, cp0_ui_osd_t *out);

typedef struct {
    int w, h;          /* pill; centred on the display */
    int radius, pad, gap;
    int spinner, arc_w;
    int font_px;       /* FreeType size */
} cp0_ui_loading_t;

void cp0_ui_overlay_loading(const cp0_ui_metrics_t *m, int display_w, int display_h, cp0_ui_loading_t *out);

#ifdef __cplusplus
}
#endif
