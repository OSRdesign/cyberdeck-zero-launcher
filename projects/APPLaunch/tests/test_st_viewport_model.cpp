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
    return 0;
}
