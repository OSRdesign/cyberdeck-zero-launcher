/*
 * SPDX-License-Identifier: MIT
 *
 * Layout of the Quick controls page. Pure, header-only, no LVGL: unit-tested on the PC by
 * tests/test_quick_controls_layout.cpp. Every number comes from the layout tokens (the page header's roller layout:
 * body area, gap, text sizes, TARGET), none from a pixel constant of its own.
 *
 *   grid     four buttons, 2 x 2 (Shutdown Reboot / Wi-Fi Bluetooth). A column of its own would leave a button under
 *            kMinCellTargets x TARGET wide, so a very narrow area gets 1 x 4 instead. The grid fills the area under
 *            the page's status line, buttons never taller than they are wide (kTallPct) so a portrait panel
 *            (480x640, 480x800) keeps square-ish buttons, centred in the area.
 *   button   stacked (icon over label over state line) when the cell is tall enough for the icon at its smallest
 *            plus the two lines, else icon left and the two lines right (the small panels' 2 x 2 and 1 x 4).
 *   icon     a Montserrat symbol glyph, the largest builtin size (even, up to 48 px) the cell leaves room for.
 */
#pragma once

#include "cp0_ui_metrics.h"
#include "cp0_ui_roller.h"

#include <algorithm>

namespace quick_controls_layout {

constexpr int kButtons = 4;
constexpr int kMinCellTargets = 2;  // a cell is at least this many TARGETs wide, else the grid is one column
constexpr int kTallPct = 100;       // a cell is at most this share of its width high (2 columns)
constexpr int kBarMaxTargets = 2;   // one column: a cell is at most this many TARGETs high
constexpr int kLineMultiplier = 13; // a text line is px * 13 / 10 high (x10)
constexpr int kIconMinPx = 24;      // the stacked layout needs an icon at least this big
constexpr int kIconMaxPx = CP0_UI_FONT_MAX_PX;

struct Cell {
    int x = 0, y = 0, w = 0, h = 0;
};

struct Input {
    int x = 0, y = 0, w = 0, h = 0;  // the area the grid may use (screen coordinates)
    int gap = 0;                     // between buttons (SPACE_M)
    int target = 0;                  // TARGET token
    int name_px = 0;                 // the label (BODY text)
    int sub_px = 0;                  // the state line (CAPTION text)
};

struct Layout {
    bool valid = false;
    int cols = 2, rows = 2;
    Cell cell[kButtons];
    bool stacked = true;
    int icon_px = 0, name_px = 0, sub_px = 0;
    int pad = 0;      // inside a button
    int dot_px = 0;   // the state dot (beside the state line)
    int radius = 0;
};

inline int line_h(int px)
{
    return px * kLineMultiplier / 10;
}

/* The grid's area from the page header's roller layout: the content column (roller_x / roller_w), under the page's
 * status line (kept free, so the buttons do not jump when a message comes), down to a gap above the screen's bottom. */
inline Input input_from_page(const cp0_ui_roller_page_t &page, const cp0_ui_metrics_t *metrics)
{
    Input in;
    in.gap = page.gap;
    in.target = metrics ? metrics->tok[CP0_TOK_TARGET] : page.row_h;
    in.name_px = metrics ? metrics->text_px[CP0_TEXT_BODY] : page.text_px;
    in.sub_px = page.caption_px;
    const int top = std::max(page.body_y, page.caption_y + line_h(page.caption_px));
    in.x = page.roller_x;
    in.w = page.roller_w;
    in.y = top + in.gap / 2;
    in.h = page.body_y + page.body_h - in.y - in.gap;
    return in;
}

inline Layout compute(const Input &in, int radius)
{
    Layout out;
    if (in.w <= 0 || in.h <= 0 || in.target <= 0) return out;
    out.name_px = cp0_ui_font_snap(in.name_px);
    out.sub_px = cp0_ui_font_snap(in.sub_px);
    out.pad = std::max(2, in.gap / 2);
    out.dot_px = std::max(8, out.sub_px * 7 / 10);
    out.radius = radius;

    out.cols = in.w < kMinCellTargets * in.target * 2 + in.gap ? 1 : 2;
    out.rows = kButtons / out.cols;
    const int cell_w = (in.w - (out.cols - 1) * in.gap) / out.cols;
    int cell_h = (in.h - (out.rows - 1) * in.gap) / out.rows;
    cell_h = std::min(cell_h, out.cols == 1 ? kBarMaxTargets * in.target : cell_w * kTallPct / 100);
    if (cell_w <= 0 || cell_h <= 0) return out;

    const int grid_w = out.cols * cell_w + (out.cols - 1) * in.gap;
    const int grid_h = out.rows * cell_h + (out.rows - 1) * in.gap;
    const int x0 = in.x + (in.w - grid_w) / 2;
    const int y0 = in.y + std::max(0, (in.h - grid_h) / 2);
    for (int i = 0; i < kButtons; ++i) {
        out.cell[i].x = x0 + (i % out.cols) * (cell_w + in.gap);
        out.cell[i].y = y0 + (i / out.cols) * (cell_h + in.gap);
        out.cell[i].w = cell_w;
        out.cell[i].h = cell_h;
    }

    const int lines = line_h(out.name_px) + line_h(out.sub_px);
    const int room = cell_h - lines - 3 * out.pad; // what the icon above the two lines may take
    if (out.cols == 2 && room >= kIconMinPx) {
        out.stacked = true;
        out.icon_px = cp0_ui_font_snap(std::min(kIconMaxPx, room));
    } else {
        out.stacked = false;
        out.icon_px = cp0_ui_font_snap(std::min(kIconMaxPx, std::max(CP0_UI_FONT_MIN_PX, cell_h * 55 / 100)));
    }
    out.valid = true;
    return out;
}

} // namespace quick_controls_layout
