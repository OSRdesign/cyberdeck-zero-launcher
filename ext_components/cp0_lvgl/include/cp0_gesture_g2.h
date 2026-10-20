/*
 * SPDX-License-Identifier: MIT
 *
 * G2, the status strip long-press (multitasking P-c; study 026 section 5.2, owner decision D3): one finger held still
 * on the status strip (the clock, Wi-Fi and Bluetooth at the top right) for half a second opens the app switcher, the
 * visible path to it and the one single-touch panels have. A pure state machine like G1 (cp0_gesture_g1.h), fed with
 * the same touch frames (screen pixels); the caller says where the strip is when the finger lands (the launcher: the
 * strip of its active screen, cp0::ui::StatusStrip). A tap on the strip is not a G2 and stays what it was.
 *   - one contact lands inside the strip, and stays the only contact;
 *   - it moves less than STILL from where it landed;
 *   - it is still down HOLD_MS after it landed: FIRED (once; nothing more until every finger is up).
 * A second contact, a lift, or more movement ends it without a report.
 */
#ifndef CP0_GESTURE_G2_H
#define CP0_GESTURE_G2_H

#include "cp0_touch_slots.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* hold, ms (study T_STRIP, decision D3) */
#define CP0_G2_HOLD_MS 500
/* the most the finger may move, mm x10 (study: under 3 mm) */
#define CP0_G2_STILL_MM_X10 30

typedef struct {
    int hold_ms;
    int still_mm_x10;
} cp0_g2_params_t;

void cp0_g2_params_default(cp0_g2_params_t *p);
/* hold 200..3000 ms, still 5..200 mm x10 */
void cp0_g2_params_normalize(cp0_g2_params_t *p);
/* APPLAUNCH_MT_G2_HOLD_MS (ms), APPLAUNCH_MT_G2_STILL_MM (mm, "3" or "2.5"); malformed values are ignored. Returns how
 * many values were taken. */
int cp0_g2_params_apply_env(cp0_g2_params_t *p, const char *(*get)(const char *name));

typedef struct {
    int x, y, w, h; /* screen px; w or h <= 0 = no strip on the screen */
} cp0_g2_rect_t;

typedef struct {
    int fired;   /* 1: the long-press is complete */
    int held_ms; /* from the landing to now */
} cp0_g2_event_t;

typedef struct {
    cp0_g2_params_t p;
    double still_px;
    int state; /* internal (cp0_gesture_g2.c) */
    int64_t t0;
    int64_t block_until;
    int32_t id, x0, y0;
} cp0_g2_t;

/* p NULL = the defaults. ppmm_x100: the screen density (<= 0: 11.3 px per mm). */
void cp0_g2_init(cp0_g2_t *g, const cp0_g2_params_t *p, int ppmm_x100);
/* One frame; strip: where the strip is now (NULL = not on the screen), looked at when a finger lands. */
cp0_g2_event_t cp0_g2_frame(cp0_g2_t *g, const cp0_touch_frame_t *f, const cp0_g2_rect_t *strip);
cp0_g2_event_t cp0_g2_tick(cp0_g2_t *g, int64_t now_us);
/* When the hold completes if a finger is held on the strip now, else -1. */
int64_t cp0_g2_due_us(const cp0_g2_t *g);
/* As cp0_g1_block(): nothing starts before a frame with no contact at or after now_us. */
void cp0_g2_block(cp0_g2_t *g, int64_t now_us);
/* As cp0_g1_reset(): nothing touches the screen now. */
void cp0_g2_reset(cp0_g2_t *g);

#ifdef __cplusplus
}
#endif

#endif /* CP0_GESTURE_G2_H */
