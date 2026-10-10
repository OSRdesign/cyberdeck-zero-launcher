/*
 * SPDX-License-Identifier: MIT
 *
 * LVGL side of the layout service (cp0_ui_metrics.h): the native display's metrics and the builtin fonts for the
 * text sizes. LVGL thread only.
 */
#pragma once

#include "cp0_ui_metrics.h"
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The metrics of the native display: the ones the display manager computed when it started (dpi-scaled backend),
 * otherwise computed once from the default display's resolution at the default density. Never NULL. */
const cp0_ui_metrics_t *cp0_ui_metrics_get(void);

/* One token of cp0_ui_metrics_get(), px (0 for an unknown token). */
int cp0_ui_tok(cp0_ui_token_t token);

/* The builtin Montserrat font for px (cp0_ui_font_snap; the nearest compiled-in size if that one is not built). */
const lv_font_t *cp0_ui_font_px(int px);

/* The font of a text style of cp0_ui_metrics_get(). */
const lv_font_t *cp0_ui_font(cp0_ui_text_t style);

#ifdef __cplusplus
}
#endif
