/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-c: the slot-aware touch tracker (cp0_touch_slots.h).
 *   - single touch is unchanged: random one-finger streams (Goodix-like packets with the kernel's pointer emulation,
 *     and plain single-touch packets) give, after every packet, exactly the pointer of the reader of before
 *     (cp0_lvgl_dpi_scaled.c touch_update up to P-b, copied here as the reference);
 *   - the flattened mode IS the reader of before, event for event, on any stream;
 *   - protocol B with several fingers: the pointer follows the first contact, the others never press it, it stays
 *     released after the first lifts until every finger is up; frames list every contact (ids, positions, sizes);
 *     slots beyond the table are counted; SYN_DROPPED asks for a resync, load() puts the state back.
 */
#include "cp0_touch_slots.h"

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail, g_pass;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (cond) g_pass++;                                                                                            \
        else {                                                                                                         \
            g_fail++;                                                                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
        }                                                                                                              \
    } while (0)

/* ---------------------------------------------------------------- the reader of before (reference) */

typedef struct {
    int raw_x, raw_y, pressed;
} ref_t;

static void ref_event(ref_t *r, int type, int code, int value)
{
    if (type == EV_ABS) {
        if (code == ABS_MT_POSITION_X || code == ABS_X) r->raw_x = value;
        else if (code == ABS_MT_POSITION_Y || code == ABS_Y) r->raw_y = value;
        else if (code == ABS_MT_TRACKING_ID) r->pressed = value >= 0;
    } else if (type == EV_KEY && code == BTN_TOUCH) {
        r->pressed = value != 0;
    }
}

/* ---------------------------------------------------------------- helpers */

static unsigned g_seed = 12345;
static int rnd(int n)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return (int)((g_seed >> 16) % (unsigned)n);
}

typedef struct {
    int type, code, value;
} ev_t;

static int feed(cp0_touch_slots_t *t, ref_t *r, const ev_t *ev, int n)
{
    int frames = 0;
    for (int i = 0; i < n; i++) {
        if (r) ref_event(r, ev[i].type, ev[i].code, ev[i].value);
        if (cp0_touch_slots_event(t, (uint16_t)ev[i].type, (uint16_t)ev[i].code, ev[i].value) == CP0_TOUCH_EV_FRAME)
            frames++;
    }
    return frames;
}

static int same_pointer(const cp0_touch_slots_t *t, const ref_t *r)
{
    int32_t x, y;
    const int p = cp0_touch_slots_pointer(t, &x, &y);
    if (p != r->pressed) return 0;
    return !p || (x == r->raw_x && y == r->raw_y);
}

/* one Goodix-like packet for a single finger: slot, tracking id on landing, position, size, then the kernel's pointer
 * emulation (ABS_X / ABS_Y, BTN_TOUCH on change), SYN_REPORT */
static int goodix_packet(ev_t *ev, int *down, int *id, int x, int y, int lift, int with_slot)
{
    int n = 0;
    if (with_slot) ev[n++] = (ev_t){EV_ABS, ABS_MT_SLOT, 0};
    if (lift) {
        ev[n++] = (ev_t){EV_ABS, ABS_MT_TRACKING_ID, -1};
        ev[n++] = (ev_t){EV_KEY, BTN_TOUCH, 0};
        *down = 0;
    } else {
        if (!*down) ev[n++] = (ev_t){EV_ABS, ABS_MT_TRACKING_ID, ++*id};
        ev[n++] = (ev_t){EV_ABS, ABS_MT_POSITION_X, x};
        ev[n++] = (ev_t){EV_ABS, ABS_MT_POSITION_Y, y};
        ev[n++] = (ev_t){EV_ABS, ABS_MT_TOUCH_MAJOR, 40 + rnd(20)};
        if (!*down) ev[n++] = (ev_t){EV_KEY, BTN_TOUCH, 1};
        ev[n++] = (ev_t){EV_ABS, ABS_X, x};
        ev[n++] = (ev_t){EV_ABS, ABS_Y, y};
        *down = 1;
    }
    ev[n++] = (ev_t){EV_SYN, SYN_REPORT, 0};
    return n;
}

static void test_single_finger_unchanged(void)
{
    for (int with_slot = 0; with_slot <= 1; with_slot++) {
        cp0_touch_slots_t t;
        cp0_touch_slots_init(&t, 1, 0);
        ref_t r = {0, 0, 0};
        int down = 0, id = 100, x = 300, y = 200, ok = 1;
        for (int step = 0; step < 4000; step++) {
            ev_t ev[16];
            const int lift = down && rnd(10) == 0;
            if (!lift) {
                x = down ? x + rnd(21) - 10 : rnd(640);
                y = down ? y + rnd(21) - 10 : rnd(480);
            }
            if (!down && !lift && rnd(3) == 0) continue; /* nothing touches for a while */
            const int n = goodix_packet(ev, &down, &id, x, y, lift, with_slot);
            feed(&t, &r, ev, n);
            if (!same_pointer(&t, &r)) ok = 0;
        }
        CHECK(ok);
    }
    /* a plain single-touch panel (no slots, BTN_TOUCH + ABS_X / ABS_Y): flattened mode */
    cp0_touch_slots_t t;
    cp0_touch_slots_init(&t, 0, 0);
    ref_t r = {0, 0, 0};
    int ok = 1;
    for (int step = 0; step < 4000; step++) {
        const ev_t ev[4] = {{EV_KEY, BTN_TOUCH, rnd(4) != 0}, {EV_ABS, ABS_X, rnd(4096)}, {EV_ABS, ABS_Y, rnd(4096)},
                            {EV_SYN, SYN_REPORT, 0}};
        feed(&t, &r, ev, 4);
        if (!same_pointer(&t, &r)) ok = 0;
    }
    CHECK(ok);
}

static void test_flattened_is_the_old_reader(void)
{
    /* any stream, even a multi-finger one: the flattened mode reproduces the old reader after every event */
    cp0_touch_slots_t t;
    cp0_touch_slots_init(&t, 0, 0);
    ref_t r = {0, 0, 0};
    static const int codes[] = {ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y,
                                ABS_MT_TOUCH_MAJOR, ABS_X, ABS_Y};
    int ok = 1;
    for (int i = 0; i < 20000; i++) {
        ev_t e;
        const int k = rnd(10);
        if (k < 7) e = (ev_t){EV_ABS, codes[k], rnd(3) == 0 ? -1 : rnd(800)};
        else if (k < 8) e = (ev_t){EV_KEY, BTN_TOUCH, rnd(2)};
        else e = (ev_t){EV_SYN, rnd(4) == 0 ? SYN_DROPPED : SYN_REPORT, 0};
        ref_event(&r, e.type, e.code, e.value);
        (void)cp0_touch_slots_event(&t, (uint16_t)e.type, (uint16_t)e.code, e.value);
        if (!same_pointer(&t, &r)) ok = 0;
    }
    CHECK(ok);
    /* its frames carry the one pointer */
    cp0_touch_frame_t f;
    cp0_touch_slots_init(&t, 0, 0);
    const ev_t down[] = {{EV_KEY, BTN_TOUCH, 1}, {EV_ABS, ABS_X, 10}, {EV_ABS, ABS_Y, 20}, {EV_SYN, SYN_REPORT, 0}};
    CHECK(feed(&t, NULL, down, 4) == 1);
    cp0_touch_slots_frame(&t, 5, &f);
    CHECK(f.n == 1 && f.c[0].x == 10 && f.c[0].y == 20 && f.c[0].major == -1 && f.t_us == 5);
    CHECK(cp0_touch_slots_down(&t) == 1);
}

/* slot s: contact id (or -1 to lift) at x, y */
static int put(ev_t *ev, int s, int id, int x, int y)
{
    int n = 0;
    ev[n++] = (ev_t){EV_ABS, ABS_MT_SLOT, s};
    ev[n++] = (ev_t){EV_ABS, ABS_MT_TRACKING_ID, id};
    if (id >= 0) {
        ev[n++] = (ev_t){EV_ABS, ABS_MT_POSITION_X, x};
        ev[n++] = (ev_t){EV_ABS, ABS_MT_POSITION_Y, y};
    }
    return n;
}

static int move(ev_t *ev, int s, int x, int y)
{
    ev[0] = (ev_t){EV_ABS, ABS_MT_SLOT, s};
    ev[1] = (ev_t){EV_ABS, ABS_MT_POSITION_X, x};
    ev[2] = (ev_t){EV_ABS, ABS_MT_POSITION_Y, y};
    return 3;
}

static int syn(ev_t *ev)
{
    ev[0] = (ev_t){EV_SYN, SYN_REPORT, 0};
    return 1;
}

static void pointer_is(const cp0_touch_slots_t *t, int pressed, int x, int y, int line)
{
    int32_t px, py;
    const int p = cp0_touch_slots_pointer(t, &px, &py);
    if (p == pressed && (!pressed || (px == x && py == y))) {
        g_pass++;
        return;
    }
    g_fail++;
    fprintf(stderr, "FAIL line %d: pointer %d (%d,%d), expected %d (%d,%d)\n", line, p, px, py, pressed, x, y);
}

static void test_first_contact_pointer(void)
{
    cp0_touch_slots_t t;
    cp0_touch_slots_init(&t, 1, 0);
    ev_t ev[32];
    int n;
    /* A lands */
    n = put(ev, 0, 10, 100, 100);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 1, 100, 100, __LINE__);
    /* B lands and moves: the pointer stays on A (it used to jump to B) */
    n = put(ev, 1, 11, 400, 300);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 1, 100, 100, __LINE__);
    n = move(ev, 1, 420, 320);
    n += move(ev + n, 0, 105, 102);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 1, 105, 102, __LINE__);
    /* B lifts: A is still pressed (lifting B used to release A) */
    n = put(ev, 1, -1, 0, 0);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 1, 105, 102, __LINE__);
    /* C lands, then A lifts: released, and C never presses */
    n = put(ev, 1, 12, 500, 50);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    n = put(ev, 0, -1, 0, 0);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 0, 0, 0, __LINE__);
    CHECK(cp0_touch_slots_down(&t) == 1);
    n = move(ev, 1, 510, 60);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 0, 0, 0, __LINE__);
    /* a finger landing while C is still down does not press either */
    n = put(ev, 0, 13, 50, 50);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 0, 0, 0, __LINE__);
    /* everything up, then a new touch presses at once */
    n = put(ev, 0, -1, 0, 0);
    n += put(ev + n, 1, -1, 0, 0);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 0, 0, 0, __LINE__);
    CHECK(cp0_touch_slots_down(&t) == 0);
    n = put(ev, 3, 14, 7, 8);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 1, 7, 8, __LINE__);
    /* two contacts landing in one frame: the lower slot is the pointer */
    n = put(ev, 3, -1, 0, 0);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    n = put(ev, 2, 15, 30, 31);
    n += put(ev + n, 1, 16, 20, 21);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 1, 20, 21, __LINE__);
    /* the pointer's slot reused by a new contact in the same frame, the other finger still down: released */
    n = put(ev, 1, 17, 60, 61);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 0, 0, 0, __LINE__);
}

static void test_frames(void)
{
    cp0_touch_slots_t t;
    cp0_touch_slots_init(&t, 1, 2); /* the device was on slot 2 when opened */
    ev_t ev[32];
    int n = 0;
    ev[n++] = (ev_t){EV_ABS, ABS_MT_TRACKING_ID, 40}; /* no ABS_MT_SLOT first: slot 2 */
    ev[n++] = (ev_t){EV_ABS, ABS_MT_POSITION_X, 11};
    ev[n++] = (ev_t){EV_ABS, ABS_MT_POSITION_Y, 12};
    ev[n++] = (ev_t){EV_ABS, ABS_MT_TOUCH_MAJOR, 55};
    n += put(ev + n, 0, 41, 21, 22);
    n += put(ev + n, 12, 42, 900, 900); /* beyond the table: counted */
    n += syn(ev + n);
    CHECK(feed(&t, NULL, ev, n) == 1);
    cp0_touch_frame_t f;
    cp0_touch_slots_frame(&t, 1000, &f);
    CHECK(f.n == 2 && f.extra == 1 && f.resync == 0 && f.t_us == 1000);
    CHECK(f.c[0].slot == 0 && f.c[0].id == 41 && f.c[0].x == 21 && f.c[0].y == 22 && f.c[0].major == -1);
    CHECK(f.c[1].slot == 2 && f.c[1].id == 40 && f.c[1].x == 11 && f.c[1].y == 12 && f.c[1].major == 55);
    CHECK(cp0_touch_slots_down(&t) == 3);
    /* a new contact in slot 2 forgets the old size */
    n = put(ev, 2, -1, 0, 0);
    n += put(ev + n, 12, -1, 0, 0);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    n = put(ev, 2, 43, 5, 6);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    cp0_touch_slots_frame(&t, 2000, &f);
    CHECK(f.n == 2 && f.extra == 0 && f.c[1].id == 43 && f.c[1].major == -1);
    /* the kernel's pointer emulation is ignored in protocol B */
    n = 0;
    ev[n++] = (ev_t){EV_KEY, BTN_TOUCH, 0};
    ev[n++] = (ev_t){EV_ABS, ABS_X, 600};
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 1, 21, 22, __LINE__);
}

static void test_dropped(void)
{
    cp0_touch_slots_t t;
    cp0_touch_slots_init(&t, 1, 0);
    ev_t ev[32];
    int n = put(ev, 0, 1, 100, 100);
    n += put(ev + n, 1, 2, 300, 300);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    /* overflow: events up to the next SYN_REPORT are ignored, then a resync is asked for */
    n = 0;
    ev[n++] = (ev_t){EV_SYN, SYN_DROPPED, 0};
    n += put(ev + n, 0, -1, 0, 0);
    int last = CP0_TOUCH_EV_NONE;
    for (int i = 0; i < n; i++) last = cp0_touch_slots_event(&t, (uint16_t)ev[i].type, (uint16_t)ev[i].code, ev[i].value);
    CHECK(last == CP0_TOUCH_EV_NONE);
    CHECK(cp0_touch_slots_down(&t) == 2); /* the lift was not taken */
    CHECK(cp0_touch_slots_event(&t, EV_SYN, SYN_REPORT, 0) == CP0_TOUCH_EV_RESYNC);
    /* read back: slot 1 still has contact 2 (it moved), slot 0 is empty */
    const int32_t ids[2] = {-1, 2}, xs[2] = {0, 310}, ys[2] = {0, 305};
    cp0_touch_slots_load(&t, ids, xs, ys, NULL, 2);
    cp0_touch_frame_t f;
    cp0_touch_slots_frame(&t, 10, &f);
    CHECK(f.resync == 1 && f.n == 1 && f.c[0].id == 2 && f.c[0].x == 310);
    pointer_is(&t, 0, 0, 0, __LINE__); /* the first contact is gone: the other one does not press */
    cp0_touch_slots_frame(&t, 11, &f);
    CHECK(f.resync == 0);
    /* nothing could be read back: every contact is cleared, nothing stays pressed */
    cp0_touch_slots_load(&t, NULL, NULL, NULL, NULL, 0);
    CHECK(cp0_touch_slots_down(&t) == 0);
    pointer_is(&t, 0, 0, 0, __LINE__);
    /* the pointer survives a resync when its contact is still there */
    n = put(ev, 0, 7, 50, 60);
    n += syn(ev + n);
    feed(&t, NULL, ev, n);
    pointer_is(&t, 1, 50, 60, __LINE__);
    CHECK(cp0_touch_slots_event(&t, EV_SYN, SYN_DROPPED, 0) == CP0_TOUCH_EV_NONE);
    CHECK(cp0_touch_slots_event(&t, EV_SYN, SYN_REPORT, 0) == CP0_TOUCH_EV_RESYNC);
    const int32_t ids2[1] = {7}, xs2[1] = {52}, ys2[1] = {61};
    cp0_touch_slots_load(&t, ids2, xs2, ys2, NULL, 1);
    pointer_is(&t, 1, 52, 61, __LINE__);
}

int main(void)
{
    test_single_finger_unchanged();
    test_flattened_is_the_old_reader();
    test_first_contact_pointer();
    test_frames();
    test_dropped();
    printf("test_touch_slots: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
