/*
 * SPDX-License-Identifier: MIT
 *
 * Pointer navigation: a mouse, a trackpoint or a small optical pad (the HackBerry's BlackBerry-style one) drives the
 * keyboard UI. Motion becomes arrow keys at a rate that follows how hard / fast it is pushed (a joystick, not a
 * cursor), the wheel becomes Up / Down, a click is Enter and a held click the options key. The launcher and every app
 * that links cp0_lvgl get it from the keyboard backend (src/cp0/cp0_pointer_reader.c): the keys enter the same queue
 * as a physical key's, so lists, dialogs and Home work unchanged.
 *
 * This header has two parts:
 *   1. the core: a pure state machine (no I/O, no clock, no globals) fed with reports and elapsed time, returning
 *      key events. Unit-tested on the PC (tests/test_pointer_nav.c) with traces of the measured HackBerry pad;
 *   2. the process-wide switch: whether pointer navigation is on in this process.
 *
 * Units: a "count" is one unit of an evdev REL_X / REL_Y value; a "report" is the motion between two SYN_REPORTs
 * (the HackBerry pad sends about 62 a second while it moves, 16 ms apart). Rates are steps per second x10.
 *
 * Rules (task: pointer navigation spike, design of 2026-10-18):
 *   - level: the motion per report, smoothed (CP0_PTR_NAV_SMOOTH_PCT), per axis. Below the dead zone nothing
 *     happens: rest noise (+-1..2 counts) and the slow drift of a finger resting on the pad never move anything;
 *   - one axis only, the dominant one when a push starts; the other axis takes over only when it is clearly stronger
 *     (CP0_PTR_NAV_AXIS_SWITCH_PCT), so noise never flips it and there are no diagonals;
 *   - first step at once, the next after CP0_PTR_NAV_REPEAT_DELAY_MS, then at a rate from the level: SLOW_RATE just
 *     above the dead zone, rising linearly to MAX_RATE at FULL_LEVEL, never faster than MAX_RATE (also across pushes);
 *   - no report for CP0_PTR_NAV_RELEASE_MS, or the level back under the dead zone: released, the steps stop;
 *   - wheel: one Up / Down per notch (REL_WHEEL > 0 = Up), at once;
 *   - button (BTN_LEFT): released before CP0_PTR_NAV_HOLD_MS with little movement (CP0_PTR_NAV_TAP_SLOP counts) =
 *     Enter on release; held CP0_PTR_NAV_HOLD_MS without moving = the options key, while it is still down; more
 *     movement while it is down is a drag: no click, no options, and the motion navigates again. While it is down
 *     and not a drag, motion does not navigate (the finger rests on the pad). A press that comes within
 *     CP0_PTR_NAV_CLICK_GUARD_MS of motion above the dead zone belongs to the push: no click, no options.
 *     A press released before min_click_ms is a firmware tap, not a click: ignored entirely (no Enter, no options;
 *     the motion did what it did). min_click_ms is 0 (off) for every device except those in the per-device table
 *     (cp0_ptr_nav_device_min_click_ms): the HackBerry pad turns a quick tap on its surface into a BTN_LEFT press of
 *     0.16-0.19 s, its physical button gives 0.35 s and more, so it gets CP0_PTR_NAV_MIN_CLICK_MS_HACKBERRY (a USB
 *     mouse clicks in 80-150 ms and keeps clicking as before). APPLAUNCH_POINTER_MIN_CLICK_MS sets it for every
 *     device (0 = off).
 * PageUp / PageDown are part of the key vocabulary, but no rule produces them yet.
 */
#ifndef CP0_POINTER_NAV_H
#define CP0_POINTER_NAV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- tuning (defaults; each has an APPLAUNCH_POINTER_* override, cp0_ptr_nav_params_apply_env) ------------------ */

/* counts per report under which motion is rest (HackBerry: rest noise +-1..2, drift 44 counts in 4 s = 0.2 / report) */
#define CP0_PTR_NAV_DEAD_ZONE 3
/* counts per report at which the step rate reaches MAX_RATE (HackBerry: a slow push peaks at 37-55, a hard one at 97) */
#define CP0_PTR_NAV_FULL_LEVEL 60
/* steps per second x10 just above the dead zone (3 / s) */
#define CP0_PTR_NAV_SLOW_RATE_X10 30
/* steps per second x10 at FULL_LEVEL and above, and the cap between any two steps (12.5 / s = one per 80 ms) */
#define CP0_PTR_NAV_MAX_RATE_X10 125
/* the first step of a push comes at once, the second this long after it (a short push = exactly one step) */
#define CP0_PTR_NAV_REPEAT_DELAY_MS 250
/* no report for this long = released (3 report periods of the HackBerry pad) */
#define CP0_PTR_NAV_RELEASE_MS 50
/* weight in % of a new report in the smoothed level (50: a single noise spike counts half) */
#define CP0_PTR_NAV_SMOOTH_PCT 50
/* the other axis takes over when its level is above this % of the current axis' level (200 = twice as strong) */
#define CP0_PTR_NAV_AXIS_SWITCH_PCT 200
/* button held this long without a drag = the options key (HackBerry: a tap is 0.18 s, a deliberate hold 1.5-2 s) */
#define CP0_PTR_NAV_HOLD_MS 600
/* counts moved while the button is down (net: |sum dx| + |sum dy|, so jitter cancels) that still count as a still
 * click / hold (HackBerry drift: 44 counts in 4 s, about 7 in the 600 ms of a hold) */
#define CP0_PTR_NAV_TAP_SLOP 24
/* a press this soon after motion above the dead zone is part of the push: no click, no options */
#define CP0_PTR_NAV_CLICK_GUARD_MS 150
/* min_click_ms: -1 = auto, the per-device table decides (cp0_ptr_nav_params_for_device), else 0 (every click counts) */
#define CP0_PTR_NAV_MIN_CLICK_MS (-1)
/* the HackBerry pad: a tap on the surface is a 0.16-0.19 s press, the physical button 0.35 s and more */
#define CP0_PTR_NAV_MIN_CLICK_MS_HACKBERRY 260
/* at most this many wheel notches per report (more is a glitch) */
#define CP0_PTR_NAV_WHEEL_MAX 8

/* the most keys one cp0_ptr_nav_step() call returns (wheel notches + a step + Enter / options, with margin) */
#define CP0_PTR_NAV_MAX_KEYS 16

typedef enum {
    CP0_PTR_KEY_NONE = 0,
    CP0_PTR_KEY_UP,
    CP0_PTR_KEY_DOWN,
    CP0_PTR_KEY_LEFT,
    CP0_PTR_KEY_RIGHT,
    CP0_PTR_KEY_ENTER,
    CP0_PTR_KEY_OPTIONS,   /* the keyboard's options shortcut: Ctrl+O (Mesh Hop's chat options, APP-CONTRACT.md) */
    CP0_PTR_KEY_PAGE_UP,   /* vocabulary only: no rule produces it yet */
    CP0_PTR_KEY_PAGE_DOWN, /* vocabulary only: no rule produces it yet */
} cp0_ptr_key_t;

const char *cp0_ptr_key_name(cp0_ptr_key_t key); /* "UP", "ENTER", ... */

typedef struct {
    int dead_zone;          /* counts per report */
    int full_level;         /* counts per report, > dead_zone */
    int slow_rate_x10;      /* steps / s x10, 1 .. max_rate_x10 */
    int max_rate_x10;       /* steps / s x10 */
    int repeat_delay_ms;
    int release_ms;
    int smooth_pct;         /* 1 .. 100 */
    int axis_switch_pct;    /* >= 100 */
    int hold_ms;
    int tap_slop;           /* counts */
    int click_guard_ms;
    int wheel_max;          /* notches per report */
    int min_click_ms;       /* a shorter press is a tap: ignored; 0 = off; -1 = auto (the per-device table, else 0) */
} cp0_ptr_nav_params_t;

/* The defaults above. */
void cp0_ptr_nav_params_default(cp0_ptr_nav_params_t *p);
/* Makes a hand-set parameter block consistent (ranges, slow <= max, full > dead zone). */
void cp0_ptr_nav_params_normalize(cp0_ptr_nav_params_t *p);
/* Overrides from the environment (`get` is getenv in the reader, a fake in the tests), then normalize:
 *   APPLAUNCH_POINTER_DEADZONE, _FULL_LEVEL, _TAP_SLOP (counts), _SLOW_RATE, _MAX_RATE (steps per second, "12" or
 *   "12.5"), _REPEAT_DELAY_MS, _RELEASE_MS, _HOLD_MS, _CLICK_GUARD_MS, _MIN_CLICK_MS (ms; _MIN_CLICK_MS=0 turns the
 *   tap filter off even for a device of the table), _SMOOTH_PCT, _AXIS_SWITCH_PCT (%).
 * A malformed value is ignored. Returns how many values were taken. */
int cp0_ptr_nav_params_apply_env(cp0_ptr_nav_params_t *p, const char *(*get)(const char *name));

/* Per-device defaults: the min_click_ms of a device in the table, matched by its USB vendor:product (EVIOCGID), or by
 * its kernel name when the ids are unknown (0); -1 when the device is not in the table. */
int cp0_ptr_nav_device_min_click_ms(unsigned vendor, unsigned product, const char *name);
/* The parameters of one device: a copy of `base` where an auto min_click_ms (-1) becomes the table's value, else 0. */
void cp0_ptr_nav_params_for_device(const cp0_ptr_nav_params_t *base, unsigned vendor, unsigned product,
                                   const char *name, cp0_ptr_nav_params_t *out);

typedef enum {
    CP0_PTR_BUTTON_SAME = 0, /* no button change in this report */
    CP0_PTR_BUTTON_DOWN = 1, /* BTN_LEFT pressed */
    CP0_PTR_BUTTON_UP = 2,   /* BTN_LEFT released */
} cp0_ptr_button_t;

typedef struct {
    int dx, dy;               /* REL_X, REL_Y summed over the report (dy > 0 = towards the user = Down) */
    int wheel;                /* REL_WHEEL notches (> 0 = away from the user = Up) */
    cp0_ptr_button_t button;
} cp0_ptr_report_t;

/* State of one pointer device. Plain data: copy, zero or reset it freely. */
typedef struct {
    cp0_ptr_nav_params_t p;
    uint64_t now;            /* ms since init: the sum of every dt */
    int vx_q, vy_q;          /* smoothed counts per report, x16, signed */
    int have_report;
    uint64_t last_report;
    /* motion -> steps */
    int active;              /* a push is being turned into steps */
    int axis;                /* 0 = X (Left / Right), 1 = Y (Up / Down) */
    int dir;                 /* +1 / -1 along the axis */
    int push_steps;          /* steps of this push so far */
    uint64_t next_step;      /* when the next step of the push is due */
    int stepped;             /* last_step is valid */
    uint64_t last_step;
    int motion_seen;         /* last_motion is valid */
    uint64_t last_motion;    /* the last report with a level above the dead zone */
    /* button */
    int down;
    int guarded;             /* pressed during a push: neither click nor options */
    int drag;                /* moved more than tap_slop since the press */
    int hold_fired;          /* the options key was sent for this press */
    int press_dx, press_dy;  /* motion summed since the press */
    int moved;               /* |press_dx| + |press_dy|: how far the finger went (net) */
    uint64_t press_at;
    /* taps ignored (min_click_ms), for the debug log: the count and the length of the last one */
    unsigned taps_ignored;
    uint32_t last_tap_ms;
} cp0_ptr_nav_t;

/* params NULL = defaults (normalized copy kept in the state). */
void cp0_ptr_nav_init(cp0_ptr_nav_t *s, const cp0_ptr_nav_params_t *params);
/* Forget every push and press (keeps the parameters and the clock): nothing is sent for them. */
void cp0_ptr_nav_reset(cp0_ptr_nav_t *s);

/* dt_ms of time pass (timers that fall due inside it fire at their own time: release, repeat steps, the hold), then,
 * when `report` is not NULL, the report is applied at the end of that time. Writes the resulting keys to out[] in
 * order (at most `max`, CP0_PTR_NAV_MAX_KEYS is always enough) and returns their number. */
int cp0_ptr_nav_step(cp0_ptr_nav_t *s, uint32_t dt_ms, const cp0_ptr_report_t *report, cp0_ptr_key_t *out, int max);

/* ms from now until the next timer (a repeat step, the release, the hold); -1 when nothing is pending (the reader then
 * sleeps until the device reports). */
int cp0_ptr_nav_next_ms(const cp0_ptr_nav_t *s);

/* 1 while a push is producing steps, or the button is down (for the debug log). */
int cp0_ptr_nav_busy(const cp0_ptr_nav_t *s);

/* ---- process-wide switch -------------------------------------------------------------------------------------- */

/* The launcher's Settings > System > Pointer navigation: 1 on, 0 off. Without a call (an app), the reader follows
 * input.state ("pointer=on|off", the app contract). APPLAUNCH_POINTER_NAV=1|0 (on|off) in the environment wins over
 * both. Any thread. */
void cp0_pointer_nav_set_enabled(int enabled);
/* -1 = never set (follow input.state), else 0 / 1. */
int cp0_pointer_nav_get_override(void);
/* The reader registers its wake-up here, so a change takes effect at once. NULL removes it. */
void cp0_pointer_nav_set_wake_hook(void (*hook)(void));

#ifdef __cplusplus
}
#endif

#endif /* CP0_POINTER_NAV_H */
