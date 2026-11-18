/*
 * SPDX-License-Identifier: MIT
 *
 * The terminal grid under the fallback on-screen keyboard (task 014 P4b): pure rules, no LVGL, unit-tested on the PC
 * (tests/test_st_viewport_model.cpp). UISTPage (page_app/ui_app_st*.cpp) applies them.
 *
 *   rows     while the keyboard is up the grid keeps the rows that fit above it (its height is the bottom inset the
 *            keyboard service publishes), at least kMinRows, never more than the full screen's; the PTY is resized to
 *            them (TIOCSWINSZ through the PTY service's Resize: the shell, ssh and full-screen programs get SIGWINCH)
 *   shrink   the cursor line stays the grid's last visible row at most: the lines above it that no longer fit go into
 *            the scrollback (as a terminal window made shorter), the lines under the cursor are dropped
 *   grow     the keyboard went: the lines the shrinks pushed into the scrollback come back at the top (as many as the
 *            new rows take and the scrollback still holds), the cursor moves down with them; new rows are blank
 */
#pragma once

#include "cp0_ui_metrics.h" /* CP0_UI_NARROW_PORTRAIT_SIZE: the one narrow-portrait predicate */

#include <cstdlib>

namespace st_viewport {

/* The compat canvas (320x170): the terminal there is the old fixed grid with the top bar and no keyboard. */
constexpr int kCompatW = 320;
constexpr int kCompatH = 170;

/* Full-screen (native) terminal: the display is bigger than the compat canvas in either direction. Not the width alone:
 * the 320x480 portrait canvas is 320 wide and needs the native terminal and the on-screen keyboard (study portrait-320). */
constexpr bool native_display(int w, int h)
{
    return w > kCompatW || h > kCompatH;
}

/* Terminal font in px: 20, except a narrow portrait display (taller than wide, under 480 wide): 14 px = 40 columns. */
constexpr int font_px(int w, int h)
{
    return CP0_UI_NARROW_PORTRAIT_SIZE(w, h) ? 14 : 20;
}

/* A terminal font size px is usable from kMinFontPx to kMaxFontPx (board.conf's APPLAUNCH_TERM_FONT and Settings >
 * Screen > Terminal text size). */
constexpr int kMinFontPx = 10;
constexpr int kMaxFontPx = 48;
constexpr bool font_px_valid(int px)
{
    return px >= kMinFontPx && px <= kMaxFontPx;
}

/* The font size the terminal opens with: the Settings choice `stored` (0 = Auto, anything unusable = Auto) first, then
 * board.conf's APPLAUNCH_TERM_FONT (`env`, atoi as the terminal always read it; null, empty or out of range: not set),
 * then the display's own default (font_px above). Auto therefore keeps honouring board.conf. */
inline int font_px_resolved(int w, int h, int stored, const char *env)
{
    if (font_px_valid(stored)) return stored;
    if (env && *env) {
        const int value = std::atoi(env);
        if (font_px_valid(value)) return value;
    }
    return font_px(w, h);
}

/* The grid of the full screen for a font cell: the terminal's own clamps (UISTPage::init_geometry: at least 20 columns
 * and 8 rows, at most max_cols x max_rows, its BIG_COLS x BIG_ROWS). */
constexpr int kMaxCols = 80; /* UISTPage::BIG_COLS x BIG_ROWS (static_assert in ui_app_st.cpp) */
constexpr int kMaxRows = 24;
constexpr int kMinCols = 20;
constexpr int kMinGridRows = 8;
constexpr int clamp_to(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}
constexpr int grid_cols(int screen_w, int cell_w, int max_cols)
{
    return clamp_to(cell_w > 0 ? screen_w / cell_w : kMinCols, kMinCols, max_cols);
}
constexpr int grid_rows(int screen_h, int cell_h, int max_rows)
{
    return clamp_to(cell_h > 0 ? screen_h / cell_h : kMinGridRows, kMinGridRows, max_rows);
}

/* fewest rows a terminal is given above the keyboard (a prompt line and the line under it) */
constexpr int kMinRows = 2;

/* Rows of the grid: full_rows (the whole screen) without the keyboard, else those that fit in screen_h - inset. */
constexpr int rows_for(int full_rows, int screen_h, int inset, int cell_h)
{
    if (inset <= 0 || cell_h <= 0 || full_rows <= 0) return full_rows;
    const int fit = (screen_h - inset) / cell_h;
    const int low = full_rows < kMinRows ? full_rows : kMinRows;
    return fit < low ? low : fit > full_rows ? full_rows : fit;
}

struct Resize {
    int scroll_up;  /* shrink: top lines moved into the scrollback */
    int pull_back;  /* grow: scrollback lines moved back to the top */
    int cursor_y;   /* the cursor's row afterwards */
    int pushed;     /* lines the keyboard pushed that may come back later */
};

/* What a change from old_rows to new_rows does with the cursor on cursor_y, `pushed` lines pushed by earlier shrinks
 * and `history` lines in the scrollback. */
constexpr Resize resize(int old_rows, int new_rows, int cursor_y, int pushed, int history)
{
    Resize r{0, 0, cursor_y, pushed < 0 ? 0 : pushed};
    if (new_rows <= 0 || old_rows <= 0 || new_rows == old_rows) return r;
    if (new_rows < old_rows) {
        r.scroll_up = cursor_y - (new_rows - 1) > 0 ? cursor_y - (new_rows - 1) : 0;
        r.cursor_y = cursor_y - r.scroll_up;
        r.pushed += r.scroll_up;
        return r;
    }
    int pull = new_rows - old_rows;
    if (pull > r.pushed) pull = r.pushed;
    if (pull > history) pull = history;
    if (pull < 0) pull = 0;
    r.pull_back = pull;
    r.cursor_y = cursor_y + pull;
    r.pushed -= pull;
    return r;
}

} // namespace st_viewport
