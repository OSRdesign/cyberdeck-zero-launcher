/*
 * SPDX-License-Identifier: MIT
 *
 * Dialog layout of the responsive shell (task 014 P3b, report 022 sections 2.2 and 3.4): a title, a body (text, text
 * fields), 1-3 buttons in one row (the primary one = Enter, the cancel one = Esc). Pure C, no LVGL: unit-tested on the
 * PC by tests/test_ui_dialog.c. The LVGL widget is cp0::ui::Dialog (cp0_ui_widgets.hpp). Used by the Wi-Fi password
 * and hidden-network dialogs (P3b), meant for the Bluetooth PIN (P3c) and the sudo prompt (P3d).
 *
 * Sizes from the tokens: width DIALOG_MAX_W (already narrower than the screen by SPACE_M each side), padding SPACE_M,
 * gaps SPACE_S, radius RADIUS_M, title HEADING text, body and buttons BODY text, buttons TARGET high (7 mm).
 *
 * Placement (cp0_ui_osk_dialog_y): centred in the room between `top` (the page header's bottom) and the on-screen
 * keyboard (the bottom of the screen when it is hidden); the page header (its title, the Esc button, the status strip)
 * is never covered. When the whole dialog does not fit:
 *   1. the keyboard is up: the buttons go (the keyboard's Enter and Esc are the primary and the cancel action);
 *   2. still too tall: its top and bottom padding becomes SPACE_S;
 *   3. still too tall (and the page lends it, header_title): the title leaves the box for the page header's title slot,
 *      where it replaces the page's title while it is there (task 014 P3c: 480x320 and 640x480 with the keyboard);
 *   4. still too tall: the top and bottom padding become 2 px;
 *   5. still too tall: the body keeps the room left and scrolls (the widget keeps the field being edited in view).
 * Without a keyboard only step 5 applies.
 */
#pragma once

#include "cp0_ui_metrics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_UI_DIALOG_MAX_BUTTONS 3

typedef struct {
    int x, y, w, h;      /* the box, screen coordinates */
    int pad, pad_y, gap, radius; /* pad: left / right padding (SPACE_M); pad_y: top / bottom (SPACE_M, SPACE_S in step 2) */
    int title_px, text_px, button_px;
    int content_w;       /* the body's width: w - 2 pad (does not depend on the content: measure the body with it) */
    int title_y;         /* the title line, from the box's top */
    int body_y, body_h;  /* the body viewport, from the box's top */
    int scrolls;         /* the content is taller than body_h */
    int buttons;         /* buttons shown: 0 when they went (the keyboard's Enter / Esc act for them) */
    int button_y, button_w, button_h, button_gap;
    int title_in_header; /* step 3: the title shows in the page header's title slot, not in the box */
} cp0_ui_dialog_t;

/* The dialog for the metrics m. top: the y it stays under (the page header's height); inset: the on-screen keyboard's
 * height (0 when hidden); title_line_h: the title's line height (0: no title); content_h: the body's natural height
 * at content_w (call once with 0 to learn content_w and the text sizes, measure, then again); buttons: 0..3;
 * header_title: the page header can show the title (step 3).
 * Returns 0, or -1 (out untouched) when m or out is NULL or the screen is empty. */
int cp0_ui_dialog_layout(const cp0_ui_metrics_t *m, int top, int inset, int title_line_h, int content_h, int buttons,
                         int header_title, cp0_ui_dialog_t *out);

#ifdef __cplusplus
}
#endif
