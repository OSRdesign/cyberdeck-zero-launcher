/*
 * SPDX-License-Identifier: MIT
 *
 * Unit test of CompactList's maths (src/cp0_ui_list.c): layout from the tokens in both densities at the four target
 * sizes, row tops with section headers, hit-test, scroll-to-visible (one row of context for keys, none for taps,
 * top-aligned, never centred), item stepping (sections skipped, wrap), the key machine (Up / Down / Home / End /
 * Page Up / Down / Enter / Right / Esc / Left, read-only scrolling), the tap rule (select, then activate; never while a
 * keyboard scroll runs) and the live refresh rule (selection and scroll kept, clamped, never reset).
 */
#include "cp0_ui_list.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        const long long va_ = (long long)(a), vb_ = (long long)(b);                                        \
        if (va_ != vb_) {                                                                                  \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, \
                    va_, vb_);                                                                             \
            failures++;                                                                                    \
        }                                                                                                  \
    } while (0)

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

/* ------------------------------------------------------------------ layout */

typedef struct {
    int w, h, ppmm, strip_left;
    cp0_ui_list_density_t density;
    int x, y, lw, lh, row_h, section_h, text_px, value_px, section_px, inset, gap, bar_w, bar_pad;
} expected_layout_t;

static void check_layout(const expected_layout_t *e)
{
    cp0_ui_metrics_t m;
    CHECK_EQ(cp0_ui_metrics_compute(e->w, e->h, 0, e->ppmm, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
    cp0_ui_roller_page_t page;
    CHECK_EQ(cp0_ui_roller_page(&m, e->strip_left, &page), 0);
    cp0_ui_list_layout_t l;
    memset(&l, 0xAB, sizeof(l));
    CHECK_EQ(cp0_ui_list_page_layout(&m, &page, e->density, &l), 0);
    /* the page's body, the roller's column: rows start right under the header */
    CHECK_EQ(l.x, e->x);
    CHECK_EQ(l.y, e->y);
    CHECK_EQ(l.y, page.body_y);
    CHECK_EQ(l.w, e->lw);
    CHECK_EQ(l.h, e->lh);
    CHECK_EQ(l.density, e->density);
    CHECK_EQ(l.row_h, e->row_h);
    CHECK_EQ(l.row_h, m.tok[e->density == CP0_UI_LIST_COMFORTABLE ? CP0_TOK_ROW_H : CP0_TOK_LIST_ROW_H]);
    CHECK_EQ(l.section_h, e->section_h);
    CHECK_EQ(l.text_px, e->text_px);
    CHECK_EQ(l.value_px, e->value_px);
    CHECK_EQ(l.section_px, e->section_px);
    CHECK(l.value_px <= l.text_px && l.section_px < l.text_px);
    CHECK_EQ(l.inset, e->inset);
    CHECK_EQ(l.inset, m.tok[CP0_TOK_SPACE_L]);
    CHECK_EQ(l.gap, e->gap);
    CHECK_EQ(l.separator_px, 0);
    CHECK_EQ(l.scrollbar_w, e->bar_w);
    CHECK_EQ(l.scrollbar_pad, e->bar_pad);
    CHECK(l.scrollbar_w + l.scrollbar_pad < l.inset); /* the scrollbar never covers a value */
    /* every size the font set has */
    CHECK(l.text_px % 2 == 0 && l.value_px % 2 == 0 && l.section_px % 2 == 0);
    /* text fits its row with room for the bar's padding: a Montserrat line is about 1.25 x its size */
    CHECK(l.text_px * 5 / 4 < l.row_h);
    CHECK(l.section_px * 5 / 4 < l.section_h);
}

static void test_layout(void)
{
    /* strip_left as in test_ui_roller.c (the strip's Bluetooth icon) */
    static const expected_layout_t layouts[] = {
        /* w    h   ppmm  left  density              x    y   lw   lh  row sec txt val sxt ins gap bw bp */
        {640, 480, 1130, 424, CP0_UI_LIST_COMPACT, 40, 56, 560, 424, 57, 43, 30, 28, 24, 20, 16, 5, 4},
        {640, 480, 1130, 424, CP0_UI_LIST_COMFORTABLE, 40, 56, 560, 424, 79, 59, 34, 32, 26, 20, 16, 5, 4},
        {480, 320, 630, 325, CP0_UI_LIST_COMPACT, 10, 40, 460, 280, 32, 24, 16, 14, 12, 14, 10, 3, 2},
        {480, 320, 630, 325, CP0_UI_LIST_COMFORTABLE, 10, 40, 460, 280, 44, 33, 20, 18, 16, 14, 10, 3, 2},
        {800, 480, 920, 584, CP0_UI_LIST_COMPACT, 120, 56, 560, 424, 46, 35, 24, 22, 18, 20, 14, 4, 4},
        {800, 480, 920, 584, CP0_UI_LIST_COMFORTABLE, 120, 56, 560, 424, 64, 48, 28, 26, 22, 20, 14, 4, 4},
        {720, 720, 1000, 504, CP0_UI_LIST_COMPACT, 80, 64, 560, 656, 50, 38, 26, 24, 20, 24, 16, 5, 4},
        {720, 720, 1000, 504, CP0_UI_LIST_COMFORTABLE, 80, 64, 560, 656, 70, 53, 30, 28, 24, 24, 16, 5, 4},
    };
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) check_layout(&layouts[i]);

    /* the deck: compact rows 5.0 mm (57 px), 7 whole ones under the header (7.4), 5 comfortable (7 mm) ones */
    cp0_ui_metrics_t m;
    cp0_ui_list_layout_t c, f;
    CHECK_EQ(cp0_ui_metrics_compute(640, 480, 0, 1130, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
    CHECK_EQ(m.tok[CP0_TOK_LIST_ROW_H], 57);
    CHECK_EQ(cp0_ui_list_layout(&m, CP0_UI_LIST_COMPACT, 40, 56, 560, 424, &c), 0);
    CHECK_EQ(cp0_ui_list_layout(&m, CP0_UI_LIST_COMFORTABLE, 40, 56, 560, 424, &f), 0);
    CHECK_EQ(c.h / c.row_h, 7);
    CHECK_EQ(f.h / f.row_h, 5);
    CHECK(c.row_h < f.row_h);

    /* the floor on a low-density panel; a row never taller than the area */
    CHECK_EQ(cp0_ui_metrics_compute(640, 480, 0, 300, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
    CHECK_EQ(m.tok[CP0_TOK_LIST_ROW_H], CP0_UI_LIST_ROW_FLOOR_PX);
    CHECK_EQ(cp0_ui_list_layout(&m, CP0_UI_LIST_COMPACT, 0, 0, 100, 20, &c), 0);
    CHECK_EQ(c.row_h, 20);

    /* bad arguments leave out untouched */
    memset(&c, 0x5A, sizeof(c));
    f = c;
    CHECK_EQ(cp0_ui_list_layout(NULL, CP0_UI_LIST_COMPACT, 0, 0, 100, 100, &c), -1);
    CHECK_EQ(cp0_ui_list_layout(&m, CP0_UI_LIST_COMPACT, 0, 0, 0, 100, &c), -1);
    CHECK_EQ(cp0_ui_list_layout(&m, CP0_UI_LIST_COMPACT, 0, 0, 100, 0, &c), -1);
    CHECK_EQ(cp0_ui_list_page_layout(&m, NULL, CP0_UI_LIST_COMPACT, &c), -1);
    CHECK(memcmp(&c, &f, sizeof(c)) == 0);
}

/* ------------------------------------------------------------------ a 40-row list in a 424 px view */

#define N40 40
static cp0_ui_list_kind_t g_kinds[N40];
static int g_tops[N40 + 1];
static cp0_ui_list_layout_t g_deck;

/* The maths below are worked out by hand for fixed sizes, whatever the tokens: 47 px items, 35 px sections, in the
 * deck's 424 px body. Section at 0 and 20, items elsewhere: tops 0, 35, 82, ... */
static void make_deck_list(void)
{
    cp0_ui_metrics_t m;
    cp0_ui_metrics_compute(640, 480, 0, 1130, CP0_UI_DENSITY_PANEL_MM, 0, &m);
    cp0_ui_list_layout(&m, CP0_UI_LIST_COMPACT, 40, 56, 560, 424, &g_deck);
    g_deck.row_h = 47;
    g_deck.section_h = 35;
    for (int i = 0; i < N40; ++i) g_kinds[i] = (i == 0 || i == 20) ? CP0_UI_LIST_SECTION : CP0_UI_LIST_ITEM;
    CHECK_EQ(cp0_ui_list_tops(&g_deck, g_kinds, N40, g_tops), 1856);
}

static cp0_ui_list_state_t deck_state(int selected, int scroll)
{
    cp0_ui_list_state_t s;
    memset(&s, 0, sizeof(s));
    s.kinds = g_kinds;
    s.tops = g_tops;
    s.count = N40;
    s.view_h = 424;
    s.row_h = g_deck.row_h;
    s.selected = selected;
    s.scroll_y = scroll;
    return s;
}

static void test_tops_and_hit(void)
{
    CHECK_EQ(g_tops[0], 0);
    CHECK_EQ(g_tops[1], 35);       /* a section band */
    CHECK_EQ(g_tops[2], 35 + 47);
    CHECK_EQ(g_tops[20], 35 + 19 * 47);
    CHECK_EQ(g_tops[21], 35 + 19 * 47 + 35);
    CHECK_EQ(g_tops[40], 2 * 35 + 38 * 47);
    CHECK_EQ(cp0_ui_list_max_scroll(g_tops[40], 424), 1856 - 424);
    CHECK_EQ(cp0_ui_list_max_scroll(200, 424), 0); /* everything fits: nothing scrolls */

    /* kinds NULL: all items */
    int tops[4];
    CHECK_EQ(cp0_ui_list_tops(&g_deck, NULL, 3, tops), 3 * 47);
    CHECK_EQ(tops[1], 47);
    CHECK_EQ(cp0_ui_list_tops(&g_deck, NULL, 0, tops), 0);
    CHECK_EQ(cp0_ui_list_tops(NULL, NULL, 3, tops), -1);
    CHECK_EQ(cp0_ui_list_tops(&g_deck, NULL, -1, tops), -1);
    CHECK_EQ(cp0_ui_list_row_height(&g_deck, CP0_UI_LIST_SECTION), 35);
    CHECK_EQ(cp0_ui_list_row_height(&g_deck, CP0_UI_LIST_ITEM), 47);

    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, -1), -1);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, 0), 0);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, 34), 0);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, 35), 1);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, 81), 1);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, 82), 2);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, g_tops[20] + 34), 20);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, g_tops[39]), 39);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, g_tops[40] - 1), 39);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, g_tops[40]), -1);
    CHECK_EQ(cp0_ui_list_row_at(g_tops, 0, 0), -1);
    CHECK_EQ(cp0_ui_list_row_at(NULL, 3, 0), -1);
    for (int i = 0; i < N40; ++i) /* every pixel row hits its own row */
        for (int y = g_tops[i]; y < g_tops[i + 1]; y += 7) CHECK_EQ(cp0_ui_list_row_at(g_tops, N40, y), i);
}

static void test_scroll_to_show(void)
{
    /* already in view: no movement, whatever the context */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 3, 0, 424, 1), 0);
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 7, 0, 424, 1), 0); /* row 8 below it ends at 411 */
    /* row 8 (bottom 411) is whole, but row 9 (its context) is cut at 424: scroll just enough for row 9 */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 8, 0, 424, 1), g_tops[10] - 424);
    /* a tap on row 8 needs no scroll; on row 9 (cut) only the cut part */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 8, 0, 424, 0), 0);
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 9, 0, 424, 0), g_tops[10] - 424);
    /* above the view: the row above it comes in too (top-aligned, never centred) */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 10, 700, 424, 1), g_tops[9]);
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 10, 700, 424, 0), g_tops[10]);
    /* far below: the row and the one under it end at the bottom edge */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 30, 0, 424, 1), g_tops[32] - 424);
    /* the last row: no row under it, the content's end */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 39, 0, 424, 1), 1856 - 424);
    /* row 1: its context is the section above it (row 0), so the top of the list */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 1, 900, 424, 1), 0);
    /* clamped: an offset past the end comes back, a bad index only clamps */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 39, 5000, 424, 1), 1856 - 424);
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, -1, -50, 424, 1), 0);
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 99, 5000, 424, 1), 1856 - 424);
    CHECK_EQ(cp0_ui_list_scroll_to_show(NULL, N40, 3, 10, 424, 1), 0);
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 3, 10, 0, 1), 0);

    /* a view too short for the row and both neighbours: the row whole, the context shared */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 10, 0, 100, 1), g_tops[11] + 27 - 100); /* 53 room: 26 + 27 */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 10, 2000, 100, 1), g_tops[10] - 26);
    /* a row taller than the view: its top */
    CHECK_EQ(cp0_ui_list_scroll_to_show(g_tops, N40, 10, 0, 40, 1), g_tops[10]);
    /* everything fits: always 0 */
    int tops[4];
    cp0_ui_list_tops(&g_deck, NULL, 3, tops);
    for (int i = 0; i < 3; ++i) CHECK_EQ(cp0_ui_list_scroll_to_show(tops, 3, i, 30, 424, 1), 0);
}

static void test_step(void)
{
    CHECK_EQ(cp0_ui_list_first(g_kinds, N40), 1);
    CHECK_EQ(cp0_ui_list_last(g_kinds, N40), 39);
    CHECK(!cp0_ui_list_selectable(g_kinds, N40, 0));
    CHECK(!cp0_ui_list_selectable(g_kinds, N40, 20));
    CHECK(cp0_ui_list_selectable(g_kinds, N40, 21));
    CHECK(!cp0_ui_list_selectable(g_kinds, N40, 40));
    CHECK(cp0_ui_list_selectable(NULL, 3, 2));

    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 5, 1, 0), 6);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 19, 1, 0), 21); /* the section is skipped */
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 21, -1, 0), 19);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 1, -1, 0), 1);  /* stops at the ends ... */
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 39, 1, 0), 39);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 1, -1, 1), 39); /* ... or wraps */
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 39, 1, 1), 1);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 35, 8, 1), 39); /* a longer step stops at the end, even with wrap */
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 15, 8, 0), 24);  /* 16..19, 21..24 */
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 24, -8, 0), 15);
    /* from a section or nowhere: the nearest item in the step's direction is the first step */
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 20, 1, 0), 21);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 20, -1, 0), 19);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, -1, 1, 0), 1);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 0, 2, 0), 2);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 20, 0, 0), 21); /* delta 0: the item itself, else the nearest */
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 7, 0, 0), 7);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 0, -1, 0), 1);
    CHECK_EQ(cp0_ui_list_step(g_kinds, N40, 0, -1, 1), 39);

    cp0_ui_list_kind_t sections[3] = {CP0_UI_LIST_SECTION, CP0_UI_LIST_SECTION, CP0_UI_LIST_SECTION};
    CHECK_EQ(cp0_ui_list_step(sections, 3, 0, 1, 1), -1);
    CHECK_EQ(cp0_ui_list_first(sections, 3), -1);
    CHECK_EQ(cp0_ui_list_step(NULL, 0, 0, 1, 1), -1);

    /* label and value: the value is never cut */
    CHECK_EQ(cp0_ui_list_label_w(560, 20, 16, 0), 520);
    CHECK_EQ(cp0_ui_list_label_w(560, 20, 16, 100), 404);
    CHECK_EQ(cp0_ui_list_label_w(560, 20, 16, 510), 0);
    CHECK_EQ(cp0_ui_list_label_w(560, 20, 16, 900), 0);
    CHECK_EQ(cp0_ui_list_label_w(30, 20, 16, 0), 0);

    CHECK_EQ(cp0_ui_list_tone_rgb(CP0_UI_LIST_TONE_NORMAL), 0x00CC66);
    CHECK_EQ(cp0_ui_list_tone_rgb(CP0_UI_LIST_TONE_OK), 0x66CC88);
    CHECK_EQ(cp0_ui_list_tone_rgb(CP0_UI_LIST_TONE_WARN), 0xF0B400);
    CHECK_EQ(cp0_ui_list_tone_rgb(CP0_UI_LIST_TONE_BAD), 0xFF6666);
    CHECK_EQ(cp0_ui_list_tone_rgb(CP0_UI_LIST_TONE_MUTED), 0x9A9A9A); /* the label grey */
    CHECK_EQ(CP0_UI_LIST_MUTED_RGB, CP0_UI_LIST_SECTION_RGB);
}

/* row i and its neighbours (when they exist) whole in the view */
static int shown_with_context(int i, int scroll)
{
    const int top = g_tops[i > 0 ? i - 1 : 0];
    const int bottom = g_tops[i + 1 < N40 ? i + 2 : N40];
    return top >= scroll && bottom <= scroll + 424;
}

static void test_keys(void)
{
    /* Down from the top: the highlight walks down, the list stays until the row under it would be cut, then scrolls
     * by just that much each press; never centred */
    cp0_ui_list_state_t s = deck_state(1, 0);
    int scroll = 0;
    for (int expect = 2; expect <= 39; ++expect) {
        if (expect == 20) continue;
        const cp0_ui_list_result_t r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN);
        CHECK_EQ(r.action, CP0_UI_LIST_MOVE);
        CHECK_EQ(r.index, expect);
        CHECK(r.scroll_y >= scroll);
        CHECK(shown_with_context(expect, r.scroll_y));
        if (shown_with_context(expect, scroll)) CHECK_EQ(r.scroll_y, scroll); /* no movement when not needed */
        if (expect <= 7) CHECK_EQ(r.scroll_y, 0);
        if (expect == 8) CHECK_EQ(r.scroll_y, g_tops[10] - 424);
        scroll = r.scroll_y;
        s.selected = r.index;
        s.scroll_y = r.scroll_y;
    }
    CHECK_EQ(scroll, 1856 - 424);
    /* Down on the last item: stays (no wrap) */
    cp0_ui_list_result_t r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN);
    CHECK_EQ(r.action, CP0_UI_LIST_MOVE);
    CHECK_EQ(r.index, 39);
    CHECK_EQ(r.scroll_y, 1856 - 424);
    /* and back up: the list holds still until the row above the highlight would be cut */
    for (int expect = 38; expect >= 1; --expect) {
        if (expect == 20) continue;
        r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_UP);
        CHECK_EQ(r.index, expect);
        CHECK(r.scroll_y <= scroll);
        CHECK(shown_with_context(expect, r.scroll_y));
        if (shown_with_context(expect, scroll)) CHECK_EQ(r.scroll_y, scroll);
        scroll = r.scroll_y;
        s.selected = r.index;
        s.scroll_y = r.scroll_y;
    }
    CHECK_EQ(scroll, 0); /* item 1 shows the section above it */

    /* wrap */
    s = deck_state(39, 1856 - 424);
    s.wrap = 1;
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN);
    CHECK_EQ(r.index, 1);
    CHECK_EQ(r.scroll_y, 0);
    s = deck_state(1, 0);
    s.wrap = 1;
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_UP);
    CHECK_EQ(r.index, 39);
    CHECK_EQ(r.scroll_y, 1856 - 424);

    /* Home / End: the first / last item, the whole top / bottom of the list */
    s = deck_state(15, 300);
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_HOME);
    CHECK_EQ(r.action, CP0_UI_LIST_MOVE);
    CHECK_EQ(r.index, 1);
    CHECK_EQ(r.scroll_y, 0);
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_END);
    CHECK_EQ(r.index, 39);
    CHECK_EQ(r.scroll_y, 1856 - 424);

    /* Page Down / Up: 424 / 47 - 1 = 8 items */
    s = deck_state(1, 0);
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_PAGE_DOWN);
    CHECK_EQ(r.index, 9);
    CHECK(shown_with_context(9, r.scroll_y));
    s = deck_state(24, 900);
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_PAGE_UP);
    CHECK_EQ(r.index, 15);
    CHECK(shown_with_context(15, r.scroll_y));

    /* a selection scrolled out of view (a finger drag) comes back on Up / Down */
    s = deck_state(5, 1000);
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN);
    CHECK_EQ(r.index, 6);
    CHECK_EQ(r.scroll_y, g_tops[5]);

    /* Enter / Right activate the selection, Esc / Left go back, other keys are not the list's */
    s = deck_state(12, 200);
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_ENTER);
    CHECK_EQ(r.action, CP0_UI_LIST_ACTIVATE);
    CHECK_EQ(r.index, 12);
    CHECK_EQ(r.scroll_y, 200);
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_RIGHT);
    CHECK_EQ(r.action, CP0_UI_LIST_ACTIVATE);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_ESC).action, CP0_UI_LIST_BACK);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_LEFT).action, CP0_UI_LIST_BACK);
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_OTHER);
    CHECK_EQ(r.action, CP0_UI_LIST_NONE);
    CHECK_EQ(r.index, 12);
    CHECK_EQ(r.scroll_y, 200);

    /* no selection: Down / Up select the first item, End the last; Enter does nothing */
    s = deck_state(-1, 0);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN).index, 1);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_UP).index, 1);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_END).index, 39);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_ENTER).action, CP0_UI_LIST_NONE);
    s.selected = 20; /* a section is no selection */
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_ENTER).action, CP0_UI_LIST_NONE);

    /* read-only: keys scroll (a row, a view minus a row, the ends), nothing is selected or activated */
    s = deck_state(-1, 100);
    s.read_only = 1;
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN);
    CHECK_EQ(r.action, CP0_UI_LIST_SCROLL);
    CHECK_EQ(r.scroll_y, 147);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_UP).scroll_y, 53);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_PAGE_DOWN).scroll_y, 100 + 424 - 47);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_PAGE_UP).scroll_y, 0);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_HOME).scroll_y, 0);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_END).scroll_y, 1856 - 424);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_ENTER).action, CP0_UI_LIST_NONE);
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_ESC).action, CP0_UI_LIST_BACK);
    s.scroll_y = 1856 - 424;
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN).scroll_y, 1856 - 424); /* bounded */

    /* three comfortable rows on the deck: all fit, nothing ever scrolls */
    cp0_ui_metrics_t m;
    cp0_ui_list_layout_t l;
    cp0_ui_metrics_compute(640, 480, 0, 1130, CP0_UI_DENSITY_PANEL_MM, 0, &m);
    cp0_ui_list_layout(&m, CP0_UI_LIST_COMFORTABLE, 40, 56, 560, 424, &l);
    int tops[4];
    CHECK_EQ(cp0_ui_list_tops(&l, NULL, 3, tops), 237);
    cp0_ui_list_state_t f;
    memset(&f, 0, sizeof(f));
    f.tops = tops;
    f.count = 3;
    f.view_h = 424;
    f.row_h = l.row_h;
    f.selected = 0;
    for (int i = 0; i < 4; ++i) {
        r = cp0_ui_list_key(&f, CP0_UI_LIST_KEY_DOWN);
        CHECK_EQ(r.scroll_y, 0);
        f.selected = r.index;
    }
    CHECK_EQ(f.selected, 2);

    /* bad state */
    CHECK_EQ(cp0_ui_list_key(NULL, CP0_UI_LIST_KEY_DOWN).action, CP0_UI_LIST_NONE);
    s = deck_state(3, 0);
    s.tops = NULL;
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN).action, CP0_UI_LIST_NONE);
    s = deck_state(-1, 0);
    s.count = 0;
    CHECK_EQ(cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN).action, CP0_UI_LIST_NONE);
}

static void test_tap(void)
{
    /* a tap selects; the scroll only uncovers a row cut at the edge, so the row stays under the finger */
    cp0_ui_list_state_t s = deck_state(1, 0);
    cp0_ui_list_result_t r = cp0_ui_list_tap(&s, 5, 0);
    CHECK_EQ(r.action, CP0_UI_LIST_MOVE);
    CHECK_EQ(r.index, 5);
    CHECK_EQ(r.scroll_y, 0);
    r = cp0_ui_list_tap(&s, 9, 0); /* cut at the bottom: 34 px up, the finger (in its visible part) still on it */
    CHECK_EQ(r.index, 9);
    CHECK_EQ(r.scroll_y, g_tops[10] - 424);
    /* a tap on the selected item activates it ... */
    s = deck_state(5, 0);
    r = cp0_ui_list_tap(&s, 5, 0);
    CHECK_EQ(r.action, CP0_UI_LIST_ACTIVATE);
    CHECK_EQ(r.index, 5);
    /* ... but not while a keyboard scroll moves the rows */
    r = cp0_ui_list_tap(&s, 5, 1);
    CHECK_EQ(r.action, CP0_UI_LIST_NONE);
    CHECK_EQ(cp0_ui_list_tap(&s, 6, 1).action, CP0_UI_LIST_MOVE); /* another row still selects */
    /* sections, bad rows and read-only lists ignore taps */
    CHECK_EQ(cp0_ui_list_tap(&s, 0, 0).action, CP0_UI_LIST_NONE);
    CHECK_EQ(cp0_ui_list_tap(&s, 20, 0).action, CP0_UI_LIST_NONE);
    CHECK_EQ(cp0_ui_list_tap(&s, 40, 0).action, CP0_UI_LIST_NONE);
    CHECK_EQ(cp0_ui_list_tap(&s, -1, 0).action, CP0_UI_LIST_NONE);
    s.read_only = 1;
    CHECK_EQ(cp0_ui_list_tap(&s, 7, 0).action, CP0_UI_LIST_NONE);
    CHECK_EQ(cp0_ui_list_tap(NULL, 7, 0).action, CP0_UI_LIST_NONE);
}

static void test_keep(void)
{
    /* the same rows again (a live refresh): selection and scroll untouched */
    cp0_ui_list_state_t s = deck_state(12, 333);
    cp0_ui_list_result_t r = cp0_ui_list_keep(&s);
    CHECK_EQ(r.action, CP0_UI_LIST_MOVE);
    CHECK_EQ(r.index, 12);
    CHECK_EQ(r.scroll_y, 333);
    /* the selection scrolled out of view stays out of view (a refresh never scrolls) */
    s = deck_state(2, 1200);
    r = cp0_ui_list_keep(&s);
    CHECK_EQ(r.index, 2);
    CHECK_EQ(r.scroll_y, 1200);

    /* rows went away: the selection and the scroll are clamped to the new rows */
    int tops[N40 + 1];
    cp0_ui_list_tops(&g_deck, g_kinds, 25, tops);
    s = deck_state(33, 1400);
    s.tops = tops;
    s.count = 25;
    r = cp0_ui_list_keep(&s);
    CHECK_EQ(r.index, 24);
    CHECK_EQ(r.scroll_y, tops[25] - 424);
    /* the selected index became a section: the next item (or the previous one at the end) */
    s = deck_state(20, 500);
    r = cp0_ui_list_keep(&s);
    CHECK_EQ(r.index, 21);
    CHECK_EQ(r.scroll_y, 500);
    cp0_ui_list_tops(&g_deck, g_kinds, 21, tops); /* rows 0..20: the last one is the section */
    s = deck_state(25, 0);
    s.tops = tops;
    s.count = 21;
    CHECK_EQ(cp0_ui_list_keep(&s).index, 19);
    /* rows where there were none: the highlight starts on the first item, at the top */
    s = deck_state(-1, 0);
    r = cp0_ui_list_keep(&s);
    CHECK_EQ(r.index, 1);
    CHECK_EQ(r.scroll_y, 0);
    /* no rows / no item / read-only: no selection */
    s = deck_state(5, 100);
    s.count = 0;
    r = cp0_ui_list_keep(&s);
    CHECK_EQ(r.index, -1);
    CHECK_EQ(r.scroll_y, 0);
    cp0_ui_list_kind_t sections[2] = {CP0_UI_LIST_SECTION, CP0_UI_LIST_SECTION};
    int stops[3];
    cp0_ui_list_tops(&g_deck, sections, 2, stops);
    s = deck_state(1, 0);
    s.kinds = sections;
    s.tops = stops;
    s.count = 2;
    CHECK_EQ(cp0_ui_list_keep(&s).index, -1);
    s = deck_state(5, 100);
    s.read_only = 1;
    r = cp0_ui_list_keep(&s);
    CHECK_EQ(r.index, -1);
    CHECK_EQ(r.scroll_y, 100);
    CHECK_EQ(cp0_ui_list_keep(NULL).action, CP0_UI_LIST_NONE);

    /* a refresh between key presses changes nothing in the next move */
    s = deck_state(8, g_tops[10] - 424);
    r = cp0_ui_list_keep(&s);
    s.selected = r.index;
    s.scroll_y = r.scroll_y;
    r = cp0_ui_list_key(&s, CP0_UI_LIST_KEY_DOWN);
    CHECK_EQ(r.index, 9);
    CHECK_EQ(r.scroll_y, g_tops[11] - 424);
}

int main(void)
{
    test_layout();
    make_deck_list();
    test_tops_and_hit();
    test_scroll_to_show();
    test_step();
    test_keys();
    test_tap();
    test_keep();
    if (failures) {
        fprintf(stderr, "test_ui_list: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ui_list: ok\n");
    return 0;
}
