/*
 * SPDX-License-Identifier: MIT
 *
 * Geometry of the dimmed clock (DarkTime screensaver, task 014 P4). Pure, header-only, no LVGL: unit-tested on the PC by
 * tests/test_screensaver_clock_layout.cpp.
 *
 * The clock was designed for the 640x480 deck: a 180 px bold time 30 px above the centre and a 28 px date 90 px below
 * it. One ratio r = min(w / 640, h / 480) scales all of it, so the 640x480 deck (r = 1) and the 480x320 Pi 3A+
 * (r = 2/3: 120 px time, 18 px date) keep exactly their numbers and a larger or squarer screen grows the clock in the
 * same proportion (720x720: 202 px, 1280x720: 270 px). Integer maths, as the compact path always did.
 */
#pragma once

namespace screensaver_clock_layout {

constexpr int kDesignW = 640;
constexpr int kDesignH = 480;
constexpr int kClockPx = 180;
constexpr int kTimeOffsetY = -30; // from the screen centre
constexpr int kDateOffsetY = 90;
constexpr int kDatePx = 28;
constexpr int kFontMinPx = 8;  // the builtin Montserrat set: even sizes 8..48
constexpr int kFontMaxPx = 48;

struct Geometry {
    int clock_px;  // FreeType bold time
    int time_y;    // offset of the time label's centre from the screen centre
    int date_px;   // builtin Montserrat (snapped even size)
    int date_y;
};

// The builtin font size for px: even (an odd px rounds down), 8..48.
constexpr int snap_font_px(int px)
{
    return px < kFontMinPx ? kFontMinPx : px > kFontMaxPx ? kFontMaxPx : (px & ~1);
}

// min(w / 640, h / 480) as num / den, exact.
constexpr int ratio_num(int w, int h) { return h * kDesignW <= w * kDesignH ? h : w; }
constexpr int ratio_den(int w, int h) { return h * kDesignW <= w * kDesignH ? kDesignH : kDesignW; }

inline Geometry compute(int w, int h)
{
    if (w <= 0 || h <= 0) return {kClockPx, kTimeOffsetY, kDatePx, kDateOffsetY};
    const int num = ratio_num(w, h);
    const int den = ratio_den(w, h);
    return {kClockPx * num / den, kTimeOffsetY * num / den, snap_font_px(kDatePx * num / den),
            kDateOffsetY * num / den};
}

} // namespace screensaver_clock_layout
