/*
 * SPDX-License-Identifier: MIT
 *
 * Pure logic of the shared status strip (task L1, report 024): where the strip sits on a native display, the Wi-Fi
 * rule of its bars and the strip's leftmost drawn column. The LVGL widget is cp0::ui::StatusStrip
 * (cp0_ui_widgets.hpp); the pixels come from the cp0_statusbar renderer. Plain C, no LVGL.
 */
#pragma once

#include "cp0_statusbar.h"
#include "cp0_ui_metrics.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x, y, w, h; /* the canvas: right-aligned at the top of the display, shell status_w x bar_h */
    int rows;       /* rows of its ARGB8888 buffer: max(bar_h, CP0_STATUSBAR_HEIGHT) */
    int top;        /* y of the clock pill in it (shell status_top) */
    int pct;        /* renderer scale (shell status_pct) */
} cp0_ui_status_strip_geom_t;

/* The strip's place for metrics m on a display display_w px wide (<= 0: m->w). */
void cp0_ui_status_strip_geom(const cp0_ui_metrics_t *m, int display_w, cp0_ui_status_strip_geom_t *out);

/* A Wi-Fi backend's signal as a percentage: 1..100 is a strength (above 100 is capped), a negative value an RSSI in
 * dBm (-100 dBm -> 0, -50 dBm -> 100), 0 is none. */
int cp0_ui_status_signal_percent(int signal);

/* The bars' rule: connected shows at least one bar (wifi_pct >= 1, wifi_up = 1); not connected shows none
 * (wifi_pct = 0, wifi_up = 0). signal as cp0_ui_status_signal_percent takes it. */
void cp0_ui_status_set_wifi(cp0_statusbar_state_t *state, int connected, int signal);

/* First column (0..w-1) of an ARGB8888 buffer (w px wide and stride, rows high) holding a pixel with alpha; w when
 * the buffer is empty. */
int cp0_ui_status_first_column(const uint32_t *argb, int w, int rows);

#ifdef __cplusplus
}
#endif
