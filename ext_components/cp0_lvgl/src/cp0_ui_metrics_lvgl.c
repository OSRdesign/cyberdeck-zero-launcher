/*
 * SPDX-License-Identifier: MIT
 *
 * LVGL side of the layout service (see cp0_ui_metrics_lvgl.h).
 */

#include "cp0_ui_metrics_lvgl.h"

#include <stddef.h>

const cp0_ui_metrics_t *cp0_ui_metrics_get(void)
{
    const cp0_ui_metrics_t *current = cp0_ui_metrics_current();
    if (current) return current;
    static cp0_ui_metrics_t fallback;
    static int computed;
    if (!computed) {
        lv_display_t *display = lv_display_get_default();
        const int w = display ? (int)lv_display_get_horizontal_resolution(display) : 0;
        const int h = display ? (int)lv_display_get_vertical_resolution(display) : 0;
        if (cp0_ui_metrics_compute(w > 0 ? w : 640, h > 0 ? h : 480, 0, 0, CP0_UI_DENSITY_DEFAULT, 0, &fallback) == 0)
            computed = 1;
    }
    return &fallback;
}

int cp0_ui_tok(cp0_ui_token_t token)
{
    if ((int)token < 0 || token >= CP0_TOK__COUNT) return 0;
    return cp0_ui_metrics_get()->tok[token];
}

static const lv_font_t *builtin_montserrat(int px)
{
    switch (px) {
#if LV_FONT_MONTSERRAT_8
    case 8: return &lv_font_montserrat_8;
#endif
#if LV_FONT_MONTSERRAT_10
    case 10: return &lv_font_montserrat_10;
#endif
#if LV_FONT_MONTSERRAT_12
    case 12: return &lv_font_montserrat_12;
#endif
#if LV_FONT_MONTSERRAT_14
    case 14: return &lv_font_montserrat_14;
#endif
#if LV_FONT_MONTSERRAT_16
    case 16: return &lv_font_montserrat_16;
#endif
#if LV_FONT_MONTSERRAT_18
    case 18: return &lv_font_montserrat_18;
#endif
#if LV_FONT_MONTSERRAT_20
    case 20: return &lv_font_montserrat_20;
#endif
#if LV_FONT_MONTSERRAT_22
    case 22: return &lv_font_montserrat_22;
#endif
#if LV_FONT_MONTSERRAT_24
    case 24: return &lv_font_montserrat_24;
#endif
#if LV_FONT_MONTSERRAT_26
    case 26: return &lv_font_montserrat_26;
#endif
#if LV_FONT_MONTSERRAT_28
    case 28: return &lv_font_montserrat_28;
#endif
#if LV_FONT_MONTSERRAT_30
    case 30: return &lv_font_montserrat_30;
#endif
#if LV_FONT_MONTSERRAT_32
    case 32: return &lv_font_montserrat_32;
#endif
#if LV_FONT_MONTSERRAT_34
    case 34: return &lv_font_montserrat_34;
#endif
#if LV_FONT_MONTSERRAT_36
    case 36: return &lv_font_montserrat_36;
#endif
#if LV_FONT_MONTSERRAT_38
    case 38: return &lv_font_montserrat_38;
#endif
#if LV_FONT_MONTSERRAT_40
    case 40: return &lv_font_montserrat_40;
#endif
#if LV_FONT_MONTSERRAT_42
    case 42: return &lv_font_montserrat_42;
#endif
#if LV_FONT_MONTSERRAT_44
    case 44: return &lv_font_montserrat_44;
#endif
#if LV_FONT_MONTSERRAT_46
    case 46: return &lv_font_montserrat_46;
#endif
#if LV_FONT_MONTSERRAT_48
    case 48: return &lv_font_montserrat_48;
#endif
    default: return NULL;
    }
}

const lv_font_t *cp0_ui_font_px(int px)
{
    const int want = cp0_ui_font_snap(px);
    /* nearest compiled-in size, the smaller one on a tie */
    for (int d = 0; d <= CP0_UI_FONT_MAX_PX - CP0_UI_FONT_MIN_PX; d += 2) {
        const lv_font_t *font = want - d >= CP0_UI_FONT_MIN_PX ? builtin_montserrat(want - d) : NULL;
        if (!font && want + d <= CP0_UI_FONT_MAX_PX) font = builtin_montserrat(want + d);
        if (font) return font;
    }
    return LV_FONT_DEFAULT;
}

const lv_font_t *cp0_ui_font(cp0_ui_text_t style)
{
    if ((int)style < 0 || style >= CP0_TEXT__COUNT) return LV_FONT_DEFAULT;
    return cp0_ui_font_px(cp0_ui_metrics_get()->text_px[style]);
}
