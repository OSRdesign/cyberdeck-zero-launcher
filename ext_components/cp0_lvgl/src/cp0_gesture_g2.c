/*
 * SPDX-License-Identifier: MIT
 *
 * G2, the status strip long-press (see cp0_gesture_g2.h). Pure C.
 */
#include "cp0_gesture_g2.h"

#include <math.h>
#include <string.h>

enum { S_IDLE, S_HOLDING, S_DONE, S_BLOCKED };

void cp0_g2_params_default(cp0_g2_params_t *p)
{
    p->hold_ms = CP0_G2_HOLD_MS;
    p->still_mm_x10 = CP0_G2_STILL_MM_X10;
}

void cp0_g2_params_normalize(cp0_g2_params_t *p)
{
    if (p->hold_ms < 200) p->hold_ms = 200;
    if (p->hold_ms > 3000) p->hold_ms = 3000;
    if (p->still_mm_x10 < 5) p->still_mm_x10 = 5;
    if (p->still_mm_x10 > 200) p->still_mm_x10 = 200;
}

/* "500" -> 500; "2.5" -> 25 with x10 (one decimal kept, no strtod: locale) */
static int parse(const char *s, int x10, int *out)
{
    if (!s || !*s) return -1;
    long v = 0;
    int digits = 0;
    for (; *s >= '0' && *s <= '9'; s++, digits++) {
        v = v * 10 + (*s - '0');
        if (v > 100000) return -1;
    }
    if (digits == 0) return -1;
    if (x10) {
        v *= 10;
        if (*s == '.') {
            s++;
            if (*s < '0' || *s > '9') return -1;
            v += *s - '0';
            s++;
            while (*s >= '0' && *s <= '9') s++;
        }
    }
    if (*s) return -1;
    *out = (int)v;
    return 0;
}

int cp0_g2_params_apply_env(cp0_g2_params_t *p, const char *(*get)(const char *name))
{
    if (!p || !get) return 0;
    int taken = 0, v = 0;
    if (parse(get("APPLAUNCH_MT_G2_HOLD_MS"), 0, &v) == 0) {
        p->hold_ms = v;
        taken++;
    }
    if (parse(get("APPLAUNCH_MT_G2_STILL_MM"), 1, &v) == 0) {
        p->still_mm_x10 = v;
        taken++;
    }
    cp0_g2_params_normalize(p);
    return taken;
}

void cp0_g2_init(cp0_g2_t *g, const cp0_g2_params_t *p, int ppmm_x100)
{
    memset(g, 0, sizeof(*g));
    if (p) g->p = *p;
    else cp0_g2_params_default(&g->p);
    cp0_g2_params_normalize(&g->p);
    g->still_px = g->p.still_mm_x10 / 10.0 * ((ppmm_x100 > 0 ? ppmm_x100 : 1130) / 100.0);
    g->state = S_IDLE;
}

static int inside(const cp0_g2_rect_t *r, int32_t x, int32_t y)
{
    return r && r->w > 0 && r->h > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

cp0_g2_event_t cp0_g2_frame(cp0_g2_t *g, const cp0_touch_frame_t *f, const cp0_g2_rect_t *strip)
{
    cp0_g2_event_t e = {0, 0};
    const int total = f->n + f->extra;
    if (g->state == S_BLOCKED) {
        if (total == 0 && f->t_us >= g->block_until) g->state = S_IDLE;
        return e;
    }
    if (total == 0) {
        g->state = S_IDLE;
        return e;
    }
    if (f->resync && g->state != S_DONE) {
        g->state = S_DONE; /* events were lost: wait until every finger is up */
        return e;
    }
    switch (g->state) {
    case S_IDLE:
        if (total == 1 && f->n == 1 && inside(strip, f->c[0].x, f->c[0].y)) {
            g->state = S_HOLDING;
            g->t0 = f->t_us;
            g->id = f->c[0].id;
            g->x0 = f->c[0].x;
            g->y0 = f->c[0].y;
        } else {
            g->state = S_DONE; /* landed elsewhere, or with other fingers: not a G2 */
        }
        break;
    case S_HOLDING:
        if (f->t_us >= g->t0 + (int64_t)g->p.hold_ms * 1000) {
            e.fired = 1;
            e.held_ms = (int)((f->t_us - g->t0) / 1000);
            g->state = S_DONE;
        } else if (total != 1 || f->n != 1 || f->c[0].id != g->id ||
                   hypot((double)(f->c[0].x - g->x0), (double)(f->c[0].y - g->y0)) > g->still_px) {
            g->state = S_DONE;
        }
        break;
    default: break;
    }
    return e;
}

cp0_g2_event_t cp0_g2_tick(cp0_g2_t *g, int64_t now_us)
{
    cp0_g2_event_t e = {0, 0};
    if (g->state == S_HOLDING && now_us >= g->t0 + (int64_t)g->p.hold_ms * 1000) {
        e.fired = 1;
        e.held_ms = (int)((now_us - g->t0) / 1000);
        g->state = S_DONE;
    }
    return e;
}

int64_t cp0_g2_due_us(const cp0_g2_t *g)
{
    return g->state == S_HOLDING ? g->t0 + (int64_t)g->p.hold_ms * 1000 : -1;
}

void cp0_g2_block(cp0_g2_t *g, int64_t now_us)
{
    g->state = S_BLOCKED;
    g->block_until = now_us;
}

void cp0_g2_reset(cp0_g2_t *g)
{
    g->state = S_IDLE;
}
