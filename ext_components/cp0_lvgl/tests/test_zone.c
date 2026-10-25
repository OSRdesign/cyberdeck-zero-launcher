/*
 * Unit test of the PDA writing zone's pure core (src/cp0_zone.c, report 041): the band's geometry on the two portrait
 * panels and at the deck's density, the hit test with the strip open and closed, the touch router (a tap in the app,
 * a stroke, a stroke that leaves the box, a second finger, a three-finger touch, a swallowed touch, a drop-out healed
 * wherever it lands, the band's rows, a gesture's cancel, a reset), the strip's sticky modifiers (arm, lock, off, used
 * by the next key, the held arrows and their repeat, Alt as a real key, release_all), what a recogniser result types
 * (US keys with Shift / Caps, the characters without a key as text alone, the keys' control text, Ctrl / Shift / Alt
 * from the strip), the mode line, the Latin-1 upper case, the US key table lookup, and the whole path end to end:
 * synthetic strokes routed by frames into the real recogniser with the shipped table, polled, typed.
 */
#include "cp0_zone.h"

#include "input_keys.h"
#include "keyboard_input.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ZONE_TABLE_DIR
#define ZONE_TABLE_DIR "../../projects/APPLaunch/APPLaunch/strokes"
#endif

static int failures;

#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        const long long va_ = (long long)(a), vb_ = (long long)(b);                                        \
        if (va_ != vb_) {                                                                                  \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, \
                    va_, vb_);                                                                             \
            failures++;                                                                                    \
        }                                                                                                  \
    } while (0)

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_STR(a, b)                                                                                           \
    do {                                                                                                          \
        if (strcmp((a), (b)) != 0) {                                                                              \
            fprintf(stderr, "%s:%d: CHECK_STR failed: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, (a), \
                    (b));                                                                                         \
            failures++;                                                                                           \
        }                                                                                                         \
    } while (0)

static cp0_ui_metrics_t metrics(int w, int h, int ppmm)
{
    cp0_ui_metrics_t m;
    memset(&m, 0, sizeof(m));
    CHECK_EQ(cp0_ui_metrics_compute(w, h, 90, ppmm, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
    return m;
}

/* the reference panel: the Pi 3A+ 4.3" DSI turned, 94x56 mm (8.54 px/mm); the 3.5" 640x480 panel turned, 71x53 mm */
static int ppmm_dsi(void) { return cp0_ui_ppmm_from_mm(480, 800, 560, 940); }
static int ppmm_35(void) { return cp0_ui_ppmm_from_mm(480, 640, 530, 710); }

static cp0_zone_layout_t layout_dsi(void)
{
    const cp0_ui_metrics_t m = metrics(480, 800, ppmm_dsi());
    cp0_zone_layout_t l;
    memset(&l, 0, sizeof(l));
    CHECK_EQ(cp0_zone_layout(&m, &l), 0);
    return l;
}

static void test_layout(void)
{
    CHECK_EQ(ppmm_dsi(), 854);
    const cp0_zone_layout_t l = layout_dsi();
    /* 480x800 at 8.54 px/mm: pad 8, keys 5 mm = 43 px, the writing zone a third of the panel: 267 px (owner, after the
     * first board test of 0.8.8-pda.4; S3 first drew 20.6 mm = 176 px) */
    CHECK_EQ(l.pad, 8);
    CHECK_EQ(l.gap, 4);
    CHECK_EQ(l.key_h, 43);
    CHECK_EQ(l.row_h, 51);
    CHECK_EQ(l.write_h, 267);
    CHECK_EQ(l.band_closed, 318);
    CHECK_EQ(l.band_open, 369);
    CHECK_EQ(l.app_h, 482);
    CHECK_EQ(l.strip_y, 431);
    CHECK_EQ(l.buttons_y, 482);
    CHECK_EQ(l.write_y, 533);
    /* the boxes: 68 % / 32 % of 456 px (the mockup's 315 / 143), 251 px high (29 mm) */
    CHECK(l.letters.x == 8 && l.letters.y == 541 && l.letters.w == 310 && l.letters.h == 251);
    CHECK(l.digits.x == 326 && l.digits.y == 541 && l.digits.w == 146 && l.digits.h == 251);
    CHECK_EQ(l.divider_x, 322);
    /* four buttons of 110 px, 8 apart, the row's keys 43 px under an 8 px gap */
    for (int i = 0; i < CP0_ZONE_BTN__COUNT; i++) {
        CHECK_EQ(l.button[i].x, 8 + i * 118);
        CHECK_EQ(l.button[i].w, 110);
        CHECK_EQ(l.button[i].y, 490);
        CHECK_EQ(l.button[i].h, 43);
    }
    /* the strip: nine keys from the left margin to the right one, 4 px apart, weights 13 15 15 12 10 10 10 10 18 */
    CHECK_EQ(l.strip_key[0].x, 8);
    CHECK_EQ(l.strip_key[CP0_ZONE_KEY_ENTER].x + l.strip_key[CP0_ZONE_KEY_ENTER].w, 472);
    for (int i = 1; i < CP0_ZONE_KEY__COUNT; i++)
        CHECK_EQ(l.strip_key[i].x, l.strip_key[i - 1].x + l.strip_key[i - 1].w + l.gap);
    CHECK(l.strip_key[CP0_ZONE_KEY_ENTER].w > l.strip_key[CP0_ZONE_KEY_CTRL].w);
    CHECK(l.strip_key[CP0_ZONE_KEY_CTRL].w > l.strip_key[CP0_ZONE_KEY_LEFT].w);
    CHECK(abs(l.strip_key[CP0_ZONE_KEY_LEFT].w - 38) <= 1);
    CHECK_EQ(l.strip_key[CP0_ZONE_KEY_UP].y, 439);
    /* every target at least 5 mm high, the arrows at least 4 mm wide */
    CHECK(l.key_h * 100 >= 5 * l.ppmm_x100 - 50);
    CHECK(l.strip_key[CP0_ZONE_KEY_LEFT].w * 100 >= 4 * l.ppmm_x100);
    CHECK_EQ(l.button_px, 18);
    CHECK_EQ(l.key_px, 16);
    CHECK_EQ(l.box_label_px, 14);
    CHECK_EQ(l.ink_px, 3);

    /* 480x640, a 3.5" panel at 9.04 px/mm: 5 mm = 45, a third of 640 = 213, the app area 374 (321 with the strip) */
    {
        const cp0_ui_metrics_t m = metrics(480, 640, ppmm_35());
        cp0_zone_layout_t s;
        CHECK_EQ(m.ppmm_x100, 904);
        CHECK_EQ(cp0_zone_layout(&m, &s), 0);
        CHECK_EQ(s.key_h, 45);
        CHECK_EQ(s.write_h, 213);
        CHECK_EQ(s.app_h, 374);
        CHECK_EQ(s.band_open, 319);
        CHECK_EQ(s.strip_y, 321);
    }
    /* the deck's 11.3 px/mm on 480x640: everything larger, still room for the app */
    {
        const cp0_ui_metrics_t m = metrics(480, 640, 1130);
        cp0_zone_layout_t s;
        CHECK_EQ(cp0_zone_layout(&m, &s), 0);
        CHECK_EQ(s.key_h, 57);
        CHECK_EQ(s.write_h, 213);
        CHECK_EQ(s.app_h, 362);
    }
    /* no zone: landscape, square, too small a panel, no metrics */
    {
        cp0_zone_layout_t s;
        memset(&s, 0x5A, sizeof(s));
        const cp0_ui_metrics_t land = metrics(800, 480, 854);
        CHECK_EQ(cp0_zone_layout(&land, &s), -1);
        const cp0_ui_metrics_t square = metrics(720, 720, 1000);
        CHECK_EQ(cp0_zone_layout(&square, &s), -1);
        const cp0_ui_metrics_t small = metrics(320, 480, 1130);
        CHECK_EQ(cp0_zone_layout(&small, &s), -1);
        CHECK_EQ(cp0_zone_layout(NULL, &s), -1);
        CHECK_EQ(s.w, 0x5A5A5A5A); /* untouched */
    }
}

static void test_hit(void)
{
    const cp0_zone_layout_t l = layout_dsi();
    CHECK_EQ(cp0_zone_band_top(&l, 0), 482);
    CHECK_EQ(cp0_zone_band_top(&l, 1), 431);
    CHECK_EQ(cp0_zone_hit(&l, 0, 100, 10), CP0_ZONE_HIT_APP);
    CHECK_EQ(cp0_zone_hit(&l, 0, 100, 481), CP0_ZONE_HIT_APP);
    CHECK_EQ(cp0_zone_hit(&l, 0, 100, 482), CP0_ZONE_HIT_BAND);
    CHECK_EQ(cp0_zone_hit(&l, 0, 100, 460), CP0_ZONE_HIT_APP);  /* the strip's rows belong to the app while closed */
    CHECK_EQ(cp0_zone_hit(&l, 1, 100, 460), CP0_ZONE_HIT_BAND); /* ... and to the band while open */
    CHECK_EQ(cp0_zone_hit(&l, 1, 100, 430), CP0_ZONE_HIT_APP);
    CHECK_EQ(cp0_zone_hit(&l, 0, 100, 532), CP0_ZONE_HIT_BAND);
    CHECK_EQ(cp0_zone_hit(&l, 0, 100, 533), CP0_ZONE_HIT_LETTERS);
    CHECK_EQ(cp0_zone_hit(&l, 0, 2, 799), CP0_ZONE_HIT_LETTERS); /* the margins count as the nearest box */
    CHECK_EQ(cp0_zone_hit(&l, 0, 321, 700), CP0_ZONE_HIT_LETTERS);
    CHECK_EQ(cp0_zone_hit(&l, 0, 322, 700), CP0_ZONE_HIT_DIGITS);
    CHECK_EQ(cp0_zone_hit(&l, 1, 479, 799), CP0_ZONE_HIT_DIGITS);
}

/* ------------------------------------------------------------------ the router */

static cp0_touch_frame_t frame(int64_t t_ms, int n, const int *xy_id)
{
    cp0_touch_frame_t f;
    memset(&f, 0, sizeof(f));
    f.t_us = t_ms * 1000;
    f.n = n;
    for (int i = 0; i < n; i++) {
        f.c[i].x = xy_id[3 * i];
        f.c[i].y = xy_id[3 * i + 1];
        f.c[i].id = xy_id[3 * i + 2];
        f.c[i].major = -1;
        f.c[i].slot = i;
    }
    return f;
}

static cp0_touch_frame_t one(int64_t t_ms, int x, int y, int id)
{
    const int c[3] = {x, y, id};
    return frame(t_ms, 1, c);
}

static cp0_touch_frame_t none(int64_t t_ms)
{
    return frame(t_ms, 0, NULL);
}

static void test_router(void)
{
    const cp0_zone_layout_t l = layout_dsi();
    cp0_zone_router_t r;
    cp0_zone_pen_t pen;
    cp0_touch_frame_t f;

    cp0_zone_router_init(&r, 50, 128);
    /* a tap in the app area: the app's, nothing for the pen */
    f = one(0, 200, 300, 1);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_APP);
    CHECK_EQ(pen.kind, CP0_ZONE_PEN_NONE);
    f = none(40);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_NONE);
    CHECK_EQ(pen.kind, CP0_ZONE_PEN_NONE);

    /* a stroke in the digits box: down, moves, up at the last position */
    f = one(1000, 400, 650, 2);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_PEN);
    CHECK(pen.kind == CP0_ZONE_PEN_DOWN && pen.box == STROKES_BOX_DIGITS && pen.x == 400 && pen.y == 650 &&
          pen.t_us == 1000000);
    f = one(1010, 400, 700, 2);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_PEN);
    CHECK(pen.kind == CP0_ZONE_PEN_MOVE && pen.y == 700);
    f = none(1020);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_NONE);
    CHECK(pen.kind == CP0_ZONE_PEN_UP && pen.box == STROKES_BOX_DIGITS && pen.x == 400 && pen.y == 700 &&
          pen.t_us == 1020000);

    /* a stroke that leaves its box upwards, into the app area, stays the pen's */
    cp0_zone_router_init(&r, 50, 128);
    f = one(2000, 100, 700, 3);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    CHECK(pen.kind == CP0_ZONE_PEN_DOWN && pen.box == STROKES_BOX_LETTERS);
    f = one(2010, 100, 400, 3);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_PEN);
    CHECK(pen.kind == CP0_ZONE_PEN_MOVE && pen.y == 400);

    /* a second finger: the stroke is cancelled, both are nobody's until every contact is up */
    {
        const int two[6] = {100, 410, 3, 300, 300, 4};
        f = frame(2020, 2, two);
        CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_IGNORED);
        CHECK_EQ(pen.kind, CP0_ZONE_PEN_CANCEL);
        f = one(2030, 300, 300, 4);
        CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_IGNORED);
        CHECK_EQ(pen.kind, CP0_ZONE_PEN_NONE);
        f = none(2040);
        CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_NONE);
        CHECK_EQ(pen.kind, CP0_ZONE_PEN_NONE); /* no lift for a cancelled stroke */
        /* nor a heal window: a press right after lands where it lands (the app) */
        f = one(2050, 100, 410, 5);
        CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_APP);
        f = none(2060);
        cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    }

    /* three fingers landing in the writing zone at once (the multitasking G1 hold): never a stroke */
    {
        const int three[9] = {60, 700, 6, 160, 700, 7, 260, 700, 8};
        f = frame(3000, 3, three);
        CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_IGNORED);
        CHECK_EQ(pen.kind, CP0_ZONE_PEN_NONE);
        f = none(3500);
        CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_NONE);
    }

    /* a swallowed touch (screensaver up, the touch that closed an app) writes nothing */
    f = one(4000, 100, 700, 9);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 1, &f, &pen), CP0_ZONE_OWNER_IGNORED);
    CHECK_EQ(pen.kind, CP0_ZONE_PEN_NONE);
    f = one(4010, 100, 720, 9);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_IGNORED); /* swallow ended: still the same touch */
    f = none(4020);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);

    /* a drop-out: the pen lifts at the top of the band and comes back 30 ms later just above it, in the app area: the
     * stroke goes on (DOWN in the same box); 80 ms later it would have been the app's */
    cp0_zone_router_init(&r, 50, 128);
    f = one(5000, 100, 700, 10);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    f = one(5010, 100, 540, 10);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    f = none(5020);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    CHECK_EQ(pen.kind, CP0_ZONE_PEN_UP);
    f = one(5050, 100, 470, 11);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_PEN);
    CHECK(pen.kind == CP0_ZONE_PEN_DOWN && pen.box == STROKES_BOX_LETTERS && pen.y == 470);
    f = none(5060);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    CHECK_EQ(pen.kind, CP0_ZONE_PEN_UP);
    f = one(5140, 100, 470, 12); /* 80 ms after the lift */
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_APP);
    f = none(5150);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    /* ... and a press within the time but far away is a new touch */
    f = one(5200, 100, 700, 13);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    f = none(5210);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    f = one(5220, 100, 100, 14);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_APP);
    f = none(5230);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);

    /* the band's rows: the buttons always, the strip row only while it is open */
    f = one(6000, 200, 500, 15);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_BAND);
    CHECK_EQ(pen.kind, CP0_ZONE_PEN_NONE);
    f = none(6010);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    f = one(6020, 200, 460, 16);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_APP);
    f = none(6030);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    f = one(6040, 200, 460, 17);
    CHECK_EQ(cp0_zone_route(&r, &l, 1, 0, &f, &pen), CP0_ZONE_OWNER_BAND);
    f = none(6050);
    cp0_zone_route(&r, &l, 1, 0, &f, &pen);

    /* the pen lifts and another contact lands in the same frame: the stroke ends, the other one is nobody's */
    f = one(7000, 100, 700, 18);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    f = one(7010, 300, 300, 19);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_IGNORED);
    CHECK(pen.kind == CP0_ZONE_PEN_UP && pen.y == 700);
    f = none(7020);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    CHECK_EQ(pen.kind, CP0_ZONE_PEN_NONE);

    /* a gesture took the contacts: cancel once, then nobody's */
    f = one(8000, 100, 700, 20);
    cp0_zone_route(&r, &l, 0, 0, &f, &pen);
    cp0_zone_router_cancel(&r, &pen);
    CHECK_EQ(pen.kind, CP0_ZONE_PEN_CANCEL);
    CHECK_EQ(r.owner, CP0_ZONE_OWNER_IGNORED);
    cp0_zone_router_cancel(&r, &pen);
    CHECK_EQ(pen.kind, CP0_ZONE_PEN_NONE);
    f = one(8010, 100, 710, 20);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_IGNORED);
    f = none(8020);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_NONE);
    cp0_zone_router_cancel(&r, &pen); /* nothing down: stays NONE */
    CHECK_EQ(r.owner, CP0_ZONE_OWNER_NONE);

    /* the panel comes back from an app with a finger down: nobody's until it lifts */
    cp0_zone_router_reset(&r, 1);
    f = one(9000, 100, 700, 21);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_IGNORED);
    f = none(9010);
    CHECK_EQ(cp0_zone_route(&r, &l, 0, 0, &f, &pen), CP0_ZONE_OWNER_NONE);
    cp0_zone_router_reset(&r, 0);
    CHECK_EQ(r.owner, CP0_ZONE_OWNER_NONE);
}

/* ------------------------------------------------------------------ the strip */

static int is_ev(const cp0_ui_osk_event_t *e, uint32_t code, int state, uint32_t mods, const char *utf8)
{
    return e->code == code && e->state == state && e->mods == mods && strcmp(e->utf8, utf8) == 0;
}

static void test_strip(void)
{
    cp0_zone_strip_t s;
    cp0_ui_osk_event_t e[CP0_ZONE_MAX_EVENTS];
    int n;
    cp0_zone_strip_init(&s);
    CHECK_EQ(cp0_zone_strip_mods(&s), 0);
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_TAB), -1);

    /* Tab and Enter alone */
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_TAB, 0, 1000, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_TAB, KBD_KEY_PRESSED, 0, "") && is_ev(&e[1], KEY_TAB, KBD_KEY_RELEASED, 0, ""));
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_TAB, 1, 1000, e, CP0_ZONE_MAX_EVENTS); /* a terminal: a tab character */
    CHECK(n == 2 && is_ev(&e[0], KEY_TAB, KBD_KEY_PRESSED, 0, "\t"));
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_ENTER, 0, 1000, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_ENTER, KBD_KEY_PRESSED, 0, "\r") && is_ev(&e[1], KEY_ENTER, KBD_KEY_RELEASED, 0, "\r"));

    /* Ctrl: armed by a tap, used by the next key (Ctrl+Enter, no text), off after it */
    CHECK_EQ(cp0_zone_strip_up(&s, CP0_ZONE_KEY_CTRL, 0, 2000, e, CP0_ZONE_MAX_EVENTS), 0);
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_CTRL), CP0_UI_OSK_MOD_ARMED);
    CHECK_EQ(cp0_zone_strip_mods(&s), KBD_MOD_CTRL);
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_ENTER, 0, 2500, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_ENTER, KBD_KEY_PRESSED, KBD_MOD_CTRL, ""));
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_CTRL), CP0_UI_OSK_MOD_OFF);

    /* a second tap later than the double tap: off again; within it: locked, and it stays after a key */
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_SHIFT, 0, 3000, e, CP0_ZONE_MAX_EVENTS);
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_SHIFT, 0, 3000 + CP0_UI_OSK_DOUBLE_TAP_MS + 1, e, CP0_ZONE_MAX_EVENTS);
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_SHIFT), CP0_UI_OSK_MOD_OFF);
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_SHIFT, 0, 4000, e, CP0_ZONE_MAX_EVENTS);
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_SHIFT, 0, 4200, e, CP0_ZONE_MAX_EVENTS);
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_SHIFT), CP0_UI_OSK_MOD_LOCKED);
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_TAB, 0, 4300, e, CP0_ZONE_MAX_EVENTS); /* Shift+Tab: the previous field */
    CHECK(n == 2 && is_ev(&e[0], KEY_TAB, KBD_KEY_PRESSED, KBD_MOD_SHIFT, ""));
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_SHIFT), CP0_UI_OSK_MOD_LOCKED);
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_SHIFT, 0, 5000, e, CP0_ZONE_MAX_EVENTS);
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_SHIFT), CP0_UI_OSK_MOD_OFF);

    /* an arrow held: press with the modifiers and its control text, repeats, release; Shift used at the release */
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_SHIFT, 0, 6000, e, CP0_ZONE_MAX_EVENTS);
    n = cp0_zone_strip_down(&s, CP0_ZONE_KEY_RIGHT, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 1 && is_ev(&e[0], KEY_RIGHT, KBD_KEY_PRESSED, KBD_MOD_SHIFT, "\033[C"));
    n = cp0_zone_strip_repeat(&s, CP0_ZONE_KEY_RIGHT, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 1 && is_ev(&e[0], KEY_RIGHT, KBD_KEY_REPEATED, KBD_MOD_SHIFT, "\033[C"));
    CHECK_EQ(cp0_zone_strip_repeat(&s, CP0_ZONE_KEY_LEFT, e, CP0_ZONE_MAX_EVENTS), 0); /* not the held one */
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_SHIFT), CP0_UI_OSK_MOD_ARMED);
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_RIGHT, 0, 6500, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 1 && is_ev(&e[0], KEY_RIGHT, KBD_KEY_RELEASED, KBD_MOD_SHIFT, "\033[C"));
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_SHIFT), CP0_UI_OSK_MOD_OFF);
    CHECK_EQ(cp0_zone_strip_up(&s, CP0_ZONE_KEY_RIGHT, 0, 6600, e, CP0_ZONE_MAX_EVENTS), 0); /* nothing owed */
    /* Ctrl + an arrow: no control text (a shortcut) */
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_CTRL, 0, 7000, e, CP0_ZONE_MAX_EVENTS);
    n = cp0_zone_strip_down(&s, CP0_ZONE_KEY_LEFT, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 1 && is_ev(&e[0], KEY_LEFT, KBD_KEY_PRESSED, KBD_MOD_CTRL, ""));
    /* another arrow before the first came up: the owed release first */
    n = cp0_zone_strip_down(&s, CP0_ZONE_KEY_UP, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_LEFT, KBD_KEY_RELEASED, KBD_MOD_CTRL, "") &&
          is_ev(&e[1], KEY_UP, KBD_KEY_PRESSED, 0, "\033[A"));
    n = cp0_zone_strip_cancel(&s, e, CP0_ZONE_MAX_EVENTS); /* the finger slid off */
    CHECK(n == 1 && is_ev(&e[0], KEY_UP, KBD_KEY_RELEASED, 0, "\033[A"));

    /* Alt is a real key: its press when it comes on, Tab with ALT, then its release (a quick Alt+Tab) */
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_ALT, 0, 8000, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 1 && is_ev(&e[0], 56 /* KEY_LEFTALT */, KBD_KEY_PRESSED, KBD_MOD_ALT, ""));
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_TAB, 1, 8100, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 3 && is_ev(&e[0], KEY_TAB, KBD_KEY_PRESSED, KBD_MOD_ALT, "") && /* no tab character with Alt */
          is_ev(&e[2], 56, KBD_KEY_RELEASED, 0, ""));
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_ALT), CP0_UI_OSK_MOD_OFF);
    /* Alt locked: its press once, nothing at the lock, its release when turned off */
    CHECK_EQ(cp0_zone_strip_up(&s, CP0_ZONE_KEY_ALT, 0, 9000, e, CP0_ZONE_MAX_EVENTS), 1);
    CHECK_EQ(cp0_zone_strip_up(&s, CP0_ZONE_KEY_ALT, 0, 9100, e, CP0_ZONE_MAX_EVENTS), 0);
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_ALT), CP0_UI_OSK_MOD_LOCKED);
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_ALT, 0, 9500, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 1 && is_ev(&e[0], 56, KBD_KEY_RELEASED, 0, ""));

    /* the strip closes with an arrow held and Alt on: both releases owed, every modifier off */
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_ALT, 0, 10000, e, CP0_ZONE_MAX_EVENTS);
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_CTRL, 0, 10000, e, CP0_ZONE_MAX_EVENTS);
    cp0_zone_strip_down(&s, CP0_ZONE_KEY_DOWN, e, CP0_ZONE_MAX_EVENTS);
    n = cp0_zone_strip_release_all(&s, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n >= 2 && e[0].code == KEY_DOWN && e[0].state == KBD_KEY_RELEASED && e[n - 1].code == 56 &&
          e[n - 1].state == KBD_KEY_RELEASED);
    CHECK_EQ(cp0_zone_strip_mods(&s), 0);
    CHECK_EQ(cp0_zone_strip_release_all(&s, e, CP0_ZONE_MAX_EVENTS), 0);
}

/* ------------------------------------------------------------------ results */

static strokes_result result_char(const char *utf8, unsigned mode)
{
    strokes_result r;
    memset(&r, 0, sizeof(r));
    r.kind = STROKES_CHAR;
    snprintf(r.utf8, sizeof(r.utf8), "%s", utf8);
    r.mode = mode;
    return r;
}

static strokes_result result_key(int key)
{
    strokes_result r;
    memset(&r, 0, sizeof(r));
    r.kind = STROKES_KEY;
    r.key = key;
    return r;
}

static void test_results(void)
{
    cp0_zone_strip_t s;
    cp0_ui_osk_event_t e[CP0_ZONE_MAX_EVENTS];
    strokes_result r;
    int n;
    cp0_zone_strip_init(&s);

    r = result_char("a", 0);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_A, KBD_KEY_PRESSED, 0, "a") && is_ev(&e[1], KEY_A, KBD_KEY_RELEASED, 0, "a"));
    r = result_char("A", STROKES_MODE_SHIFT); /* the recogniser's one-shot shift already made it upper case */
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_A, KBD_KEY_PRESSED, KBD_MOD_SHIFT, "A"));
    r = result_char("Q", STROKES_MODE_CAPS); /* under its caps lock: as the keyboard's Caps lock */
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_Q, KBD_KEY_PRESSED, KBD_MOD_CAPS, "Q"));
    r = result_char("!", STROKES_MODE_PUNCT);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_1, KBD_KEY_PRESSED, KBD_MOD_SHIFT, "!"));
    r = result_char("7", 0);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_7, KBD_KEY_PRESSED, 0, "7"));
    /* characters without a US key: the text alone, code 0 (Latin-1 and beyond) */
    const char *const no_key[] = {"\xe2\x82\xac" /* euro */, "\xc2\xa3" /* pound */, "\xc3\x9f" /* sharp s */,
                                  "\xc3\xa9" /* e acute */, "\xc2\xa9" /* copyright */, "\xe2\x80\xa2" /* bullet */};
    for (size_t i = 0; i < sizeof(no_key) / sizeof(no_key[0]); i++) {
        r = result_char(no_key[i], STROKES_MODE_EXT);
        n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
        CHECK(n == 2 && is_ev(&e[0], 0, KBD_KEY_PRESSED, 0, no_key[i]) && is_ev(&e[1], 0, KBD_KEY_RELEASED, 0, no_key[i]));
    }

    /* keys: their KEY_* and the keyboard's control text */
    r = result_key(STROKES_KEY_SPACE);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_SPACE, KBD_KEY_PRESSED, 0, " "));
    r = result_key(STROKES_KEY_BACKSPACE);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_BACKSPACE, KBD_KEY_PRESSED, 0, "\x7f"));
    r = result_key(STROKES_KEY_ENTER);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_ENTER, KBD_KEY_PRESSED, 0, "\r"));
    r = result_key(STROKES_KEY_TAB);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_TAB, KBD_KEY_PRESSED, 0, ""));
    n = cp0_zone_result_events(&s, &r, 1, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_TAB, KBD_KEY_PRESSED, 0, "\t"));
    /* modes and rejections type nothing */
    r = result_key(STROKES_KEY_NONE);
    CHECK_EQ(cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS), 0);
    memset(&r, 0, sizeof(r));
    r.kind = STROKES_MODE;
    r.mode = STROKES_MODE_SHIFT;
    CHECK_EQ(cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS), 0);
    r.kind = STROKES_REJECT;
    CHECK_EQ(cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS), 0);

    /* the strip's Ctrl: Ctrl+C (the code, no text), then off; a character without a code types and keeps Ctrl */
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_CTRL, 0, 1000, e, CP0_ZONE_MAX_EVENTS);
    r = result_char("\xc3\xa9", 0);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], 0, KBD_KEY_PRESSED, 0, "\xc3\xa9"));
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_CTRL), CP0_UI_OSK_MOD_ARMED);
    r = result_char("c", 0);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_C, KBD_KEY_PRESSED, KBD_MOD_CTRL, "") && is_ev(&e[1], KEY_C, KBD_KEY_RELEASED, KBD_MOD_CTRL, ""));
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_CTRL), CP0_UI_OSK_MOD_OFF);
    /* Ctrl + a stroke key: Ctrl+Backspace, no text */
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_CTRL, 0, 2000, e, CP0_ZONE_MAX_EVENTS);
    r = result_key(STROKES_KEY_BACKSPACE);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_BACKSPACE, KBD_KEY_PRESSED, KBD_MOD_CTRL, ""));

    /* the strip's Shift: a letter upper case (accented ones too), used once */
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_SHIFT, 0, 3000, e, CP0_ZONE_MAX_EVENTS);
    r = result_char("g", 0);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_G, KBD_KEY_PRESSED, KBD_MOD_SHIFT, "G"));
    r = result_char("g", 0);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_G, KBD_KEY_PRESSED, 0, "g"));
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_SHIFT, 0, 4000, e, CP0_ZONE_MAX_EVENTS);
    r = result_char("\xc3\xa9", 0);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], 0, KBD_KEY_PRESSED, KBD_MOD_SHIFT, "\xc3\x89"));
    CHECK_EQ(cp0_zone_strip_mod_state(&s, CP0_ZONE_KEY_SHIFT), CP0_UI_OSK_MOD_OFF);
    /* Shift + a stroke space: Shift+Space with its text */
    cp0_zone_strip_up(&s, CP0_ZONE_KEY_SHIFT, 0, 5000, e, CP0_ZONE_MAX_EVENTS);
    r = result_key(STROKES_KEY_SPACE);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 2 && is_ev(&e[0], KEY_SPACE, KBD_KEY_PRESSED, KBD_MOD_SHIFT, " "));

    /* the strip's Alt: Alt+f with its text (as a physical Alt+key), then Alt's release */
    n = cp0_zone_strip_up(&s, CP0_ZONE_KEY_ALT, 0, 6000, e, CP0_ZONE_MAX_EVENTS);
    CHECK_EQ(n, 1);
    r = result_char("f", 0);
    n = cp0_zone_result_events(&s, &r, 0, e, CP0_ZONE_MAX_EVENTS);
    CHECK(n == 3 && is_ev(&e[0], KEY_F, KBD_KEY_PRESSED, KBD_MOD_ALT, "f") && is_ev(&e[2], 56, KBD_KEY_RELEASED, 0, ""));
    CHECK_EQ(cp0_zone_strip_mods(&s), 0);
}

static void test_mode_text(void)
{
    char letters[8], hint[64];
    cp0_zone_mode_text(0, 0, letters, sizeof(letters), hint, sizeof(hint));
    CHECK_STR(letters, "abc");
    CHECK_STR(hint, "");
    cp0_zone_mode_text(STROKES_MODE_SHIFT, 0, letters, sizeof(letters), hint, sizeof(hint));
    CHECK_STR(letters, "Abc");
    CHECK_STR(hint, "Shift");
    cp0_zone_mode_text(STROKES_MODE_CAPS, 0, letters, sizeof(letters), hint, sizeof(hint));
    CHECK_STR(letters, "ABC");
    CHECK_STR(hint, "Caps");
    cp0_zone_mode_text(STROKES_MODE_PUNCT, 0, letters, sizeof(letters), hint, sizeof(hint));
    CHECK_STR(letters, "abc");
    CHECK_STR(hint, "Punct");
    cp0_zone_mode_text(STROKES_MODE_EXT | STROKES_MODE_CAPS, 0, letters, sizeof(letters), hint, sizeof(hint));
    CHECK_STR(letters, "ABC");
    CHECK_STR(hint, "Caps  Ext");
    cp0_zone_mode_text(STROKES_MODE_ACCENT | STROKES_MODE_SHIFT, STROKES_ACCENT_ACUTE, letters, sizeof(letters), hint,
                       sizeof(hint));
    CHECK_STR(letters, "Abc");
    CHECK_STR(hint, "Shift  Accent: acute");
    cp0_zone_mode_text(STROKES_MODE_ACCENT, STROKES_ACCENT_NONE, letters, sizeof(letters), hint, sizeof(hint));
    CHECK_STR(hint, ""); /* no accent kind known: nothing to say */
    cp0_zone_mode_text(STROKES_MODE_PUNCT, 0, NULL, 0, hint, 4); /* a short buffer: cut, terminated */
    CHECK_STR(hint, "Pun");
}

static void test_upper_and_us_keys(void)
{
    char out[16];
    CHECK_EQ(cp0_zone_upper("a", out, sizeof(out)), 1);
    CHECK_STR(out, "A");
    CHECK_EQ(cp0_zone_upper("\xc3\xa9", out, sizeof(out)), 1); /* e acute */
    CHECK_STR(out, "\xc3\x89");
    CHECK_EQ(cp0_zone_upper("\xc3\xb7", out, sizeof(out)), 0); /* the division sign has no upper case */
    CHECK_STR(out, "\xc3\xb7");
    CHECK_EQ(cp0_zone_upper("\xc3\xbf", out, sizeof(out)), 0); /* y diaeresis: none in Latin-1 */
    CHECK_EQ(cp0_zone_upper("ab", out, sizeof(out)), 0);
    CHECK_STR(out, "ab");
    CHECK_EQ(cp0_zone_upper("7", out, sizeof(out)), 0);

    uint32_t code = 99;
    int shift = 9;
    CHECK(cp0_ui_osk_char_key("a", &code, &shift) == 1 && code == KEY_A && shift == 0);
    CHECK(cp0_ui_osk_char_key("Z", &code, &shift) == 1 && code == KEY_Z && shift == 1);
    CHECK(cp0_ui_osk_char_key(" ", &code, &shift) == 1 && code == KEY_SPACE && shift == 0);
    CHECK(cp0_ui_osk_char_key("@", &code, &shift) == 1 && code == KEY_2 && shift == 1);
    CHECK(cp0_ui_osk_char_key("~", &code, &shift) == 1 && shift == 1);
    CHECK(cp0_ui_osk_char_key("`", &code, &shift) == 1 && shift == 0);
    CHECK(cp0_ui_osk_char_key("\xe2\x82\xac", &code, &shift) == 0 && code == 0 && shift == 0);
    CHECK(cp0_ui_osk_char_key("\xc3\xa9", &code, &shift) == 0 && code == 0);
    CHECK(cp0_ui_osk_char_key("", &code, &shift) == 0);
    CHECK(cp0_ui_osk_char_key(NULL, NULL, NULL) == 0);
    /* every printable ASCII character has a US key */
    for (int c = 0x20; c < 0x7F; c++) {
        const char text[2] = {(char)c, 0};
        if (!cp0_ui_osk_char_key(text, &code, &shift)) {
            fprintf(stderr, "no US key for '%c'\n", c);
            failures++;
        }
    }
}

/* ------------------------------------------------------------------ end to end with the real recogniser */

typedef struct {
    cp0_zone_router_t router;
    cp0_zone_strip_t strip;
    strokes_engine *engine;
    int64_t t_ms;
    int id;
    char typed[64];
    int keys[16];
    uint32_t mods[16];
    int n_keys;
} pipeline_t;

static void feed(pipeline_t *p, const cp0_zone_layout_t *l, const cp0_touch_frame_t *f)
{
    cp0_zone_pen_t pen;
    cp0_zone_route(&p->router, l, 0, 0, f, &pen);
    switch (pen.kind) {
    case CP0_ZONE_PEN_DOWN: strokes_down(p->engine, pen.box, (float)pen.x, (float)pen.y, (uint64_t)pen.t_us); break;
    case CP0_ZONE_PEN_MOVE: strokes_move(p->engine, (float)pen.x, (float)pen.y, (uint64_t)pen.t_us); break;
    case CP0_ZONE_PEN_UP: strokes_up(p->engine, (uint64_t)pen.t_us); break;
    case CP0_ZONE_PEN_CANCEL: strokes_cancel(p->engine); break;
    default: break;
    }
}

static void collect(pipeline_t *p)
{
    strokes_result r;
    while (strokes_poll(p->engine, (uint64_t)(p->t_ms * 1000), &r)) {
        cp0_ui_osk_event_t e[CP0_ZONE_MAX_EVENTS];
        const int n = cp0_zone_result_events(&p->strip, &r, 0, e, CP0_ZONE_MAX_EVENTS);
        for (int i = 0; i < n; i++) {
            if (e[i].state != KBD_KEY_PRESSED) continue;
            if (p->n_keys < 16) {
                p->keys[p->n_keys] = (int)e[i].code;
                p->mods[p->n_keys++] = e[i].mods;
            }
            strncat(p->typed, e[i].utf8, sizeof(p->typed) - strlen(p->typed) - 1);
        }
    }
}

/* one stroke through polyline points (0..100 template units, y down) drawn size_mm high, starting at (x0, y0) in the
 * panel, one frame every 8 ms at about 100 mm/s, then a lift; 80 ms later the results are polled */
static void write_stroke(pipeline_t *p, const cp0_zone_layout_t *l, const float *pts, int n, int x0, int y0,
                         float size_mm)
{
    const float px_per_unit = size_mm * (float)l->ppmm_x100 / 100.0f / 100.0f;
    const float step_px = 0.8f * (float)l->ppmm_x100 / 100.0f; /* 0.8 mm per 8 ms frame */
    const int id = ++p->id;
    float x = (float)x0 + pts[0] * px_per_unit, y = (float)y0 + pts[1] * px_per_unit;
    cp0_touch_frame_t f = one(p->t_ms, (int)lroundf(x), (int)lroundf(y), id);
    feed(p, l, &f);
    for (int i = 1; i < n; i++) {
        const float tx = (float)x0 + pts[2 * i] * px_per_unit, ty = (float)y0 + pts[2 * i + 1] * px_per_unit;
        const float fx = (float)x0 + pts[2 * i - 2] * px_per_unit, fy = (float)y0 + pts[2 * i - 1] * px_per_unit;
        const float len = hypotf(tx - fx, ty - fy);
        const int steps = (int)fmaxf(1.0f, ceilf(len / step_px));
        for (int k = 1; k <= steps; k++) {
            x = fx + (tx - fx) * (float)k / (float)steps;
            y = fy + (ty - fy) * (float)k / (float)steps;
            p->t_ms += 8;
            f = one(p->t_ms, (int)lroundf(x), (int)lroundf(y), id);
            feed(p, l, &f);
        }
    }
    p->t_ms += 8;
    f = none(p->t_ms);
    feed(p, l, &f);
    p->t_ms += 80;
    collect(p);
    p->t_ms += 300;
}

static void test_end_to_end(void)
{
    const cp0_zone_layout_t l = layout_dsi();
    strokes_table *t = strokes_table_new();
    strokes_error err;
    if (strokes_table_add_file(t, ZONE_TABLE_DIR "/glyphs.txt", &err) != 0 ||
        strokes_table_add_file(t, ZONE_TABLE_DIR "/glyphs-extra.txt", &err) != 0) {
        fprintf(stderr, "table: %s:%d: %s\n", err.source, err.line, err.msg);
        failures++;
        strokes_table_free(t);
        return;
    }
    strokes_config cfg;
    strokes_config_default(&cfg);
    cfg.ppmm = (float)l.ppmm_x100 / 100.0f;
    pipeline_t p;
    memset(&p, 0, sizeof(p));
    cp0_zone_router_init(&p.router, (int)cfg.heal_ms, (int)(cfg.heal_mm * cfg.ppmm * 4.0f));
    cp0_zone_strip_init(&p.strip);
    p.engine = strokes_engine_new(t, &cfg);
    p.t_ms = 10000;
    CHECK(p.engine != NULL);
    if (!p.engine) {
        strokes_table_free(t);
        return;
    }

    static const float down[] = {0, 0, 0, 100};          /* i, 1 */
    static const float up[] = {0, 100, 0, 0};            /* the shift gesture */
    static const float right[] = {0, 0, 100, 0};         /* space */
    static const float down_right[] = {0, 0, 100, 100};  /* the extended prefix */
    static const float l_shape[] = {0, 0, 0, 100, 60, 100}; /* l; after the extended prefix the pound sign */
    const int lx = l.letters.x + 100, ly = l.letters.y + 30;
    const int dx = l.digits.x + 60, dy = l.digits.y + 30;

    write_stroke(&p, &l, down, 2, lx, ly, 12.0f);             /* i */
    write_stroke(&p, &l, up, 2, lx, ly, 12.0f);               /* shift */
    CHECK_EQ(strokes_get_mode(p.engine), STROKES_MODE_SHIFT);
    write_stroke(&p, &l, down, 2, lx, ly, 12.0f);             /* I */
    write_stroke(&p, &l, right, 2, lx - 40, ly + 40, 12.0f); /* space */
    write_stroke(&p, &l, down, 2, dx, dy, 12.0f);             /* 1, in the digits box */
    write_stroke(&p, &l, down_right, 2, lx, ly, 10.0f);       /* the extended prefix ... */
    CHECK_EQ(strokes_get_mode(p.engine), STROKES_MODE_EXT);
    write_stroke(&p, &l, l_shape, 3, lx, ly, 12.0f);          /* ... and an l: the pound sign, no US key */
    CHECK_STR(p.typed, "iI 1\xc2\xa3");
    CHECK_EQ(p.n_keys, 5);
    CHECK(p.keys[0] == KEY_I && p.mods[0] == 0);
    CHECK(p.keys[1] == KEY_I && p.mods[1] == KBD_MOD_SHIFT);
    CHECK(p.keys[2] == KEY_SPACE);
    CHECK(p.keys[3] == KEY_1);
    CHECK(p.keys[4] == 0 && p.mods[4] == 0);

    /* what the first board test refused (report 041 section 12): a lower-case b, then the shift gesture and a plain V
     * (no flick), written 18 mm high as the taller writing zone invites; the extended mode left first */
    {
        static const float b_lower[] = {0, 0, 0, 100, 0, 70, 6, 50, 27, 42, 51, 50, 61, 72, 53, 93, 28, 100, 4, 97};
        static const float vee[] = {0, 0, 25, 100, 50, 0};
        strokes_set_mode(p.engine, 0);
        write_stroke(&p, &l, b_lower, 10, lx, ly, 18.0f);
        write_stroke(&p, &l, up, 2, lx, ly, 12.0f);
        write_stroke(&p, &l, vee, 3, lx, ly, 18.0f);
        CHECK_STR(p.typed, "iI 1\xc2\xa3"
                           "bV");
        CHECK_EQ(p.n_keys, 7);
        CHECK(p.keys[5] == KEY_B && p.mods[5] == 0);
        CHECK(p.keys[6] == KEY_V && p.mods[6] == KBD_MOD_SHIFT);
    }

    /* a stroke cut by a second finger types nothing */
    {
        const int before = p.n_keys;
        const int id = ++p.id;
        cp0_touch_frame_t f = one(p.t_ms, lx, ly, id);
        feed(&p, &l, &f);
        for (int k = 1; k <= 10; k++) {
            p.t_ms += 8;
            f = one(p.t_ms, lx, ly + 10 * k, id);
            feed(&p, &l, &f);
        }
        const int two[6] = {lx, ly + 110, id, 200, 200, id + 1};
        p.t_ms += 8;
        f = frame(p.t_ms, 2, two);
        feed(&p, &l, &f);
        p.t_ms += 8;
        f = none(p.t_ms);
        feed(&p, &l, &f);
        p.t_ms += 200;
        collect(&p);
        CHECK_EQ(p.n_keys, before);
        CHECK(!strokes_busy(p.engine));
    }

    strokes_engine_free(p.engine);
    strokes_table_free(t);
}

int main(void)
{
    test_layout();
    test_hit();
    test_router();
    test_strip();
    test_results();
    test_mode_text();
    test_upper_and_us_keys();
    test_end_to_end();
    if (failures) {
        fprintf(stderr, "test_zone: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_zone: OK\n");
    return 0;
}
