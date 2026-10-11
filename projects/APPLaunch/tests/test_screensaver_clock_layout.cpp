/*
 * SPDX-License-Identifier: MIT
 *
 * Dimmed clock geometry (task 014 P4): the deck and the Pi 3A+ keep today's numbers, other sizes scale by
 * min(w / 640, h / 480).
 */

#include "../main/ui/model/screensaver_clock_layout.hpp"

#include <cstdio>
#include <cstdlib>

using namespace screensaver_clock_layout;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

static void check_geometry(int w, int h, int clock_px, int time_y, int date_px, int date_y)
{
    const Geometry g = compute(w, h);
    if (g.clock_px != clock_px || g.time_y != time_y || g.date_px != date_px || g.date_y != date_y) {
        std::fprintf(stderr, "%dx%d: got %d %d %d %d, want %d %d %d %d\n", w, h, g.clock_px, g.time_y, g.date_px,
                     g.date_y, clock_px, time_y, date_px, date_y);
        std::exit(1);
    }
}

int main()
{
    // pinned: the literals of the old code (deck: 180 / -30 / Montserrat 28 / +90; compact path: 120 / -20 / 18 / +60)
    check_geometry(640, 480, 180, -30, 28, 90);
    check_geometry(480, 320, 120, -20, 18, 60);
    // the old code drew the full-size clock on any canvas 400 px or more high, so 800x480 is unchanged (r = 1)
    check_geometry(800, 480, 180, -30, 28, 90);
    // computed growth
    check_geometry(720, 720, 202, -33, 30, 101);                            // r = 1.125: date 31 -> 30
    check_geometry(1280, 720, 270, -45, 42, 135);                    // r = 1.5
    check_geometry(1024, 600, 225, -37, 34, 112);                    // r = 1.25
    // builtin font limit: the date never leaves 8..48 (r = 2.25 at 1920x1080)
    CHECK(compute(1920, 1080).date_px == 48);
    CHECK(compute(1920, 1080).clock_px == 405);

    // every size: the clock block (time above centre + date below) stays inside the screen
    const int sizes[][2] = {{480, 320}, {640, 480}, {800, 480}, {720, 720}, {1280, 720}, {1024, 600}, {1920, 1080},
                            {480, 480}, {800, 320}};
    for (const auto &s : sizes) {
        const Geometry g = compute(s[0], s[1]);
        const int cy = s[1] / 2;
        CHECK(cy + g.time_y - g.clock_px / 2 >= 0);       // top of the time label
        CHECK(cy + g.date_y + g.date_px <= s[1]);          // bottom of the date label (generous: px as its height)
        CHECK(g.date_px >= kFontMinPx && g.date_px <= kFontMaxPx && g.date_px % 2 == 0);
    }
    CHECK(compute(0, 0).clock_px == 180);
    std::puts("test_screensaver_clock_layout OK");
    return 0;
}
