/*
 * SPDX-License-Identifier: MIT
 *
 * Quick controls layout: from the page header's roller layout (the real layout service, cp0_ui_metrics.c and
 * cp0_ui_roller.c are linked) at the sizes of the harness scenes and the portrait panels: four cells inside the area,
 * no overlap, a finger-sized button (TARGET) everywhere, 2 x 2 on every size here, buttons not taller than wide.
 */

#include "../main/ui/page_app/quick_controls_layout.hpp"

#include <cstdio>
#include <cstdlib>

using namespace quick_controls_layout;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

static bool overlap(const Cell &a, const Cell &b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void check_size(int w, int h, int ppmm_x100)
{
    cp0_ui_metrics_t m;
    CHECK(cp0_ui_metrics_compute(w, h, 0, ppmm_x100, CP0_UI_DENSITY_DEFAULT, 0, &m) == 0);
    cp0_ui_roller_page_t page{};
    CHECK(cp0_ui_roller_page(&m, 0, &page) == 0);
    const Input in = input_from_page(page, &m);
    const Layout l = compute(in, page.esc_radius);
    std::printf("%dx%d: area %d,%d %dx%d -> %dx%d cells %dx%d, icon %d name %d sub %d %s\n", w, h, in.x, in.y, in.w,
                in.h, l.cols, l.rows, l.cell[0].w, l.cell[0].h, l.icon_px, l.name_px, l.sub_px,
                l.stacked ? "stacked" : "row");
    CHECK(l.valid);
    CHECK(l.cols == 2 && l.rows == 2); // every size of the scenes is 2 x 2
    const int target = m.tok[CP0_TOK_TARGET];
    for (int i = 0; i < kButtons; ++i) {
        const Cell &c = l.cell[i];
        CHECK(c.x >= in.x && c.y >= in.y && c.x + c.w <= in.x + in.w && c.y + c.h <= in.y + in.h); // inside the area
        CHECK(c.x + c.w <= w && c.y + c.h <= h);                                                    // on the screen
        CHECK(c.h >= target && c.w >= target);                                                      // a finger fits
        CHECK(c.y >= page.header_h);                                                                // under the header
        for (int j = i + 1; j < kButtons; ++j) CHECK(!overlap(c, l.cell[j]));
    }
    CHECK(l.cell[0].h <= l.cell[0].w * kTallPct / 100);
    CHECK(l.icon_px >= CP0_UI_FONT_MIN_PX && l.icon_px <= kIconMaxPx);
    if (l.stacked) // the three rows of a stacked button fit in the cell
        CHECK(l.icon_px + line_h(l.name_px) + line_h(l.sub_px) + 3 * l.pad <= l.cell[0].h + l.icon_px / 4 + 1);
}

static void test_scene_sizes()
{
    check_size(480, 320, 0);    // Pi 3A+
    check_size(640, 480, 1130); // the deck
    check_size(800, 480, 0);
    check_size(720, 720, 0);
    check_size(1280, 720, 0);
    check_size(480, 640, 0);    // portrait
    check_size(480, 800, 0);    // PDA mode
}

static void test_grid_order_and_one_column()
{
    cp0_ui_metrics_t m;
    CHECK(cp0_ui_metrics_compute(640, 480, 0, 1130, CP0_UI_DENSITY_DEFAULT, 0, &m) == 0);
    cp0_ui_roller_page_t page{};
    CHECK(cp0_ui_roller_page(&m, 0, &page) == 0);
    const Layout l = compute(input_from_page(page, &m), page.esc_radius);
    // row-major: Shutdown Reboot / Wi-Fi Bluetooth
    CHECK(l.cell[0].y == l.cell[1].y && l.cell[2].y == l.cell[3].y && l.cell[2].y > l.cell[0].y);
    CHECK(l.cell[0].x == l.cell[2].x && l.cell[1].x == l.cell[3].x && l.cell[1].x > l.cell[0].x);

    // an area too narrow for two fingers side by side: one column of four bars
    Input in;
    in.x = 10, in.y = 60, in.w = 170, in.h = 400, in.gap = 8, in.target = 44, in.name_px = 16, in.sub_px = 12;
    const Layout one = compute(in, 8);
    CHECK(one.valid && one.cols == 1 && one.rows == 4 && !one.stacked);
    for (int i = 1; i < kButtons; ++i) CHECK(one.cell[i].y > one.cell[i - 1].y && one.cell[i].x == one.cell[0].x);
    CHECK(one.cell[0].h <= kBarMaxTargets * in.target);

    // a nonsense area yields no layout (the page then shows nothing rather than crash)
    Input bad;
    CHECK(!compute(bad, 8).valid);
}

int main()
{
    test_scene_sizes();
    test_grid_order_and_one_column();
    std::printf("test_quick_controls_layout ok\n");
    return 0;
}
