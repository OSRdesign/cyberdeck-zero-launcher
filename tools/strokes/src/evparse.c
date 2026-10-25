/*
 * SPDX-License-Identifier: MIT
 *
 * Touch event parser of strokes-capture (see evparse.h). Our own code, written for this tool.
 */
#include "evparse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int evp_init(evp *p)
{
    memset(p, 0, sizeof *p);
    p->group_ms = 150.0f;
    p->ppmm = 8.54f;
    p->primary = -1;
    for (int i = 0; i < EVP_SLOTS; ++i)
        p->slots[i].tid = -1;
    p->parse_axis_code = -1;
    p->cur = malloc(sizeof *p->cur);
    if (!p->cur)
        return -1;
    sample_clear(p->cur);
    return 0;
}

void evp_free(evp *p)
{
    free(p->cur);
    p->cur = NULL;
}

void evp_set_axis(evp *p, int code, int min, int max)
{
    evp_axis *a = code == EVP_ABS_X ? &p->ax : code == EVP_ABS_Y ? &p->ay : code == EVP_ABS_MT_POSITION_X ? &p->mx
                                                         : code == EVP_ABS_MT_POSITION_Y ? &p->my : NULL;
    if (!a || max <= min)
        return;
    a->min = min;
    a->max = max;
    a->valid = 1;
}

void evp_map(const evp *p, int mt, int rx, int ry, float *x, float *y)
{
    const evp_axis *ax = mt ? &p->mx : &p->ax, *ay = mt ? &p->my : &p->ay;
    float fx = (float)rx, fy = (float)ry, w = 0.0f, h = 0.0f;
    if (p->out_w > 0.0f && p->out_h > 0.0f && ax->valid && ay->valid) {
        fx = (float)(rx - ax->min) * p->out_w / (float)(ax->max - ax->min + 1);
        fy = (float)(ry - ay->min) * p->out_h / (float)(ay->max - ay->min + 1);
        w = p->out_w;
        h = p->out_h;
    } else if (ax->valid && ay->valid) {
        fx = (float)(rx - ax->min);
        fy = (float)(ry - ay->min);
        w = (float)(ax->max - ax->min + 1);
        h = (float)(ay->max - ay->min + 1);
    }
    switch (w > 0.0f ? p->rotate : 0) {
    case 90: /* clockwise: the top-left corner goes to the top-right */
        *x = h - 1.0f - fy;
        *y = fx;
        break;
    case 180:
        *x = w - 1.0f - fx;
        *y = h - 1.0f - fy;
        break;
    case 270:
        *x = fy;
        *y = w - 1.0f - fx;
        break;
    default:
        *x = fx;
        *y = fy;
        break;
    }
}

static void emit(evp *p)
{
    if (!p->open)
        return;
    if (p->cur->n > 0) {
        p->cur->ppmm = p->ppmm;
        if (p->overflow)
            p->overflows++;
        p->samples++;
        if (p->on_sample)
            p->on_sample(p->ctx, p->cur);
    }
    sample_clear(p->cur);
    p->open = p->down = p->overflow = 0;
}

static void add_point(evp *p, uint64_t t, int mt, int rx, int ry)
{
    float x, y;
    evp_map(p, mt, rx, ry, &x, &y);
    if (sample_add(p->cur, x, y, t))
        p->overflow = 1;
}

static void press(evp *p, uint64_t t, int mt, int rx, int ry)
{
    if (p->open && !p->down && t >= p->last_lift && (float)(t - p->last_lift) / 1000.0f > p->group_ms)
        emit(p);
    if (!p->open) {
        sample_clear(p->cur);
        p->open = 1;
    }
    /* times inside a sample start at 0 at its first press */
    add_point(p, t, mt, rx, ry);
    p->down = 1;
}

static void lift(evp *p, uint64_t t)
{
    if (!p->down)
        return;
    if (sample_lift(p->cur, t))
        p->overflow = 1;
    p->down = 0;
    p->last_lift = t;
}

static void frame(evp *p, uint64_t t)
{
    p->frames++;
    if (p->has_mt) {
        if (p->primary >= 0 && p->slots[p->primary].tid < 0) {
            lift(p, t);
            p->primary = -1;
        } else if (p->primary >= 0 && p->moved) {
            if (p->down)
                add_point(p, t, 1, p->slots[p->primary].x, p->slots[p->primary].y);
        }
        if (p->primary < 0) {
            for (int i = 0; i < EVP_SLOTS; ++i)
                if (p->slots[i].tid >= 0 && p->slots[i].seen) {
                    p->primary = i;
                    press(p, t, 1, p->slots[i].x, p->slots[i].y);
                    break;
                }
        }
    } else {
        /* st_prev: a single-touch contact is down (pressed once its position is known) */
        if (p->st_touch && !p->st_prev) {
            if (p->st_seen) {
                press(p, t, 0, p->st_x, p->st_y);
                p->st_prev = 1;
            }
        } else if (!p->st_touch && p->st_prev) {
            lift(p, t);
            p->st_prev = 0;
        } else if (p->st_touch && p->moved) {
            add_point(p, t, 0, p->st_x, p->st_y);
        }
    }
    p->moved = 0;
}

void evp_event(evp *p, uint64_t t_us, int type, int code, int value)
{
    p->events++;
    if (type == EVP_EV_SYN) {
        if (code == EVP_SYN_DROPPED) {
            p->dropped = 1;
            return;
        }
        if (code != EVP_SYN_REPORT)
            return;
        if (p->dropped) { /* events were lost: this frame is not trusted, the next one resyncs */
            p->dropped = 0;
            p->moved = 0;
            return;
        }
        frame(p, t_us);
        return;
    }
    if (p->dropped)
        return;
    if (type == EVP_EV_KEY && code == EVP_BTN_TOUCH) {
        p->st_touch = value != 0;
        return;
    }
    if (type != EVP_EV_ABS)
        return;
    switch (code) {
    case EVP_ABS_MT_SLOT:
        p->has_mt = 1;
        p->slot = value >= 0 && value < EVP_SLOTS ? value : -1;
        break;
    case EVP_ABS_MT_TRACKING_ID:
        p->has_mt = 1;
        if (p->slot >= 0) {
            p->slots[p->slot].tid = value;
            if (value < 0)
                p->slots[p->slot].seen = 0;
        }
        break;
    case EVP_ABS_MT_POSITION_X:
    case EVP_ABS_MT_POSITION_Y:
        p->has_mt = 1;
        if (p->slot >= 0) {
            if (code == EVP_ABS_MT_POSITION_X)
                p->slots[p->slot].x = value;
            else
                p->slots[p->slot].y = value;
            p->slots[p->slot].seen = 1;
            if (p->slot == p->primary)
                p->moved = 1;
        }
        break;
    case EVP_ABS_X:
        p->st_x = value;
        p->st_seen = 1;
        if (!p->has_mt)
            p->moved = 1;
        break;
    case EVP_ABS_Y:
        p->st_y = value;
        p->st_seen = 1;
        if (!p->has_mt)
            p->moved = 1;
        break;
    default:
        break;
    }
}

void evp_tick(evp *p, uint64_t now_us)
{
    if (p->open && !p->down && now_us >= p->last_lift && (float)(now_us - p->last_lift) / 1000.0f > p->group_ms)
        emit(p);
}

void evp_finish(evp *p)
{
    if (p->down) {
        uint64_t t = p->cur->n ? p->cur->p[p->cur->n - 1].t_us : 0;
        lift(p, t);
    }
    emit(p);
}

int evp_evtest_line(evp *p, const char *line)
{
    const char *s = line;
    while (*s == ' ' || *s == '\t')
        s++;
    if (strncmp(s, "Input device name:", 18) == 0) {
        const char *q = strchr(s, '"');
        const char *e = q ? strrchr(q + 1, '"') : NULL;
        if (q && e && e > q)
            snprintf(p->device, sizeof p->device, "%.*s", (int)(e - q - 1), q + 1);
        return 0;
    }
    int code, v;
    if (sscanf(s, "Event code %d", &code) == 1) {
        p->parse_axis_code = code;
        return 0;
    }
    if (sscanf(s, "Min %d", &v) == 1 && p->parse_axis_code >= 0) {
        evp_axis *a = p->parse_axis_code == EVP_ABS_X ? &p->ax : p->parse_axis_code == EVP_ABS_Y ? &p->ay
                    : p->parse_axis_code == EVP_ABS_MT_POSITION_X ? &p->mx
                    : p->parse_axis_code == EVP_ABS_MT_POSITION_Y ? &p->my : NULL;
        if (a)
            a->min = v;
        return 0;
    }
    if (sscanf(s, "Max %d", &v) == 1 && p->parse_axis_code >= 0) {
        evp_axis *a = p->parse_axis_code == EVP_ABS_X ? &p->ax : p->parse_axis_code == EVP_ABS_Y ? &p->ay
                    : p->parse_axis_code == EVP_ABS_MT_POSITION_X ? &p->mx
                    : p->parse_axis_code == EVP_ABS_MT_POSITION_Y ? &p->my : NULL;
        if (a) {
            a->max = v;
            a->valid = a->max > a->min;
        }
        return 0;
    }
    unsigned long long sec, usec;
    if (sscanf(s, "Event: time %llu.%llu,", &sec, &usec) != 2)
        return 0;
    uint64_t t = (uint64_t)sec * 1000000ull + (uint64_t)usec;
    if (strstr(s, "SYN_REPORT")) {
        evp_event(p, t, EVP_EV_SYN, EVP_SYN_REPORT, 0);
        return 1;
    }
    if (strstr(s, "SYN_DROPPED")) {
        evp_event(p, t, EVP_EV_SYN, EVP_SYN_DROPPED, 0);
        return 1;
    }
    const char *ty = strstr(s, "type ");
    const char *co = strstr(s, "code ");
    const char *va = strstr(s, "value ");
    int type, value;
    if (!ty || !co || !va || sscanf(ty, "type %d", &type) != 1 || sscanf(co, "code %d", &code) != 1 ||
        sscanf(va, "value %d", &value) != 1)
        return 0;
    evp_event(p, t, type, code, value);
    return 1;
}
