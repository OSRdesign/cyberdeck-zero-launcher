/*
 * Unit test of the roller maths and the native Settings page layout (src/cp0_ui_roller.c, task 014 P2a): centre row,
 * snap, row under a point, stepping with and without wrap, text falloff, fade, width cap, and the page layout at the
 * five target sizes (header, Esc, title and status strip at the home grid's place, roller geometry from the tokens).
 */
#include "cp0_ui_roller.h"

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

static void test_centre_and_snap(void)
{
    /* row i is centred at scroll_y = i * row_h: the centre is the nearest row, clamped */
    CHECK_EQ(cp0_ui_roller_centre(0, 79, 7), 0);
    CHECK_EQ(cp0_ui_roller_centre(-30, 79, 7), 0);
    CHECK_EQ(cp0_ui_roller_centre(39, 79, 7), 0);  /* under half a row: still row 0 */
    CHECK_EQ(cp0_ui_roller_centre(40, 79, 7), 1);  /* half a row (rounded up): the next one */
    CHECK_EQ(cp0_ui_roller_centre(79 * 3, 79, 7), 3);
    CHECK_EQ(cp0_ui_roller_centre(79 * 3 + 38, 79, 7), 3);
    CHECK_EQ(cp0_ui_roller_centre(79 * 30, 79, 7), 6); /* elastic overscroll past the end */
    CHECK_EQ(cp0_ui_roller_centre(100, 79, 0), -1);
    CHECK_EQ(cp0_ui_roller_centre(100, 0, 3), 0);

    CHECK_EQ(cp0_ui_roller_snap(0, 44, 5), 0);
    CHECK_EQ(cp0_ui_roller_snap(21, 44, 5), 0);
    CHECK_EQ(cp0_ui_roller_snap(22, 44, 5), 44);
    CHECK_EQ(cp0_ui_roller_snap(44 * 2 + 30, 44, 5), 44 * 3);
    CHECK_EQ(cp0_ui_roller_snap(44 * 9, 44, 5), 44 * 4);
    CHECK_EQ(cp0_ui_roller_snap(50, 44, 0), 0);
    for (int y = -50; y < 44 * 6; y += 7) /* a snapped offset is a fixed point */
        CHECK_EQ(cp0_ui_roller_snap(cp0_ui_roller_snap(y, 44, 5), 44, 5), cp0_ui_roller_snap(y, 44, 5));
}

static void test_row_at(void)
{
    /* body 362 high, rows 79, edge pad 141: row 0 at the top of the list sits under the bar (141..219) */
    CHECK_EQ(cp0_ui_roller_row_at(0, 0, 141, 79, 7), -1);
    CHECK_EQ(cp0_ui_roller_row_at(140, 0, 141, 79, 7), -1);
    CHECK_EQ(cp0_ui_roller_row_at(141, 0, 141, 79, 7), 0);
    CHECK_EQ(cp0_ui_roller_row_at(219, 0, 141, 79, 7), 0);
    CHECK_EQ(cp0_ui_roller_row_at(220, 0, 141, 79, 7), 1);
    CHECK_EQ(cp0_ui_roller_row_at(361, 0, 141, 79, 7), 2);
    /* scrolled to row 3: the bar shows row 3, row 2 above it, row 6 is the last */
    CHECK_EQ(cp0_ui_roller_row_at(141, 79 * 3, 141, 79, 7), 3);
    CHECK_EQ(cp0_ui_roller_row_at(100, 79 * 3, 141, 79, 7), 2);
    CHECK_EQ(cp0_ui_roller_row_at(141 + 79 * 3, 79 * 3, 141, 79, 7), 6);
    CHECK_EQ(cp0_ui_roller_row_at(141 + 79 * 4, 79 * 3, 141, 79, 7), -1);
    CHECK_EQ(cp0_ui_roller_row_at(10, 0, 0, 79, 0), -1);
}

static void test_step(void)
{
    CHECK_EQ(cp0_ui_roller_step(0, 1, 5, 1), 1);
    CHECK_EQ(cp0_ui_roller_step(4, 1, 5, 1), 0);  /* lists wrap like today's roller */
    CHECK_EQ(cp0_ui_roller_step(0, -1, 5, 1), 4);
    CHECK_EQ(cp0_ui_roller_step(2, -7, 5, 1), 0);
    CHECK_EQ(cp0_ui_roller_step(4, 1, 5, 0), 4);  /* value lists stop at the ends */
    CHECK_EQ(cp0_ui_roller_step(0, -1, 5, 0), 0);
    CHECK_EQ(cp0_ui_roller_step(2, 1, 5, 0), 3);
    CHECK_EQ(cp0_ui_roller_step(9, 0, 5, 0), 4);  /* an index out of range is clamped first */
    CHECK_EQ(cp0_ui_roller_step(0, 1, 0, 1), -1);
    CHECK_EQ(cp0_ui_roller_step(0, 1, 1, 1), 0);
}

static void test_falloff(void)
{
    /* sizes the font set has: even, 8..48, an odd px takes the larger one */
    CHECK_EQ(cp0_ui_roller_font_px(19), 20);
    CHECK_EQ(cp0_ui_roller_font_px(34), 34);
    CHECK_EQ(cp0_ui_roller_font_px(35), 36);
    CHECK_EQ(cp0_ui_roller_font_px(3), 8);
    CHECK_EQ(cp0_ui_roller_font_px(49), 48);
    CHECK_EQ(cp0_ui_roller_font_px(60), 48);

    /* 8 % smaller per step, never under 70 %, snapped */
    CHECK_EQ(cp0_ui_roller_text_px(30, 0), 30);
    CHECK_EQ(cp0_ui_roller_text_px(30, 1), 28); /* 27.6 */
    CHECK_EQ(cp0_ui_roller_text_px(30, 2), 26); /* 25.2 -> 25 -> 26 */
    CHECK_EQ(cp0_ui_roller_text_px(30, 3), 24); /* 22.8 -> 23 -> 24 */
    CHECK_EQ(cp0_ui_roller_text_px(30, 4), 22); /* 70 %: 21 -> 22 */
    CHECK_EQ(cp0_ui_roller_text_px(30, 9), 22);
    CHECK_EQ(cp0_ui_roller_text_px(30, -2), 26);
    CHECK_EQ(cp0_ui_roller_text_px(34, 1), 32);
    CHECK_EQ(cp0_ui_roller_text_px(34, 2), 30);
    CHECK_EQ(cp0_ui_roller_text_px(34, 4), 24);
    CHECK_EQ(cp0_ui_roller_text_px(20, 1), 18);
    CHECK_EQ(cp0_ui_roller_text_px(20, 4), 14);
    CHECK_EQ(cp0_ui_roller_text_px(8, 6), 8);   /* the 8 px floor */
    CHECK_EQ(cp0_ui_roller_text_px(36, 1000000), 26);
    for (int px = 8; px <= 48; px += 2) {
        CHECK_EQ(cp0_ui_roller_text_px(px, 0), px);
        for (int d = 0; d < 12; ++d) {
            CHECK(cp0_ui_roller_text_px(px, d + 1) <= cp0_ui_roller_text_px(px, d));
            CHECK(cp0_ui_roller_text_px(px, d) % 2 == 0);
            CHECK(cp0_ui_roller_text_px(px, d) * 10 + 10 >= px * 7); /* 70 % (to the px): rows stay readable */
        }
    }

    /* opacity 1 - 0.2 d, at least 0.35 */
    CHECK_EQ(cp0_ui_roller_opa(0), 255);
    CHECK_EQ(cp0_ui_roller_opa(1), 204);
    CHECK_EQ(cp0_ui_roller_opa(2), 153);
    CHECK_EQ(cp0_ui_roller_opa(3), 102);
    CHECK_EQ(cp0_ui_roller_opa(4), 89);
    CHECK_EQ(cp0_ui_roller_opa(-1), 204);
    CHECK_EQ(cp0_ui_roller_opa(1 << 30), 89);
}

static void test_width_cap(void)
{
    CHECK_EQ(cp0_ui_roller_width(CP0_UI_STANDARD, 640, 16), 560);
    CHECK_EQ(cp0_ui_roller_width(CP0_UI_WIDE, 800, 14), 560);
    CHECK_EQ(cp0_ui_roller_width(CP0_UI_SQUARE, 720, 16), 560);
    CHECK_EQ(cp0_ui_roller_width(CP0_UI_LARGE, 1280, 16), 640);
    CHECK_EQ(cp0_ui_roller_width(CP0_UI_COMPACT, 480, 10), 460); /* narrower than the cap: margins only */
    CHECK_EQ(cp0_ui_roller_width(CP0_UI_LARGE, 600, 16), 568);
    CHECK_EQ(cp0_ui_roller_width(CP0_UI_STANDARD, 20, 16), 0);
    CHECK_EQ(cp0_ui_roller_edge_pad(362, 79), 141);
    CHECK_EQ(cp0_ui_roller_edge_pad(40, 79), 0);
}

typedef struct {
    int w, h, ppmm, strip_left;
    int header_h, title_max_w, esc_x, esc_y, esc_h;
    int body_h, roller_x, roller_w, row_h, edge_pad, text_px, value_px, chevron_px, caption_w;
} expected_page_t;

static void check_page(const expected_page_t *e)
{
    cp0_ui_metrics_t m;
    CHECK_EQ(cp0_ui_metrics_compute(e->w, e->h, 0, e->ppmm, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
    cp0_ui_roller_page_t p;
    memset(&p, 0xAB, sizeof(p));
    CHECK_EQ(cp0_ui_roller_page(&m, e->strip_left, &p), 0);
    CHECK_EQ(p.w, e->w);
    CHECK_EQ(p.h, e->h);
    /* the header is the home grid's bar; title and status strip are the home grid's, from the same metrics */
    CHECK_EQ(p.header_h, m.shell.bar_h);
    CHECK_EQ(p.header_h, e->header_h);
    CHECK_EQ(p.title_x, m.shell.title_x);
    CHECK_EQ(p.title_y, m.shell.title_y);
    CHECK_EQ(p.title_px, m.shell.title_px);
    CHECK_EQ(p.strip_x, e->w - m.shell.status_w);
    CHECK_EQ(p.strip_w, m.shell.status_w);
    CHECK_EQ(p.strip_h, m.shell.bar_h);
    CHECK_EQ(p.strip_y, 0);
    CHECK_EQ(p.strip_left, e->strip_left);
    CHECK_EQ(p.title_max_w, e->title_max_w);
    /* the Esc: TARGET wide, TARGET high but inside the bar's margins, centred on the pill's line, SPACE_M left of
     * the strip's Bluetooth icon, never over the title */
    CHECK_EQ(p.esc_w, m.tok[CP0_TOK_TARGET]);
    CHECK_EQ(p.esc_h, e->esc_h);
    CHECK(p.esc_h <= m.tok[CP0_TOK_TARGET]);
    CHECK(p.esc_h <= p.header_h - 2 * m.shell.status_top);
    CHECK_EQ(p.esc_y, e->esc_y);
    CHECK_EQ(2 * p.esc_y + p.esc_h, p.header_h);
    CHECK_EQ(p.esc_x, e->esc_x);
    CHECK_EQ(p.esc_x + p.esc_w + m.tok[CP0_TOK_SPACE_M], p.strip_left);
    CHECK(p.title_x + p.title_max_w < p.esc_x);
    CHECK_EQ(p.esc_radius, m.shell.tb_radius);
    CHECK_EQ(p.esc_text_px, m.shell.tb_text_px);
    /* the roller fills the screen under the header, rows from the ROW token, width capped and centred */
    CHECK_EQ(p.body_y, p.header_h);
    CHECK_EQ(p.body_h, e->body_h);
    CHECK_EQ(p.body_y + p.body_h, e->h);
    CHECK_EQ(p.roller_x, e->roller_x);
    CHECK_EQ(p.roller_w, e->roller_w);
    CHECK_EQ(2 * p.roller_x + p.roller_w, e->w);
    CHECK_EQ(p.row_h, e->row_h);
    CHECK_EQ(p.row_h, m.tok[CP0_TOK_ROW_H]);
    CHECK_EQ(p.edge_pad, e->edge_pad);
    CHECK_EQ(p.list_h, p.row_h + 2 * p.edge_pad);
    CHECK(p.list_h <= p.body_h && p.list_h >= p.body_h - 1);
    /* text 0.43 x the row and values 0.9 x that, sizes of the font set; chevrons 0.55 x the row */
    CHECK_EQ(p.text_px, e->text_px);
    CHECK_EQ(p.value_px, e->value_px);
    CHECK_EQ(p.chevron_px, e->chevron_px);
    CHECK(p.text_px * 100 >= p.row_h * 42 && p.text_px * 100 <= p.row_h * 47);
    CHECK_EQ(p.inset, m.tok[CP0_TOK_SPACE_L]);
    CHECK_EQ(p.gap, m.tok[CP0_TOK_SPACE_M]);
    /* status line over the top left of the body, clear of the centred chevron */
    CHECK_EQ(p.caption_x, p.title_x);
    CHECK_EQ(p.caption_y, p.body_y + m.tok[CP0_TOK_SPACE_S]);
    CHECK_EQ(p.caption_px, m.text_px[CP0_TEXT_CAPTION]);
    CHECK_EQ(p.caption_w, e->caption_w);
    CHECK(p.caption_x + p.caption_w < e->w / 2 - p.chevron_px / 2);
}

static void test_page_layout(void)
{
    /* strip_left: the strip canvas' left + where its Bluetooth icon starts (about 104 px at 100 %, 75 px at 72 %) */
    static const expected_page_t pages[] = {
        /* w    h   ppmm  left   hdr tmax escx escy esch body  rx   rw  row  pad txt val chev capw */
        {640, 480, 1130, 424, 56, 270, 306, 8, 40, 424, 40, 560, 79, 172, 34, 32, 43, 241},
        {480, 320, 630, 325, 40, 234, 258, 5, 30, 280, 10, 460, 44, 118, 20, 18, 24, 192},
        {800, 480, 920, 584, 56, 453, 487, 8, 40, 424, 120, 560, 64, 180, 28, 26, 35, 331},
        {720, 720, 1000, 504, 64, 362, 398, 12, 40, 656, 80, 560, 70, 293, 30, 28, 39, 285},
        {1280, 720, 1160, 1064, 64, 908, 944, 12, 40, 656, 320, 640, 81, 287, 36, 32, 45, 559},
    };
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) check_page(&pages[i]);

    /* no measured strip: the Esc goes SPACE_M left of the strip canvas */
    cp0_ui_metrics_t m;
    cp0_ui_roller_page_t p;
    CHECK_EQ(cp0_ui_metrics_compute(640, 480, 0, 1130, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
    CHECK_EQ(cp0_ui_roller_page(&m, 0, &p), 0);
    CHECK_EQ(p.strip_left, 320);
    CHECK_EQ(p.esc_x, 320 - 16 - 102);
    CHECK_EQ(cp0_ui_roller_page(&m, 700, &p), 0); /* past the screen: ignored */
    CHECK_EQ(p.strip_left, 320);

    CHECK_EQ(cp0_ui_roller_page(NULL, 0, &p), -1);
    CHECK_EQ(cp0_ui_metrics_compute(640, 40, 0, 1130, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
    CHECK_EQ(cp0_ui_roller_page(&m, 0, &p), -1); /* no room for a body under the header */
}

int main(void)
{
    test_centre_and_snap();
    test_row_at();
    test_step();
    test_falloff();
    test_width_cap();
    test_page_layout();
    if (failures) {
        fprintf(stderr, "test_ui_roller: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ui_roller: ok\n");
    return 0;
}
