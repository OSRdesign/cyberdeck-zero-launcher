/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-c: G1, the three-finger hold (cp0_gesture_g1.h), table-driven with the event sequences of study 026
 * section 5.3 (deck pixels, 640x480 at 11.3 px/mm, hold 1000 ms per decision D2: the positive case fires at 1090 ms,
 * not the study's 790). Every case is played through the slot tracker (cp0_touch_slots.h) in both coordinate
 * orientations the display manager has: "buffer" (raw = screen) and the deck's legacy one (raw X along the display
 * height, swapped and mirrored, 640 x 480 raw units normalised as cp0_lvgl_dpi_scaled.c does), with a tick every
 * 10 ms between frames as the launcher's touch reader gives. Then G2 (cp0_gesture_g2.h), the parameters, the log text.
 */
#include "cp0_gesture_g1.h"
#include "cp0_gesture_g2.h"
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

enum { W = 640, H = 480, PPMM = 1130 };

/* one row of a sequence: at t ms, slot gets contact id at (x, y) (screen px), or lifts (id -1); major -1 = none */
typedef struct {
    int t, slot, id, x, y, major;
} step_t;

#define END {-1, 0, 0, 0, 0, 0}

/* ---------------------------------------------------------------- orientations */

enum { ORIENT_BUFFER, ORIENT_LEGACY };
static const char *orient_name[] = {"buffer", "legacy swap+invert"};

/* screen -> raw for the deck's legacy setup: raw X 0..639 runs down the display, raw Y 0..479 runs right to left */
static void to_raw(int orient, int x, int y, int *rx, int *ry)
{
    if (orient == ORIENT_BUFFER) {
        *rx = x;
        *ry = y;
        return;
    }
    *rx = (int)(y * 639.0 / (H - 1) + 0.5);
    *ry = (int)((W - 1 - x) * 479.0 / (W - 1) + 0.5);
}

/* raw -> screen exactly as the display manager's legacy branch (normalise, swap, invert X, scale, truncate) */
static void to_screen(int orient, int rx, int ry, int32_t *x, int32_t *y)
{
    if (orient == ORIENT_BUFFER) {
        *x = rx;
        *y = ry;
        return;
    }
    double nx = rx / 639.0, ny = ry / 479.0;
    const double t = nx;
    nx = ny;
    ny = t;
    nx = 1.0 - nx;
    int px = (int)(nx * (W - 1)), py = (int)(ny * (H - 1));
    if (px < 0) px = 0;
    if (px >= W) px = W - 1;
    if (py < 0) py = 0;
    if (py >= H) py = H - 1;
    *x = px;
    *y = py;
}

/* ---------------------------------------------------------------- playing a sequence */

typedef struct {
    int fired, fire_t, cancelled;
    cp0_g1_reason_t reason;
    int value, contacts, held_ms;
    char text[160];
} result_t;

static void take(result_t *r, const cp0_g1_t *g, const cp0_g1_event_t *e, int t_ms)
{
    if (e->outcome == CP0_G1_FIRED) {
        r->fired++;
        if (r->fired == 1) {
            r->fire_t = t_ms;
            r->held_ms = e->held_ms;
            r->contacts = e->contacts;
        }
    } else if (e->outcome == CP0_G1_CANCELLED) {
        r->cancelled++;
        r->reason = e->reason;
        r->value = e->value;
        r->contacts = e->contacts;
    } else {
        return;
    }
    cp0_g1_describe(g, e, r->text, sizeof(r->text));
}

static result_t play(const step_t *steps, int orient, const cp0_g1_params_t *p)
{
    result_t r;
    memset(&r, 0, sizeof(r));
    cp0_g1_t g;
    cp0_g1_init(&g, p, W, H, PPMM);
    cp0_touch_slots_t t;
    cp0_touch_slots_init(&t, 1, 0);
    int now = 0;
    for (int i = 0; steps[i].t >= 0;) {
        const int ft = steps[i].t;
        for (; now < ft; now += 10) { /* the reader's ticks between frames */
            const cp0_g1_event_t e = cp0_g1_tick(&g, (int64_t)now * 1000);
            take(&r, &g, &e, now);
        }
        for (; steps[i].t == ft; i++) {
            const step_t *s = &steps[i];
            cp0_touch_slots_event(&t, EV_ABS, ABS_MT_SLOT, s->slot);
            cp0_touch_slots_event(&t, EV_ABS, ABS_MT_TRACKING_ID, s->id);
            if (s->id >= 0) {
                int rx, ry;
                to_raw(orient, s->x, s->y, &rx, &ry);
                cp0_touch_slots_event(&t, EV_ABS, ABS_MT_POSITION_X, rx);
                cp0_touch_slots_event(&t, EV_ABS, ABS_MT_POSITION_Y, ry);
                if (s->major >= 0) cp0_touch_slots_event(&t, EV_ABS, ABS_MT_TOUCH_MAJOR, s->major);
            }
        }
        cp0_touch_slots_event(&t, EV_SYN, SYN_REPORT, 0);
        cp0_touch_frame_t f;
        cp0_touch_slots_frame(&t, (int64_t)ft * 1000, &f);
        for (int k = 0; k < f.n; k++) to_screen(orient, f.c[k].x, f.c[k].y, &f.c[k].x, &f.c[k].y);
        const cp0_g1_event_t e = cp0_g1_frame(&g, &f);
        take(&r, &g, &e, ft);
        now = ft;
    }
    for (int k = 0; k < 300; k++, now += 10) {
        const cp0_g1_event_t e = cp0_g1_tick(&g, (int64_t)now * 1000);
        take(&r, &g, &e, now);
    }
    return r;
}

/* ---------------------------------------------------------------- the cases (study 5.3) */

/* the positive case: three contacts at t 0, 40, 90 (separations 101 / 100 / 200 px >= 90), jitter <= 10 px */
static const step_t positive[] = {
    {0, 0, 10, 200, 240, -1}, {40, 1, 11, 300, 250, -1}, {90, 2, 12, 400, 245, -1},
    {200, 0, 10, 205, 243, -1}, {300, 1, 11, 296, 254, -1}, {500, 2, 12, 408, 240, -1},
    {700, 0, 10, 198, 236, -1}, {900, 1, 11, 303, 247, -1}, {1050, 2, 12, 402, 250, -1},
    {1100, 0, 10, 201, 241, -1}, {1500, 1, 11, 302, 251, -1},
    {1700, 0, -1, 0, 0, -1}, {1700, 1, -1, 0, 0, -1}, {1700, 2, -1, 0, 0, -1},
    END};

/* the positive case, held 3 s with no events after t 90 at all (a still hand on a quiet panel): the tick fires */
static const step_t positive_quiet[] = {
    {0, 0, 10, 200, 240, -1}, {40, 1, 11, 300, 250, -1}, {90, 2, 12, 400, 245, -1},
    {3000, 0, -1, 0, 0, -1}, {3000, 1, -1, 0, 0, -1}, {3000, 2, -1, 0, 0, -1},
    END};

static const step_t third_late[] = { /* T_LAND */
    {0, 0, 10, 200, 240, -1}, {40, 1, 11, 300, 250, -1}, {300, 2, 12, 400, 245, -1},
    {1600, 0, -1, 0, 0, -1}, {1600, 1, -1, 0, 0, -1}, {1600, 2, -1, 0, 0, -1},
    END};

static const step_t moves[] = { /* D_STILL: slot 1 moves 50 px (4.4 mm) at t 400 */
    {0, 0, 10, 200, 240, -1}, {40, 1, 11, 300, 250, -1}, {90, 2, 12, 400, 245, -1},
    {400, 1, 11, 300, 300, -1},
    {1600, 0, -1, 0, 0, -1}, {1600, 1, -1, 0, 0, -1}, {1600, 2, -1, 0, 0, -1},
    END};

static const step_t fourth[] = { /* a 4th contact at t 500 */
    {0, 0, 10, 200, 240, -1}, {40, 1, 11, 300, 250, -1}, {90, 2, 12, 400, 245, -1},
    {500, 3, 13, 500, 300, -1},
    {1600, 0, -1, 0, 0, -1}, {1600, 1, -1, 0, 0, -1}, {1600, 2, -1, 0, 0, -1}, {1600, 3, -1, 0, 0, -1},
    END};

static const step_t at_edge[] = { /* E_EDGE: a contact lands at x 10 (0.9 mm) */
    {0, 0, 10, 10, 240, -1}, {40, 1, 11, 300, 250, -1}, {90, 2, 12, 400, 245, -1},
    {1600, 0, -1, 0, 0, -1}, {1600, 1, -1, 0, 0, -1}, {1600, 2, -1, 0, 0, -1},
    END};

static const step_t too_close[] = { /* D_SEP: two contacts 60 px (5.3 mm) apart */
    {0, 0, 10, 200, 240, -1}, {40, 1, 11, 260, 240, -1}, {90, 2, 12, 400, 245, -1},
    {1600, 0, -1, 0, 0, -1}, {1600, 1, -1, 0, 0, -1}, {1600, 2, -1, 0, 0, -1},
    END};

static const step_t lifts[] = { /* slot 2 lifts at t 600 */
    {0, 0, 10, 200, 240, -1}, {40, 1, 11, 300, 250, -1}, {90, 2, 12, 400, 245, -1},
    {600, 2, -1, 0, 0, -1},
    {1600, 0, -1, 0, 0, -1}, {1600, 1, -1, 0, 0, -1},
    END};

/* a 2-finger pinch over 1 s (viz1090): the fingers spread 200 -> 400 px */
static step_t pinch[64];
/* alternating two-thumb taps of 80 ms on the on-screen keyboard for 3 s */
static step_t osk[128];
/* a ghost contact living 20 ms during a pinch */
static step_t ghost[64];

static const step_t palm[] = { /* 3 contacts with TOUCH_MAJOR above MAJOR_MAX (run with major_max 120) */
    {0, 0, 10, 200, 240, 200}, {30, 1, 11, 300, 250, 210}, {60, 2, 12, 400, 245, 190},
    {1600, 0, -1, 0, 0, -1}, {1600, 1, -1, 0, 0, -1}, {1600, 2, -1, 0, 0, -1},
    END};

static void build_dynamic_cases(void)
{
    int n = 0;
    pinch[n++] = (step_t){0, 0, 20, 220, 240, -1};
    pinch[n++] = (step_t){20, 1, 21, 420, 240, -1};
    for (int t = 50; t <= 1000; t += 50) {
        const int d = (t - 20) / 10; /* spreads 1 px per 10 ms each side */
        pinch[n++] = (step_t){t, 0, 20, 220 - d / 2, 240, -1};
        pinch[n++] = (step_t){t, 1, 21, 420 + d / 2, 240, -1};
    }
    pinch[n++] = (step_t){1100, 0, -1, 0, 0, -1};
    pinch[n++] = (step_t){1100, 1, -1, 0, 0, -1};
    pinch[n] = (step_t)END;

    n = 0;
    for (int t = 0, k = 0; t < 3000; t += 60, k++) { /* left thumb, right thumb, overlapping by 20 ms */
        const int slot = k % 2, id = 100 + k;
        osk[n++] = (step_t){t, slot, id, slot ? 500 : 140, 400, -1};
        osk[n++] = (step_t){t + 80, slot, -1, 0, 0, -1};
    }
    /* sort by time (the lifts interleave with the next landing) */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && osk[j].t < osk[j - 1].t; j--) {
            const step_t s = osk[j];
            osk[j] = osk[j - 1];
            osk[j - 1] = s;
        }
    osk[n] = (step_t)END;

    n = 0;
    ghost[n++] = (step_t){0, 0, 30, 220, 240, -1};
    ghost[n++] = (step_t){20, 1, 31, 420, 240, -1};
    for (int t = 50; t <= 1000; t += 50) {
        if (t == 100) ghost[n++] = (step_t){t, 2, 32, 330, 120, -1};
        if (t == 150) ghost[n++] = (step_t){120, 2, -1, 0, 0, -1};
        ghost[n++] = (step_t){t, 0, 30, 220 - (t - 20) / 20, 240, -1};
        ghost[n++] = (step_t){t, 1, 31, 420 + (t - 20) / 20, 240, -1};
    }
    ghost[n++] = (step_t){1100, 0, -1, 0, 0, -1};
    ghost[n++] = (step_t){1100, 1, -1, 0, 0, -1};
    ghost[n] = (step_t)END;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && ghost[j].t < ghost[j - 1].t; j--) {
            const step_t s = ghost[j];
            ghost[j] = ghost[j - 1];
            ghost[j - 1] = s;
        }
}

typedef struct {
    const char *name;
    const step_t *steps;
    int fires;                 /* expected FIRED count */
    int fire_t;                /* when (ms), -1 = do not check */
    cp0_g1_reason_t reason;    /* expected cancel reason, WHY_NONE = no cancel report expected */
    int major_max;
} case_t;

static void run_cases(void)
{
    build_dynamic_cases();
    const case_t cases[] = {
        {"positive", positive, 1, 1090, CP0_G1_WHY_NONE, 0},
        {"positive, no events during the hold", positive_quiet, 1, 1090, CP0_G1_WHY_NONE, 0},
        {"third contact at t 300 (T_LAND)", third_late, 0, -1, CP0_G1_WHY_LAND, 0},
        {"slot 1 moves 50 px at t 400 (D_STILL)", moves, 0, -1, CP0_G1_WHY_MOVE, 0},
        {"a 4th contact at t 500", fourth, 0, -1, CP0_G1_WHY_EXTRA, 0},
        {"a contact at x 10 (E_EDGE)", at_edge, 0, -1, CP0_G1_WHY_EDGE, 0},
        {"two contacts 60 px apart (D_SEP)", too_close, 0, -1, CP0_G1_WHY_SEP, 0},
        {"slot 2 lifts at t 600", lifts, 0, -1, CP0_G1_WHY_LIFT, 0},
        {"2-finger pinch over 1 s", pinch, 0, -1, CP0_G1_WHY_NONE, 0},
        {"two-thumb OSK taps, 80 ms, 3 s", osk, 0, -1, CP0_G1_WHY_NONE, 0},
        {"3-contact palm, TOUCH_MAJOR 190-210 > 120", palm, 0, -1, CP0_G1_WHY_MAJOR, 120},
        {"ghost contact of 20 ms during a pinch", ghost, 0, -1, (cp0_g1_reason_t)-1, 0},
    };
    for (int orient = ORIENT_BUFFER; orient <= ORIENT_LEGACY; orient++) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            const case_t *c = &cases[i];
            cp0_g1_params_t p;
            cp0_g1_params_default(&p);
            p.major_max = c->major_max;
            const result_t r = play(c->steps, orient, &p);
            int ok = r.fired == c->fires;
            if (ok && c->fire_t >= 0) ok = r.fire_t == c->fire_t;
            if (ok && (int)c->reason >= 0) {
                if (c->reason == CP0_G1_WHY_NONE) ok = r.cancelled == 0;
                else ok = r.cancelled == 1 && r.reason == c->reason;
            }
            if (ok) g_pass++;
            else g_fail++;
            printf("%s  %-44s %-19s fired %d (t %d, held %d ms), cancelled %d (%s)%s%s\n", ok ? "ok  " : "FAIL",
                   c->name, orient_name[orient], r.fired, r.fired ? r.fire_t : -1, r.held_ms, r.cancelled,
                   r.cancelled ? cp0_g1_reason_name(r.reason) : "-", r.text[0] ? ": " : "", r.text);
        }
    }
}

/* ---------------------------------------------------------------- single rules */

static cp0_touch_frame_t frame3(int t_ms, int x0, int x1, int x2, int y)
{
    cp0_touch_frame_t f;
    memset(&f, 0, sizeof(f));
    f.t_us = (int64_t)t_ms * 1000;
    const int xs[3] = {x0, x1, x2};
    for (int i = 0; i < 3; i++) {
        f.c[i].id = 50 + i;
        f.c[i].x = xs[i];
        f.c[i].y = y;
        f.c[i].major = -1;
        f.c[i].slot = i;
    }
    f.n = 3;
    return f;
}

static cp0_touch_frame_t empty(int t_ms)
{
    cp0_touch_frame_t f;
    memset(&f, 0, sizeof(f));
    f.t_us = (int64_t)t_ms * 1000;
    return f;
}

static void test_rules(void)
{
    cp0_g1_t g;
    cp0_g1_init(&g, NULL, W, H, PPMM);
    /* fires once, never again before every finger is up */
    cp0_touch_frame_t f = frame3(0, 100, 300, 500, 240);
    CHECK(cp0_g1_frame(&g, &f).outcome == CP0_G1_NONE);
    CHECK(cp0_g1_due_us(&g) == 1000000);
    CHECK(cp0_g1_tick(&g, 999999).outcome == CP0_G1_NONE);
    cp0_g1_event_t e = cp0_g1_tick(&g, 1003000);
    CHECK(e.outcome == CP0_G1_FIRED && e.held_ms == 1003 && e.contacts == 3 && e.t_due_us == 1000000);
    char text[160];
    cp0_g1_describe(&g, &e, text, sizeof(text));
    CHECK(strcmp(text, "G1 fired (3 contacts, held 1003 ms)") == 0);
    CHECK(cp0_g1_tick(&g, 5000000).outcome == CP0_G1_NONE);
    f = frame3(5000, 100, 300, 500, 240);
    CHECK(cp0_g1_frame(&g, &f).outcome == CP0_G1_NONE);
    CHECK(cp0_g1_tick(&g, 9000000).outcome == CP0_G1_NONE);
    f = empty(9100);
    CHECK(cp0_g1_frame(&g, &f).outcome == CP0_G1_NONE); /* a fired session ends without a cancel report */
    /* and again after all lifted */
    f = frame3(10000, 100, 300, 500, 240);
    cp0_g1_frame(&g, &f);
    CHECK(cp0_g1_tick(&g, 11000000).outcome == CP0_G1_FIRED);
    f = empty(11500);
    cp0_g1_frame(&g, &f);

    /* block: the hand that is down when the screen changes hands is not a gesture, and old queued frames (an empty one
     * included) do not end the block; a fresh empty frame does */
    f = frame3(20000, 100, 300, 500, 240);
    cp0_g1_frame(&g, &f);
    cp0_g1_block(&g, 20500000);
    CHECK(cp0_g1_due_us(&g) == -1);
    CHECK(cp0_g1_tick(&g, 30000000).outcome == CP0_G1_NONE);
    f = empty(20400); /* queued before the block */
    cp0_g1_frame(&g, &f);
    f = frame3(20450, 100, 300, 500, 240);
    cp0_g1_frame(&g, &f);
    CHECK(cp0_g1_tick(&g, 25000000).outcome == CP0_G1_NONE);
    f = empty(20600);
    cp0_g1_frame(&g, &f);
    f = frame3(21000, 100, 300, 500, 240);
    cp0_g1_frame(&g, &f);
    CHECK(cp0_g1_tick(&g, 22000000).outcome == CP0_G1_FIRED);
    f = empty(22100);
    cp0_g1_frame(&g, &f);

    /* lost events while armed: cancelled, reported when the last finger lifts */
    f = frame3(30000, 100, 300, 500, 240);
    cp0_g1_frame(&g, &f);
    f = frame3(30500, 100, 300, 500, 240);
    f.resync = 1;
    CHECK(cp0_g1_frame(&g, &f).outcome == CP0_G1_NONE);
    CHECK(cp0_g1_tick(&g, 32000000).outcome == CP0_G1_NONE);
    f = empty(32100);
    e = cp0_g1_frame(&g, &f);
    CHECK(e.outcome == CP0_G1_CANCELLED && e.reason == CP0_G1_WHY_DROPPED);
    /* a resync frame with fingers down and nothing tracked: blocked until they lift */
    f = frame3(40000, 100, 300, 500, 240);
    f.resync = 1;
    cp0_g1_frame(&g, &f);
    CHECK(cp0_g1_tick(&g, 45000000).outcome == CP0_G1_NONE);
    f = empty(45100);
    CHECK(cp0_g1_frame(&g, &f).outcome == CP0_G1_NONE);

    /* contacts beyond the tracker's table count as more contacts */
    f = frame3(50000, 100, 300, 500, 240);
    f.extra = 1;
    cp0_g1_frame(&g, &f);
    CHECK(cp0_g1_tick(&g, 52000000).outcome == CP0_G1_NONE);
    f = empty(52100);
    e = cp0_g1_frame(&g, &f);
    CHECK(e.outcome == CP0_G1_CANCELLED && e.reason == CP0_G1_WHY_EXTRA && e.contacts == 4);
    cp0_g1_describe(&g, &e, text, sizeof(text));
    CHECK(strcmp(text, "G1 cancelled: 4 contacts in one touch (a palm or a grip?)") == 0);

    /* the 1.0 s hold is a named default, overridable from the environment */
    cp0_g1_params_t p;
    cp0_g1_params_default(&p);
    CHECK(p.hold_ms == 1000 && CP0_G1_HOLD_MS == 1000 && p.land_ms == 250 && p.still_mm_x10 == 40 &&
          p.sep_mm_x10 == 80 && p.edge_mm_x10 == 20 && p.major_max == 0);
}

static const char *fake_env(const char *name)
{
    if (strcmp(name, "APPLAUNCH_MT_G1_HOLD_MS") == 0) return "700";
    if (strcmp(name, "APPLAUNCH_MT_G1_STILL_MM") == 0) return "3.5";
    if (strcmp(name, "APPLAUNCH_MT_G1_SEP_MM") == 0) return "6,5"; /* malformed: ignored */
    if (strcmp(name, "APPLAUNCH_MT_G1_MAJOR_MAX") == 0) return "150";
    if (strcmp(name, "APPLAUNCH_MT_G1_LAND_MS") == 0) return "999999"; /* clamped */
    if (strcmp(name, "APPLAUNCH_MT_G2_HOLD_MS") == 0) return "800";
    if (strcmp(name, "APPLAUNCH_MT_G2_STILL_MM") == 0) return "x";
    return NULL;
}

static void test_params(void)
{
    cp0_g1_params_t p;
    cp0_g1_params_default(&p);
    CHECK(cp0_g1_params_apply_env(&p, fake_env) == 4);
    CHECK(p.hold_ms == 700 && p.still_mm_x10 == 35 && p.sep_mm_x10 == 80 && p.major_max == 150 && p.land_ms == 2000);
    cp0_g2_params_t q;
    cp0_g2_params_default(&q);
    CHECK(cp0_g2_params_apply_env(&q, fake_env) == 1);
    CHECK(q.hold_ms == 800 && q.still_mm_x10 == 30);
    /* the thresholds become pixels with the density: 4 mm at 11.3 px/mm = 45.2 px, at 8.51 = 34 px (study 5.2) */
    cp0_g1_t g;
    cp0_g1_init(&g, NULL, W, H, 1130);
    CHECK((int)(g.still_px + 0.5) == 45 && (int)(g.sep_px + 0.5) == 90 && (int)(g.edge_px + 0.5) == 23);
    cp0_g1_init(&g, NULL, 800, 480, 851);
    CHECK((int)(g.still_px + 0.5) == 34 && (int)(g.sep_px + 0.5) == 68 && (int)(g.edge_px + 0.5) == 17);
}

static void test_describe(void)
{
    cp0_g1_t g;
    cp0_g1_init(&g, NULL, W, H, PPMM);
    char text[160];
    const struct {
        cp0_g1_reason_t why;
        int value, land_ms;
        const char *want;
    } rows[] = {
        {CP0_G1_WHY_LAND, 312, 312, "G1 cancelled: the third finger came 312 ms after the first (limit 250 ms), 3 contacts"},
        {CP0_G1_WHY_LAND, 260, -1, "G1 cancelled: no third finger within 250 ms of the first, 3 contacts"},
        {CP0_G1_WHY_SEP, 61, 30, "G1 cancelled: two fingers 6.1 mm apart (min 8.0 mm), 3 contacts"},
        {CP0_G1_WHY_EDGE, 9, 30, "G1 cancelled: a finger 0.9 mm from the edge (min 2.0 mm), 3 contacts"},
        {CP0_G1_WHY_LIFT, 640, 30, "G1 cancelled: a finger lifted after 640 ms (hold 1000 ms), 3 contacts"},
        {CP0_G1_WHY_MOVE, 53, 30, "G1 cancelled: a finger moved 5.3 mm (limit 4.0 mm), 3 contacts"},
        {CP0_G1_WHY_DROPPED, 0, 30, "G1 cancelled: touch events were lost (the reader fell behind), 3 contacts"},
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        cp0_g1_event_t e;
        memset(&e, 0, sizeof(e));
        e.outcome = CP0_G1_CANCELLED;
        e.reason = rows[i].why;
        e.value = rows[i].value;
        e.contacts = 3;
        e.land_ms = rows[i].land_ms;
        cp0_g1_describe(&g, &e, text, sizeof(text));
        if (strcmp(text, rows[i].want) == 0) g_pass++;
        else {
            g_fail++;
            fprintf(stderr, "FAIL describe: \"%s\", expected \"%s\"\n", text, rows[i].want);
        }
    }
}

/* ---------------------------------------------------------------- G2 */

static cp0_touch_frame_t one(int t_ms, int id, int x, int y)
{
    cp0_touch_frame_t f = empty(t_ms);
    f.n = 1;
    f.c[0].id = id;
    f.c[0].x = x;
    f.c[0].y = y;
    f.c[0].major = -1;
    return f;
}

static void test_g2(void)
{
    const cp0_g2_rect_t strip = {440, 0, 200, 56}; /* the deck's strip: top right */
    cp0_g2_t g;
    cp0_g2_init(&g, NULL, PPMM);
    /* held 500 ms on the strip: fires once */
    cp0_touch_frame_t f = one(0, 1, 560, 20);
    CHECK(cp0_g2_frame(&g, &f, &strip).fired == 0);
    CHECK(cp0_g2_due_us(&g) == 500000);
    CHECK(cp0_g2_tick(&g, 499000).fired == 0);
    cp0_g2_event_t e = cp0_g2_tick(&g, 503000);
    CHECK(e.fired == 1 && e.held_ms == 503);
    CHECK(cp0_g2_tick(&g, 900000).fired == 0);
    f = empty(1000);
    cp0_g2_frame(&g, &f, &strip);
    /* a tap is not a G2 */
    f = one(2000, 2, 560, 20);
    cp0_g2_frame(&g, &f, &strip);
    f = empty(2150);
    cp0_g2_frame(&g, &f, &strip);
    CHECK(cp0_g2_tick(&g, 3000000).fired == 0);
    /* outside the strip, or no strip on the screen: nothing */
    f = one(4000, 3, 100, 200);
    cp0_g2_frame(&g, &f, &strip);
    CHECK(cp0_g2_tick(&g, 5000000).fired == 0);
    f = empty(5100);
    cp0_g2_frame(&g, &f, &strip);
    f = one(6000, 4, 560, 20);
    cp0_g2_frame(&g, &f, NULL);
    CHECK(cp0_g2_tick(&g, 7000000).fired == 0);
    f = empty(7100);
    cp0_g2_frame(&g, &f, NULL);
    /* moved more than 3 mm (34 px): nothing */
    f = one(8000, 5, 560, 20);
    cp0_g2_frame(&g, &f, &strip);
    f = one(8200, 5, 600, 20);
    cp0_g2_frame(&g, &f, &strip);
    CHECK(cp0_g2_tick(&g, 9000000).fired == 0);
    f = empty(9100);
    cp0_g2_frame(&g, &f, &strip);
    /* a second finger: nothing */
    f = one(10000, 6, 560, 20);
    cp0_g2_frame(&g, &f, &strip);
    f = one(10100, 6, 560, 20);
    f.n = 2;
    f.c[1] = f.c[0];
    f.c[1].id = 7;
    f.c[1].x = 200;
    cp0_g2_frame(&g, &f, &strip);
    CHECK(cp0_g2_tick(&g, 11000000).fired == 0);
    f = empty(11100);
    cp0_g2_frame(&g, &f, &strip);
    /* blocked: a finger held on the strip when the launcher comes back is not a G2 */
    f = one(12000, 8, 560, 20);
    cp0_g2_frame(&g, &f, &strip);
    cp0_g2_block(&g, 12100000);
    CHECK(cp0_g2_tick(&g, 13000000).fired == 0);
    f = empty(13100);
    cp0_g2_frame(&g, &f, &strip);
    f = one(14000, 9, 560, 20);
    cp0_g2_frame(&g, &f, &strip);
    f = one(14600, 9, 562, 21); /* the hold completes at a frame too */
    CHECK(cp0_g2_frame(&g, &f, &strip).fired == 1);
}

int main(void)
{
    run_cases();
    test_rules();
    test_params();
    test_describe();
    test_g2();
    printf("test_gesture_g1: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
