/*
 * Unit test of the InfoRow layout (src/cp0_ui_info.c, task 014 P3a): key and value side by side, the key cap, dots,
 * the two-line wrap on the Compact class, a key alone, the two-line text size.
 */
#include "cp0_ui_info.h"

#include "cp0_ui_metrics.h"
#include "cp0_ui_roller.h"

#include <stdio.h>

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

static void test_wrap_px(void)
{
    /* (row - 8) / 2 / 1.25, even, capped by the row's value size */
    CHECK_EQ(cp0_ui_info_wrap_px(44, 18), 14);  /* 480x320: 36 / 2.5 = 14.4 -> 14 */
    CHECK_EQ(cp0_ui_info_wrap_px(79, 32), 28);  /* deck: 71 / 2.5 = 28.4 -> 28 */
    CHECK_EQ(cp0_ui_info_wrap_px(81, 32), 28);  /* 73 / 2.5 = 29.2 -> 28 (even, rounded down) */
    CHECK_EQ(cp0_ui_info_wrap_px(200, 20), 20); /* never above the row's value size */
    CHECK_EQ(cp0_ui_info_wrap_px(10, 18), CP0_UI_FONT_MIN_PX);
    CHECK_EQ(cp0_ui_info_wrap_px(0, 18), CP0_UI_FONT_MIN_PX);
}

static void test_side_by_side(void)
{
    cp0_ui_info_row_t r;
    /* 480x320 roller: 460 wide, inset 14, gap 10 -> 432 of content */
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 120, 20, 150, 18, 44, 1, &r), 0);
    CHECK_EQ(r.key_w, 120);
    CHECK_EQ(r.value_w, 150);
    CHECK_EQ(r.value_x, 460 - 14 - 150); /* right-aligned at the inset */
    CHECK_EQ(r.value_lines, 1);
    CHECK_EQ(r.value_px, 18);
    CHECK_EQ(r.truncated, 0);
    /* exactly the content width still fits */
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 200, 20, 222, 18, 44, 0, &r), 0);
    CHECK_EQ(r.key_w, 200);
    CHECK_EQ(r.value_w, 222);
    CHECK_EQ(r.truncated, 0);
}

static void test_truncate(void)
{
    cp0_ui_info_row_t r;
    /* deck roller 560, inset 20, gap 16 -> 520; key 150 + value 600: the key keeps its width (under the 45 % cap =
     * 234), the value takes the rest and ends with dots (not Compact: no wrap) */
    CHECK_EQ(cp0_ui_info_row(560, 20, 16, 150, 34, 600, 32, 79, 0, &r), 0);
    CHECK_EQ(r.key_w, 150);
    CHECK_EQ(r.value_w, 520 - 16 - 150);
    CHECK_EQ(r.value_x, 560 - 20 - r.value_w);
    CHECK_EQ(r.value_lines, 1);
    CHECK_EQ(r.value_px, 32);
    CHECK_EQ(r.truncated, 1);
    /* a long key and a long value: the key is capped at 45 % (234), the value takes the rest */
    CHECK_EQ(cp0_ui_info_row(560, 20, 16, 400, 34, 400, 32, 79, 0, &r), 0);
    CHECK_EQ(r.key_w, 520 * 45 / 100);
    CHECK_EQ(r.key_cut, 1);
    CHECK_EQ(r.value_w, 520 - 16 - 234);
    CHECK_EQ(r.truncated, 1);
    /* a long key and a short value: the key gets all the value leaves it, no dots on the value */
    CHECK_EQ(cp0_ui_info_row(560, 20, 16, 600, 34, 60, 32, 79, 0, &r), 0);
    CHECK_EQ(r.key_w, 520 - 16 - 60);
    CHECK_EQ(r.value_w, 60);
    CHECK_EQ(r.truncated, 0);
}

static void test_compact_wrap(void)
{
    cp0_ui_info_row_t r;
    /* 480x320: "Password" + "Password changes are not supported" at 18 px (say 300 px): content 432, key 80 ->
     * value room 342; wraps (Compact) at 14 px: 300 * 14 / 18 = 233 fits one line at the smaller size */
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 80, 20, 360, 18, 44, 1, &r), 0);
    CHECK_EQ(r.key_w, 80);
    CHECK_EQ(r.value_w, 432 - 10 - 80);
    CHECK_EQ(r.value_px, 14);
    CHECK_EQ(r.value_lines, 1); /* 360 * 14 / 18 = 280 <= 342 */
    CHECK_EQ(r.truncated, 0);
    /* longer: two lines at 14 px, no dots while it fits twice the room */
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 80, 20, 600, 18, 44, 1, &r), 0);
    CHECK_EQ(r.value_px, 14);
    CHECK_EQ(r.value_lines, 2); /* 600 * 14 / 18 = 466 > 342 */
    CHECK_EQ(r.truncated, 0);   /* <= 684 */
    /* much longer: two lines and dots */
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 80, 20, 1000, 18, 44, 1, &r), 0);
    CHECK_EQ(r.value_lines, 2);
    CHECK_EQ(r.truncated, 1); /* 777 > 684 */
    /* the same row off Compact: one line with dots at the row size */
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 80, 20, 600, 18, 44, 0, &r), 0);
    CHECK_EQ(r.value_lines, 1);
    CHECK_EQ(r.value_px, 18);
    CHECK_EQ(r.truncated, 1);
}

static void test_key_alone_and_errors(void)
{
    cp0_ui_info_row_t r;
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 150, 20, 0, 18, 44, 1, &r), 0);
    CHECK_EQ(r.key_w, 150);
    CHECK_EQ(r.value_w, 0);
    CHECK_EQ(r.value_lines, 1);
    CHECK_EQ(r.key_lines, 1);
    CHECK_EQ(r.key_px, 20);
    CHECK_EQ(r.key_cut, 0);
    /* longer than the row: two lines at the two-line size (any class), dots only past two lines */
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 900, 20, 0, 18, 44, 0, &r), 0);
    CHECK_EQ(r.key_w, 432);
    CHECK_EQ(r.key_px, 14);   /* cp0_ui_info_wrap_px(44, 20) */
    CHECK_EQ(r.key_lines, 2); /* 900 * 14 / 20 = 630 > 432 */
    CHECK_EQ(r.key_cut, 0);   /* <= 864 */
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 500, 20, 0, 18, 44, 0, &r), 0);
    CHECK_EQ(r.key_lines, 1); /* 500 * 14 / 20 = 350: one line at the smaller size */
    CHECK_EQ(r.key_px, 14);
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 2000, 20, 0, 18, 44, 1, &r), 0);
    CHECK_EQ(r.key_cut, 1); /* 1400 > 864 */
    CHECK_EQ(cp0_ui_info_row(460, 14, 10, 1, 20, 1, 18, 44, 1, NULL), -1);
    CHECK_EQ(cp0_ui_info_row(20, 14, 10, 1, 20, 1, 18, 44, 1, &r), -1); /* no room */
}

/* the real roller rows at the five target sizes: every row keeps inside the roller, the key never passes the cap
 * while the value needs room, and only Compact wraps */
static void test_target_sizes(void)
{
    static const struct {
        int w, h, ppmm;
    } sizes[] = {{480, 320, 630}, {640, 480, 1130}, {800, 480, 920}, {720, 720, 1000}, {1280, 720, 1160}};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        cp0_ui_metrics_t m;
        cp0_ui_roller_page_t p;
        CHECK_EQ(cp0_ui_metrics_compute(sizes[i].w, sizes[i].h, 0, sizes[i].ppmm, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
        CHECK_EQ(cp0_ui_roller_page(&m, 0, &p), 0);
        const int wrap = m.cls == CP0_UI_COMPACT;
        for (int key = 0; key <= 700; key += 70)
            for (int value = 1; value <= 1400; value += 97) {
                cp0_ui_info_row_t r;
                CHECK_EQ(cp0_ui_info_row(p.roller_w, p.inset, p.gap, key, p.text_px, value, p.value_px, p.row_h, wrap,
                                         &r),
                         0);
                CHECK(r.key_w >= 0 && r.value_w >= 0);
                CHECK(r.key_w + p.gap + r.value_w <= p.roller_w - 2 * p.inset);
                CHECK_EQ(r.value_x + r.value_w, p.roller_w - p.inset);
                CHECK(r.value_lines == 1 || (wrap && r.value_lines == 2));
                if (r.value_lines == 2) CHECK(2 * r.value_px * CP0_UI_INFO_LINE_X100 / 100 <= p.row_h);
                if (r.value_w < value && key > (p.roller_w - 2 * p.inset) * CP0_UI_INFO_KEY_MAX_PCT / 100)
                    CHECK(r.key_w >= (p.roller_w - 2 * p.inset) * CP0_UI_INFO_KEY_MAX_PCT / 100);
            }
    }
}

int main(void)
{
    test_wrap_px();
    test_side_by_side();
    test_truncate();
    test_compact_wrap();
    test_key_alone_and_errors();
    test_target_sizes();
    if (failures) {
        fprintf(stderr, "test_ui_info: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ui_info: ok\n");
    return 0;
}
