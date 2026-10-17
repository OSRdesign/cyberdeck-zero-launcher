/*
 * SPDX-License-Identifier: MIT
 *
 * Calculator layout (task 014 P4): the deck and the Pi 3A+ presets equal the literals the page always drew; computed
 * canvases leave no overlap (header, display, keypad), keep the keypad inside the screen and fill it (decision
 * 2026-10-18: the Calculator is exempt from the touch-target token; the keys take all the room the display leaves).
 * Links ext_components/cp0_lvgl/src/cp0_ui_metrics.c (the real tokens).
 */

#include "../main/ui/page_app/calculator_layout.hpp"

#include <cstdio>
#include <cstdlib>

using namespace calculator_layout;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

static cp0_ui_metrics_t metrics(int w, int h, int ppmm_x100)
{
    cp0_ui_metrics_t m;
    CHECK(cp0_ui_metrics_compute(w, h, 0, ppmm_x100, CP0_UI_DENSITY_DEFAULT, 0, &m) == 0);
    return m;
}

static void test_deck()
{
    const Layout l = compute(metrics(640, 480, 1130));
    CHECK(!l.computed && !l.header_page);
    CHECK(l.title_px == 28 && l.title_x == 16 && l.title_y == 6);
    CHECK(l.text_x == 10 && l.text_w == 640 - 20 - 8);
    CHECK(l.src_y == 46 && l.src_px == 28);
    CHECK(l.main_y == 82 && l.main_bottom == 0);
    CHECK(l.main_px[0] == 48 && l.main_px[1] == 36 && l.main_px[2] == 28);
    CHECK(l.gap == 6 && l.kp_x == 10 && l.kp_y == 148);
    CHECK(l.cell_w == 150 && l.cell_h == 48);
    CHECK(l.radius == 14 && l.key_px == 32 && l.esc_px == 28);
    CHECK(l.cell_x(3) == 10 + 3 * 156 && l.cell_y(5) == 148 + 5 * 54);
    CHECK(l.cell_span_w(3) == 3 * 150 + 2 * 6);
    // the density does not move a pinned preset
    const Layout dense = compute(metrics(640, 480, 2000));
    CHECK(dense.cell_h == l.cell_h && dense.src_y == l.src_y && dense.key_px == l.key_px);
}

static void test_compact()
{
    const Layout l = compute(metrics(480, 320, 630));
    CHECK(!l.computed && !l.header_page);
    CHECK(l.title_px == 20 && l.title_x == 10 && l.title_y == 8);
    CHECK(l.text_x == 6 && l.text_w == 480 - 12 - 8);
    CHECK(l.src_y == 38 && l.src_px == 18);
    CHECK(l.main_y == 58 && l.main_bottom == 0);
    CHECK(l.main_px[0] == 30 && l.main_px[1] == 24 && l.main_px[2] == 20);
    CHECK(l.gap == 4 && l.kp_x == 6 && l.kp_y == 100);
    CHECK(l.cell_w == (480 - 12 - 12) / 4 && l.cell_h == (320 - 100 - 6 - 20) / 6);
    CHECK(l.radius == 8 && l.key_px == 20 && l.esc_px == 20);
}

static void check_computed(int w, int h, int ppmm_x100)
{
    const cp0_ui_metrics_t m = metrics(w, h, ppmm_x100);
    const Layout l = compute(m);
    std::printf("  %dx%d @%d: header %d display %d..%d keypad x %d w %d y %d key %dx%d gap %d font %d result %d/%d/%d src %d\n",
                w, h, ppmm_x100, m.tok[CP0_TOK_HEADER_H], l.display_top, l.display_bottom, l.kp_x, l.kp_w, l.kp_y,
                l.cell_w, l.cell_h, l.gap, l.key_px, l.main_px[0], l.main_px[1], l.main_px[2], l.src_px);
    CHECK(l.computed && l.header_page);
    const int header_h = m.tok[CP0_TOK_HEADER_H];
    const int space_s = m.tok[CP0_TOK_SPACE_S];
    // header above the display, source above the result, result above the keys, never touching
    CHECK(l.display_top == header_h);
    CHECK(l.src_y >= header_h + space_s);
    CHECK(l.src_y + text_h(l.src_px) <= l.main_bottom - text_h(l.main_px[0]));
    CHECK(l.main_bottom <= l.display_bottom);
    CHECK(l.display_bottom <= l.kp_y);
    CHECK(l.display_bottom - l.main_bottom >= m.tok[CP0_TOK_SPACE_M]);
    // keypad inside the screen and centred
    const int margin = m.tok[CP0_TOK_SPACE_M];
    CHECK(l.kp_x >= 0 && l.kp_x + l.kp_w <= w);
    CHECK(l.kp_x == w - l.kp_x - l.kp_w || l.kp_x + 1 == w - l.kp_x - l.kp_w);
    CHECK(l.kp_w == kCols * l.cell_w + (kCols - 1) * l.gap);
    CHECK(l.keypad_bottom() <= h);
    CHECK(l.keypad_bottom() == h - margin);
    // the keypad fills the screen: full width between the margins (less the rounding of 4 columns), and the display
    // takes no more than its biggest font needs (unless the keys hit their height cap) and no more than kDisplayPct
    // of the room (unless even the smallest font needs more), the keys all the rest (less the rounding of 6 rows)
    const int free_w = w - 2 * margin;
    const int body_h = h - header_h - margin;
    CHECK(l.kp_w > free_w - kCols);
    const int display_h = l.display_bottom - l.display_top;
    const bool tall_capped = l.cell_h == l.cell_w * kKeyTallPct / 100;
    CHECK(tall_capped || display_h < display_need(m, l.main_px[0]) + kRows);
    CHECK(display_h <= body_h * kDisplayPct / 100 + kRows || l.main_px[0] == kMainMinPx || tall_capped);
    CHECK(display_h >= display_need(m, l.main_px[0]));
    // a bigger result font would not have fit in the display share (the display font scales with the space)
    CHECK(l.main_px[0] == kMainMaxPx || display_need(m, l.main_px[0] + 2) > body_h * kDisplayPct / 100);
    // nothing of the TARGET token: keys are not capped or floored on it, only on their own width
    CHECK(l.cell_h <= l.cell_w * kKeyTallPct / 100);
    CHECK(l.cell_h >= 40); // no key under 40 px on any of these canvases
    CHECK(l.text_x >= 0 && l.text_x + l.text_w <= w && l.text_w >= l.kp_w - 2 * margin - kCols);
    // the labels fit in the keys and in the display
    CHECK(text_h(l.key_px) <= l.cell_h);
    CHECK(l.key_px <= l.cell_w * kKeyLabelWPct / 100 + 1 && l.key_px % 2 == 0 && l.key_px <= CP0_UI_FONT_MAX_PX);
    CHECK(l.esc_px <= l.key_px && l.esc_px % 2 == 0);
    CHECK(l.main_px[0] >= l.main_px[1] && l.main_px[1] >= l.main_px[2] && l.main_px[2] >= 8);
    CHECK(l.src_px < l.main_px[0]);
    CHECK(l.gap == space_s && l.radius == m.tok[CP0_TOK_RADIUS_M]);
}

// The pinned presets fill the screen too (their padding is the margin): no more than the rounding of the keys left.
static void check_pinned_fill(int w, int h, int ppmm_x100, int pad)
{
    const Layout l = compute(metrics(w, h, ppmm_x100));
    std::printf("  %dx%d pinned: keypad x %d w %d y %d key %dx%d\n", w, h, l.kp_x, l.kp_w, l.kp_y, l.cell_w, l.cell_h);
    CHECK(!l.computed);
    CHECK(l.kp_x == pad && l.kp_w > w - 2 * pad - kCols);
    CHECK(l.keypad_bottom() <= h - pad && l.keypad_bottom() > h - pad - kRows);
}

int main()
{
    test_deck();
    test_compact();
    check_pinned_fill(640, 480, 1130, 10);
    check_pinned_fill(480, 320, 630, 6);
    std::puts("computed layouts:");
    check_computed(800, 480, 920);
    check_computed(800, 480, 0);
    check_computed(720, 720, 1000);
    check_computed(1280, 720, 1160);
    check_computed(1280, 720, 0);
    check_computed(1024, 600, 660);
    check_computed(1024, 600, 1130);
    check_computed(1920, 1080, 1130);
    check_computed(1920, 1080, 660);
    check_computed(800, 600, 1130); // 800x600 is not a pinned size
    check_computed(480, 800, 1130); // portrait
    check_computed(480, 480, 1130);
    check_computed(480, 1280, 1130); // very tall: the keys stop at kKeyTallPct, the display takes the rest
    {
        // the wide keypad is not capped any more: on 1280x720 the keys span the screen, wider than the old 640 px
        const Layout l = compute(metrics(1280, 720, 1160));
        CHECK(l.kp_w > 1200 && l.cell_w > 2 * l.cell_h);
        // the density changes the tokens (margins, gaps) only: no TARGET-based key height
        const Layout lo = compute(metrics(1024, 600, 660));
        const Layout hi = compute(metrics(1024, 600, 1130));
        CHECK(std::abs(lo.cell_h - hi.cell_h) <= 4);
    }
    std::puts("test_calculator_layout OK");
    return 0;
}
