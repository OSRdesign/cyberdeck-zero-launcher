/*
 * SPDX-License-Identifier: MIT
 *
 * The writing zone of the PDA profile (report 038 sections 3.1 and 4, report 041: phase S3): the pure C core, no LVGL,
 * unit-tested on the PC by tests/test_zone.c. The LVGL side (the band's widgets, the ink, the recogniser's timer) is
 * cp0_zone_ui.h; the display manager (cp0_lvgl_dpi_scaled.c) draws the band as a third display under the app area and
 * routes the touch screen's frames with the router below.
 *
 * The band, from the top (owner decisions of 2026-10-25, mockup variant A), on a portrait panel only:
 *
 *   +-------------------------------+ 0
 *   | app area (the native display) |
 *   +-------------------------------+ strip_y      (only while the Keys strip is open: it covers the app area's
 *   | Tab Ctrl Shift Alt < ^ v > Ent|               bottom row_h pixels, which the pages leave free: the inset)
 *   +-------------------------------+ app_h = buttons_y
 *   | Home | Menu | Apps | Keys     |   the soft-button row: pad above a key_h high button
 *   +-------------------------------+ write_y
 *   | letters box     | digits box  |   the writing zone, write_h high (a third of the panel), the boxes inset by pad
 *   +-------------------------------+ h
 *
 * Sizes: pad = SPACE_S, key_h = 5 mm (the mockup's buttons, panel density, cp0_ui_mm_px), write_h = a third of the
 * panel's height, rounded (owner, after the first board test of 0.8.8-pda.4: 267 px on the 4.3" DSI's 480x800, 213 px
 * on 480x640; S3 first drew 20.6 mm, 176 px, which was too small to write in); the letters box takes 68 % of the boxes'
 * width. The app area is what the pages get (their own metrics, cp0_ui_metrics_compute of w x app_h); the strip does
 * not resize it: it covers its bottom and the pages leave that room free like the on-screen keyboard's (cp0::ui::osk
 * inset).
 *
 * Input rule: where the zone is shown it is the text input (input.state vkb=zone, no fallback keyboard). A contact that
 * starts in the writing zone is a pen stroke for the recogniser (strokes.h) and never reaches the app; one that starts
 * on a button row is the band's; one that starts in the app area is the app's, as before. Only the first contact of a
 * touch writes: a second finger cancels the stroke (a three-finger gesture is never a stroke).
 */
#ifndef CP0_ZONE_H
#define CP0_ZONE_H

#include "cp0_touch_slots.h"
#include "cp0_ui_metrics.h"
#include "cp0_ui_osk.h"
#include "strokes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_ZONE_WRITE_DIV 3      /* the writing zone with its padding: h / 3, rounded (267 px at 480x800) */
#define CP0_ZONE_KEY_MM_X10 50    /* a soft button, a strip key: 5 mm (the mockup's 41-43 px at 8.54 px/mm) */
#define CP0_ZONE_KEY_FLOOR_PX 32
#define CP0_ZONE_LETTERS_PCT 68   /* the letters box: 68 % of the two boxes' width */
#define CP0_ZONE_MIN_APP_H 300    /* a panel that would leave less to the app gets no zone */

/* Look (mockup variant A, 2026-10-25) */
#define CP0_ZONE_BAND_RGB 0x181C1C      /* behind the strip and the soft buttons */
#define CP0_ZONE_WRITE_RGB 0x0A0C0C     /* behind the boxes */
#define CP0_ZONE_BOX_RGB 0x141818       /* a box */
#define CP0_ZONE_BOX_BORDER_RGB 0x465050
#define CP0_ZONE_KEY_RGB 0x242A2A       /* a button, a strip key */
#define CP0_ZONE_KEY_PRESSED_RGB 0x3A4444
#define CP0_ZONE_LIT_RGB 0x1E6496       /* Keys while the strip is open; an armed modifier */
#define CP0_ZONE_LOCKED_RGB 0xE8E8E8    /* a locked modifier (double tap): highlighted, dark label */
#define CP0_ZONE_LABEL_RGB 0xEBEBEB
#define CP0_ZONE_BOX_LABEL_RGB 0x8C9696 /* "abc", "123" */
#define CP0_ZONE_INK_RGB 0xFFBE00       /* the ink of a stroke, the mode line */
#define CP0_ZONE_REJECT_RGB 0xFF5A5A    /* a stroke the recogniser refused: the ink flashes red */

/* ------------------------------------------------------------------ layout */

typedef enum {
    CP0_ZONE_BTN_HOME = 0,
    CP0_ZONE_BTN_MENU, /* Ctrl+O, the apps' options key (APP-CONTRACT.md section 10b) */
    CP0_ZONE_BTN_APPS, /* the multitasking switcher */
    CP0_ZONE_BTN_KEYS, /* opens / closes the strip; lit while it is open */
    CP0_ZONE_BTN__COUNT
} cp0_zone_button_t;

typedef enum {
    CP0_ZONE_KEY_TAB = 0,
    CP0_ZONE_KEY_CTRL,
    CP0_ZONE_KEY_SHIFT,
    CP0_ZONE_KEY_ALT,
    CP0_ZONE_KEY_LEFT,
    CP0_ZONE_KEY_UP,
    CP0_ZONE_KEY_DOWN,
    CP0_ZONE_KEY_RIGHT,
    CP0_ZONE_KEY_ENTER,
    CP0_ZONE_KEY__COUNT
} cp0_zone_key_t;

typedef struct {
    int x, y, w, h;
} cp0_zone_rect_t;

typedef struct {
    int w, h;          /* the panel (logical canvas) */
    int ppmm_x100;
    int pad;           /* SPACE_S: margins, gaps, the boxes' inset */
    int gap;           /* between two strip keys */
    int key_h;         /* a soft button, a strip key */
    int radius;        /* their corners (RADIUS_S) */
    int row_h;         /* one key row: pad + key_h */
    int write_h;       /* the writing zone with its padding */
    int band_closed;   /* the band under the app area: buttons + writing zone */
    int band_open;     /* ... + the strip row */
    int app_h;         /* the app area: h - band_closed (the native display's height) */
    int strip_y;       /* panel y of the strip row (open): app_h - row_h */
    int buttons_y;     /* panel y of the buttons row (= app_h) */
    int write_y;       /* panel y of the writing zone */
    int divider_x;     /* a pen press left of this is in the letters box, else in the digits box */
    cp0_zone_rect_t letters, digits;                 /* the boxes, panel coordinates */
    cp0_zone_rect_t button[CP0_ZONE_BTN__COUNT];     /* panel coordinates */
    cp0_zone_rect_t strip_key[CP0_ZONE_KEY__COUNT];  /* panel coordinates (shown only while the strip is open) */
    int button_px;     /* text sizes (builtin Montserrat, even) */
    int key_px;
    int box_label_px;
    int ink_px;        /* the ink's line width */
} cp0_zone_layout_t;

/* The band for the panel metrics m (the whole panel: its size and density). Returns 0, or -1 (out untouched) when m is
 * NULL, the panel is not portrait (h <= w) or the app area would be under CP0_ZONE_MIN_APP_H. */
int cp0_zone_layout(const cp0_ui_metrics_t *m, cp0_zone_layout_t *out);

/* Panel y where the band starts: strip_y while the strip is open, else app_h. */
int cp0_zone_band_top(const cp0_zone_layout_t *l, int strip_open);

typedef enum {
    CP0_ZONE_HIT_APP = 0, /* above the band */
    CP0_ZONE_HIT_BAND,    /* the strip row (open) or the buttons row: the band's own pointer */
    CP0_ZONE_HIT_LETTERS, /* the writing zone, left of divider_x */
    CP0_ZONE_HIT_DIGITS,  /* the writing zone, right of it */
} cp0_zone_hit_t;

cp0_zone_hit_t cp0_zone_hit(const cp0_zone_layout_t *l, int strip_open, int x, int y);

/* ------------------------------------------------------------------ touch routing */

typedef enum {
    CP0_ZONE_OWNER_NONE = 0, /* no contact down */
    CP0_ZONE_OWNER_APP,      /* the touch started in the app area: the app's pointers, as before */
    CP0_ZONE_OWNER_PEN,      /* it started in the writing zone: the recogniser's (never the app's) */
    CP0_ZONE_OWNER_BAND,     /* it started on the strip or the buttons: the band's pointer */
    CP0_ZONE_OWNER_IGNORED,  /* nobody's until every contact is up (swallowed, cancelled, several fingers at once) */
} cp0_zone_owner_t;

typedef enum {
    CP0_ZONE_PEN_NONE = 0,
    CP0_ZONE_PEN_DOWN,   /* strokes_down(box, x, y, t) */
    CP0_ZONE_PEN_MOVE,   /* strokes_move */
    CP0_ZONE_PEN_UP,     /* strokes_up */
    CP0_ZONE_PEN_CANCEL, /* strokes_cancel: a second contact, a gesture */
} cp0_zone_pen_kind_t;

typedef struct {
    cp0_zone_pen_kind_t kind;
    int box;       /* enum strokes_box (DOWN) */
    int x, y;      /* panel logical px */
    int64_t t_us;  /* the frame's time */
} cp0_zone_pen_t;

typedef struct {
    cp0_zone_owner_t owner;
    int32_t pen_id;   /* the pen contact's tracking id */
    int pen_live;     /* 1 while the pen contact is down and followed */
    int pen_box;
    int has_up;       /* a pen lift happened (the heal window below applies) */
    int64_t up_t_us;
    int up_x, up_y, up_box;
    int64_t heal_us;  /* a new touch within this time ... */
    int heal_px;      /* ... and this distance of a pen lift continues the stroke wherever it lands (a drop-out) */
} cp0_zone_router_t;

/* heal_ms / heal_px: the drop-out window (the recogniser's heal_ms and a generous distance; it decides the healing
 * itself, the router only keeps such a press away from the app). */
void cp0_zone_router_init(cp0_zone_router_t *r, int heal_ms, int heal_px);

/* One frame of the touch screen (contacts in panel logical px). swallow: a new touch belongs to nobody (the screensaver,
 * the touch that woke it or closed an app): it is IGNORED. Fills *pen (kind NONE when the recogniser gets nothing) and
 * returns the owner after this frame. */
cp0_zone_owner_t cp0_zone_route(cp0_zone_router_t *r, const cp0_zone_layout_t *l, int strip_open, int swallow,
                                const cp0_touch_frame_t *f, cp0_zone_pen_t *pen);

/* A gesture took the contacts (multitasking G1 / G2): a stroke in progress is cancelled (*pen CANCEL, else NONE) and the
 * contacts are IGNORED until every one is up. */
void cp0_zone_router_cancel(cp0_zone_router_t *r, cp0_zone_pen_t *pen);

/* The touch screen changed hands (an app was in front, a resync): contacts_down > 0 = IGNORED until they lift, else
 * NONE; no heal window. */
void cp0_zone_router_reset(cp0_zone_router_t *r, int contacts_down);

/* ------------------------------------------------------------------ the strip's keys and the typed results */

/* The sticky modifiers of the strip (as the on-screen keyboard's: a tap arms, a second tap within
 * CP0_UI_OSK_DOUBLE_TAP_MS locks, a tap on a locked or (later) armed one turns it off; an armed one is used by the
 * next key or character, a locked one stays). Ctrl: the next key with a code is sent with KBD_MOD_CTRL and no text
 * (Ctrl+C); Shift: KBD_MOD_SHIFT, a letter upper case; Alt: a real key, its press when it comes on and its release
 * when it goes off or after the key it was used by (Alt, Tab = a quick Alt+Tab), the keys in between carry
 * KBD_MOD_ALT. The arrows act while held (press, repeat, release); Tab and Enter on a click. */
typedef struct {
    cp0_ui_osk_mod_t ctrl, shift, alt;
    uint32_t held_code; /* an arrow whose release is owed (0: none) ... */
    uint32_t held_mods; /* ... and the modifiers it was pressed with */
    char held_utf8[16];
} cp0_zone_strip_t;

#define CP0_ZONE_MAX_EVENTS 6 /* press + release, an owed release, Alt's release */

void cp0_zone_strip_init(cp0_zone_strip_t *s);
/* The modifier's state for drawing: CP0_UI_OSK_MOD_OFF / _ARMED / _LOCKED (-1 for a key that is not one). */
int cp0_zone_strip_mod_state(const cp0_zone_strip_t *s, cp0_zone_key_t key);
/* The modifiers the next key carries now (KBD_MOD_*). */
uint32_t cp0_zone_strip_mods(const cp0_zone_strip_t *s);

/* A finger went down on / repeated on / came up on a strip key, or its press was lost. Returns the events to inject
 * (cp0_keyboard_inject_key), at most max. multiline: the focused field is a multi-line one (a terminal): Tab types a
 * tab character, as the on-screen keyboard's. */
int cp0_zone_strip_down(cp0_zone_strip_t *s, cp0_zone_key_t key, cp0_ui_osk_event_t *out, int max);
int cp0_zone_strip_repeat(cp0_zone_strip_t *s, cp0_zone_key_t key, cp0_ui_osk_event_t *out, int max);
int cp0_zone_strip_up(cp0_zone_strip_t *s, cp0_zone_key_t key, int multiline, uint64_t now_ms, cp0_ui_osk_event_t *out,
                      int max);
int cp0_zone_strip_cancel(cp0_zone_strip_t *s, cp0_ui_osk_event_t *out, int max);
/* The strip closes, the zone goes away: the owed releases (an arrow, Alt), every modifier off. */
int cp0_zone_strip_release_all(cp0_zone_strip_t *s, cp0_ui_osk_event_t *out, int max);

/* What a Strokes result types, with the strip's modifiers (which it then uses): a character as its US key with its
 * text (cp0_ui_osk_char_key: 'a' = KEY_A + "a", 'A' = KEY_A + Shift (Caps lock: KBD_MOD_CAPS) + "A", '!' = KEY_1 +
 * Shift + "!"); one the US layout has no key for (the euro sign, an accented letter, the copyright sign) as text alone
 * (code 0); a key (space, backspace, enter, tab) as its KEY_* with the keyboard's control text (Enter "\r", Backspace
 * "\x7f", Tab "\t" only in a multi-line field). Modes and rejections type nothing (0 events). */
int cp0_zone_result_events(cp0_zone_strip_t *s, const strokes_result *r, int multiline, cp0_ui_osk_event_t *out,
                           int max);

/* ------------------------------------------------------------------ the mode line */

/* The writing zone's box labels and mode hint from strokes_get_mode() and the pending accent (strokes_result.accent):
 * letters "abc" ("Abc" while the one-shot shift is on, "ABC" with caps lock); hint, empty when there is none: "Shift",
 * "Caps", "Punct", "Ext" and "Accent: <name>" (strokes_accent_name), joined by "  ". */
void cp0_zone_mode_text(unsigned mode, int accent, char *letters, size_t letters_size, char *hint, size_t hint_size);

/* The upper-case form of a lower-case letter (a-z and the Latin-1 letters U+00E0..U+00FE, except the division sign)
 * written to out; any other text is copied as it is. Returns 1 when it changed. */
int cp0_zone_upper(const char *utf8, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* CP0_ZONE_H */
