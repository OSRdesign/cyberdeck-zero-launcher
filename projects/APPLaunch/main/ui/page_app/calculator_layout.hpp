/*
 * SPDX-License-Identifier: MIT
 *
 * Layout of the native Calculator (task 014 P4). Pure, header-only, no LVGL: unit-tested on the PC by
 * tests/test_calculator_layout.cpp.
 *
 * Two pinned presets return the numbers the page always drew, whatever the density, so the deck (640x480) and the
 * Pi 3A+ (480x320) stay pixel-identical:
 *   DECK     padding 10, top bar 44, display 104, gap 6, title 28 at (16, 6), radius 14, keys 32 / 28 (Esc)
 *   COMPACT  padding 6, top bar 36, display 64, gap 4, title 20 at (10, 8), radius 8, keys 20
 * Any other canvas is COMPUTED to fill the screen. The Calculator is exempt from the touch-target token (decision
 * 2026-10-18): its keys are as big as the room allows, never capped at or floored on TARGET.
 *   - header: the Settings page header (cp0::ui::Page: the home grid's title place, the Esc button, the status strip),
 *     HEADER_H high;
 *   - display: a source line and a result line right-aligned across the keypad's width, the result anchored at the
 *     bottom so it stays next to the keys; at most kDisplayPct of the room under the header, the biggest result font
 *     (up to the 48 px the font set has) that fits in it, and no taller than that font needs;
 *   - keypad: 6 x 4 keys over the full width between the SPACE_M margins and down to the bottom margin, gap SPACE_S,
 *     radius RADIUS_M; keys stretch as wide as the screen is, but never get taller than kKeyTallPct of their width
 *     (a very tall screen gives the rest to the display); label fonts from the key size, snapped to the font set.
 */
#pragma once

#include "cp0_ui_metrics.h"

namespace calculator_layout {

constexpr int kCols = 4;
constexpr int kRows = 6;
constexpr int kDisplayPct = 22;       // computed layouts: the display takes at most this share of the room under the
                                      // header (the deck preset gives it 104 of 426 px, 24 %), the keys the rest
constexpr int kMainMaxPx = 48;        // biggest result font (the largest builtin Montserrat; DejaVu is drawn as large)
constexpr int kMainMinPx = 20;        // smallest result font a display gets, even when the room is tighter
constexpr int kKeyTallPct = 150;      // a key is at most this share of its width high (very tall screens)
constexpr int kKeyLabelPct = 55;      // key label: this share of the key height (the deck preset: 32 of 48 px) ...
constexpr int kKeyLabelWPct = 45;     // ... and at most this share of its width ("Esc" stays well inside)
constexpr int kKeyLabelMinPx = 12;
constexpr int kMainStepShort = 12;    // result text up to this many bytes: the big font ...
constexpr int kMainStepMid = 18;      // ... up to this: the middle font, else the small one

struct Layout {
    bool computed = false;     // false: a pinned preset
    bool header_page = false;  // true: title, Esc and strip come from cp0::ui::Page (the Settings header)
    int w = 0, h = 0;

    // pinned only: the page draws its own title label and the status strip on the container
    int title_x = 0, title_y = 0, title_px = 0;

    // display: source line (small, grey) and result line (big, white), both right-aligned
    int display_top = 0, display_bottom = 0;
    int text_x = 0, text_w = 0;
    int src_y = 0, src_px = 0;
    int main_y = 0;            // pinned: top of the result label
    int main_bottom = 0;       // computed: bottom of the result label (0 for the pinned presets)
    int main_px[3] = {0, 0, 0}; // big, middle, small result font

    // keypad
    int kp_x = 0, kp_y = 0, kp_w = 0;
    int cell_w = 0, cell_h = 0, gap = 0;
    int radius = 0;
    int key_px = 0;            // digits, operators, symbols (and the DejaVu x and divide signs)
    int esc_px = 0;            // the Esc key

    // x / y / width of the key at (col, row) spanning span columns (the cells are laid out as before)
    int cell_x(int col) const { return kp_x + col * (cell_w + gap); }
    int cell_y(int row) const { return kp_y + row * (cell_h + gap); }
    int cell_span_w(int span) const { return span * cell_w + (span - 1) * gap; }
    int keypad_bottom() const { return cell_y(kRows - 1) + cell_h; }
};

// Upper bound of a text line's height for a font of px (Montserrat and DejaVu are about 1.17 x px).
constexpr int text_h(int px) { return px * 6 / 5 + 2; }

constexpr int mini(int a, int b) { return a < b ? a : b; }
constexpr int maxi(int a, int b) { return a > b ? a : b; }

// Computed layouts: the source line's font for a result font (7/12 of it, at least the caption size).
inline int display_src_px(const cp0_ui_metrics_t &m, int main_px)
{
    return cp0_ui_font_snap(maxi(main_px * 7 / 12, m.text_px[CP0_TEXT_CAPTION]));
}

// Computed layouts: the display height a result font needs (SPACE_S, the source line, the result line, SPACE_M).
inline int display_need(const cp0_ui_metrics_t &m, int main_px)
{
    return m.tok[CP0_TOK_SPACE_S] + text_h(display_src_px(m, main_px)) + text_h(main_px) + m.tok[CP0_TOK_SPACE_M];
}

// The old code, kept as it was (480x320 compact: height < 400, else the 640x480 numbers).
inline Layout pinned(int w, int h, bool compact)
{
    Layout l;
    l.w = w;
    l.h = h;
    const int pad = compact ? 6 : 10;
    const int top_bar = compact ? 36 : 44;
    const int display_h = compact ? 64 : 104;
    l.gap = compact ? 4 : 6;
    l.title_px = compact ? 20 : 28;
    l.title_x = compact ? 10 : 16;
    l.title_y = compact ? 8 : 6;
    l.text_x = pad;
    l.text_w = w - 2 * pad - 8;
    l.display_top = top_bar;
    l.display_bottom = top_bar + display_h;
    l.src_y = top_bar + 2;
    l.src_px = compact ? 18 : 28;
    l.main_y = top_bar + (compact ? 22 : 38);
    l.main_px[0] = compact ? 30 : 48;
    l.main_px[1] = compact ? 24 : 36;
    l.main_px[2] = compact ? 20 : 28;
    l.kp_x = pad;
    l.kp_y = top_bar + display_h;
    l.cell_w = (w - 2 * pad - (kCols - 1) * l.gap) / kCols;
    l.cell_h = (h - l.kp_y - pad - (kRows - 1) * l.gap) / kRows;
    l.kp_w = kCols * l.cell_w + (kCols - 1) * l.gap;
    l.radius = compact ? 8 : 14;
    l.key_px = compact ? 20 : 32;
    l.esc_px = compact ? 20 : 28;
    return l;
}

inline Layout computed(const cp0_ui_metrics_t &m)
{
    Layout l;
    l.computed = true;
    l.header_page = true;
    l.w = m.w;
    l.h = m.h;
    const int space_s = m.tok[CP0_TOK_SPACE_S];
    const int space_m = m.tok[CP0_TOK_SPACE_M];
    const int header_h = m.tok[CP0_TOK_HEADER_H];

    l.gap = space_s;
    l.radius = m.tok[CP0_TOK_RADIUS_M];
    const int margin = space_m;
    // the keypad and the display take the full width between the margins
    const int column_w = m.w - 2 * margin;
    l.text_x = margin + space_m;
    l.text_w = column_w - 2 * space_m;

    // display: the biggest result font that fits in kDisplayPct of the room, and only the height that font needs
    const int rows_gap = (kRows - 1) * l.gap;
    const int body_h = m.h - header_h - margin;
    const int display_room = body_h * kDisplayPct / 100;
    int main_px = kMainMaxPx;
    while (main_px > kMainMinPx && display_need(m, main_px) > display_room) main_px -= 2;
    const int display_h = display_need(m, main_px);
    const int src_px = display_src_px(m, main_px);

    // keys: everything else, as wide as the screen (stretched keys are fine), never taller than kKeyTallPct of their
    // width (the extra height goes to the display)
    l.cell_w = maxi((column_w - (kCols - 1) * l.gap) / kCols, 1);
    l.cell_h = maxi((body_h - display_h - rows_gap) / kRows, 1);
    l.cell_h = mini(l.cell_h, maxi(l.cell_w * kKeyTallPct / 100, 1));
    l.kp_w = kCols * l.cell_w + (kCols - 1) * l.gap;
    l.kp_x = (m.w - l.kp_w) / 2;
    // the rounding rest and any height the keys do not take go to the display (the keypad stays against the bottom
    // margin)
    const int keys_h = kRows * l.cell_h + rows_gap;
    l.kp_y = m.h - margin - keys_h;
    l.display_top = header_h;
    l.display_bottom = l.kp_y;
    l.main_px[0] = main_px;
    l.main_px[1] = cp0_ui_font_snap(main_px * 3 / 4);
    l.main_px[2] = cp0_ui_font_snap(main_px * 58 / 100);
    l.src_px = src_px;
    l.main_bottom = l.display_bottom - space_m;
    l.src_y = l.main_bottom - text_h(main_px) - text_h(src_px);
    l.main_y = l.main_bottom - text_h(main_px);
    // key labels from the key: kKeyLabelPct of its height, at most kKeyLabelWPct of its width, snapped to the font set
    // (every even size 8..48), never taller than the key
    int key_px = mini(l.cell_h * kKeyLabelPct / 100, l.cell_w * kKeyLabelWPct / 100);
    key_px = cp0_ui_font_snap(maxi(key_px, kKeyLabelMinPx));
    while (key_px > CP0_UI_FONT_MIN_PX && text_h(key_px) > l.cell_h) key_px -= 2;
    l.key_px = key_px;
    l.esc_px = cp0_ui_font_snap(l.key_px * 7 / 8);
    return l;
}

// The layout for a canvas: the pinned preset of the metrics, else computed from them.
inline Layout compute(const cp0_ui_metrics_t &m)
{
    if (m.preset == CP0_UI_PRESET_DECK) return pinned(m.w, m.h, false);
    if (m.preset == CP0_UI_PRESET_COMPACT) return pinned(m.w, m.h, true);
    return computed(m);
}

} // namespace calculator_layout
