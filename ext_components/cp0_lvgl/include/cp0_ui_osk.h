/*
 * SPDX-License-Identifier: MIT
 *
 * On-screen keyboard of the responsive shell (task 014 P2b, report 022 section 3.4.1, F1 rules of task 012): the
 * pure C core, no LVGL, unit-tested on the PC by tests/test_ui_osk.c. The LVGL widget that draws it and the service
 * that shows / hides it are in cp0_ui_widgets.hpp (cp0::ui::OnScreenKeyboard, cp0::ui::osk).
 *
 *   layout    key sizes from the layout service's tokens (cp0_ui_metrics.h): key pitch = min((w - 2 margins - 9 gaps)
 *             / 10, 10 mm); key height = the 6.5 mm token, lowered until the whole keyboard (utility row + 4 rows)
 *             fits the class height cap (report 022 section 3.2: Compact 60 %, Standard 300 px = 62.5 %, Wide 50 %,
 *             Large 45 %). Square (user test on the HackBerry 720x720): the keyboard's target is half the screen
 *             height, the keys grow to reach it (not limited by the token; key width unchanged), and the room above
 *             always keeps the header and one field with its label (cp0_ui_osk_field_reserve). The utility row is
 *             80 % of a letter row; the key area is centred (it only matters once the 10 mm cap applies, Large).
 *   pages     rows from the top: (0) utility row Esc, Tab, Ctrl (and Alt on a terminal's keyboard) and, when the width
 *             allows, the four arrows at the right; (1) q..p; (2) a..l; (3) Shift z..m Backspace; (4) 123, ',', space,
 *             '.', Enter. The 123 page has
 *             1..0 on row 1 and symbols below, '#+=' a second symbol page, 'abc' goes back. A Number field gets the
 *             number pad (1-9, Backspace 0 Enter; 3 key widths, centred; the full key area on a portrait canvas) under the same utility row.
 *   modifiers one-shot Shift, Ctrl and Alt: a tap arms (highlighted), a second tap within CP0_UI_OSK_DOUBLE_TAP_MS
 *             locks (Caps lock / Ctrl lock / Alt lock), a tap on a locked modifier turns it off; the next character
 *             key uses and drops an armed modifier.
 *   Alt       (report 028 X4) only on the keyboard of a terminal (a multi-line field: cp0_ui_osk_layout_alt), for
 *             Alt+Tab (the app switcher) and Alt shortcuts without a physical keyboard. It is a real key: Alt on sends
 *             KEY_LEFTALT's press, Alt off its release (so Alt, Tab = a quick Alt+Tab; Alt locked, Tab, Tab, Alt = an
 *             Alt+Tab session), and the keys in between carry KBD_MOD_ALT with their usual text. Esc neither carries
 *             nor drops it (it cancels an Alt+Tab session).
 *   events    what a key injects (press + release key_items, cp0_keyboard_inject_key): a character carries its Linux
 *             KEY_* code (US layout), its UTF-8 text and KBD_MOD_SHIFT when the US layout needs Shift (KBD_MOD_CAPS for
 *             a letter under Caps lock); with Ctrl on, any key with a code is sent as that code with KBD_MOD_CTRL and
 *             NO text (Ctrl+C = KEY_C + CTRL). Esc, Backspace and the arrows are hold keys like the toolbar keys:
 *             press when the finger goes down, repeat while it stays, release when it leaves, and the release is
 *             always owed (finger lost, key or keyboard deleted: cp0_ui_osk_key_cancel / cp0_ui_osk_release_all).
 *             Control keys carry the same control UTF-8 as a physical key / the toolbar (Esc "\x1b", Enter "\r",
 *             Backspace "\x7f", arrows "\033[A"...), except Tab: "\t" only for a multi-line field, else the code
 *             alone.
 *   policy    may the keyboard show (F1 rules): never with a physical keyboard present, nor for
 *             CP0_UI_OSK_BT_ASLEEP_S after a Bluetooth keyboard went away (it sleeps), nor for CP0_UI_OSK_GRACE_S after
 *             any keyboard went away; never with the setting Off, under the lock screen, or while a desktop session
 *             owns the screen. APPLAUNCH_OSK=force|auto|off (development, render harness) overrides.
 *   dialogs   where a dialog sits so that it is centred in the room above the keyboard.
 */
#pragma once

#include "cp0_app_contract.h"
#include "cp0_ui_metrics.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_UI_OSK_KEY_MM_X10 65        /* key height token: 6.5 mm (the 6-7 mm rule) ... */
#define CP0_UI_OSK_KEY_MIN_PX 24        /* ... never lowered under this by the height cap */
#define CP0_UI_OSK_KEY_W_MAX_MM_X10 100 /* key pitch cap: 10 mm */
#define CP0_UI_OSK_UTIL_PCT 80          /* utility row height: 80 % of a letter row */
#define CP0_UI_OSK_LABEL_PCT 45         /* key label: 45 % of the smaller key side, even px */
#define CP0_UI_OSK_SMALL_PCT 75         /* utility / page-toggle labels: 75 % of that, at least 12 px */
#define CP0_UI_OSK_ROWS 5               /* utility row + 4 */
#define CP0_UI_OSK_MAX_KEYS 48
#define CP0_UI_OSK_DOUBLE_TAP_MS 400u   /* second tap on an armed modifier within this: lock */
/* F1: a Bluetooth keyboard that went away counts as present for N = 10 min; any keyboard that went away counts as
 * present for a short grace. The rule itself is the app contract's (cp0_app_osk_rule), so apps decide like the
 * launcher. */
#define CP0_UI_OSK_BT_ASLEEP_S CP0_APP_OSK_BT_ASLEEP_S
#define CP0_UI_OSK_GRACE_S CP0_APP_OSK_GRACE_S

/* Look (Controller design, approved mockups) */
#define CP0_UI_OSK_BG_RGB 0x111111
#define CP0_UI_OSK_BORDER_RGB 0x333333      /* 1 px line along the top */
#define CP0_UI_OSK_KEY_RGB 0x1C1C1C
#define CP0_UI_OSK_KEY_PRESSED_RGB 0x3A3A3A /* pressed: lighter */
#define CP0_UI_OSK_LABEL_RGB 0xEEEEEE
#define CP0_UI_OSK_ENTER_RGB 0x1F5F8A
#define CP0_UI_OSK_ENTER_PRESSED_RGB 0x2A7AB0
#define CP0_UI_OSK_ARMED_RGB 0xE8E8E8       /* an armed Shift / Ctrl: highlighted (dark label) */
#define CP0_UI_OSK_ARMED_LABEL_RGB 0x111111
#define CP0_UI_OSK_LOCKED_RGB 0x1F5F8A      /* a locked one (Caps lock, Ctrl lock): the Enter blue */
#define CP0_UI_OSK_RADIUS 10

/* ------------------------------------------------------------------ layout */

typedef struct {
    int w, h;          /* screen (logical canvas) */
    int y, height;     /* the keyboard: full width, from y to the bottom of the screen; height = bottom inset */
    int border;        /* top line, px */
    int pad_y;         /* above the utility row and under the bottom row */
    int gap;           /* between keys and between rows (SPACE_S) */
    int margin;        /* at least this on each side of the key area (SPACE_S) */
    int key_w, key_h;  /* one letter key */
    int util_h;        /* utility row height */
    int util_key_w;    /* Esc, Tab, Ctrl: the TARGET token, at least one key, at most two keys and a gap */
    int arrows;        /* 1: the four arrow keys (one key wide each) at the right of the utility row */
    int alt;           /* 1: Alt after Ctrl in the utility row (a terminal's keyboard, cp0_ui_osk_layout_alt) */
    int row_x, row_w;  /* key area: 10 keys and 9 gaps, centred on the screen */
    int pad_x, pad_w;  /* number pad: 3 keys and 2 gaps, centred */
    int pad_key_w;     /* one number pad key: a letter key, on a portrait canvas (h > w) a third of the key area */
    int label_px;      /* key labels (even px) */
    int small_px;      /* utility and page-toggle labels (even px) */
    int cap;           /* the class height cap, px */
    int key_h_token;   /* 6.5 mm in px (the key height before the cap; Square grows past it) */
    int key_w_cap;     /* 10 mm in px */
} cp0_ui_osk_layout_t;

/* Height cap of the keyboard for a class and a screen height, px (report 022 section 3.2; Square: 50 %, a target). */
int cp0_ui_osk_height_cap(cp0_ui_class_t cls, int screen_h);

/* Room a page needs above the keyboard to show the field being edited: the header, the body's top padding, the field's
 * label line, the label / field gap, the field (ROW token) and a margin under it, px. */
int cp0_ui_osk_field_reserve(const cp0_ui_metrics_t *m);

/* The keyboard layout for the metrics m. Returns 0, or -1 (out untouched) when m is NULL or the screen is empty. */
int cp0_ui_osk_layout(const cp0_ui_metrics_t *m, cp0_ui_osk_layout_t *out);
/* The same for a terminal's keyboard: Alt after Ctrl. The utility keys keep their width when the arrows still fit,
 * else they narrow (never under one key) so that the arrows stay; with no room even then, no arrows (as without Alt). */
int cp0_ui_osk_layout_alt(const cp0_ui_metrics_t *m, cp0_ui_osk_layout_t *out);

/* ------------------------------------------------------------------ pages and keys */

typedef enum {
    CP0_UI_OSK_PAGE_LETTERS = 0,
    CP0_UI_OSK_PAGE_SYMBOLS,  /* "123": digits and the first symbols */
    CP0_UI_OSK_PAGE_SYMBOLS2, /* "#+=" */
    CP0_UI_OSK_PAGE_NUMPAD,   /* Number fields */
    CP0_UI_OSK_PAGE__COUNT
} cp0_ui_osk_page_t;

typedef enum {
    CP0_UI_OSK_K_CHAR = 0, /* types text: letters, digits, symbols, space */
    CP0_UI_OSK_K_SHIFT,
    CP0_UI_OSK_K_CTRL,
    CP0_UI_OSK_K_PAGE,     /* switches to `target` */
    CP0_UI_OSK_K_BACKSPACE,
    CP0_UI_OSK_K_ENTER,
    CP0_UI_OSK_K_ESC,
    CP0_UI_OSK_K_TAB,
    CP0_UI_OSK_K_ARROW,    /* code KEY_LEFT / KEY_UP / KEY_DOWN / KEY_RIGHT */
    CP0_UI_OSK_K_ALT,      /* code KEY_LEFTALT (a terminal's keyboard) */
} cp0_ui_osk_kind_t;

typedef struct {
    cp0_ui_osk_kind_t kind;
    const char *label;  /* drawn text (a letter in lower case: upper case while Shift is on); NULL: none (space) */
    const char *text;   /* CHAR: the UTF-8 it types (lower case for a letter) */
    uint32_t code;      /* Linux KEY_* of the US layout key that types it (0: none, the text alone) */
    int shift;          /* CHAR: the US layout needs Shift for it ('!' = Shift + 1) */
    int width_x10;      /* rows 1-4: width in letter keys x10 (15 = one and a half); row 0: unused */
    cp0_ui_osk_page_t target; /* PAGE */
} cp0_ui_osk_key_t;

typedef struct {
    const cp0_ui_osk_key_t *key;
    int row;        /* 0 utility .. 4 bottom */
    int x, y, w, h; /* inside the keyboard (0, 0 = its top left corner, above the border) */
} cp0_ui_osk_place_t;

/* 1 for the keys that act while held (Esc, Backspace, arrows): press on finger down, release owed. */
int cp0_ui_osk_is_hold_key(const cp0_ui_osk_key_t *key);

/* The US layout key of a character, from the keyboard's own tables (the PDA writing zone types through it, report
 * 041): 1 and *code = its KEY_*, *shift = 1 when the US layout needs Shift for it ('A' = KEY_A + Shift, '!' = KEY_1 +
 * Shift, ' ' = KEY_SPACE); 0 (code 0, shift 0) for a character no US key types (the euro sign, an accented letter):
 * it is sent as text alone. code / shift may be NULL. */
int cp0_ui_osk_char_key(const char *utf8, uint32_t *code, int *shift);

/* Places the keys of a page: fills out[0..n-1] (row by row, left to right) and returns n, or -1. */
int cp0_ui_osk_place_keys(const cp0_ui_osk_layout_t *l, cp0_ui_osk_page_t page, cp0_ui_osk_place_t *out, int max);

/* ------------------------------------------------------------------ modifiers */

typedef enum { CP0_UI_OSK_MOD_OFF = 0, CP0_UI_OSK_MOD_ARMED, CP0_UI_OSK_MOD_LOCKED } cp0_ui_osk_mod_state_t;

typedef struct {
    cp0_ui_osk_mod_state_t state;
    uint64_t armed_at_ms;
} cp0_ui_osk_mod_t;

/* A tap on the modifier key: OFF -> ARMED; ARMED -> LOCKED within CP0_UI_OSK_DOUBLE_TAP_MS, else OFF; LOCKED -> OFF. */
void cp0_ui_osk_mod_tap(cp0_ui_osk_mod_t *m, uint64_t now_ms);
/* A key was typed with it: ARMED -> OFF (LOCKED stays). */
void cp0_ui_osk_mod_used(cp0_ui_osk_mod_t *m);
void cp0_ui_osk_mod_reset(cp0_ui_osk_mod_t *m);
int cp0_ui_osk_mod_on(const cp0_ui_osk_mod_t *m);

/* ------------------------------------------------------------------ key events */

typedef struct {
    uint32_t code;  /* Linux KEY_* (0 for a character the US layout has no key for: the text alone) */
    int state;      /* KBD_KEY_PRESSED / KBD_KEY_RELEASED / KBD_KEY_REPEATED */
    uint32_t mods;  /* KBD_MOD_* */
    char utf8[16];
} cp0_ui_osk_event_t;

#define CP0_UI_OSK_MAX_EVENTS 4 /* a key's press and release, an owed release, Alt's release */

typedef struct {
    cp0_ui_osk_page_t page; /* page shown */
    cp0_ui_osk_page_t home; /* LETTERS, or NUMPAD for a Number field */
    cp0_ui_osk_mod_t shift, ctrl, alt;
    int multiline;
    uint32_t held_code;     /* a hold key whose release is owed (0: none) ... */
    uint32_t held_mods;     /* ... and the modifiers it was pressed with */
    char held_utf8[16];
} cp0_ui_osk_state_t;

void cp0_ui_osk_state_init(cp0_ui_osk_state_t *s, int numeric, int multiline);

/* The finger went down on a key: a hold key sends its press (releasing a hold key still owed first). */
int cp0_ui_osk_key_down(cp0_ui_osk_state_t *s, const cp0_ui_osk_key_t *k, cp0_ui_osk_event_t *out, int max);
/* It stays down (long-press repeat): a hold key sends KBD_KEY_REPEATED. */
int cp0_ui_osk_key_repeat(cp0_ui_osk_state_t *s, const cp0_ui_osk_key_t *k, cp0_ui_osk_event_t *out, int max);
/* It came up on the key: a hold key sends its release; any other key acts now (a click): a character, Tab or Enter
 * sends press + release, Shift / Ctrl / Alt change state (Alt: its key's press or release), a page key switches the
 * page. */
int cp0_ui_osk_key_up(cp0_ui_osk_state_t *s, const cp0_ui_osk_key_t *k, uint64_t now_ms, cp0_ui_osk_event_t *out,
                      int max);
/* The press was lost (finger slid off, key deleted): only the release a hold key owes. */
int cp0_ui_osk_key_cancel(cp0_ui_osk_state_t *s, cp0_ui_osk_event_t *out, int max);
/* The keyboard goes away (hidden, destroyed, a physical keyboard appeared): the owed releases (a hold key, Alt),
 * modifiers off. */
int cp0_ui_osk_release_all(cp0_ui_osk_state_t *s, cp0_ui_osk_event_t *out, int max);

/* ------------------------------------------------------------------ policy */

typedef enum {
    CP0_UI_OSK_ENV_UNSET = 0,
    CP0_UI_OSK_ENV_AUTO,
    CP0_UI_OSK_ENV_FORCE, /* show even with a physical keyboard (HackBerry's built-in keyboard, render harness) */
    CP0_UI_OSK_ENV_OFF,
} cp0_ui_osk_env_t;

/* APPLAUNCH_OSK value: "force", "auto", "off" (anything else: UNSET). */
cp0_ui_osk_env_t cp0_ui_osk_env_parse(const char *value);

typedef struct {
    cp0_ui_osk_env_t env;
    int setting_off;     /* Settings > System > On-screen keyboard: Off */
    int locked;          /* the lock screen is up */
    int desktop_session; /* a desktop session owns the screen */
    unsigned keyboards;  /* real keyboards present now (cp0_keyboard_presence.h rule) */
    int64_t now_s;       /* monotonic seconds */
    int64_t any_gone_s;  /* when the last keyboard went away (monotonic s; 0: never) */
    int64_t bt_gone_s;   /* when the last Bluetooth keyboard went away (monotonic s; 0: never) */
    int zone;            /* the PDA writing zone is the text input (report 041): never, whatever the rest says */
} cp0_ui_osk_policy_t;

/* The on-screen keyboard rule in force (published as input.state vkb=): env OFF -> off, env FORCE -> force, the setting
 * Off or a desktop session -> off, else auto. */
cp0_app_vkb_t cp0_ui_osk_effective_vkb(cp0_ui_osk_env_t env, int setting_off, int desktop_session);

/* 1 when the keyboard may show for a focused text field (rules at the top of this file): never while locked or while
 * the writing zone is the text input (zone), else
 * cp0_app_osk_rule(cp0_ui_osk_effective_vkb(...), keyboards, now_s, any_gone_s, bt_gone_s). */
int cp0_ui_osk_policy_allows(const cp0_ui_osk_policy_t *p);

/* ------------------------------------------------------------------ dialogs */

/* y of a dialog dialog_h high centred in the room between top and the keyboard (screen_h - inset); top-aligned when
 * it does not fit. */
int cp0_ui_osk_dialog_y(int screen_h, int top, int inset, int dialog_h);

#ifdef __cplusplus
}
#endif
