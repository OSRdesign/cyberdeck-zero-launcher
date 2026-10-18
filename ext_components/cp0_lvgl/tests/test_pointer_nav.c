/*
 * Unit test of the pointer navigation core (src/cp0_pointer_nav.c) with synthetic traces shaped like the HackBerry
 * pad measured on 2026-10-18 (62 reports / s, 16 ms apart; rest noise +-1..2 counts; a slow push 170-205 counts in
 * 0.1-0.2 s peaking at 37-55; a hard push about 420 counts in 1.1 s peaking at 97; a tap 0.18 s; a hold 1.5-2 s with
 * 44 counts of drift in 4 s):
 *   dead zone, axis choice and its hysteresis, the step rate against time and level, the rate cap (also across
 *   pushes), release stops, tap / hold / drag / the click guard, the wheel, reset, the timer API, the environment
 *   overrides and the process-wide switch.
 */
#include "cp0_pointer_nav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

#define REPORT_MS 16u

typedef struct {
    uint64_t t;
    cp0_ptr_key_t key;
} event_t;

static cp0_ptr_nav_t S;
static event_t E[8192];
static int N;

static void record(const cp0_ptr_key_t *keys, int n)
{
    for (int i = 0; i < n; i++) {
        CHECK(N < (int)(sizeof(E) / sizeof(E[0])));
        E[N].t = S.now;
        E[N].key = keys[i];
        N++;
    }
}

/* What the reader does: the timers are served at their own time (poll timeout = next_ms), then the report. */
static void advance(uint32_t dt, const cp0_ptr_report_t *report)
{
    cp0_ptr_key_t keys[CP0_PTR_NAV_MAX_KEYS];
    uint32_t left = dt;
    for (;;) {
        const int next = cp0_ptr_nav_next_ms(&S);
        if (next < 0 || (uint32_t)next >= left) break;
        record(keys, cp0_ptr_nav_step(&S, (uint32_t)next, NULL, keys, CP0_PTR_NAV_MAX_KEYS));
        left -= (uint32_t)next;
        if (next == 0) { /* due now: served; make sure we progress */
            const int again = cp0_ptr_nav_next_ms(&S);
            CHECK(again != 0);
        }
    }
    record(keys, cp0_ptr_nav_step(&S, left, report, keys, CP0_PTR_NAV_MAX_KEYS));
}

static void report(int dx, int dy)
{
    const cp0_ptr_report_t r = {dx, dy, 0, CP0_PTR_BUTTON_SAME};
    advance(REPORT_MS, &r);
}

static void report_full(uint32_t dt, int dx, int dy, int wheel, cp0_ptr_button_t button)
{
    const cp0_ptr_report_t r = {dx, dy, wheel, button};
    advance(dt, &r);
}

static void idle(uint32_t ms)
{
    advance(ms, NULL);
}

static void start(const cp0_ptr_nav_params_t *p)
{
    cp0_ptr_nav_init(&S, p);
    N = 0;
    idle(1000); /* the clock does not start at 0: nothing assumes it */
}

static int count(cp0_ptr_key_t key)
{
    int n = 0;
    for (int i = 0; i < N; i++) n += E[i].key == key;
    return n;
}

static int count_from(int first, cp0_ptr_key_t key)
{
    int n = 0;
    for (int i = first; i < N; i++) n += E[i].key == key;
    return n;
}

/* smallest time between two consecutive events of a kind (UINT64_MAX with fewer than two) */
static uint64_t min_gap(cp0_ptr_key_t a, cp0_ptr_key_t b)
{
    uint64_t best = UINT64_MAX, last = 0;
    int have = 0;
    for (int i = 0; i < N; i++) {
        if (E[i].key != a && E[i].key != b) continue;
        if (have && E[i].t - last < best) best = E[i].t - last;
        last = E[i].t;
        have = 1;
    }
    return best;
}

static uint64_t time_of(int nth, cp0_ptr_key_t key)
{
    for (int i = 0; i < N; i++)
        if (E[i].key == key && nth-- == 0) return E[i].t;
    return UINT64_MAX;
}

/* deterministic small pseudo-random sequence */
static unsigned s_seed = 12345u;
static int noise(int amplitude)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return (int)((s_seed >> 16) % (unsigned)(2 * amplitude + 1)) - amplitude;
}

/* ------------------------------------------------------------------ dead zone, noise, drift */

static void test_rest_noise_and_drift(void)
{
    start(NULL);
    /* 4 s of rest noise +-2 on both axes, a report every 16 ms */
    for (int i = 0; i < 250; i++) report(noise(2), noise(2));
    CHECK(N == 0);
    /* 4 s of a resting finger drifting 44 counts (one count now and then, same way) mixed with noise +-1 */
    for (int i = 0; i < 250; i++) report(noise(1), (i % 6 == 0) ? 1 : noise(1));
    CHECK(N == 0);
    /* single spikes of 5 counts between rest reports: a spike counts half (smoothing), under the dead zone */
    for (int i = 0; i < 50; i++) {
        report(0, 5);
        report(0, 0);
        report(0, 1);
        idle(100);
    }
    CHECK(N == 0);
    CHECK(cp0_ptr_nav_next_ms(&S) == -1); /* nothing pending: the reader sleeps */
}

static void test_dead_zone_edge(void)
{
    /* a steady push of exactly the dead zone counts (the smoothed level reaches it), one count less never */
    start(NULL);
    for (int i = 0; i < 60; i++) report(0, CP0_PTR_NAV_DEAD_ZONE - 1);
    CHECK(N == 0);
    idle(200);
    for (int i = 0; i < 60; i++) report(0, CP0_PTR_NAV_DEAD_ZONE);
    CHECK(count(CP0_PTR_KEY_DOWN) >= 2);
    CHECK(count(CP0_PTR_KEY_DOWN) == N);
}

/* ------------------------------------------------------------------ measured pushes */

static void test_slow_push_is_one_step(void)
{
    /* slow push up: 190 counts in 9 reports (144 ms), peak 45 */
    static const int trace[] = {-8, -20, -35, -45, -38, -25, -12, -5, -2};
    start(NULL);
    int total = 0;
    for (size_t i = 0; i < sizeof(trace) / sizeof(trace[0]); i++) {
        report(noise(1), trace[i]);
        total += trace[i];
    }
    CHECK(total == -190);
    for (int i = 0; i < 30; i++) report(noise(1), noise(1)); /* the finger rests again */
    CHECK(N == 1);
    CHECK(E[0].key == CP0_PTR_KEY_UP);

    /* the same short push as 12 reports (0.19 s, 205 counts, peak 55), sideways: one Right */
    static const int trace2[] = {6, 14, 28, 44, 55, 30, 12, 7, 4, 3, 1, 1};
    N = 0;
    for (size_t i = 0; i < sizeof(trace2) / sizeof(trace2[0]); i++) report(trace2[i], noise(2));
    idle(300);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_RIGHT);
}

static void test_hard_push_repeats(void)
{
    /* hard push down for 1.1 s (69 reports), 420 counts, peak 97: a short burst, then a steady push of about 4 (the
     * measured total leaves no more than that for the rest of the push) */
    start(NULL);
    int total = 0;
    static const int burst[] = {20, 97, 40};
    for (size_t i = 0; i < sizeof(burst) / sizeof(burst[0]); i++) {
        report(noise(1), burst[i]);
        total += burst[i];
    }
    for (int i = 0; i < 66; i++) {
        const int v = 3 + (i % 3); /* 3, 4, 5 */
        report(noise(1), v);
        total += v;
    }
    CHECK(total >= 400 && total <= 440);
    const uint64_t first = time_of(0, CP0_PTR_KEY_DOWN);
    idle(500);
    const int steps = count(CP0_PTR_KEY_DOWN);
    CHECK(steps == N); /* only Down */
    CHECK(steps >= 3 && steps <= 7);
    /* the second step waits for the repeat delay */
    CHECK(time_of(1, CP0_PTR_KEY_DOWN) - first >= CP0_PTR_NAV_REPEAT_DELAY_MS);
    CHECK(min_gap(CP0_PTR_KEY_DOWN, CP0_PTR_KEY_DOWN) >= 80);
    /* nothing after the release (last report + RELEASE_MS) */
    const uint64_t release_at = first + 68 * REPORT_MS + CP0_PTR_NAV_RELEASE_MS;
    CHECK(E[N - 1].t <= release_at);
}

/* ------------------------------------------------------------------ rate against level and time */

static int steps_for_level(int level, int ms)
{
    start(NULL);
    for (int t = 0; t < ms; t += (int)REPORT_MS) report(0, level);
    return count(CP0_PTR_KEY_DOWN);
}

static void test_rate_follows_level(void)
{
    /* 3 s of a steady push: just above the dead zone about SLOW_RATE (3 / s), at FULL_LEVEL MAX_RATE (12.5 / s),
     * far above it still MAX_RATE */
    const int slow = steps_for_level(CP0_PTR_NAV_DEAD_ZONE + 1, 3000);
    const int mid = steps_for_level((CP0_PTR_NAV_DEAD_ZONE + CP0_PTR_NAV_FULL_LEVEL) / 2, 3000);
    const int full = steps_for_level(CP0_PTR_NAV_FULL_LEVEL, 3000);
    const int over = steps_for_level(400, 3000);
    /* slow: 1 + 1 + (3000 - 250 - 333) / 333 .. about 9-10 */
    CHECK(slow >= 8 && slow <= 11);
    CHECK(mid > slow && mid < full);
    /* full: 1 + 1 + (3000 - 250) / 80 = 35 at most */
    CHECK(full >= 33 && full <= 36);
    CHECK(over == full);
    CHECK(min_gap(CP0_PTR_KEY_DOWN, CP0_PTR_KEY_DOWN) >= 80); /* the cap, last run */
}

static void test_first_step_at_once_then_delay(void)
{
    start(NULL);
    report(0, 80); /* one strong report: the first step comes with it */
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_DOWN);
    const uint64_t first = E[0].t;
    for (int i = 0; i < 30; i++) report(0, 80);
    CHECK(time_of(1, CP0_PTR_KEY_DOWN) - first == CP0_PTR_NAV_REPEAT_DELAY_MS);
    CHECK(time_of(2, CP0_PTR_KEY_DOWN) - time_of(1, CP0_PTR_KEY_DOWN) == 80);
}

static void test_release_stops(void)
{
    start(NULL);
    for (int i = 0; i < 40; i++) report(0, -70); /* 640 ms of a strong push up */
    const int during = count(CP0_PTR_KEY_UP);
    CHECK(during >= 5);
    const uint64_t last_report = S.now;
    idle(2000); /* the pad stops reporting */
    CHECK(E[N - 1].t <= last_report + CP0_PTR_NAV_RELEASE_MS);
    CHECK(cp0_ptr_nav_next_ms(&S) == -1);

    /* the same push, then rest noise instead of silence: stops as soon as the level falls */
    start(NULL);
    for (int i = 0; i < 40; i++) report(0, -70);
    const int before = N;
    const uint64_t stop = S.now;
    for (int i = 0; i < 100; i++) report(noise(1), noise(1));
    CHECK(N - before <= 1); /* at most a step already due */
    if (N > before) CHECK(E[N - 1].t <= stop + 3 * REPORT_MS);
}

static void test_rate_cap_across_pushes(void)
{
    /* quick flicks (two strong reports, then silence past the release) every 60 ms: each flick may start a push,
     * but steps never come closer than MAX_RATE allows */
    start(NULL);
    for (int i = 0; i < 40; i++) {
        report(0, 60);
        report(0, 60);
        idle(60);
    }
    CHECK(count(CP0_PTR_KEY_DOWN) >= 20);
    CHECK(min_gap(CP0_PTR_KEY_DOWN, CP0_PTR_KEY_DOWN) >= 80);
}

/* ------------------------------------------------------------------ axis */

static void test_axis_choice(void)
{
    /* diagonal: one axis only (X when equal), never both */
    start(NULL);
    for (int i = 0; i < 40; i++) report(20, 20);
    CHECK(count(CP0_PTR_KEY_RIGHT) == N && N >= 3);

    /* mostly sideways with a strong vertical component: X keeps it below twice */
    start(NULL);
    for (int i = 0; i < 20; i++) report(30, 10);
    for (int i = 0; i < 40; i++) report(30, 25);
    CHECK(count(CP0_PTR_KEY_RIGHT) == N);

    /* the other axis clearly stronger: it takes over at once (a step right away) */
    const int before = N;
    for (int i = 0; i < 40; i++) report(5, 40);
    CHECK(count_from(before, CP0_PTR_KEY_DOWN) >= 3);
    CHECK(count_from(before, CP0_PTR_KEY_RIGHT) <= 1);

    /* vertical noise of +-10 on a sideways push never flips the axis */
    start(NULL);
    for (int i = 0; i < 120; i++) report(-25, noise(10));
    CHECK(count(CP0_PTR_KEY_LEFT) == N && N >= 5);

    /* a reversal: the level passes through the dead zone (released), then the other way */
    start(NULL);
    for (int i = 0; i < 20; i++) report(0, 40);
    for (int i = 0; i < 20; i++) report(0, -40);
    CHECK(count(CP0_PTR_KEY_DOWN) >= 1 && count(CP0_PTR_KEY_UP) >= 1);
    CHECK(count(CP0_PTR_KEY_DOWN) + count(CP0_PTR_KEY_UP) == N);
}

/* ------------------------------------------------------------------ button */

static void test_tap_is_enter(void)
{
    /* a 0.18 s tap with the finger resting (noise +-1) */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    for (int i = 0; i < 11; i++) report(noise(1), noise(1));
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_ENTER);
    idle(1000);
    CHECK(N == 1);

    /* the press and the release in one report each with no motion report between (a USB mouse): Enter */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    report_full(120, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_ENTER);
}

static void test_hold_is_options(void)
{
    /* a 1.5 s hold drifting the measured way (44 counts in 4 s: about 17 in 1.5 s, a sparse report every 32 ms)
     * with sideways jitter of +-2 that goes back and forth */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    const uint64_t pressed = S.now;
    for (int i = 0; i < 47; i++) report_full(32, (i % 2) ? 2 : -2, (i % 3 == 0) ? 1 : 0, 0, CP0_PTR_BUTTON_SAME);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_OPTIONS);
    CHECK(E[0].t == pressed + CP0_PTR_NAV_HOLD_MS); /* while still held, not at the release */

    /* a 4 s hold with all 44 counts of drift: the drift ends up past the slop, but long after the options key, and
     * at about one count a report it is far under the dead zone: no arrow */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    for (int i = 0; i < 125; i++) report_full(32, (i % 2) ? 1 : -1, (i % 3 == 0) ? 1 : 0, 0, CP0_PTR_BUTTON_SAME);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_OPTIONS);

    /* held without any report at all (a mouse): the hold still comes, from the timer */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    CHECK(cp0_ptr_nav_next_ms(&S) == CP0_PTR_NAV_HOLD_MS);
    idle(2000);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_OPTIONS);

    /* released just before the hold time: a tap */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    report_full(CP0_PTR_NAV_HOLD_MS - 1, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_ENTER);
}

static void test_motion_while_held(void)
{
    /* the finger rests on the pad while the button is down: small motion moves nothing */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    for (int i = 0; i < 4; i++) report(0, 4); /* 16 counts, inside the tap slop, above the dead zone */
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_ENTER);

    /* a drag: more than the slop while down = no click, no options, and the motion navigates */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    for (int i = 0; i < 60; i++) report(0, 30); /* about 1 s of a push */
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(count(CP0_PTR_KEY_ENTER) == 0 && count(CP0_PTR_KEY_OPTIONS) == 0);
    CHECK(count(CP0_PTR_KEY_DOWN) >= 3);

    /* a drag after the hold fired: the options key stays the only key of the hold, then motion navigates */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    idle(700);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_OPTIONS);
    for (int i = 0; i < 30; i++) report(30, 0);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(count(CP0_PTR_KEY_OPTIONS) == 1 && count(CP0_PTR_KEY_ENTER) == 0);
    CHECK(count(CP0_PTR_KEY_RIGHT) >= 1);
}

static void test_no_click_while_pushing(void)
{
    /* the pad pressed in the middle of a push while the finger keeps going: a drag, no click, no options (the
     * motion keeps navigating) */
    start(NULL);
    for (int i = 0; i < 20; i++) report(0, 40);
    report_full(REPORT_MS, 0, 30, 0, CP0_PTR_BUTTON_DOWN);
    for (int i = 0; i < 20; i++) report(0, 40);
    for (int i = 0; i < 5; i++) report(0, 2);
    idle(900);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(count(CP0_PTR_KEY_ENTER) == 0 && count(CP0_PTR_KEY_OPTIONS) == 0);
    CHECK(count(CP0_PTR_KEY_DOWN) == N);

    /* pressed at the end of a push, the finger stopping on it: guarded (no click, no options) and the steps stop */
    start(NULL);
    for (int i = 0; i < 20; i++) report(0, 40);
    const int steps = N;
    report_full(REPORT_MS, 0, 6, 0, CP0_PTR_BUTTON_DOWN);
    for (int i = 0; i < 5; i++) report(0, 2);
    idle(900);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(N == steps);

    /* pressed 100 ms after the push stopped: still guarded */
    start(NULL);
    for (int i = 0; i < 10; i++) report(0, 40);
    idle(100 - CP0_PTR_NAV_RELEASE_MS / 2);
    report_full(CP0_PTR_NAV_RELEASE_MS / 2, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    report_full(150, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(count(CP0_PTR_KEY_ENTER) == 0);

    /* pressed 300 ms after it: a click */
    start(NULL);
    for (int i = 0; i < 10; i++) report(0, 40);
    idle(300);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    report_full(150, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(count(CP0_PTR_KEY_ENTER) == 1);
}

/* ------------------------------------------------------------------ wheel */

static void test_wheel(void)
{
    start(NULL);
    report_full(REPORT_MS, 0, 0, 1, CP0_PTR_BUTTON_SAME);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_UP);
    report_full(REPORT_MS, 0, 0, -3, CP0_PTR_BUTTON_SAME);
    CHECK(N == 4 && count(CP0_PTR_KEY_DOWN) == 3);
    /* a glitch of 50 notches: capped */
    report_full(REPORT_MS, 0, 0, 50, CP0_PTR_BUTTON_SAME);
    CHECK(count(CP0_PTR_KEY_UP) == 1 + CP0_PTR_NAV_WHEEL_MAX);
    /* notches come at once, back to back, with no rate cap and no repeat */
    idle(1000);
    CHECK(N == 4 + CP0_PTR_NAV_WHEEL_MAX);
    CHECK(cp0_ptr_nav_next_ms(&S) == -1);
    /* the wheel while the button is held (a click on the wheel's own button is not BTN_LEFT): still Up / Down */
    start(NULL);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    report_full(REPORT_MS, 0, 0, -1, CP0_PTR_BUTTON_SAME);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_DOWN);
}

/* ------------------------------------------------------------------ reset, timers, parameters */

static void test_reset_and_timers(void)
{
    start(NULL);
    CHECK(cp0_ptr_nav_next_ms(&S) == -1);
    CHECK(!cp0_ptr_nav_busy(&S));
    report(0, 50);
    CHECK(N == 1);
    CHECK(cp0_ptr_nav_busy(&S));
    /* release (50 ms) comes before the repeat (250 ms) */
    CHECK(cp0_ptr_nav_next_ms(&S) == CP0_PTR_NAV_RELEASE_MS);
    cp0_ptr_nav_reset(&S);
    CHECK(cp0_ptr_nav_next_ms(&S) == -1);
    CHECK(!cp0_ptr_nav_busy(&S));

    /* a press forgotten by a reset (pause, setting off) sends nothing when it is released */
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    cp0_ptr_nav_reset(&S);
    idle(1000);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(N == 1);

    /* a step returns at most max keys, and NULL output only counts */
    cp0_ptr_nav_init(&S, NULL);
    const cp0_ptr_report_t wheel = {0, 0, 5, CP0_PTR_BUTTON_SAME};
    cp0_ptr_key_t two[2];
    CHECK(cp0_ptr_nav_step(&S, 16, &wheel, two, 2) == 2);
    CHECK(two[0] == CP0_PTR_KEY_UP && two[1] == CP0_PTR_KEY_UP);
    CHECK(cp0_ptr_nav_step(&S, 16, &wheel, NULL, 0) == 0);
    CHECK(cp0_ptr_nav_step(NULL, 16, &wheel, two, 2) == 0);
    CHECK(cp0_ptr_nav_next_ms(NULL) == -1);

    CHECK(strcmp(cp0_ptr_key_name(CP0_PTR_KEY_OPTIONS), "OPTIONS") == 0);
    CHECK(strcmp(cp0_ptr_key_name(CP0_PTR_KEY_PAGE_DOWN), "PAGE_DOWN") == 0);
    CHECK(strcmp(cp0_ptr_key_name((cp0_ptr_key_t)99), "NONE") == 0);
}

static const char *fake_env(const char *name)
{
    if (strcmp(name, "APPLAUNCH_POINTER_DEADZONE") == 0) return "5";
    if (strcmp(name, "APPLAUNCH_POINTER_MAX_RATE") == 0) return "8.5";
    if (strcmp(name, "APPLAUNCH_POINTER_SLOW_RATE") == 0) return "2";
    if (strcmp(name, "APPLAUNCH_POINTER_HOLD_MS") == 0) return "800";
    if (strcmp(name, "APPLAUNCH_POINTER_TAP_SLOP") == 0) return "12x"; /* malformed: ignored */
    if (strcmp(name, "APPLAUNCH_POINTER_REPEAT_DELAY_MS") == 0) return "";
    if (strcmp(name, "APPLAUNCH_POINTER_CLICK_GUARD_MS") == 0) return "0";
    return NULL;
}

static const char *bad_env(const char *name)
{
    if (strcmp(name, "APPLAUNCH_POINTER_SLOW_RATE") == 0) return "20";  /* above the max: clamped to it */
    if (strcmp(name, "APPLAUNCH_POINTER_FULL_LEVEL") == 0) return "1";  /* under the dead zone: dead zone + 1 */
    if (strcmp(name, "APPLAUNCH_POINTER_MAX_RATE") == 0) return "3,5";  /* a comma: ignored */
    if (strcmp(name, "APPLAUNCH_POINTER_SMOOTH_PCT") == 0) return "0";  /* clamped to 1 */
    return NULL;
}

static void test_params(void)
{
    cp0_ptr_nav_params_t p;
    cp0_ptr_nav_params_default(&p);
    CHECK(p.dead_zone == CP0_PTR_NAV_DEAD_ZONE && p.max_rate_x10 == CP0_PTR_NAV_MAX_RATE_X10);
    CHECK(cp0_ptr_nav_params_apply_env(&p, fake_env) == 5);
    CHECK(p.dead_zone == 5);
    CHECK(p.max_rate_x10 == 85);
    CHECK(p.slow_rate_x10 == 20);
    CHECK(p.hold_ms == 800);
    CHECK(p.tap_slop == CP0_PTR_NAV_TAP_SLOP);
    CHECK(p.repeat_delay_ms == CP0_PTR_NAV_REPEAT_DELAY_MS);
    CHECK(p.click_guard_ms == 0);

    cp0_ptr_nav_params_default(&p);
    CHECK(cp0_ptr_nav_params_apply_env(&p, bad_env) == 3);
    CHECK(p.slow_rate_x10 == p.max_rate_x10 && p.max_rate_x10 == CP0_PTR_NAV_MAX_RATE_X10);
    CHECK(p.full_level == p.dead_zone + 1);
    CHECK(p.smooth_pct == 1);
    CHECK(cp0_ptr_nav_params_apply_env(NULL, bad_env) == 0);

    /* the overrides act: a hold of 800 ms, a dead zone of 5 */
    cp0_ptr_nav_params_default(&p);
    (void)cp0_ptr_nav_params_apply_env(&p, fake_env);
    start(&p);
    for (int i = 0; i < 60; i++) report(0, 4);
    CHECK(N == 0);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    const uint64_t pressed = S.now;
    idle(1000);
    CHECK(N == 1 && E[0].key == CP0_PTR_KEY_OPTIONS && E[0].t == pressed + 800);
    /* the click guard is off: a press right after a push clicks */
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_UP);
    for (int i = 0; i < 5; i++) report(0, 40);
    report_full(REPORT_MS, 0, 0, 0, CP0_PTR_BUTTON_DOWN);
    report_full(100, 0, 0, 0, CP0_PTR_BUTTON_UP);
    CHECK(count(CP0_PTR_KEY_ENTER) == 1);

    /* a hand-made block is made consistent by init */
    memset(&p, 0, sizeof(p));
    cp0_ptr_nav_init(&S, &p);
    CHECK(S.p.dead_zone == 1 && S.p.full_level == 2 && S.p.max_rate_x10 == 1 && S.p.slow_rate_x10 == 1);
    CHECK(S.p.hold_ms == 50 && S.p.wheel_max == 1 && S.p.axis_switch_pct == 100);
}

/* ------------------------------------------------------------------ process-wide switch */

static int s_wakes;
static void wake_hook(void) { s_wakes++; }

static void test_switch(void)
{
    CHECK(cp0_pointer_nav_get_override() == -1); /* never set: follow input.state */
    cp0_pointer_nav_set_wake_hook(wake_hook);
    cp0_pointer_nav_set_enabled(1);
    CHECK(cp0_pointer_nav_get_override() == 1 && s_wakes == 1);
    cp0_pointer_nav_set_enabled(7); /* any non-zero is on: no change, no wake */
    CHECK(cp0_pointer_nav_get_override() == 1 && s_wakes == 1);
    cp0_pointer_nav_set_enabled(0);
    CHECK(cp0_pointer_nav_get_override() == 0 && s_wakes == 2);
    cp0_pointer_nav_set_wake_hook(NULL);
    cp0_pointer_nav_set_enabled(1);
    CHECK(cp0_pointer_nav_get_override() == 1 && s_wakes == 2);
}

int main(void)
{
    test_rest_noise_and_drift();
    test_dead_zone_edge();
    test_slow_push_is_one_step();
    test_hard_push_repeats();
    test_rate_follows_level();
    test_first_step_at_once_then_delay();
    test_release_stops();
    test_rate_cap_across_pushes();
    test_axis_choice();
    test_tap_is_enter();
    test_hold_is_options();
    test_motion_while_held();
    test_no_click_while_pushing();
    test_wheel();
    test_reset_and_timers();
    test_params();
    test_switch();
    printf("test_pointer_nav: all passed\n");
    return 0;
}
