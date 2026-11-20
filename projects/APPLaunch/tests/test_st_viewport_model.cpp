/*
 * SPDX-License-Identifier: MIT
 *
 * The terminal grid under the on-screen keyboard (task 014 P4b, main/ui/model/st_viewport_model.hpp).
 */

#include "../main/ui/model/st_viewport_model.hpp"

#include <cassert>

int main()
{
    using st_viewport::resize;
    using st_viewport::rows_for;

    // no keyboard: the whole screen's rows
    static_assert(rows_for(12, 320, 0, 26) == 12);
    static_assert(rows_for(12, 320, -5, 26) == 12);
    static_assert(rows_for(12, 320, 189, 0) == 12);

    // the five harness sizes with the keyboard heights of task 014 P2b (JetBrains Mono Bold 20 px: 26 px rows here;
    // the page measures the real cell from the font)
    assert(rows_for(12, 320, 189, 26) == 5);  /* 480x320: 131 px left */
    assert(rows_for(18, 480, 299, 26) == 6);  /* 640x480 deck: 181 px */
    assert(rows_for(18, 480, 236, 26) == 9);  /* 800x480: 244 px */
    assert(rows_for(24, 720, 356, 26) == 14); /* 720x720: 364 px */
    assert(rows_for(24, 720, 320, 26) == 15); /* 1280x720: 400 px */

    // never fewer than kMinRows, never more than the full screen
    assert(rows_for(12, 320, 310, 26) == st_viewport::kMinRows);
    assert(rows_for(12, 320, 400, 26) == st_viewport::kMinRows);
    assert(rows_for(1, 320, 300, 26) == 1);
    assert(rows_for(8, 720, 10, 26) == 8);

    // shrink with the prompt on row 8 of 12 to 5 rows: 4 lines into the scrollback, the prompt on the last row
    constexpr auto shrink = resize(12, 5, 8, 0, 30);
    static_assert(shrink.scroll_up == 4 && shrink.pull_back == 0 && shrink.cursor_y == 4 && shrink.pushed == 4);
    // the prompt already fits: nothing moves (the lines under the cursor are dropped by the page)
    constexpr auto fits = resize(12, 5, 2, 0, 30);
    static_assert(fits.scroll_up == 0 && fits.cursor_y == 2 && fits.pushed == 0);
    // the prompt on the last row of a full screen
    constexpr auto bottom = resize(12, 5, 11, 0, 0);
    static_assert(bottom.scroll_up == 7 && bottom.cursor_y == 4 && bottom.pushed == 7);

    // grow back: the pushed lines return, the prompt where it was
    constexpr auto grow = resize(5, 12, 4, 4, 34);
    static_assert(grow.pull_back == 4 && grow.scroll_up == 0 && grow.cursor_y == 8 && grow.pushed == 0);
    // only as many as the new rows take (two steps), and as many as the scrollback still holds
    constexpr auto part = resize(5, 7, 4, 4, 34);
    static_assert(part.pull_back == 2 && part.cursor_y == 6 && part.pushed == 2);
    constexpr auto trimmed = resize(5, 12, 4, 7, 3);
    static_assert(trimmed.pull_back == 3 && trimmed.cursor_y == 7 && trimmed.pushed == 4);
    // nothing pushed (the session started with the keyboard up, or after a reset): blank rows at the bottom
    constexpr auto fresh = resize(5, 12, 1, 0, 50);
    static_assert(fresh.pull_back == 0 && fresh.cursor_y == 1 && fresh.pushed == 0);

    // a round trip restores the cursor row
    constexpr auto down = resize(18, 6, 15, 0, 100);
    constexpr auto up = resize(6, 18, down.cursor_y, down.pushed, 100 + down.scroll_up);
    static_assert(up.cursor_y == 15 && up.pushed == 0);

    // no change, bad input
    constexpr auto same = resize(12, 12, 3, 2, 10);
    static_assert(same.scroll_up == 0 && same.pull_back == 0 && same.cursor_y == 3 && same.pushed == 2);
    constexpr auto bad = resize(12, 0, 3, -4, 10);
    static_assert(bad.scroll_up == 0 && bad.pull_back == 0 && bad.cursor_y == 3 && bad.pushed == 0);
    // the full-screen terminal: bigger than the 320x170 compat canvas in either direction (study portrait-320, PR-A)
    static_assert(!st_viewport::native_display(320, 170));  /* the compat display and SDL */
    static_assert(st_viewport::native_display(320, 480));   /* zero8 portrait: 320 wide, but not the compat canvas */
    static_assert(st_viewport::native_display(480, 320));
    static_assert(st_viewport::native_display(640, 480));
    static_assert(st_viewport::native_display(480, 640));
    static_assert(st_viewport::native_display(480, 800));
    static_assert(st_viewport::native_display(800, 1280));
    static_assert(st_viewport::native_display(321, 170));
    static_assert(st_viewport::native_display(320, 171));
    static_assert(!st_viewport::native_display(240, 160));

    // the terminal font: 14 px (40 columns) on a narrow portrait display, 20 px everywhere else
    static_assert(st_viewport::font_px(320, 480) == 14);
    static_assert(st_viewport::font_px(240, 320) == 14);
    static_assert(st_viewport::font_px(480, 800) == 20);
    static_assert(st_viewport::font_px(480, 640) == 20);
    static_assert(st_viewport::font_px(480, 320) == 20);
    static_assert(st_viewport::font_px(640, 480) == 20);
    static_assert(st_viewport::font_px(320, 170) == 20);
    static_assert(st_viewport::font_px(800, 1280) == 20);
    static_assert(st_viewport::font_px(479, 800) == 14);  /* the shared narrow-portrait predicate (cp0_ui_metrics.h) */
    static_assert(st_viewport::font_px(360, 640) == 14);
    static_assert(st_viewport::font_px(320, 320) == 20);  /* not taller than wide */
    static_assert(CP0_UI_NARROW_PORTRAIT_SIZE(320, 480) && !CP0_UI_NARROW_PORTRAIT_SIZE(480, 640));

    // Settings > Screen > Terminal text size (2026-11-18): the font the terminal opens with. Precedence: the setting
    // (stored px, 0 = Auto) over board.conf's APPLAUNCH_TERM_FONT over the display's default.
    using st_viewport::font_px_resolved;
    assert(font_px_resolved(480, 800, 0, nullptr) == 20);       /* Auto, nothing in board.conf: today's default */
    assert(font_px_resolved(320, 480, 0, nullptr) == 14);       /* Auto on the narrow portrait: 14 */
    assert(font_px_resolved(480, 800, 0, "") == 20);
    assert(font_px_resolved(480, 800, 0, "17") == 17);          /* Auto honours board.conf */
    assert(font_px_resolved(320, 480, 0, "24") == 24);
    assert(font_px_resolved(480, 800, 0, "9") == 20);           /* board.conf out of range: ignored, as before */
    assert(font_px_resolved(480, 800, 0, "49") == 20);
    assert(font_px_resolved(480, 800, 0, "abc") == 20);
    assert(font_px_resolved(480, 800, 14, nullptr) == 14);      /* the setting */
    assert(font_px_resolved(480, 800, 14, "24") == 14);         /* the setting wins over board.conf */
    assert(font_px_resolved(320, 480, 24, "17") == 24);
    assert(font_px_resolved(480, 800, 5, "17") == 17);          /* a stored value out of range is Auto */
    assert(font_px_resolved(480, 800, -3, nullptr) == 20);
    static_assert(st_viewport::font_px_valid(10) && st_viewport::font_px_valid(48));
    static_assert(!st_viewport::font_px_valid(9) && !st_viewport::font_px_valid(49));

    // the grid (the terminal's own clamps: 20..80 columns, 8..24 rows)
    static_assert(st_viewport::grid_cols(480, 8, st_viewport::kMaxCols) == 60);
    static_assert(st_viewport::grid_cols(800, 8, st_viewport::kMaxCols) == 80);   /* clamped to BIG_COLS */
    static_assert(st_viewport::grid_cols(100, 12, st_viewport::kMaxCols) == 20);  /* at least 20 */
    static_assert(st_viewport::grid_rows(800, 18, st_viewport::kMaxRows) == 24);  /* clamped to BIG_ROWS */
    static_assert(st_viewport::grid_rows(320, 26, st_viewport::kMaxRows) == 12);
    static_assert(st_viewport::grid_rows(100, 26, st_viewport::kMaxRows) == 8);   /* at least 8 */
    static_assert(st_viewport::grid_cols(480, 0, st_viewport::kMaxCols) == 20);   /* no cell: the minimum, no division */

    // the 80x24 cap only up to 720x720 px (owner decision 2026-11-20): 800x480 and 720x720 (and their portraits)
    // capped, anything larger lifted up to the 255 x 255 safety bound
    using st_viewport::grid_capped;
    static_assert(grid_capped(800, 480) && grid_capped(480, 800) && grid_capped(720, 720));
    static_assert(grid_capped(640, 480) && grid_capped(480, 320) && grid_capped(320, 170) && grid_capped(800, 600));
    static_assert(grid_capped(721, 719) && !grid_capped(721, 720) && !grid_capped(720, 721));
    static_assert(!grid_capped(1024, 600) && !grid_capped(600, 1024) && !grid_capped(1280, 720));
    static_assert(!grid_capped(1280, 800) && !grid_capped(1920, 1080) && !grid_capped(1080, 1920));
    static_assert(st_viewport::max_cols_for(800, 480) == 80 && st_viewport::max_rows_for(480, 800) == 24);
    static_assert(st_viewport::max_cols_for(1024, 600) == 255 && st_viewport::max_rows_for(1024, 600) == 255);
    static_assert(st_viewport::grid_cols(1920, 8, st_viewport::max_cols_for(1920, 1080)) == 240);
    static_assert(st_viewport::grid_cols(4000, 8, st_viewport::max_cols_for(4000, 2000)) == 255); /* the safety bound */
    static_assert(st_viewport::grid_rows(2000, 4, st_viewport::max_rows_for(4000, 2000)) == 255);

    return 0;
}
