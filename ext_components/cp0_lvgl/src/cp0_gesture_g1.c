/*
 * SPDX-License-Identifier: MIT
 *
 * G1, the three-finger hold (see cp0_gesture_g1.h). Pure C: no I/O, no clock, no globals.
 */
#include "cp0_gesture_g1.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum { S_IDLE, S_TRACKING, S_ARMED, S_DONE, S_BLOCKED };

void cp0_g1_params_default(cp0_g1_params_t *p)
{
    p->land_ms = CP0_G1_LAND_MS;
    p->hold_ms = CP0_G1_HOLD_MS;
    p->still_mm_x10 = CP0_G1_STILL_MM_X10;
    p->sep_mm_x10 = CP0_G1_SEP_MM_X10;
    p->edge_mm_x10 = CP0_G1_EDGE_MM_X10;
    p->major_max = CP0_G1_MAJOR_MAX;
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void cp0_g1_params_normalize(cp0_g1_params_t *p)
{
    p->land_ms = clamp(p->land_ms, 50, 2000);
    p->hold_ms = clamp(p->hold_ms, 200, 5000);
    p->still_mm_x10 = clamp(p->still_mm_x10, 5, 300);
    p->sep_mm_x10 = clamp(p->sep_mm_x10, 0, 400);
    p->edge_mm_x10 = clamp(p->edge_mm_x10, 0, 200);
    p->major_max = clamp(p->major_max, 0, 10000);
}

static int parse_uint(const char *s, int *out)
{
    if (!s || !*s) return -1;
    long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        if (v > 1000000) return -1;
    }
    *out = (int)v;
    return 0;
}

/* "4" or "4.5" (one decimal kept; never strtod: the locale may want a comma) -> x10 */
static int parse_x10(const char *s, int *out)
{
    if (!s || !*s) return -1;
    long v = 0;
    int digits = 0;
    for (; *s >= '0' && *s <= '9'; s++, digits++) {
        v = v * 10 + (*s - '0');
        if (v > 100000) return -1;
    }
    if (digits == 0) return -1;
    v *= 10;
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') return -1;
        v += *s - '0';
        s++;
        while (*s >= '0' && *s <= '9') s++;
    }
    if (*s) return -1;
    *out = (int)v;
    return 0;
}

int cp0_g1_params_apply_env(cp0_g1_params_t *p, const char *(*get)(const char *name))
{
    if (!p || !get) return 0;
    static const struct {
        const char *name;
        size_t offset;
        int x10;
    } table[] = {
        {"APPLAUNCH_MT_G1_HOLD_MS", offsetof(cp0_g1_params_t, hold_ms), 0},
        {"APPLAUNCH_MT_G1_LAND_MS", offsetof(cp0_g1_params_t, land_ms), 0},
        {"APPLAUNCH_MT_G1_STILL_MM", offsetof(cp0_g1_params_t, still_mm_x10), 1},
        {"APPLAUNCH_MT_G1_SEP_MM", offsetof(cp0_g1_params_t, sep_mm_x10), 1},
        {"APPLAUNCH_MT_G1_EDGE_MM", offsetof(cp0_g1_params_t, edge_mm_x10), 1},
        {"APPLAUNCH_MT_G1_MAJOR_MAX", offsetof(cp0_g1_params_t, major_max), 0},
    };
    int taken = 0;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        const char *text = get(table[i].name);
        int value = 0;
        if (!text || !*text) continue;
        if ((table[i].x10 ? parse_x10(text, &value) : parse_uint(text, &value)) != 0) continue;
        memcpy((char *)p + table[i].offset, &value, sizeof(value));
        taken++;
    }
    cp0_g1_params_normalize(p);
    return taken;
}

const char *cp0_g1_reason_name(cp0_g1_reason_t reason)
{
    switch (reason) {
    case CP0_G1_WHY_LAND: return "land";
    case CP0_G1_WHY_SEP: return "sep";
    case CP0_G1_WHY_EDGE: return "edge";
    case CP0_G1_WHY_MAJOR: return "major";
    case CP0_G1_WHY_EXTRA: return "extra";
    case CP0_G1_WHY_LIFT: return "lift";
    case CP0_G1_WHY_MOVE: return "move";
    case CP0_G1_WHY_DROPPED: return "dropped";
    case CP0_G1_WHY_NONE: break;
    }
    return "none";
}

void cp0_g1_init(cp0_g1_t *g, const cp0_g1_params_t *p, int w, int h, int ppmm_x100)
{
    memset(g, 0, sizeof(*g));
    if (p) g->p = *p;
    else cp0_g1_params_default(&g->p);
    cp0_g1_params_normalize(&g->p);
    g->w = w > 0 ? w : 1;
    g->h = h > 0 ? h : 1;
    g->ppmm = (ppmm_x100 > 0 ? ppmm_x100 : 1130) / 100.0;
    g->still_px = g->p.still_mm_x10 / 10.0 * g->ppmm;
    g->sep_px = g->p.sep_mm_x10 / 10.0 * g->ppmm;
    g->edge_px = g->p.edge_mm_x10 / 10.0 * g->ppmm;
    g->state = S_IDLE;
    g->land_ms = -1;
}

static cp0_g1_event_t no_event(void)
{
    cp0_g1_event_t e;
    memset(&e, 0, sizeof(e));
    e.land_ms = -1;
    return e;
}

static int mm_x10(const cp0_g1_t *g, double px)
{
    return (int)lround(px / g->ppmm * 10.0);
}

static int ms_between(int64_t a_us, int64_t b_us)
{
    return (int)((b_us - a_us) / 1000);
}

static int find_id(const cp0_g1_t *g, int32_t id)
{
    for (int i = 0; i < g->n_ids; i++)
        if (g->ids[i] == id) return i;
    return -1;
}

static void cancel(cp0_g1_t *g, cp0_g1_reason_t why, int value)
{
    g->reason = why;
    g->value = value;
    g->state = S_DONE;
}

static cp0_g1_event_t fire(cp0_g1_t *g, int64_t now_us)
{
    cp0_g1_event_t e = no_event();
    e.outcome = CP0_G1_FIRED;
    e.contacts = g->max_n;
    e.held_ms = ms_between(g->t3, now_us);
    e.land_ms = g->land_ms;
    e.t_due_us = g->t3 + (int64_t)g->p.hold_ms * 1000;
    g->fired = 1;
    g->state = S_DONE;
    return e;
}

static void start_session(cp0_g1_t *g, int64_t t)
{
    g->state = S_TRACKING;
    g->t0 = t;
    g->t3 = 0;
    g->max_n = 0;
    g->n_ids = 0;
    g->ids_overflow = 0;
    g->fired = 0;
    g->reason = CP0_G1_WHY_NONE;
    g->value = 0;
    g->land_ms = -1;
}

/* At t3: spacing, edges, sizes (study 5.2 step 2). */
static void arm(cp0_g1_t *g, const cp0_touch_frame_t *f)
{
    const int64_t t = f->t_us;
    g->t3 = t;
    g->land_ms = ms_between(g->t0, t);
    double closest = -1;
    for (int i = 0; i < f->n; i++)
        for (int j = i + 1; j < f->n; j++) {
            const double d = hypot((double)(f->c[i].x - f->c[j].x), (double)(f->c[i].y - f->c[j].y));
            if (closest < 0 || d < closest) closest = d;
        }
    double edge = -1;
    for (int i = 0; i < f->n; i++) {
        double d = f->c[i].x;
        if (f->c[i].y < d) d = f->c[i].y;
        if (g->w - 1 - f->c[i].x < d) d = g->w - 1 - f->c[i].x;
        if (g->h - 1 - f->c[i].y < d) d = g->h - 1 - f->c[i].y;
        if (edge < 0 || d < edge) edge = d;
    }
    int major = -1;
    for (int i = 0; i < f->n; i++) {
        const int k = find_id(g, f->c[i].id);
        if (k >= 0 && g->major[k] > major) major = g->major[k];
    }
    if (t - g->t0 > (int64_t)g->p.land_ms * 1000) cancel(g, CP0_G1_WHY_LAND, g->land_ms);
    else if (closest >= 0 && closest < g->sep_px) cancel(g, CP0_G1_WHY_SEP, mm_x10(g, closest));
    else if (edge >= 0 && edge < g->edge_px) cancel(g, CP0_G1_WHY_EDGE, mm_x10(g, edge < 0 ? 0 : edge));
    else if (g->p.major_max > 0 && major > g->p.major_max) cancel(g, CP0_G1_WHY_MAJOR, major);
    else g->state = S_ARMED;
}

cp0_g1_event_t cp0_g1_frame(cp0_g1_t *g, const cp0_touch_frame_t *f)
{
    cp0_g1_event_t e = no_event();
    const int64_t t = f->t_us;
    const int total = f->n + f->extra;
    if (g->state == S_BLOCKED) {
        if (total == 0 && t >= g->block_until) g->state = S_IDLE;
        return e;
    }
    if (f->resync) {
        /* events were lost: what happened meanwhile is unknown */
        if (g->state == S_TRACKING || g->state == S_ARMED) {
            g->max_n = total > g->max_n ? total : g->max_n;
            cancel(g, CP0_G1_WHY_DROPPED, 0);
        } else if (total > 0 && g->state != S_DONE) {
            g->state = S_BLOCKED;
            g->block_until = t;
            return e;
        }
    }
    if (g->state == S_IDLE) {
        if (total == 0) return e;
        start_session(g, t);
    }
    if (total > g->max_n) g->max_n = total;
    if (f->extra > 0) g->ids_overflow = 1;
    int new_ids = 0;
    for (int i = 0; i < f->n; i++) {
        int k = find_id(g, f->c[i].id);
        if (k < 0) {
            new_ids++;
            if (g->n_ids >= CP0_G1_IDS) {
                g->ids_overflow = 1;
                continue;
            }
            k = g->n_ids++;
            g->ids[k] = f->c[i].id;
            g->x0[k] = f->c[i].x;
            g->y0[k] = f->c[i].y;
            g->major[k] = -1;
        }
        if (f->c[i].major > g->major[k]) g->major[k] = f->c[i].major;
    }
    if (g->state == S_ARMED && t >= g->t3 + (int64_t)g->p.hold_ms * 1000) e = fire(g, t);
    if (g->state == S_TRACKING) {
        const int seen = g->n_ids + (g->ids_overflow ? CP0_G1_IDS : 0);
        int lifted = 0;
        for (int k = 0; k < g->n_ids && !lifted; k++) {
            int down = 0;
            for (int i = 0; i < f->n && !down; i++) down = f->c[i].id == g->ids[k];
            lifted = !down;
        }
        if (seen > CP0_G1_CONTACTS) cancel(g, CP0_G1_WHY_EXTRA, g->max_n > g->n_ids ? g->max_n : g->n_ids);
        else if (lifted) cancel(g, CP0_G1_WHY_LIFT, ms_between(g->t0, t));
        else if (total == CP0_G1_CONTACTS) arm(g, f);
        else if (t - g->t0 > (int64_t)g->p.land_ms * 1000) cancel(g, CP0_G1_WHY_LAND, ms_between(g->t0, t));
    } else if (g->state == S_ARMED) {
        if (new_ids || total > CP0_G1_CONTACTS) {
            cancel(g, CP0_G1_WHY_EXTRA, g->max_n > g->n_ids ? g->max_n : g->n_ids);
        } else if (total < CP0_G1_CONTACTS) {
            cancel(g, CP0_G1_WHY_LIFT, ms_between(g->t3, t));
        } else {
            for (int i = 0; i < f->n; i++) {
                const int k = find_id(g, f->c[i].id);
                if (k < 0) continue;
                const double d = hypot((double)(f->c[i].x - g->x0[k]), (double)(f->c[i].y - g->y0[k]));
                if (d > g->still_px) {
                    cancel(g, CP0_G1_WHY_MOVE, mm_x10(g, d));
                    break;
                }
            }
        }
    }
    if (total == 0) {
        if (g->state == S_DONE && !g->fired && g->max_n >= CP0_G1_CONTACTS) {
            e = no_event();
            e.outcome = CP0_G1_CANCELLED;
            e.reason = g->reason;
            e.value = g->value;
            e.contacts = g->max_n;
            e.land_ms = g->land_ms;
        }
        g->state = S_IDLE;
    }
    return e;
}

cp0_g1_event_t cp0_g1_tick(cp0_g1_t *g, int64_t now_us)
{
    if (g->state == S_ARMED && now_us >= g->t3 + (int64_t)g->p.hold_ms * 1000) return fire(g, now_us);
    return no_event();
}

int64_t cp0_g1_due_us(const cp0_g1_t *g)
{
    return g->state == S_ARMED ? g->t3 + (int64_t)g->p.hold_ms * 1000 : -1;
}

void cp0_g1_block(cp0_g1_t *g, int64_t now_us)
{
    g->state = S_BLOCKED;
    g->block_until = now_us;
}

void cp0_g1_reset(cp0_g1_t *g)
{
    g->state = S_IDLE;
}

int cp0_g1_describe(const cp0_g1_t *g, const cp0_g1_event_t *e, char *buf, size_t size)
{
    if (!buf || size == 0) return 0;
    buf[0] = '\0';
    if (!e || e->outcome == CP0_G1_NONE) return 0;
    if (e->outcome == CP0_G1_FIRED)
        return snprintf(buf, size, "G1 fired (%d contacts, held %d ms)", e->contacts, e->held_ms);
    const cp0_g1_params_t *p = &g->p;
    char why[128];
    const int v = e->value;
    switch (e->reason) {
    case CP0_G1_WHY_LAND:
        if (e->land_ms >= 0)
            snprintf(why, sizeof(why), "the third finger came %d ms after the first (limit %d ms)", v, p->land_ms);
        else
            snprintf(why, sizeof(why), "no third finger within %d ms of the first", p->land_ms);
        break;
    case CP0_G1_WHY_SEP:
        snprintf(why, sizeof(why), "two fingers %d.%d mm apart (min %d.%d mm)", v / 10, v % 10, p->sep_mm_x10 / 10,
                 p->sep_mm_x10 % 10);
        break;
    case CP0_G1_WHY_EDGE:
        snprintf(why, sizeof(why), "a finger %d.%d mm from the edge (min %d.%d mm)", v / 10, v % 10,
                 p->edge_mm_x10 / 10, p->edge_mm_x10 % 10);
        break;
    case CP0_G1_WHY_MAJOR:
        snprintf(why, sizeof(why), "a contact of size %d (max %d: a palm?)", v, p->major_max);
        break;
    case CP0_G1_WHY_EXTRA: snprintf(why, sizeof(why), "%d contacts in one touch (a palm or a grip?)", v); break;
    case CP0_G1_WHY_LIFT:
        if (e->land_ms >= 0)
            snprintf(why, sizeof(why), "a finger lifted after %d ms (hold %d ms)", v, p->hold_ms);
        else
            snprintf(why, sizeof(why), "a finger lifted before the three were down");
        break;
    case CP0_G1_WHY_MOVE:
        snprintf(why, sizeof(why), "a finger moved %d.%d mm (limit %d.%d mm)", v / 10, v % 10, p->still_mm_x10 / 10,
                 p->still_mm_x10 % 10);
        break;
    case CP0_G1_WHY_DROPPED: snprintf(why, sizeof(why), "touch events were lost (the reader fell behind)"); break;
    case CP0_G1_WHY_NONE: snprintf(why, sizeof(why), "not a gesture"); break;
    }
    if (e->reason == CP0_G1_WHY_EXTRA) return snprintf(buf, size, "G1 cancelled: %s", why);
    return snprintf(buf, size, "G1 cancelled: %s, %d contacts", why, e->contacts);
}
