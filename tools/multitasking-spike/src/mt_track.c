/*
 * SPDX-License-Identifier: MIT
 *
 * Pure multitouch contact tracker. See mt_track.h.
 */
#include "mt_track.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <string.h>

void mt_stats_reset(mt_stats *s)
{
    memset(s, 0, sizeof(*s));
    s->min_pair_mm = -1;
    s->major_min = -1;
    s->major_max = -1;
    s->shortest_us = -1;
}

void mt_init(mt_tracker *t, const mt_geom *g, int mode, int nslots)
{
    memset(t, 0, sizeof(*t));
    t->g = *g;
    t->mode = mode;
    t->nslots = nslots < 1 ? 1 : (nslots > MT_MAX_SLOTS ? MT_MAX_SLOTS : nslots);
    t->seg = -1;
    t->next_id = 1;
    for (int i = 0; i < MT_MAX_SLOTS; ++i) {
        t->slot[i].tid = -1;
        t->slot_major[i] = -1;
        t->slot_width[i] = -1;
        t->slot_pressure[i] = -1;
    }
    mt_stats_reset(&t->total);
}

void mt_to_mm(const mt_geom *g, int x, int y, double *mx, double *my)
{
    const double sx = g->max_x > g->min_x ? (double)(x - g->min_x) / (double)(g->max_x - g->min_x) : 0.0;
    const double sy = g->max_y > g->min_y ? (double)(y - g->min_y) / (double)(g->max_y - g->min_y) : 0.0;
    if (g->swap) {
        *mx = sy * g->mm_w;
        *my = sx * g->mm_h;
    } else {
        *mx = sx * g->mm_w;
        *my = sy * g->mm_h;
    }
}

double mt_dist_mm(const mt_geom *g, int x1, int y1, int x2, int y2)
{
    double ax, ay, bx, by;
    mt_to_mm(g, x1, y1, &ax, &ay);
    mt_to_mm(g, x2, y2, &bx, &by);
    return hypot(ax - bx, ay - by);
}

int mt_active(const mt_tracker *t)
{
    int n = 0;
    for (int i = 0; i < t->nslots; ++i)
        if (t->slot[i].tid >= 0) ++n;
    return n;
}

void mt_stats_add_contact(mt_stats *s, const mt_contact *c)
{
    const long long dur = c->t_end - c->t_start;
    s->contacts++;
    if (dur < MT_GHOST_US) s->ghosts++;
    if (dur > s->longest_us) s->longest_us = dur;
    if (s->shortest_us < 0 || dur < s->shortest_us) s->shortest_us = dur;
    if (c->maxdisp_mm > s->max_disp_mm) s->max_disp_mm = c->maxdisp_mm;
    if (c->path_mm > s->max_path_mm) s->max_path_mm = c->path_mm;
    if (c->major_min >= 0 && (s->major_min < 0 || c->major_min < s->major_min)) s->major_min = c->major_min;
    if (c->major_max > s->major_max) s->major_max = c->major_max;
}

static void start_contact(mt_tracker *t, int i, long long t_us, int tid)
{
    mt_contact *c = &t->slot[i];
    const int x = c->x, y = c->y; /* a slot keeps its last position: the kernel does not resend equal values */
    memset(c, 0, sizeof(*c));
    c->x = x;
    c->y = y;
    c->tid = tid;
    c->id = t->next_id++;
    c->t_start = t_us;
    c->t_end = t_us;
    c->major_min = -1;
    c->major_max = -1;
    c->width_max = -1;
    c->pressure_max = -1;
    c->seg = t->seg;
}

static void end_contact(mt_tracker *t, int i, long long t_us)
{
    mt_contact *c = &t->slot[i];
    if (c->tid < 0) return;
    if (!c->landed) {
        c->x0 = c->lastx = c->x;
        c->y0 = c->lasty = c->y;
        c->landed = true;
    }
    c->t_end = t_us;
    mt_stats_add_contact(&t->total, c);
    if (t->on_end) t->on_end(t->user, c);
    c->tid = -1;
}

static void sample(mt_tracker *t, int i)
{
    mt_contact *c = &t->slot[i];
    if (!c->landed) {
        c->x0 = c->lastx = c->x;
        c->y0 = c->lasty = c->y;
        c->landed = true;
    } else {
        c->path_mm += mt_dist_mm(&t->g, c->lastx, c->lasty, c->x, c->y);
        c->lastx = c->x;
        c->lasty = c->y;
        const double d = mt_dist_mm(&t->g, c->x0, c->y0, c->x, c->y);
        if (d > c->maxdisp_mm) c->maxdisp_mm = d;
    }
    const int m = t->slot_major[i];
    if (m >= 0) {
        if (c->major_min < 0 || m < c->major_min) c->major_min = m;
        if (m > c->major_max) c->major_max = m;
    }
    if (t->slot_width[i] > c->width_max) c->width_max = t->slot_width[i];
    if (t->slot_pressure[i] > c->pressure_max) c->pressure_max = t->slot_pressure[i];
}

static void frame(mt_tracker *t, long long t_us)
{
    t->total.frames++;
    if (t->mode == MT_MODE_A) {
        const int active = t->a_count;
        t->a_count = 0;
        if (active > t->total.max_active) t->total.max_active = active;
        if (t->on_frame) t->on_frame(t->user, t_us, active, -1);
        return;
    }
    int active = 0;
    double min_pair = -1;
    for (int i = 0; i < t->nslots; ++i) {
        if (t->slot[i].tid < 0) continue;
        sample(t, i);
        ++active;
    }
    for (int i = 0; i < t->nslots; ++i) {
        if (t->slot[i].tid < 0) continue;
        for (int j = i + 1; j < t->nslots; ++j) {
            if (t->slot[j].tid < 0) continue;
            const double d = mt_dist_mm(&t->g, t->slot[i].x, t->slot[i].y, t->slot[j].x, t->slot[j].y);
            if (min_pair < 0 || d < min_pair) min_pair = d;
        }
    }
    if (active > t->total.max_active) t->total.max_active = active;
    if (min_pair >= 0 && (t->total.min_pair_mm < 0 || min_pair < t->total.min_pair_mm)) t->total.min_pair_mm = min_pair;
    if (t->on_frame) t->on_frame(t->user, t_us, active, min_pair);
}

void mt_feed(mt_tracker *t, long long t_us, int type, int code, int value)
{
    if (type == EV_SYN) {
        if (code == SYN_DROPPED) {
            /* the kernel buffer overflowed: the slot state is unknown until the next SYN_REPORT */
            t->in_drop = true;
            t->total.dropped++;
            for (int i = 0; i < t->nslots; ++i) end_contact(t, i, t_us);
            t->st_down = false;
        } else if (code == SYN_REPORT) {
            if (t->in_drop) {
                t->in_drop = false;
                return;
            }
            frame(t, t_us);
        } else if (code == SYN_MT_REPORT && t->mode == MT_MODE_A) {
            if (t->a_has_data) t->a_count++;
            t->a_has_data = false;
        }
        return;
    }
    if (t->in_drop) return;

    if (t->mode == MT_MODE_A) {
        if (type == EV_ABS && (code == ABS_MT_POSITION_X || code == ABS_MT_POSITION_Y)) t->a_has_data = true;
        return;
    }
    if (t->mode == MT_MODE_SINGLE) {
        if (type == EV_KEY && code == BTN_TOUCH) {
            if (value && !t->st_down) start_contact(t, 0, t_us, (int)(t->next_id & 0x7fffffff));
            if (!value && t->st_down) end_contact(t, 0, t_us);
            t->st_down = value != 0;
        } else if (type == EV_ABS) {
            if (code == ABS_X) t->slot[0].x = value;
            else if (code == ABS_Y) t->slot[0].y = value;
            else if (code == ABS_PRESSURE) t->slot_pressure[0] = value;
        }
        return;
    }
    /* protocol B */
    if (type != EV_ABS) return;
    if (code == ABS_MT_SLOT) {
        t->cur = (value >= 0 && value < t->nslots) ? value : -1;
        return;
    }
    if (t->cur < 0) return;
    const int i = t->cur;
    switch (code) {
    case ABS_MT_TRACKING_ID:
        if (value < 0) {
            end_contact(t, i, t_us);
        } else if (t->slot[i].tid != value) {
            end_contact(t, i, t_us);
            start_contact(t, i, t_us, value);
        }
        break;
    case ABS_MT_POSITION_X: t->slot[i].x = value; break;
    case ABS_MT_POSITION_Y: t->slot[i].y = value; break;
    case ABS_MT_TOUCH_MAJOR: t->slot_major[i] = value; break;
    case ABS_MT_WIDTH_MAJOR: t->slot_width[i] = value; break;
    case ABS_MT_PRESSURE: t->slot_pressure[i] = value; break;
    default: break;
    }
}

void mt_finish(mt_tracker *t, long long t_us)
{
    if (t->mode == MT_MODE_A) return;
    for (int i = 0; i < t->nslots; ++i) end_contact(t, i, t_us);
    t->st_down = false;
}
