/*
 * SPDX-License-Identifier: MIT
 *
 * Slot-aware touch tracker (see cp0_touch_slots.h). Pure C: no I/O, no clock.
 */
#include "cp0_touch_slots.h"

#include <string.h>

/* The evdev codes used (linux/input-event-codes.h values, a stable ABI; spelled out so this file also builds where
 * that header does not exist). */
enum {
    EVC_SYN = 0x00, EVC_KEY = 0x01, EVC_ABS = 0x03,
    EVC_SYN_REPORT = 0, EVC_SYN_DROPPED = 3,
    EVC_ABS_X = 0x00, EVC_ABS_Y = 0x01,
    EVC_ABS_MT_SLOT = 0x2f, EVC_ABS_MT_TOUCH_MAJOR = 0x30, EVC_ABS_MT_POSITION_X = 0x35, EVC_ABS_MT_POSITION_Y = 0x36,
    EVC_ABS_MT_TRACKING_ID = 0x39,
    EVC_BTN_TOUCH = 0x14a,
};

void cp0_touch_slots_init(cp0_touch_slots_t *t, int mt, int slot_now)
{
    memset(t, 0, sizeof(*t));
    t->mt = mt != 0;
    t->cur = t->mt ? slot_now : 0;
    for (int i = 0; i < CP0_TOUCH_SLOTS_MAX; i++) {
        t->s[i].id = -1;
        t->s[i].prev_id = -1;
        t->s[i].major = -1;
    }
    t->primary = -1;
    t->primary_id = -1;
}

static int popcount32(uint32_t v)
{
    int n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}

int cp0_touch_slots_down(const cp0_touch_slots_t *t)
{
    if (!t->mt) return t->ptr_pressed ? 1 : 0;
    int n = popcount32(t->extra_mask);
    for (int i = 0; i < CP0_TOUCH_SLOTS_MAX; i++)
        if (t->s[i].id >= 0) n++;
    return n;
}

/* End of a frame (protocol B): who is the single pointer now. */
static void commit(cp0_touch_slots_t *t)
{
    if (t->primary >= 0 && t->s[t->primary].id != t->primary_id) {
        /* the first contact lifted (or its slot holds another contact now) */
        t->primary = -1;
        t->primary_id = -1;
        int survivors = t->extra_mask != 0;
        for (int i = 0; i < CP0_TOUCH_SLOTS_MAX; i++)
            if (t->s[i].id >= 0 && t->s[i].id == t->s[i].prev_id) survivors = 1;
        if (survivors) t->wait_empty = 1;
    }
    if (cp0_touch_slots_down(t) == 0) t->wait_empty = 0;
    if (t->primary < 0 && !t->wait_empty) {
        for (int i = 0; i < CP0_TOUCH_SLOTS_MAX; i++) {
            if (t->s[i].id < 0) continue;
            t->primary = i; /* contacts that land in the same frame: the lowest slot */
            t->primary_id = t->s[i].id;
            break;
        }
    }
    t->ptr_pressed = t->primary >= 0;
    if (t->ptr_pressed) {
        t->ptr_x = t->s[t->primary].x;
        t->ptr_y = t->s[t->primary].y;
    }
    for (int i = 0; i < CP0_TOUCH_SLOTS_MAX; i++) t->s[i].prev_id = t->s[i].id;
}

/* Today's flattened reader, event for event (the reading of every non protocol B device). */
static cp0_touch_ev_t flat_event(cp0_touch_slots_t *t, uint16_t type, uint16_t code, int32_t value)
{
    if (type == EVC_ABS) {
        if (code == EVC_ABS_MT_POSITION_X || code == EVC_ABS_X) t->ptr_x = value;
        else if (code == EVC_ABS_MT_POSITION_Y || code == EVC_ABS_Y) t->ptr_y = value;
        else if (code == EVC_ABS_MT_TRACKING_ID) t->ptr_pressed = value >= 0;
    } else if (type == EVC_KEY && code == EVC_BTN_TOUCH) {
        t->ptr_pressed = value != 0;
    } else if (type == EVC_SYN && code == EVC_SYN_REPORT) {
        return CP0_TOUCH_EV_FRAME;
    }
    return CP0_TOUCH_EV_NONE;
}

cp0_touch_ev_t cp0_touch_slots_event(cp0_touch_slots_t *t, uint16_t type, uint16_t code, int32_t value)
{
    if (!t->mt) return flat_event(t, type, code, value);
    if (type == EVC_SYN) {
        if (code == EVC_SYN_DROPPED) {
            t->dropping = 1;
        } else if (code == EVC_SYN_REPORT) {
            if (t->dropping) {
                t->dropping = 0;
                return CP0_TOUCH_EV_RESYNC;
            }
            commit(t);
            return CP0_TOUCH_EV_FRAME;
        }
        return CP0_TOUCH_EV_NONE;
    }
    if (t->dropping || type != EVC_ABS) return CP0_TOUCH_EV_NONE;
    if (code == EVC_ABS_MT_SLOT) {
        t->cur = value;
        return CP0_TOUCH_EV_NONE;
    }
    if (t->cur < 0) return CP0_TOUCH_EV_NONE;
    if (t->cur >= CP0_TOUCH_SLOTS_MAX) {
        const int bit = t->cur - CP0_TOUCH_SLOTS_MAX;
        if (code == EVC_ABS_MT_TRACKING_ID && bit < 32) {
            if (value >= 0) t->extra_mask |= 1u << bit;
            else t->extra_mask &= ~(1u << bit);
        }
        return CP0_TOUCH_EV_NONE;
    }
    switch (code) {
    case EVC_ABS_MT_TRACKING_ID:
        t->s[t->cur].id = value >= 0 ? value : -1;
        t->s[t->cur].major = -1; /* a new contact: no size until the device reports one */
        break;
    case EVC_ABS_MT_POSITION_X: t->s[t->cur].x = value; break;
    case EVC_ABS_MT_POSITION_Y: t->s[t->cur].y = value; break;
    case EVC_ABS_MT_TOUCH_MAJOR: t->s[t->cur].major = value; break;
    default: break;
    }
    return CP0_TOUCH_EV_NONE;
}

void cp0_touch_slots_load(cp0_touch_slots_t *t, const int32_t *ids, const int32_t *xs, const int32_t *ys,
                          const int32_t *majors, int n)
{
    t->extra_mask = 0;
    for (int i = 0; i < CP0_TOUCH_SLOTS_MAX; i++) {
        const int have = ids && i < n;
        t->s[i].id = have && ids[i] >= 0 ? ids[i] : -1;
        if (have && xs) t->s[i].x = xs[i];
        if (have && ys) t->s[i].y = ys[i];
        t->s[i].major = have && majors ? majors[i] : -1;
    }
    t->dropping = 0;
    t->resync_next = 1;
    if (t->mt) commit(t);
}

void cp0_touch_slots_frame(cp0_touch_slots_t *t, int64_t t_us, cp0_touch_frame_t *out)
{
    memset(out, 0, sizeof(*out));
    out->t_us = t_us;
    out->resync = t->resync_next;
    t->resync_next = 0;
    if (!t->mt) {
        if (t->ptr_pressed) {
            out->n = 1;
            out->c[0].x = t->ptr_x;
            out->c[0].y = t->ptr_y;
            out->c[0].major = -1;
        }
        return;
    }
    for (int i = 0; i < CP0_TOUCH_SLOTS_MAX; i++) {
        if (t->s[i].id < 0) continue;
        cp0_touch_contact_t *c = &out->c[out->n++];
        c->id = t->s[i].id;
        c->x = t->s[i].x;
        c->y = t->s[i].y;
        c->major = t->s[i].major;
        c->slot = i;
    }
    out->extra = popcount32(t->extra_mask);
}

int cp0_touch_slots_pointer(const cp0_touch_slots_t *t, int32_t *x, int32_t *y)
{
    if (x) *x = t->ptr_x;
    if (y) *y = t->ptr_y;
    return t->ptr_pressed;
}
