/*
 * SPDX-License-Identifier: MIT
 *
 * Touch event parser of strokes-capture: Linux evdev events (from /dev/input/eventN or from evtest's text output)
 * to stroke samples. Follows one contact (the first finger or stylus down, multitouch protocol B, or the single-touch
 * BTN_TOUCH / ABS_X / ABS_Y events), maps raw axes to logical pixels, and groups contacts closer than group_ms into
 * one sample so that drop-outs stay inside it (the recogniser heals them at replay). No I/O; host unit-tested.
 */
#ifndef STROKES_EVPARSE_H
#define STROKES_EVPARSE_H

#include "sample.h"

#define EVP_SLOTS 16

/* Linux input-event codes used here (stable kernel ABI, linux/input-event-codes.h). */
enum {
    EVP_EV_SYN = 0, EVP_EV_KEY = 1, EVP_EV_ABS = 3,
    EVP_SYN_REPORT = 0, EVP_SYN_DROPPED = 3,
    EVP_BTN_TOUCH = 330,
    EVP_ABS_X = 0, EVP_ABS_Y = 1, EVP_ABS_MT_SLOT = 47, EVP_ABS_MT_POSITION_X = 53, EVP_ABS_MT_POSITION_Y = 54,
    EVP_ABS_MT_TRACKING_ID = 57
};

typedef struct {
    int min, max, valid;
} evp_axis;

typedef void (*evp_sample_fn)(void *ctx, stk_sample *s);

typedef struct {
    /* settings (set before the first event) */
    float out_w, out_h; /* panel size the raw axes map onto, in its native orientation; 0 = raw units */
    int rotate;         /* 0, 90, 180, 270: clockwise, into the writing orientation */
    float group_ms;
    float ppmm;
    evp_sample_fn on_sample;
    void *ctx;
    /* axis ranges: ABS_X/Y and ABS_MT_POSITION_X/Y */
    evp_axis ax, ay, mx, my;
    char device[128];
    /* state */
    int has_mt, slot, primary, dropped, moved;
    struct {
        int tid, x, y, seen;
    } slots[EVP_SLOTS];
    int st_touch, st_prev, st_x, st_y, st_seen;
    stk_sample *cur;
    int open, down, overflow;
    uint64_t last_lift;
    long events, frames, samples, overflows;
    int parse_axis_code; /* evtest header: the code whose Min/Max lines follow */
} evp;

/* Initialises (allocates the sample buffer). Returns 0, -1 when out of memory. */
int evp_init(evp *p);
void evp_free(evp *p);
void evp_set_axis(evp *p, int code, int min, int max);
void evp_event(evp *p, uint64_t t_us, int type, int code, int value);
/* Emits the open sample once now_us is past the group window with the pen up. */
void evp_tick(evp *p, uint64_t now_us);
/* Emits the open sample (end of input). */
void evp_finish(evp *p);
/* One line of evtest output (header or event). Returns 1 for an event, 0 otherwise. */
int evp_evtest_line(evp *p, const char *line);
/* Raw position to logical pixels (exposed for tests). */
void evp_map(const evp *p, int mt, int rx, int ry, float *x, float *y);

#endif
