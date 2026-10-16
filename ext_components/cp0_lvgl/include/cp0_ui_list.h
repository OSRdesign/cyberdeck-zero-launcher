/*
 * SPDX-License-Identifier: MIT
 *
 * List maths and layout of CompactList (cp0_ui_widgets.hpp): the scrolling list for apps, next to the Settings roller.
 * Pure C, no LVGL: unit-tested on the PC by tests/test_ui_list.c. The LVGL widget that draws it is
 * src/cp0_ui_list_widget.cpp.
 *
 * Roller or CompactList: the Roller (cp0_ui_roller.h) centres the selected row under a fixed bar, shows two or three big
 * rows each side and orange chevrons; it is the Settings menu look. CompactList is a plain list for apps: top-aligned
 * (row 0 directly under the list area's top edge, the highlight starts on row 0 and moves down; the list scrolls only
 * when the highlight would leave the view, keeping one row of context, never centring), no chevrons, a thin scrollbar
 * at the right edge that shows while scrolling and hides 1 s after (none when every row fits).
 *
 * Two densities, from the layout service's tokens (cp0_ui_metrics.h):
 *   COMPACT      dense data (process lists, logs, sensor tables): rows are the LIST_ROW token (5.0 mm, at least
 *                24 px: 57 px on the deck, about 7.5 rows under a header), label 50 % of the row. Below D5's 7 mm
 *                list row on purpose: a mis-tap only moves the highlight (activation needs a second tap)
 *   COMFORTABLE  a few touch-friendly rows (3-6 serial ports, a menu inside an app): rows are the ROW token (7 mm, the
 *                roller's row), label 43 % of the row (the roller's centre text). Use it for any list whose rows
 *                trigger an action
 * Values: 90 % of the label size; a section header: a band 75 % of the item row, its text 75 % of the label, grey,
 * upper case, never selected. Text sizes are snapped like the roller's (cp0_ui_roller_font_px).
 *
 * Rows: item (label left, cut with dots; optional value right, never cut, coloured by a tone) or section header.
 * Row tops come from cp0_ui_list_tops(); every other function works on that array (count + 1 entries, the last one
 * the content height), so a list of any length costs one pass to lay out and a binary search to hit-test.
 *
 * Keys and taps (cp0_ui_list_key, cp0_ui_list_tap): Up / Down move the highlight over the items (sections are skipped;
 * wrap optional, off by default), Home / End to the first / last item, Page Up / Down by a view of rows, Enter and Right
 * activate, Esc and Left go back (the Roller's keys). A tap on an item selects it; a tap on the item already selected
 * activates it, except while a keyboard scroll is still running (the Roller's rule: a tap must never activate a row
 * that moved under the finger). Read-only lists have no highlight: Up / Down scroll by a row, Home / End / Page Up /
 * Down scroll, taps do nothing.
 */
#pragma once

#include "cp0_ui_metrics.h"
#include "cp0_ui_roller.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_UI_LIST_TEXT_PCT_COMPACT 50                        /* compact label: 50 % of the row ... */
#define CP0_UI_LIST_TEXT_PCT_COMFORTABLE CP0_UI_ROLLER_TEXT_PCT /* ... comfortable: 43 %, the roller's centre text */
#define CP0_UI_LIST_VALUE_PCT CP0_UI_ROLLER_VALUE_PCT           /* value: 90 % of the label (the roller's) */
#define CP0_UI_LIST_SECTION_PCT 75      /* section header: a band 75 % of the item row, text 75 % of the label */
#define CP0_UI_LIST_SCROLLBAR_HIDE_MS 1000 /* the scrollbar hides this long after the last scroll movement */

/* Colours: the shell's palette (dark theme only). */
#define CP0_UI_LIST_BAR_RGB CP0_UI_ROLLER_BAR_RGB /* the selected row: the roller's highlight bar #2B2B2B */
#define CP0_UI_LIST_TEXT_RGB 0xFFFFFF             /* item labels (the roller's) */
#define CP0_UI_LIST_SECTION_RGB 0x9A9A9A          /* section headers, the empty line (the text fields' label grey) */
#define CP0_UI_LIST_SEPARATOR_RGB CP0_UI_ROLLER_BAR_RGB /* hairline between items (when separator_px > 0) */
#define CP0_UI_LIST_SCROLLBAR_RGB 0x555555        /* TextBlock's scrollbar */
#define CP0_UI_LIST_VALUE_RGB 0x00CC66            /* NORMAL value: the roller's value green */
#define CP0_UI_LIST_OK_RGB 0x66CC88               /* OK: the status line's green */
#define CP0_UI_LIST_WARN_RGB 0xF0B400             /* WARN: the status line's amber */
#define CP0_UI_LIST_BAD_RGB 0xFF6666              /* BAD: the status line's red */
#define CP0_UI_LIST_MUTED_RGB CP0_UI_LIST_SECTION_RGB /* MUTED: an absent / unset thing (the label grey) */

typedef enum {
    CP0_UI_LIST_COMPACT = 0, /* dense data: LIST_ROW token rows */
    CP0_UI_LIST_COMFORTABLE, /* a few touch-friendly rows: ROW token rows */
} cp0_ui_list_density_t;

typedef enum {
    CP0_UI_LIST_ITEM = 0, /* label, optional value; selectable */
    CP0_UI_LIST_SECTION,  /* section header: grey, upper case, never selected */
} cp0_ui_list_kind_t;

typedef enum {
    CP0_UI_LIST_TONE_NORMAL = 0,
    CP0_UI_LIST_TONE_OK,
    CP0_UI_LIST_TONE_WARN,
    CP0_UI_LIST_TONE_BAD,
    CP0_UI_LIST_TONE_MUTED, /* grey: an absent / unset thing; never the only signal of status */
} cp0_ui_list_tone_t;

typedef struct {
    int x, y, w, h;                /* the list area on its parent; row 0 starts at y */
    cp0_ui_list_density_t density;
    int row_h;                     /* item row: LIST_ROW (compact) / ROW (comfortable) token, at most h */
    int section_h;                 /* section header row: 75 % of row_h */
    int text_px;                   /* item label: 50 % (compact) / 43 % (comfortable) of row_h, snapped */
    int value_px;                  /* value: 90 % of text_px, snapped */
    int section_px;                /* section header text: 75 % of text_px, snapped */
    int inset;                     /* label / value inset from the row's edges: SPACE_L (the roller's) */
    int gap;                       /* between a label and its value: SPACE_M */
    int separator_px;              /* hairline between two items: 0 = none (default); set 1 to draw it */
    int scrollbar_w;               /* the scrollbar: max(3, SPACE_M / 3) wide (TextBlock's) ... */
    int scrollbar_pad;             /* ... max(2, SPACE_S / 2) from the right edge, inside the inset */
} cp0_ui_list_layout_t;

/* The list in the area x, y, w, h of its parent for the metrics m. Returns 0, or -1 (out untouched) when m or out is
 * NULL or the area is empty. */
int cp0_ui_list_layout(const cp0_ui_metrics_t *m, cp0_ui_list_density_t density, int x, int y, int w, int h,
                       cp0_ui_list_layout_t *out);

/* The list filling a page's body (cp0_ui::Page): the roller's column (roller_x, roller_w), under the header to the
 * bottom of the screen. Returns 0, or -1 (out untouched) when an argument is NULL or the body is empty. */
int cp0_ui_list_page_layout(const cp0_ui_metrics_t *m, const cp0_ui_roller_page_t *page, cp0_ui_list_density_t density,
                            cp0_ui_list_layout_t *out);

/* Height of a row of that kind (row_h or section_h). 0 when l is NULL. */
int cp0_ui_list_row_height(const cp0_ui_list_layout_t *l, cp0_ui_list_kind_t kind);

/* Lays the rows out from the top: tops[i] = row i's top in the content (row 0 at 0), tops[count] = the content height
 * (tops holds count + 1 ints). kinds NULL: every row is an item. Returns the content height, -1 when l or tops is NULL
 * or count < 0. */
int cp0_ui_list_tops(const cp0_ui_list_layout_t *l, const cp0_ui_list_kind_t *kinds, int count, int *tops);

/* The largest scroll offset: content_h - view_h, at least 0 (0 = every row fits: nothing scrolls). */
int cp0_ui_list_max_scroll(int content_h, int view_h);

/* The row under the content y (0 = row 0's top, i.e. a point in the view + the scroll offset), -1 above the first or
 * below the last row. */
int cp0_ui_list_row_at(const int *tops, int count, int y);

/* The scroll offset that shows row `index` whole with the least movement from scroll_y: unchanged when it is already
 * in view, else just enough. context 1 (keyboard): the rows just above and below it are shown whole too (one row of
 * context; less when the view is too short for both, the selected row always first); context 0 (a tap): only the row
 * itself, so the row stays under the finger. A row taller than the view shows its top. The result is clamped to
 * 0..max scroll; a bad index returns scroll_y clamped. 0 when tops is NULL, count <= 0 or view_h <= 0. */
int cp0_ui_list_scroll_to_show(const int *tops, int count, int index, int scroll_y, int view_h, int context);

/* 1 when row index exists and is an item (kinds NULL: every row is an item). */
int cp0_ui_list_selectable(const cp0_ui_list_kind_t *kinds, int count, int index);
/* The first / last item, -1 when the list has none. */
int cp0_ui_list_first(const cp0_ui_list_kind_t *kinds, int count);
int cp0_ui_list_last(const cp0_ui_list_kind_t *kinds, int count);
/* The item |delta| items after (delta > 0) or before (delta < 0) index, sections skipped. Without wrap it stops at the
 * first / last item; with wrap one step past an end goes to the other end (a longer step stops there). An index that
 * is not an item (or -1) starts from the nearest item in the step's direction. -1 when the list has no item. */
int cp0_ui_list_step(const cp0_ui_list_kind_t *kinds, int count, int index, int delta, int wrap);

/* Width a label gets in a row of row_w px: row_w - 2 inset, minus the value and a gap when the value is not empty
 * (value_w > 0). The value is never cut: it takes up to row_w - 2 inset, the label what is left (at least 0). */
int cp0_ui_list_label_w(int row_w, int inset, int gap, int value_w);

/* The colour of a value of that tone (0xRRGGBB). */
unsigned cp0_ui_list_tone_rgb(cp0_ui_list_tone_t tone);

/* ---- state machine (what the widget does with a key, a tap or new rows) */

typedef enum {
    CP0_UI_LIST_KEY_OTHER = 0,
    CP0_UI_LIST_KEY_UP,
    CP0_UI_LIST_KEY_DOWN,
    CP0_UI_LIST_KEY_LEFT,
    CP0_UI_LIST_KEY_RIGHT,
    CP0_UI_LIST_KEY_ENTER,
    CP0_UI_LIST_KEY_ESC,
    CP0_UI_LIST_KEY_HOME,
    CP0_UI_LIST_KEY_END,
    CP0_UI_LIST_KEY_PAGE_UP,
    CP0_UI_LIST_KEY_PAGE_DOWN,
} cp0_ui_list_key_t;

typedef enum {
    CP0_UI_LIST_NONE = 0, /* nothing to do (the key is not the list's) */
    CP0_UI_LIST_MOVE,     /* select `index` (it may be the current one) and scroll to `scroll_y` */
    CP0_UI_LIST_SCROLL,   /* scroll to `scroll_y` (read-only lists) */
    CP0_UI_LIST_ACTIVATE, /* activate item `index` */
    CP0_UI_LIST_BACK,     /* the page's back action */
} cp0_ui_list_action_t;

typedef struct {
    const cp0_ui_list_kind_t *kinds; /* NULL: every row is an item */
    const int *tops;                 /* count + 1 entries (cp0_ui_list_tops) */
    int count;
    int view_h;                      /* the list area's height */
    int row_h;                       /* the item row (a read-only list scrolls by it) */
    int selected;                    /* the highlighted item, -1 for none */
    int scroll_y;                    /* the current scroll offset */
    int read_only;                   /* no highlight, no activation: keys scroll */
    int wrap;                        /* Up on the first item goes to the last (and Down on the last to the first) */
} cp0_ui_list_state_t;

typedef struct {
    cp0_ui_list_action_t action;
    int index;    /* MOVE / ACTIVATE: the item */
    int scroll_y; /* MOVE / SCROLL: the new scroll offset (clamped); otherwise the current one */
} cp0_ui_list_result_t;

/* What a key does (see the top of this file). A selectable list without a selection (-1) selects the first item on
 * Up / Down / Home and the last on End; a selection scrolled out of view comes back into view on Up / Down. Page Up /
 * Down move by the items of one view minus one row (at least one). Activation needs a selected item. */
cp0_ui_list_result_t cp0_ui_list_key(const cp0_ui_list_state_t *s, cp0_ui_list_key_t key);

/* A tap on row `index`. busy: a programmatic (keyboard) scroll is still running. Selects an item (MOVE; the scroll
 * only uncovers a row cut at an edge, context 0) or activates the item already selected (ACTIVATE, not while busy).
 * NONE for a section, a bad index or a read-only list. */
cp0_ui_list_result_t cp0_ui_list_tap(const cp0_ui_list_state_t *s, int index, int busy);

/* After the rows changed under the list (a live refresh, a row added or gone), with s describing the NEW rows and the
 * OLD selection and scroll: what to keep. The selection keeps its index, clamped to the rows; on a section it moves to
 * the next item, else the previous one; -1 when there is no item (or the list is read-only). The scroll keeps its
 * offset, clamped to the new content. Nothing scrolls to the selection: a refresh never moves what the user looks at.
 * Returned as a MOVE (index, scroll_y); NONE when s is NULL. */
cp0_ui_list_result_t cp0_ui_list_keep(const cp0_ui_list_state_t *s);

#ifdef __cplusplus
}
#endif
