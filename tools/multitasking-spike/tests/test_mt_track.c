/*
 * SPDX-License-Identifier: MIT
 *
 * Unit tests of the pure multitouch tracker (src/mt_track.c) used by mtrec. Host only:
 *   sh build.sh host && build/host/test_mt_track
 */
#include "mt_track.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>

static int failures, checks;

#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        ++checks;                                                                                                     \
        if (!(cond)) {                                                                                                \
            ++failures;                                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                               \
            printf(__VA_ARGS__);                                                                                      \
            printf("\n");                                                                                             \
        }                                                                                                             \
    } while (0)

#define NEAR(a, b) (fabs((a) - (b)) < 0.01)

/* the deck: raw X 0..639 runs along the display's 42.5 mm height, raw Y 0..479 along its 56.6 mm width */
static const mt_geom deck = {0, 639, 0, 479, 56.6, 42.5, 1};
static const mt_geom flat = {0, 799, 0, 479, 94.0, 56.0, 0};

static void abs_ev(mt_tracker *t, long long us, int code, int v) { mt_feed(t, us, EV_ABS, code, v); }
static void syn(mt_tracker *t, long long us) { mt_feed(t, us, EV_SYN, SYN_REPORT, 0); }
static void down(mt_tracker *t, long long us, int slot, int tid, int x, int y)
{
    abs_ev(t, us, ABS_MT_SLOT, slot);
    abs_ev(t, us, ABS_MT_TRACKING_ID, tid);
    abs_ev(t, us, ABS_MT_POSITION_X, x);
    abs_ev(t, us, ABS_MT_POSITION_Y, y);
}
static void up(mt_tracker *t, long long us, int slot)
{
    abs_ev(t, us, ABS_MT_SLOT, slot);
    abs_ev(t, us, ABS_MT_TRACKING_ID, -1);
}

static void test_tap_and_ghost(void)
{
    mt_tracker t;
    mt_init(&t, &flat, MT_MODE_B, 10);
    down(&t, 0, 0, 10, 400, 240);
    syn(&t, 0);
    syn(&t, 50000);
    up(&t, 100000, 0);
    syn(&t, 100000);
    CHECK(t.total.contacts == 1, "tap: contacts %lld", t.total.contacts);
    CHECK(t.total.ghosts == 0, "tap: ghosts %lld", t.total.ghosts);
    CHECK(t.total.max_active == 1, "tap: max_active %d", t.total.max_active);
    down(&t, 200000, 0, 11, 100, 100);
    syn(&t, 200000);
    up(&t, 220000, 0);
    syn(&t, 220000);
    CHECK(t.total.contacts == 2 && t.total.ghosts == 1, "ghost: contacts %lld ghosts %lld", t.total.contacts,
          t.total.ghosts);
    CHECK(t.total.shortest_us == 20000 && t.total.longest_us == 100000, "durations %lld %lld", t.total.shortest_us,
          t.total.longest_us);
}

static void test_swap_distances(void)
{
    mt_tracker t;
    mt_init(&t, &deck, MT_MODE_B, 5);
    down(&t, 0, 0, 1, 100, 100);
    down(&t, 0, 1, 2, 100, 200); /* 100 raw Y = 100/479 of the 56.6 mm width */
    syn(&t, 0);
    CHECK(NEAR(t.total.min_pair_mm, 100.0 / 479.0 * 56.6), "swap y: %f", t.total.min_pair_mm);
    down(&t, 10000, 2, 3, 200, 100); /* 100 raw X = 100/639 of the 42.5 mm height */
    syn(&t, 10000);
    CHECK(NEAR(t.total.min_pair_mm, 100.0 / 639.0 * 42.5), "swap x: %f", t.total.min_pair_mm);
    CHECK(t.total.max_active == 3, "3 fingers: %d", t.total.max_active);
    double mx, my;
    mt_to_mm(&deck, 639, 0, &mx, &my);
    CHECK(NEAR(mx, 0) && NEAR(my, 42.5), "corner mapping %f %f", mx, my);
}

static void test_travel(void)
{
    mt_tracker t;
    mt_init(&t, &deck, MT_MODE_B, 5);
    down(&t, 0, 0, 7, 100, 100);
    syn(&t, 0);
    abs_ev(&t, 10000, ABS_MT_POSITION_Y, 150);
    syn(&t, 10000);
    abs_ev(&t, 20000, ABS_MT_POSITION_Y, 100);
    syn(&t, 20000);
    up(&t, 30000, 0);
    syn(&t, 30000);
    const double step = 50.0 / 479.0 * 56.6;
    CHECK(NEAR(t.total.max_path_mm, 2 * step), "path %f", t.total.max_path_mm);
    CHECK(NEAR(t.total.max_disp_mm, step), "disp %f", t.total.max_disp_mm);
}

static long long last_x0, last_y0;
static void remember(void *u, const mt_contact *c)
{
    (void)u;
    last_x0 = c->x0;
    last_y0 = c->y0;
}

static void test_slot_reuse_keeps_position(void)
{
    mt_tracker t;
    mt_init(&t, &flat, MT_MODE_B, 10);
    t.on_end = remember;
    down(&t, 0, 0, 1, 300, 300);
    syn(&t, 0);
    up(&t, 50000, 0);
    syn(&t, 50000);
    /* the kernel does not resend X/Y equal to the slot's last values */
    abs_ev(&t, 100000, ABS_MT_SLOT, 0);
    abs_ev(&t, 100000, ABS_MT_TRACKING_ID, 2);
    syn(&t, 100000);
    up(&t, 200000, 0);
    syn(&t, 200000);
    CHECK(last_x0 == 300 && last_y0 == 300, "reused slot landing %lld,%lld", last_x0, last_y0);
}

static void test_tid_change_and_major(void)
{
    mt_tracker t;
    mt_init(&t, &flat, MT_MODE_B, 10);
    down(&t, 0, 0, 1, 10, 10);
    abs_ev(&t, 0, ABS_MT_TOUCH_MAJOR, 10);
    syn(&t, 0);
    abs_ev(&t, 10000, ABS_MT_TOUCH_MAJOR, 14);
    syn(&t, 10000);
    abs_ev(&t, 20000, ABS_MT_TOUCH_MAJOR, 12);
    syn(&t, 20000);
    abs_ev(&t, 50000, ABS_MT_TRACKING_ID, 2); /* new id without a -1 first: the old contact ended */
    syn(&t, 50000);
    CHECK(t.total.contacts == 1, "tid change ends the old one: %lld", t.total.contacts);
    CHECK(t.total.major_min == 10 && t.total.major_max == 14, "major %d..%d", t.total.major_min, t.total.major_max);
    mt_finish(&t, 90000);
    CHECK(t.total.contacts == 2, "finish ends the open one: %lld", t.total.contacts);
}

static void test_protocol_a_and_single(void)
{
    mt_tracker t;
    mt_init(&t, &flat, MT_MODE_A, 1);
    abs_ev(&t, 0, ABS_MT_POSITION_X, 1);
    abs_ev(&t, 0, ABS_MT_POSITION_Y, 1);
    mt_feed(&t, 0, EV_SYN, SYN_MT_REPORT, 0);
    abs_ev(&t, 0, ABS_MT_POSITION_X, 100);
    abs_ev(&t, 0, ABS_MT_POSITION_Y, 100);
    mt_feed(&t, 0, EV_SYN, SYN_MT_REPORT, 0);
    syn(&t, 0);
    mt_feed(&t, 10000, EV_SYN, SYN_MT_REPORT, 0); /* empty frame: no fingers */
    syn(&t, 10000);
    CHECK(t.total.max_active == 2 && t.total.frames == 2, "protocol A: %d fingers %lld frames", t.total.max_active,
          t.total.frames);

    mt_init(&t, &flat, MT_MODE_SINGLE, 1);
    mt_feed(&t, 0, EV_KEY, BTN_TOUCH, 1);
    abs_ev(&t, 0, ABS_X, 50);
    abs_ev(&t, 0, ABS_Y, 60);
    syn(&t, 0);
    mt_feed(&t, 80000, EV_KEY, BTN_TOUCH, 0);
    syn(&t, 80000);
    CHECK(t.total.contacts == 1 && t.total.max_active == 1 && t.total.ghosts == 0, "single: %lld %d",
          t.total.contacts, t.total.max_active);
}

static void test_dropped(void)
{
    mt_tracker t;
    mt_init(&t, &flat, MT_MODE_B, 10);
    down(&t, 0, 0, 1, 10, 10);
    down(&t, 0, 1, 2, 300, 10);
    syn(&t, 0);
    mt_feed(&t, 40000, EV_SYN, SYN_DROPPED, 0);
    abs_ev(&t, 40000, ABS_MT_POSITION_X, 999); /* ignored until the next SYN_REPORT */
    syn(&t, 40000);
    CHECK(t.total.dropped == 1 && t.total.contacts == 2 && mt_active(&t) == 0, "dropped: %lld %lld %d",
          t.total.dropped, t.total.contacts, mt_active(&t));
}

int main(void)
{
    test_tap_and_ghost();
    test_swap_distances();
    test_travel();
    test_slot_reuse_keeps_position();
    test_tid_change_and_major();
    test_protocol_a_and_single();
    test_dropped();
    printf("test_mt_track: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
