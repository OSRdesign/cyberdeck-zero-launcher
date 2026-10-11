/*
 * Unit test of the overlay sizes (src/cp0_ui_overlay.c, task 014 P4): the compat branch (display width <= 320) and
 * the pinned DECK / COMPACT tables equal the numbers the toast, media OSD and loading overlay drew before; computed
 * sizes come from the tokens, stay on the display and grow with the density.
 */
#include "cp0_ui_overlay.h"

#include <stdio.h>
#include <string.h>

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

static cp0_ui_metrics_t metrics(int w, int h, int ppmm_x100)
{
    cp0_ui_metrics_t m;
    cp0_ui_metrics_compute(w, h, 0, ppmm_x100, CP0_UI_DENSITY_DEFAULT, 0, &m);
    return m;
}

/* today's toast, the way launcher_toast.cpp scaled it: design px * display width / 320 (never down) */
static int old_toast(int design_px, int width)
{
    return design_px * (width > 320 ? width : 320) / 320;
}

static void check_toast_literals(const cp0_ui_toast_t *t, int width, int lines)
{
    CHECK_EQ(t->w, old_toast(280, width));
    CHECK_EQ(t->h, old_toast(lines >= 2 ? 38 : 22, width));
    CHECK_EQ(t->top, old_toast(4, width));
    CHECK_EQ(t->radius, old_toast(6, width));
    CHECK_EQ(t->font_px, old_toast(12, width));
    CHECK_EQ(t->label_w, old_toast(268, width));
}

static void test_compat_branch(void)
{
    /* any metrics (here a computed 320x170 Compact one, what cp0_ui_metrics_get() gives on the compat display, and
     * the deck's), or none at all */
    const cp0_ui_metrics_t small = metrics(320, 170, 0), deck = metrics(640, 480, 0);
    const cp0_ui_metrics_t *cases[3] = {&small, &deck, NULL};
    for (int i = 0; i < 3; i++) {
        cp0_ui_toast_t t;
        cp0_ui_osd_t o;
        cp0_ui_loading_t l;
        cp0_ui_overlay_toast(cases[i], 320, 1, &t);
        CHECK_EQ(t.w, 280); CHECK_EQ(t.h, 22); CHECK_EQ(t.top, 4); CHECK_EQ(t.radius, 6);
        CHECK_EQ(t.font_px, 12); CHECK_EQ(t.label_w, 268);
        cp0_ui_overlay_toast(cases[i], 320, 2, &t);
        CHECK_EQ(t.h, 38);
        cp0_ui_overlay_toast(cases[i], 320, 3, &t);
        CHECK_EQ(t.h, 38);
        cp0_ui_overlay_osd(cases[i], 320, 170, &o);
        CHECK_EQ(o.w, 190); CHECK_EQ(o.h, 82); CHECK_EQ(o.radius, 8); CHECK_EQ(o.border, 1);
        CHECK_EQ(o.font_px, 12); CHECK_EQ(o.icon_px, 0);
        CHECK_EQ(o.icon.x, 14); CHECK_EQ(o.icon.y, 10); CHECK_EQ(o.icon.w, 30); CHECK_EQ(o.icon.h, 24);
        CHECK_EQ(o.title.x, 48); CHECK_EQ(o.title.y, 12); CHECK_EQ(o.title.w, 76); CHECK_EQ(o.title.h, 18);
        CHECK_EQ(o.value.x, 124); CHECK_EQ(o.value.y, 12); CHECK_EQ(o.value.w, 48); CHECK_EQ(o.value.h, 18);
        CHECK_EQ(o.bar.x, 18); CHECK_EQ(o.bar.y, 50); CHECK_EQ(o.bar.w, 154); CHECK_EQ(o.bar.h, 10);
        CHECK_EQ(o.bar_radius, 5);
        CHECK_EQ(o.mute_icon.x, 65); CHECK_EQ(o.mute_icon.y, 12); CHECK_EQ(o.mute_icon.w, 60);
        CHECK_EQ(o.mute_icon.h, 32);
        CHECK_EQ(o.mute_title.x, 0); CHECK_EQ(o.mute_title.y, 50); CHECK_EQ(o.mute_title.w, 190);
        CHECK_EQ(o.mute_title.h, 18);
        cp0_ui_overlay_loading(cases[i], 320, 170, &l);
        CHECK_EQ(l.w, 200); CHECK_EQ(l.h, 40); CHECK_EQ(l.radius, 8); CHECK_EQ(l.pad, 6); CHECK_EQ(l.gap, 8);
        CHECK_EQ(l.spinner, 24); CHECK_EQ(l.arc_w, 3); CHECK_EQ(l.font_px, 14);
    }
    /* narrower than the compat window: still today's numbers */
    {
        cp0_ui_toast_t t;
        cp0_ui_overlay_toast(&small, 240, 1, &t);
        CHECK_EQ(t.w, 280);
    }
    CHECK(cp0_ui_overlay_is_compat_display(320));
    CHECK(!cp0_ui_overlay_is_compat_display(321));
    /* the ribbon's rectangle is the compat toast's */
    {
        cp0_ui_toast_t t;
        cp0_ui_overlay_toast(NULL, 320, 1, &t);
        CHECK_EQ(CP0_UI_RIBBON_W, t.w);
        CHECK_EQ(CP0_UI_RIBBON_H, t.h);
        CHECK_EQ(CP0_UI_RIBBON_Y, t.top);
        CHECK_EQ(CP0_UI_RIBBON_X, (320 - t.w) / 2);
    }
}

static void test_pinned_toast(void)
{
    const cp0_ui_metrics_t deck = metrics(640, 480, 0), pi = metrics(480, 320, 630);
    cp0_ui_toast_t t;
    CHECK_EQ(deck.preset, CP0_UI_PRESET_DECK);
    CHECK_EQ(pi.preset, CP0_UI_PRESET_COMPACT);
    cp0_ui_overlay_toast(&deck, 640, 1, &t);
    CHECK_EQ(t.w, 560); CHECK_EQ(t.h, 44); CHECK_EQ(t.top, 8); CHECK_EQ(t.radius, 12);
    CHECK_EQ(t.font_px, 24); CHECK_EQ(t.label_w, 536);
    check_toast_literals(&t, 640, 1);
    cp0_ui_overlay_toast(&deck, 640, 2, &t);
    CHECK_EQ(t.h, 76);
    check_toast_literals(&t, 640, 2);
    cp0_ui_overlay_toast(&pi, 480, 1, &t);
    CHECK_EQ(t.w, 420); CHECK_EQ(t.h, 33); CHECK_EQ(t.top, 6); CHECK_EQ(t.radius, 9);
    CHECK_EQ(t.font_px, 18); CHECK_EQ(t.label_w, 402);
    check_toast_literals(&t, 480, 1);
    cp0_ui_overlay_toast(&pi, 480, 2, &t);
    CHECK_EQ(t.h, 57);
    check_toast_literals(&t, 480, 2);
    /* the density does not move a pinned size */
    {
        const cp0_ui_metrics_t deck2 = metrics(640, 480, 1800);
        cp0_ui_toast_t t2;
        cp0_ui_overlay_toast(&deck2, 640, 1, &t2);
        CHECK_EQ(t2.w, 560);
        CHECK_EQ(t2.font_px, 24);
    }
}

static void test_pinned_osd_loading(void)
{
    const cp0_ui_metrics_t deck = metrics(640, 480, 0), pi = metrics(480, 320, 630);
    cp0_ui_osd_t o;
    cp0_ui_loading_t l;
    cp0_ui_overlay_osd(&deck, 640, 480, &o);
    CHECK_EQ(o.w, 380); CHECK_EQ(o.h, 164); CHECK_EQ(o.radius, 16); CHECK_EQ(o.border, 2);
    CHECK_EQ(o.font_px, 24); CHECK_EQ(o.icon_px, 28);
    CHECK_EQ(o.title.x, 96); CHECK_EQ(o.title.w, 152); CHECK_EQ(o.value.x, 248);
    CHECK_EQ(o.bar.x, 36); CHECK_EQ(o.bar.y, 100); CHECK_EQ(o.bar.w, 308); CHECK_EQ(o.bar.h, 20);
    CHECK_EQ(o.mute_title.w, 380);
    cp0_ui_overlay_loading(&deck, 640, 480, &l);
    CHECK_EQ(l.w, 400); CHECK_EQ(l.h, 80); CHECK_EQ(l.spinner, 48); CHECK_EQ(l.font_px, 28);
    CHECK_EQ(l.pad, 12); CHECK_EQ(l.gap, 16); CHECK_EQ(l.arc_w, 6); CHECK_EQ(l.radius, 16);
    cp0_ui_overlay_osd(&pi, 480, 320, &o);
    CHECK_EQ(o.w, 285); CHECK_EQ(o.h, 123); CHECK_EQ(o.radius, 12); CHECK_EQ(o.font_px, 18);
    CHECK_EQ(o.bar.w, 231); CHECK_EQ(o.bar.h, 15); CHECK_EQ(o.icon_px, 20);
    cp0_ui_overlay_loading(&pi, 480, 320, &l);
    CHECK_EQ(l.w, 300); CHECK_EQ(l.h, 60); CHECK_EQ(l.spinner, 36); CHECK_EQ(l.font_px, 21);
}

static void test_computed(void)
{
    static const struct { int w, h, ppmm; } sizes[] = {
        {800, 480, 920}, {720, 720, 1000}, {1280, 720, 1160}, {1024, 600, 660}, {854, 480, 900}, {400, 400, 1130},
    };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        const cp0_ui_metrics_t m = metrics(sizes[i].w, sizes[i].h, sizes[i].ppmm);
        cp0_ui_toast_t t1, t2, t3;
        cp0_ui_osd_t o;
        cp0_ui_loading_t l;
        CHECK_EQ(m.preset, CP0_UI_PRESET_COMPUTED);
        cp0_ui_overlay_toast(&m, sizes[i].w, 1, &t1);
        cp0_ui_overlay_toast(&m, sizes[i].w, 2, &t2);
        cp0_ui_overlay_toast(&m, sizes[i].w, 9, &t3);
        CHECK_EQ(t1.font_px, m.text_px[CP0_TEXT_BODY]);
        CHECK_EQ(t1.top, m.tok[CP0_TOK_SPACE_S]);
        CHECK(t1.w <= sizes[i].w - 2 * m.tok[CP0_TOK_SPACE_L]);
        CHECK(t1.w <= m.tok[CP0_TOK_DIALOG_MAX_W] || m.tok[CP0_TOK_DIALOG_MAX_W] <= 0);
        CHECK(t1.label_w < t1.w);
        CHECK(t1.h >= t1.font_px + 2 * m.tok[CP0_TOK_SPACE_S]);
        CHECK(t2.h > t1.h);
        CHECK(t3.h > t2.h);
        CHECK(t1.radius > 0 && t1.radius <= t1.h / 2);
        /* the toast is nowhere near the whole width of a wide display (it was 1120 px of 1280) */
        CHECK(t1.w * 2 <= sizes[i].w * 2 - 4 * m.tok[CP0_TOK_SPACE_L]);
        cp0_ui_overlay_osd(&m, sizes[i].w, sizes[i].h, &o);
        CHECK(o.w >= 190 && o.h >= 82);
        CHECK(o.w <= sizes[i].w - 2 * m.tok[CP0_TOK_SPACE_L] || o.w == 190);
        CHECK(o.h <= sizes[i].h - 2 * m.tok[CP0_TOK_SPACE_L] || o.h == 82);
        CHECK(o.bar.x + o.bar.w <= o.w);
        CHECK(o.value.x + o.value.w <= o.w);
        CHECK(o.title.x + o.title.w <= o.value.x + 1);
        CHECK(o.bar.y + o.bar.h <= o.h);
        CHECK(o.icon_px >= 14 && (o.icon_px % 2) == 0);
        cp0_ui_overlay_loading(&m, sizes[i].w, sizes[i].h, &l);
        CHECK(l.w >= 200 && l.h >= 40);
        CHECK(l.w <= sizes[i].w - 2 * m.tok[CP0_TOK_SPACE_L] || l.w == 200);
        CHECK(l.spinner + 2 * l.pad <= l.h);
        CHECK(l.font_px >= 14);
    }
}

static void test_monotonic_and_clamp(void)
{
    /* a larger body font never gives a smaller overlay (same display); a tiny display clamps the scale */
    int last_osd = 0, last_load = 0;
    for (int ppmm = 600; ppmm <= 2000; ppmm += 100) {
        const cp0_ui_metrics_t m = metrics(1280, 720, ppmm);
        cp0_ui_osd_t o;
        cp0_ui_loading_t l;
        cp0_ui_overlay_osd(&m, 1280, 720, &o);
        cp0_ui_overlay_loading(&m, 1280, 720, &l);
        CHECK(o.w >= last_osd);
        CHECK(l.w >= last_load);
        last_osd = o.w;
        last_load = l.w;
    }
    {
        const cp0_ui_metrics_t m = metrics(400, 300, 2000);
        cp0_ui_osd_t o;
        cp0_ui_loading_t l;
        cp0_ui_toast_t t;
        cp0_ui_overlay_osd(&m, 400, 300, &o);
        cp0_ui_overlay_loading(&m, 400, 300, &l);
        cp0_ui_overlay_toast(&m, 400, 1, &t);
        CHECK(o.w <= 400);
        CHECK(l.w <= 400);
        CHECK(t.w <= 400);
    }
}

int main(void)
{
    test_compat_branch();
    test_pinned_toast();
    test_pinned_osd_loading();
    test_computed();
    test_monotonic_and_clamp();
    if (failures) {
        fprintf(stderr, "test_ui_overlay: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ui_overlay: ok\n");
    return 0;
}
