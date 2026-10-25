/*
 * SPDX-License-Identifier: MIT
 *
 * The writing zone's LVGL side (report 041, PDA phase S3; the geometry, the router and the typing rules are the pure
 * core, cp0_zone.h). It draws the band on the display manager's third display (cp0_lvgl_dpi_scaled.c): the key strip,
 * the letters and digits boxes with their labels and the mode line, and the ink of the stroke being written; it owns
 * the Strokes recogniser (strokes.h) and its timer and types what it recognises through cp0_keyboard_inject_key(), as
 * the on-screen keyboard does. LVGL thread only.
 *
 * The recogniser's tables: $APPLAUNCH_STROKES_DIR (default /usr/share/APPLaunch/strokes)/glyphs.txt, then glyphs-extra.txt
 * (the accents and the rarer characters; APPLAUNCH_STROKES_EXTRA=0 leaves it out), then the user overlay
 * $APPLAUNCH_STROKES_USER (default $XDG_CONFIG_HOME/applaunch/strokes-user.txt, else ~/.config/applaunch/...; "" = none).
 * A table that cannot be read is logged ("[zone]") and skipped: every stroke is then refused. APPLAUNCH_STROKES_LOG=FILE
 * (development) appends every stroke to FILE in the corpus format of tools/strokes (label "?", the result in a comment
 * line before it), for strokes-replay and the S6 tuning.
 *
 * On demand (report 042): the band (the strip and the writing zone) shows while a text field wants input
 * (cp0_zone_ui_set_text_wanted, from the on-screen keyboard's policy) or while a page pins it (cp0_zone_ui_set_pinned),
 * like the keyboard; what it covers of the app area is the keyboard's inset. The strip's keyboard key puts the
 * on-screen keyboard's keys (cp0::ui::OnScreenKeyboard) in the writing boxes' place and back; a password field opens
 * on them (report 042 section 13). A capture (cp0_zone_ui_set_capture, the
 * Strokes training page) takes every finished stroke instead of typing it.
 */
#ifndef CP0_ZONE_UI_H
#define CP0_ZONE_UI_H

#include "cp0_zone.h"
#include "lvgl/lvgl.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the band shows changed (cp0_zone_vis_t: hidden, the writing boxes, the keyboard): the display manager moves the
 * line between the app area and the band and routes the touches; the host publishes its input rule (vkb=zone only while
 * the writing boxes show). */
typedef void (*cp0_zone_vis_cb_t)(int vis);

/* The kind of the text field the band shows for (cp0_zone_ui_set_field). */
typedef enum { CP0_ZONE_FIELD_TEXT = 0, CP0_ZONE_FIELD_NUMBER, CP0_ZONE_FIELD_PASSWORD } cp0_zone_field_kind_t;

/* One finished stroke while a capture is set (cp0_zone_ui_set_capture): nothing is typed, the modes are cleared after
 * it. All of it is valid during the callback only. */
typedef struct {
    const strokes_result *result; /* what the recogniser made of it in the box's set */
    int box;                      /* enum strokes_box of the press */
    const strokes_point *pts;     /* its points (panel px, the frames' t_us), drop-outs healed into it */
    int n;
    const char *corpus;           /* the same in the corpus format of tools/strokes: "t,x,y ... ^t", ms from the press */
    float ppmm;                   /* the panel's density */
} cp0_zone_capture_t;
typedef void (*cp0_zone_capture_cb_t)(const cp0_zone_capture_t *capture, void *user);

/* Builds the band on `display` (layout->w x layout->band_h; its row 0 is the panel's row layout->strip_y), loads the
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

/* 1 while the band shows; what it shows (cp0_zone_vis_t). */
int cp0_zone_ui_shown(void);
int cp0_zone_ui_vis(void);
/* The field that wants input now (before cp0_zone_ui_set_text_wanted(1); field: its identity): a password field opens
 * the band on the keyboard, any other the session's choice; the keyboard in the band is built for its kind (a Number
 * field's number pad, a terminal's Alt). */
void cp0_zone_ui_set_field(int kind, int multiline, const void *field);
/* The strip's keyboard key: the writing boxes <-> the keyboard (cp0_zone_show_toggle). */
void cp0_zone_ui_toggle_keys(void);
/* The band display's screen (the render harness finds the band's keys on it), NULL before creation. */
lv_obj_t *cp0_zone_ui_screen(void);
/* A text field wants input (the on-screen keyboard's policy says it may show, cp0::ui::osk::set_zone): the band shows;
 * 0 when that ends. Hiding sends what the strip owes (an arrow's release, Alt's), turns its modifiers off and drops a
 * stroke in progress. */
void cp0_zone_ui_set_text_wanted(int on);
/* A page keeps the band shown (Strokes training); 0 when it closes. */
void cp0_zone_ui_set_pinned(int on);
void cp0_zone_ui_set_vis_cb(cp0_zone_vis_cb_t cb);       /* the display manager's */
void cp0_zone_ui_set_vis_listener(cp0_zone_vis_cb_t cb); /* the host's */

/* Every finished stroke goes to cb instead of being typed (NULL: typing again). */
void cp0_zone_ui_set_capture(cp0_zone_capture_cb_t cb, void *user);
/* The recogniser's table and settings (NULL before creation or without a table); the table changes on a reload. */
const strokes_table *cp0_zone_ui_table(void);
const strokes_config *cp0_zone_ui_config(void);
/* The user overlay file ("" when none: APPLAUNCH_STROKES_USER=""). */
const char *cp0_zone_ui_overlay_path(void);
/* Loads the tables again (the user overlay changed). 0; -1 when the overlay is refused (err says why; the shipped tables
 * are loaded without it) or nothing could be loaded (the previous tables stay). A stroke in progress is dropped. */
int cp0_zone_ui_reload(strokes_error *err);

/* The screensaver is up: the band is drawn black (its touches are swallowed by the display manager anyway). */
void cp0_zone_ui_set_blank(int on);
/* The panel goes to an app or comes back: the stroke in progress is dropped (no result), the strip's owed releases go
 * out, the recogniser's modes (shift, punctuation, an accent) are cleared. What the band shows is kept. */
void cp0_zone_ui_reset_input(void);

/* A line for the start-up log: the band's sizes, the tables loaded. */
const char *cp0_zone_ui_describe(void);

#ifdef __cplusplus
}
#endif

#endif /* CP0_ZONE_UI_H */
