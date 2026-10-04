/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's top-right status bar (clock pill, Wi-Fi bars, Bluetooth rune), drawn into an ARGB8888 pixel
 * buffer. One implementation shared by the launcher's native screens (home grid, Calculator) and by
 * full-screen apps (the display bridge of viz1090), so the bar looks exactly the same everywhere.
 *
 * Geometry, colours and thresholds are the ones of the home grid (native_ui.cpp): a 104x40 brown clock pill
 * 16 px from the right edge and 8 px from the top, four Wi-Fi bars (8 px wide, 12 px pitch) left of it and
 * the Bluetooth glyph left of the bars. Text and glyph are rendered with FreeType from the same fonts the
 * launcher's builtin Montserrat 28 is made of (Montserrat-Medium.ttf and the FontAwesome symbol font).
 *
 * Plain C99, needs only FreeType. Not thread safe: use one instance per thread.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_STATUSBAR_HEIGHT 56          /* rows of the strip at the top of the screen that the bar uses */

typedef struct {
    int wifi_up;                         /* connected */
    int wifi_pct;                        /* 0..100 */
    int bt_on;                           /* adapter on: the glyph is shown */
    int bt_connected;                    /* a device is connected: the glyph is blue */
    char clock[8];                       /* "HH:MM" */
} cp0_statusbar_state_t;

typedef struct cp0_statusbar cp0_statusbar_t;

/* text_font: Montserrat-Medium.ttf, icon_font: FontAwesome5-Solid+Brands+Regular.woff. NULL on failure. */
cp0_statusbar_t *cp0_statusbar_create(const char *text_font, const char *icon_font);
void cp0_statusbar_destroy(cp0_statusbar_t *bar);

/* Reads Wi-Fi / Bluetooth state from sysfs and procfs and the time from the system clock (what an app that
 * has no access to the launcher's own services can see). Cheap, but not meant for every frame: ~2 s. */
void cp0_statusbar_read_state(cp0_statusbar_state_t *state);

/* Draws the bar into the top CP0_STATUSBAR_HEIGHT rows of an ARGB8888 buffer `width` pixels wide
 * (`stride_px` pixels per row). The pixels it does not touch are left as they are, so the buffer can be a
 * transparent overlay or the finished frame of an app.
 *   shift_left: extra space kept free at the right edge (for a close button).
 *   top: y of the clock pill (the launcher uses 8).
 *   backing_alpha: 0..255, a translucent black strip behind the icons (0 for none). */
void cp0_statusbar_render(cp0_statusbar_t *bar, uint32_t *argb, int width, int stride_px,
                          int shift_left, int top, int backing_alpha, const cp0_statusbar_state_t *state);

#ifdef __cplusplus
}
#endif
