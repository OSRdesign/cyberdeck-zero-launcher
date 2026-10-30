/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's top-right status bar (clock pill, Wi-Fi bars, Bluetooth rune), drawn into an ARGB8888 pixel
 * buffer. One implementation shared by the launcher's native screens (home grid, Calculator) and by
 * full-screen apps (the display bridge of viz1090), so the bar looks exactly the same everywhere.
 *
 * Geometry, colours and thresholds are the ones of the home grid (native_ui.cpp): a 104x40 brown clock pill
 * 16 px from the right edge and 8 px from the top, four Wi-Fi bars (8 px wide, 12 px pitch) left of it and
 * the Bluetooth glyph left of the bars; a battery, when the state has one, left of the Bluetooth glyph's place
 * (its level glyph from the symbol font, its percentage at 18 px); the SSH indicator, when the state has an incoming
 * SSH session, in the leftmost slot (the terminal glyph). Text and glyphs are rendered with FreeType
 * from the same fonts the launcher's builtin Montserrat 28 is made of (Montserrat-Medium.ttf and the
 * FontAwesome symbol font).
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
    /* A battery (decision X12; 0 = none, nothing drawn: a zeroed state looks as before): its level glyph and its
     * charge in percent, left of the Bluetooth glyph's place, white; green (the Wi-Fi bars' colour) while charging. */
    int battery;                         /* a battery is present */
    int battery_pct;                     /* 0..100 */
    int battery_charging;                /* charging */
    /* The SSH indicator (2026-10-30; 0 = no incoming SSH session, nothing drawn: a zeroed state looks as before): the
     * terminal glyph U+F120 (">_") in the leftmost slot, left of the battery's level glyph, else left of the Bluetooth
     * glyph's place; the Bluetooth "connected" blue, white (the battery's colour) while ssh_bright. With a battery its
     * percentage is left out while the glyph is shown (the level glyph stays), so the strip keeps its width. */
    int ssh;                             /* incoming SSH connections (0 = none) */
    int ssh_active;                      /* bytes moved on one since the source's previous sample: the strip then
                                            alternates ssh_bright on its 1 s tick. Not drawn itself */
    int ssh_bright;                      /* the blink phase: the glyph white instead of blue */
} cp0_statusbar_state_t;

typedef struct cp0_statusbar cp0_statusbar_t;

/* text_font: Montserrat-Medium.ttf, icon_font: FontAwesome5-Solid+Brands+Regular.woff. NULL on failure. */
cp0_statusbar_t *cp0_statusbar_create(const char *text_font, const char *icon_font);
void cp0_statusbar_destroy(cp0_statusbar_t *bar);

/* Reads Wi-Fi / Bluetooth state from sysfs and procfs and the time from the system clock (what an app that
 * has no access to the launcher's own services can see). Cheap, but not meant for every frame: ~2 s. The battery
 * fields stay 0 (cp0::ui::StatusStrip reads the battery itself, cp0_power_supply.h), and so do the SSH fields (only the
 * launcher looks for SSH sessions: status.state ssh=, cp0_app_contract.h). */
void cp0_statusbar_read_state(cp0_statusbar_state_t *state);

/* Draws the bar into the top CP0_STATUSBAR_HEIGHT rows of an ARGB8888 buffer `width` pixels wide
 * (`stride_px` pixels per row). The pixels it does not touch are left as they are, so the buffer can be a
 * transparent overlay or the finished frame of an app.
 *   shift_left: extra space kept free at the right edge (for a close button).
 *   top: y of the clock pill (the launcher uses 8).
 *   backing_alpha: 0..255, a translucent black strip behind the icons (0 for none). */
void cp0_statusbar_render(cp0_statusbar_t *bar, uint32_t *argb, int width, int stride_px,
                          int shift_left, int top, int backing_alpha, const cp0_statusbar_state_t *state);

/* Same bar with every dimension (and the font) scaled to pct percent (25..100; 100 = cp0_statusbar_render
 * exactly), for smaller panels such as the 480x320 one. `top` is in output pixels. */
void cp0_statusbar_render_scaled(cp0_statusbar_t *bar, uint32_t *argb, int width, int stride_px,
                                 int shift_left, int top, int backing_alpha, const cp0_statusbar_state_t *state,
                                 int pct);

#ifdef __cplusplus
}
#endif
