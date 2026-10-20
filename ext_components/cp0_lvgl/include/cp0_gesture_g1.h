/*
 * SPDX-License-Identifier: MIT
 *
 * G1, the three-finger hold (multitasking P-c; study 026 section 5.2, owner decision D2 of 2026-10-14): three fingers
 * put down together and held still for a second open the app switcher. A pure state machine (no I/O, no clock, no
 * globals, no LVGL) fed with touch frames (cp0_touch_slots.h, positions in screen pixels) and the time: the launcher
 * runs one on its own touch reader while it is in front and one in the supervisor while an app is in front; the app
 * shims (viz1090, Mesh Hop) can copy this file pair. Unit-tested on the PC (tests/test_gesture_g1.c, the cases of study
 * section 5.3) and replayed over the P-a recordings of the deck and the Pi 3A+ (tests/test_g1_replay.c).
 *
 * The rules (the same as tools/multitasking-spike/mta_analyse.py, the reference the P-a numbers came from). A session
 * starts when the first contact lands (t0) and ends when the last one lifts.
 *   1. TRACKING: the third contact must land within LAND_MS of the first (t3), with exactly three contacts down.
 *      At t3: every two contacts at least SEP apart, none within EDGE of a screen edge, and (when MAJOR_MAX is set and
 *      the panel reports contact sizes) none larger than MAJOR_MAX. Then ARMED.
 *   2. Cancelled (the session then waits for every finger to lift):
 *      - a fourth contact (palm, grip), at any point;
 *      - a contact lifts before the hold is complete;
 *      - a contact moves more than STILL from where it landed;
 *      - LAND_MS passes without a third contact (a one- or two-finger touch, a pinch: never reported);
 *      - the touch events were lost (SYN_DROPPED: the reader fell behind).
 *   3. FIRED at t3 + HOLD_MS, fingers still down. No second fire before every finger is up.
 * Distances are taken in millimetres on the glass, after the display manager's transform (the deck's raw axes are
 * not square: 14.9 raw units per mm across, 8.5 along; P-a), with the screen density (ppmm, screen.state).
 *
 * Reported: FIRED once per gesture; CANCELLED once per session that had at least three contacts down at some point
 * (an attempt), when its last finger lifts, with the reason. One- and two-finger touches report nothing.
 */
#ifndef CP0_GESTURE_G1_H
#define CP0_GESTURE_G1_H

#include "cp0_touch_slots.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- tuning (defaults; each has an APPLAUNCH_MT_G1_* override, cp0_g1_params_apply_env) ------------------------- */

/* contacts of the gesture */
#define CP0_G1_CONTACTS 3
/* all three down within this time of the first (study T_LAND; P-a: the third landed 11-23 ms after the first, at
 * most 97 ms) */
#define CP0_G1_LAND_MS 250
/* hold, from the third contact (owner decision D2: 1 s, not the 700 ms the study proposed; P-a at 1000 ms: deck 17 of
 * 20 holds, Pi 3A+ 9 of 10, no false positive) */
#define CP0_G1_HOLD_MS 1000
/* the most a contact may move from where it landed, mm x10 (study D_STILL 4 mm) */
#define CP0_G1_STILL_MM_X10 40
/* the least distance between two contacts at t3, mm x10 (study D_SEP 8 mm; P-a: three fingers at least 11.5 mm) */
#define CP0_G1_SEP_MM_X10 80
/* no contact closer than this to an edge at t3, mm x10 (study E_EDGE 2 mm) */
#define CP0_G1_EDGE_MM_X10 20
/* the largest ABS_MT_TOUCH_MAJOR of a finger; 0 = not used (P-a did not use it: the default) */
#define CP0_G1_MAJOR_MAX 0

typedef struct {
    int land_ms;
    int hold_ms;
    int still_mm_x10;
    int sep_mm_x10;
    int edge_mm_x10;
    int major_max; /* 0 = off */
} cp0_g1_params_t;

/* The defaults above. */
void cp0_g1_params_default(cp0_g1_params_t *p);
/* Keeps a hand-set block in range: land 50..2000 ms, hold 200..5000 ms, still 5..300 mm x10, sep 0..400 mm x10,
 * edge 0..200 mm x10, major 0..10000. */
void cp0_g1_params_normalize(cp0_g1_params_t *p);
/* Overrides from the environment (`get` is getenv in the launcher, a fake in the tests), then normalize:
 *   APPLAUNCH_MT_G1_HOLD_MS, APPLAUNCH_MT_G1_LAND_MS (ms), APPLAUNCH_MT_G1_STILL_MM, APPLAUNCH_MT_G1_SEP_MM,
 *   APPLAUNCH_MT_G1_EDGE_MM (mm, "4" or "4.5"), APPLAUNCH_MT_G1_MAJOR_MAX (0 = off).
 * A malformed value is ignored. Returns how many values were taken. */
int cp0_g1_params_apply_env(cp0_g1_params_t *p, const char *(*get)(const char *name));

typedef enum {
    CP0_G1_NONE = 0,
    CP0_G1_FIRED,     /* the gesture: open the switcher */
    CP0_G1_CANCELLED, /* an attempt (three or more contacts) that did not fire, reported when its last finger lifted */
} cp0_g1_outcome_t;

typedef enum {
    CP0_G1_WHY_NONE = 0,
    CP0_G1_WHY_LAND,    /* value: ms from the first to the third contact (LAND_MS passed without it: the time then) */
    CP0_G1_WHY_SEP,     /* value: the closest two contacts, mm x10 */
    CP0_G1_WHY_EDGE,    /* value: the contact closest to an edge, mm x10 from it */
    CP0_G1_WHY_MAJOR,   /* value: the largest contact size */
    CP0_G1_WHY_EXTRA,   /* value: contacts in the touch (4 or more, not always at once) */
    CP0_G1_WHY_LIFT,    /* value: ms the three were held before one lifted */
    CP0_G1_WHY_MOVE,    /* value: how far the contact moved, mm x10 */
    CP0_G1_WHY_DROPPED, /* value: 0 */
} cp0_g1_reason_t;

const char *cp0_g1_reason_name(cp0_g1_reason_t reason); /* "land", "sep", ... */

typedef struct {
    cp0_g1_outcome_t outcome;
    cp0_g1_reason_t reason; /* CANCELLED */
    int value;              /* see cp0_g1_reason_t */
    int contacts;           /* the most contacts down at once in the session */
    int held_ms;            /* FIRED: from the third contact to now */
    int land_ms;            /* first to third contact (-1 when there was no third) */
    int64_t t_due_us;       /* FIRED: when the hold was complete (t3 + HOLD_MS) */
} cp0_g1_event_t;

/* Plain data: copy, zero or re-init it freely. */
#define CP0_G1_IDS 8 /* contacts remembered per session (more is a cancel anyway) */
typedef struct {
    cp0_g1_params_t p;
    int w, h;                           /* screen, px */
    double ppmm;                        /* px per mm */
    double still_px, sep_px, edge_px;
    int state;                          /* internal (cp0_gesture_g1.c) */
    int64_t t0, t3;
    int64_t block_until;                /* BLOCKED: only an empty frame at or after this time ends it */
    int max_n;
    int n_ids;
    int32_t ids[CP0_G1_IDS];
    int32_t x0[CP0_G1_IDS], y0[CP0_G1_IDS]; /* where each landed */
    int32_t major[CP0_G1_IDS];          /* the largest size seen, -1 none */
    int ids_overflow;                   /* more distinct contacts than CP0_G1_IDS */
    int fired;
    cp0_g1_reason_t reason;             /* why the session was cancelled */
    int value;
    int land_ms;
} cp0_g1_t;

/* p NULL = the defaults (normalized either way). w, h: the screen the frames' positions are in (px); ppmm_x100: its
 * density (1130 = 11.3 px per mm; <= 0: 11.3). */
void cp0_g1_init(cp0_g1_t *g, const cp0_g1_params_t *p, int w, int h, int ppmm_x100);

/* One frame, in time order. */
cp0_g1_event_t cp0_g1_frame(cp0_g1_t *g, const cp0_touch_frame_t *f);
/* The time passes without a frame (fingers held perfectly still send nothing on some panels): fires when due.
 * Call it after the frames read up to now. */
cp0_g1_event_t cp0_g1_tick(cp0_g1_t *g, int64_t now_us);
/* When the hold completes if it is ARMED now (for a poll timeout), else -1. */
int64_t cp0_g1_due_us(const cp0_g1_t *g);
/* The contacts down now are not a gesture (the touch screen just changed hands: an app came to the front, the
 * launcher came back): the session in progress is dropped without a report, and nothing starts before a frame with
 * no contact at or after now_us (older frames still queued are not enough). */
void cp0_g1_block(cp0_g1_t *g, int64_t now_us);
/* Nothing touches the screen now (the caller read the device up to now): any session in progress is dropped without a
 * report and the next contact starts a new one. */
void cp0_g1_reset(cp0_g1_t *g);

/* One log line, without the "[mt] " prefix: "G1 fired (3 contacts, held 1003 ms)",
 * "G1 cancelled: a finger moved 5.3 mm (limit 4 mm), 3 contacts". Returns snprintf's value. */
int cp0_g1_describe(const cp0_g1_t *g, const cp0_g1_event_t *e, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* CP0_GESTURE_G1_H */
