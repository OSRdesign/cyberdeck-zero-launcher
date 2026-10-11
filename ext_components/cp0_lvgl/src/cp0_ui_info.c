/*
 * SPDX-License-Identifier: MIT
 *
 * InfoRow layout (see cp0_ui_info.h). Pure C: no LVGL.
 */

#include "cp0_ui_info.h"

#include "cp0_ui_metrics.h"

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

int cp0_ui_info_wrap_px(int row_h, int value_px)
{
    const int room = imax(0, row_h - 2 * CP0_UI_INFO_WRAP_PAD);
    int px = room * 100 / (2 * CP0_UI_INFO_LINE_X100);
    px &= ~1; /* the even sizes exist: round down so two lines stay inside the row */
    px = imin(px, value_px);
    return imax(CP0_UI_FONT_MIN_PX, px);
}

/* width of a text measured at px once drawn at to_px */
static int rescale(int text_w, int px, int to_px)
{
    return px > 0 ? (int)((long long)text_w * to_px / px) : text_w;
}

int cp0_ui_info_row(int row_w, int inset, int gap, int key_text_w, int key_px, int value_text_w, int value_px,
                    int row_h, int wrap, cp0_ui_info_row_t *out)
{
    if (!out) return -1;
    const int content = row_w - 2 * imax(0, inset);
    if (content <= 0) return -1;
    key_text_w = imax(0, key_text_w);
    value_text_w = imax(0, value_text_w);
    gap = imax(0, gap);

    cp0_ui_info_row_t r = {0};
    r.key_lines = 1;
    r.key_px = key_px;
    r.value_lines = 1;
    r.value_px = value_px;
    if (value_text_w == 0) { /* a key alone: the whole row; longer: two lines at the smaller size, then dots */
        r.key_w = imin(key_text_w, content);
        if (key_text_w > content) {
            r.key_px = cp0_ui_info_wrap_px(row_h, key_px);
            const int scaled = rescale(key_text_w, key_px, r.key_px);
            r.key_lines = scaled <= content ? 1 : 2;
            r.key_cut = scaled > 2 * content;
        }
        r.value_x = row_w - inset;
        *out = r;
        return 0;
    }
    if (key_text_w + gap + value_text_w <= content) { /* side by side as they are */
        r.key_w = key_text_w;
        r.value_w = value_text_w;
    } else {
        /* the key keeps its width up to the cap, or more when the value is short enough to leave it the room */
        const int cap = content * CP0_UI_INFO_KEY_MAX_PCT / 100;
        r.key_w = imin(key_text_w, imax(cap, content - gap - value_text_w));
        r.key_cut = r.key_w < key_text_w;
        r.value_w = imax(0, content - gap - r.key_w);
        if (value_text_w > r.value_w) {
            if (wrap && r.value_w > 0) {
                r.value_px = cp0_ui_info_wrap_px(row_h, value_px);
                /* the text shrinks with its size (one line may be enough then); two lines hold twice the width */
                const int scaled = rescale(value_text_w, value_px, r.value_px);
                r.value_lines = scaled <= r.value_w ? 1 : 2;
                r.truncated = scaled > 2 * r.value_w;
            } else {
                r.truncated = 1;
            }
        }
    }
    r.value_x = row_w - inset - r.value_w;
    *out = r;
    return 0;
}
