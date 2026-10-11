/*
 * SPDX-License-Identifier: MIT
 *
 * InfoRow / InfoTable layout of the responsive shell (task 014 P3a, report 022 section 3.4): a key on the left and its
 * value on the right of one roller row (rows are the ROW token, 7 mm), for the read-only Settings pages (About, User,
 * Storage, Date & Time info, Wi-Fi network details). Pure C, no LVGL: unit-tested on the PC by tests/test_ui_info.c.
 * The LVGL widget is cp0::ui::InfoTable (cp0_ui_widgets.hpp), a cp0::ui::Roller of RowKind::Info rows.
 *
 * Rule: both texts keep their own width when they fit side by side. Otherwise the key keeps its width up to
 * CP0_UI_INFO_KEY_MAX_PCT of the row (more when the value is short) and the value takes the rest: it is cut with dots,
 * or, on the Compact class (wrap = 1), wrapped onto a second line at a smaller size that fits two lines in the row
 * (still cut with dots when two lines are not enough). A row with no value is a key alone across the row; when it is
 * longer than the row it wraps the same way (on every class: it has the whole row), dots past two lines.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_UI_INFO_KEY_MAX_PCT 45  /* a key never takes more of the row than this when the value needs the room */
#define CP0_UI_INFO_LINE_X100 125   /* line height of the row font: 1.25 x its size (Montserrat ascent + descent) */
#define CP0_UI_INFO_WRAP_PAD 4      /* room kept above and below two wrapped lines */

typedef struct {
    int key_w;       /* key label width */
    int key_lines;   /* 1, or 2 for a key alone longer than the row */
    int key_px;      /* key text size: the row's (key_px), or the two-line size */
    int key_cut;     /* the key ends with dots */
    int value_x;     /* value label x from the row's left edge (right-aligned at row_w - inset) */
    int value_w;     /* value label width (0: no value) */
    int value_lines; /* 1, or 2 when the value wraps (Compact) */
    int value_px;    /* value text size: the row's, or the smaller two-line size (Compact, a value that did not fit;
                        it may then fit on one line) */
    int truncated;   /* the value does not fit even so: it ends with dots */
} cp0_ui_info_row_t;

/* Text size for two lines of a value in a row row_h high: (row_h - 2 x CP0_UI_INFO_WRAP_PAD) / 2 / 1.25, even,
 * never larger than value_px, at least CP0_UI_FONT_MIN_PX. */
int cp0_ui_info_wrap_px(int row_h, int value_px);

/* The layout of one info row. row_w: the roller's width; inset: each side's inset (SPACE_L); gap: between key and
 * value (SPACE_M); key_text_w / value_text_w: the texts' natural widths at the row's centre sizes (key: key_px, the
 * row text; value: value_px); row_h: the row height; wrap: 1 on the Compact class. Returns 0, or -1 (out untouched)
 * for a NULL out or a row with no room. */
int cp0_ui_info_row(int row_w, int inset, int gap, int key_text_w, int key_px, int value_text_w, int value_px,
                    int row_h, int wrap, cp0_ui_info_row_t *out);

#ifdef __cplusplus
}
#endif
