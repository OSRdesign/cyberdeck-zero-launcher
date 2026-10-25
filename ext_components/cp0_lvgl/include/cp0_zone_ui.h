/*
 * SPDX-License-Identifier: MIT
 *
 * The writing zone's LVGL side (report 041, PDA phase S3; the geometry, the router and the typing rules are the pure
 * core, cp0_zone.h). It draws the band on the display manager's third display (cp0_lvgl_dpi_scaled.c): the key strip,
 * the soft buttons Home / Menu / Apps / Keys, the letters and digits boxes with their labels and the mode line, and the
 * ink of the stroke being written; it owns the Strokes recogniser (strokes.h) and its timer and types what it
 * recognises through cp0_keyboard_inject_key(), as the on-screen keyboard does. LVGL thread only.
 *
 * The recogniser's tables: $APPLAUNCH_STROKES_DIR (default /usr/share/APPLaunch/strokes)/glyphs.txt, then glyphs-extra.txt
 * (the accents and the rarer characters; APPLAUNCH_STROKES_EXTRA=0 leaves it out), then the user overlay
 * $APPLAUNCH_STROKES_USER (default $XDG_CONFIG_HOME/applaunch/strokes-user.txt, else ~/.config/applaunch/...; "" = none).
 * A table that cannot be read is logged ("[zone]") and skipped: every stroke is then refused. APPLAUNCH_STROKES_LOG=FILE
 * (development) appends every stroke to FILE in the corpus format of tools/strokes (label "?", the result in a comment
 * line before it), for strokes-replay and the S6 tuning.
 */
#ifndef CP0_ZONE_UI_H
#define CP0_ZONE_UI_H

#include "cp0_zone.h"
#include "lvgl/lvgl.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CP0_ZONE_ACTION_HOME = 1, /* the Home soft button */
    CP0_ZONE_ACTION_APPS = 2, /* the Apps soft button */
} cp0_zone_action_t;

/* Home and Apps are the host's (the launcher: its home grid, the multitasking switcher); without a callback they do
 * nothing. Menu sends Ctrl+O (the options key) and Keys opens / closes the strip by themselves. */
typedef void (*cp0_zone_action_cb_t)(cp0_zone_action_t action);
/* The strip opened (1) or closed (0): the display manager moves the line between the app area and the band. */
typedef void (*cp0_zone_strip_cb_t)(int open);

/* Builds the band on `display` (layout->w x layout->band_open; its row 0 is the panel's row layout->strip_y), loads the
 * tables, creates the recogniser and its timer. clock_us: the time base of the pen events (the touch frames' t_us).
 * 0, or -1 (nothing built). Once. */
int cp0_zone_ui_create(lv_display_t *display, const cp0_zone_layout_t *layout, int64_t (*clock_us)(void));
/* 1 once created. */
int cp0_zone_ui_active(void);
/* The layout given at creation, NULL before. */
const cp0_zone_layout_t *cp0_zone_ui_layout(void);

/* A pen event of the router (panel coordinates): the recogniser and the ink. */
void cp0_zone_ui_pen(const cp0_zone_pen_t *pen);
/* The recogniser's drop-out window, for the router: heal_ms and a distance in px that surely covers it. */
void cp0_zone_ui_heal(int *ms, int *px);

int cp0_zone_ui_strip_open(void);
/* Opens / closes the strip (the Keys button does it too). Closing sends what the strip owes (an arrow's release,
 * Alt's) and turns its modifiers off. */
void cp0_zone_ui_set_strip_open(int open);
void cp0_zone_ui_set_strip_cb(cp0_zone_strip_cb_t cb);
void cp0_zone_ui_set_action_cb(cp0_zone_action_cb_t cb);

/* The screensaver is up: the band is drawn black (its touches are swallowed by the display manager anyway). */
void cp0_zone_ui_set_blank(int on);
/* The panel goes to an app or comes back: the stroke in progress is dropped (no result), the strip's owed releases go
 * out, the recogniser's modes (shift, punctuation, an accent) are cleared. */
void cp0_zone_ui_reset_input(void);

/* A line for the start-up log: the band's sizes, the tables loaded. */
const char *cp0_zone_ui_describe(void);

#ifdef __cplusplus
}
#endif

#endif /* CP0_ZONE_UI_H */
