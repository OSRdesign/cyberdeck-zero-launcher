/*
 * Unit test of the dialog layout (src/cp0_ui_dialog.c, task 014 P3b): sizes from the tokens, centred under the header
 * without a keyboard, re-centred in the room above the on-screen keyboard, the steps when it does not fit (buttons
 * go, tighter padding, over the header, the body scrolls), and the Wi-Fi password dialog at the five target sizes
 * with their real keyboards (cp0_ui_osk_layout).
 */
#include "cp0_ui_dialog.h"

#include "cp0_ui_osk.h"

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

static cp0_ui_metrics_t deck(void)
{
    cp0_ui_metrics_t m;
    cp0_ui_metrics_compute(640, 480, 0, 0, CP0_UI_DENSITY_DEFAULT, 0, &m);
    return m;
}

static void test_tokens(void)
{
    const cp0_ui_metrics_t m = deck(); /* Standard: dialog 560, space 8 / 16, radius 18, heading 28, body 24 */
    cp0_ui_dialog_t d;
    CHECK_EQ(cp0_ui_dialog_layout(&m, 56, 0, 0, 0, 2, &d), 0);
    CHECK_EQ(d.w, 560);
    CHECK_EQ(d.x, 40);
    CHECK_EQ(d.pad, 16);
    CHECK_EQ(d.pad_y, 16);
    CHECK_EQ(d.gap, 8);
    CHECK_EQ(d.radius, 18);
    CHECK_EQ(d.title_px, 28);
    CHECK_EQ(d.text_px, 24);
    CHECK_EQ(d.button_px, 24);
    CHECK_EQ(d.button_h, m.tok[CP0_TOK_TARGET]); /* 9 mm: 102 on the deck */
    CHECK_EQ(d.button_h, 102);
    CHECK_EQ(d.content_w, 560 - 32);
    CHECK_EQ(d.buttons, 2);
    CHECK_EQ(d.button_w, (528 - 8) / 2);
    CHECK_EQ(cp0_ui_dialog_layout(NULL, 0, 0, 0, 0, 0, &d), -1);
    CHECK_EQ(cp0_ui_dialog_layout(&m, 0, 0, 0, 0, 0, NULL), -1);
}

static void test_centred_without_keyboard(void)
{
    const cp0_ui_metrics_t m = deck();
    cp0_ui_dialog_t d;
    /* title 31, body 120, 2 buttons: 16 + 31 + 8 + 120 + 8 + 102 + 16 = 301 in the 424 under the header */
    CHECK_EQ(cp0_ui_dialog_layout(&m, 56, 0, 31, 120, 2, &d), 0);
    CHECK_EQ(d.h, 301);
    CHECK_EQ(d.y, 56 + (424 - 301) / 2);
    CHECK_EQ(d.title_y, 16);
    CHECK_EQ(d.body_y, 16 + 31 + 8);
    CHECK_EQ(d.body_h, 120);
    CHECK_EQ(d.scrolls, 0);
    CHECK_EQ(d.button_y, 55 + 120 + 8);
    CHECK_EQ(d.button_y + d.button_h + d.pad_y, d.h);
    CHECK_EQ(d.covers_header, 0);
    /* no title, no buttons: the body alone */
    CHECK_EQ(cp0_ui_dialog_layout(&m, 56, 0, 0, 100, 0, &d), 0);
    CHECK_EQ(d.h, 132);
    CHECK_EQ(d.body_y, 16);
    CHECK_EQ(d.button_y, 0);
    CHECK_EQ(d.button_w, 0);
    /* too tall without a keyboard: the buttons stay, the header too, the body scrolls */
    CHECK_EQ(cp0_ui_dialog_layout(&m, 56, 0, 31, 900, 3, &d), 0);
    CHECK_EQ(d.buttons, 3);
    CHECK_EQ(d.pad_y, 16);
    CHECK_EQ(d.h, 424);
    CHECK_EQ(d.y, 56);
    CHECK_EQ(d.body_h, 424 - (16 + 31 + 8 + 8 + 102 + 16));
    CHECK_EQ(d.scrolls, 1);
    CHECK_EQ(d.covers_header, 0);
    CHECK_EQ(d.button_w, (528 - 2 * 8) / 3);
}

static void test_above_keyboard(void)
{
    const cp0_ui_metrics_t m = deck();
    cp0_ui_dialog_t d;
    /* a 100 px keyboard: the room is 56..380 (324); 301 fits, centred there */
    CHECK_EQ(cp0_ui_dialog_layout(&m, 56, 100, 31, 120, 2, &d), 0);
    CHECK_EQ(d.buttons, 2);
    CHECK_EQ(d.h, 301);
    CHECK_EQ(d.y, 56 + (324 - 301) / 2);
    CHECK(d.y + d.h <= 480 - 100);
    /* the deck's keyboard (299): room 125. Step 1: no buttons (191 > 125); step 2: padding 8 (175 > 125), then over the
     * header: room 181, 175 fits, centred in 0..181 */
    CHECK_EQ(cp0_ui_dialog_layout(&m, 56, 299, 31, 120, 2, &d), 0);
    CHECK_EQ(d.buttons, 0);
    CHECK_EQ(d.pad_y, 8);
    CHECK_EQ(d.pad, 16);
    CHECK_EQ(d.h, 8 + 31 + 8 + 120 + 8);
    CHECK_EQ(d.y, (181 - d.h) / 2);
    CHECK_EQ(d.covers_header, 1);
    CHECK_EQ(d.scrolls, 0);
    CHECK_EQ(d.body_h, 120);
    CHECK(d.y + d.h <= 480 - 299);
    /* step 1 is enough: the buttons go (431 > 324), the rest fits under the header (321) */
    CHECK_EQ(cp0_ui_dialog_layout(&m, 56, 100, 31, 250, 2, &d), 0);
    CHECK_EQ(d.buttons, 0);
    CHECK_EQ(d.pad_y, 16);
    CHECK_EQ(d.h, 16 + 31 + 8 + 250 + 16);
    CHECK_EQ(d.covers_header, 0);
    /* nothing fits: the whole room over the header, the body scrolls */
    CHECK_EQ(cp0_ui_dialog_layout(&m, 56, 299, 31, 400, 2, &d), 0);
    CHECK_EQ(d.buttons, 0);
    CHECK_EQ(d.y, 0);
    CHECK_EQ(d.h, 181);
    CHECK_EQ(d.body_h, 181 - (8 + 31 + 8 + 8));
    CHECK_EQ(d.scrolls, 1);
}

/* The Wi-Fi password dialog (title, a Password field: label + ROW high box, a one-line status) at the five target
 * sizes with their real keyboards: it always sits above the keyboard, inside the screen, the field box always fits
 * the body (the label may scroll away), and the buttons show whenever there is room for them. */
static void test_password_dialog_at_target_sizes(void)
{
    static const struct {
        int w, h, ppmm;
        int buttons_with_keyboard; /* expected: buttons shown while the keyboard is up */
        int covers_header;         /* expected: the dialog lies over the header while the keyboard is up */
    } sizes[] = {
        /* the keyboard leaves no room for 9 mm buttons under a title, a field and its status line at any size: the
         * keyboard's Enter and Esc act for them; at 480x320 and 640x480 the dialog also lies over the header */
        {480, 320, 630, 0, 1}, {640, 480, 1130, 0, 1}, {800, 480, 920, 0, 0}, {720, 720, 1000, 0, 0}, {1280, 720, 1160, 0, 0},
    };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        cp0_ui_metrics_t m;
        cp0_ui_osk_layout_t k;
        CHECK_EQ(cp0_ui_metrics_compute(sizes[i].w, sizes[i].h, 0, sizes[i].ppmm, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
        CHECK_EQ(cp0_ui_osk_layout(&m, &k), 0);
        const int header = m.shell.bar_h;
        const int title = m.text_px[CP0_TEXT_HEADING] * 5 / 4;
        const int label = m.text_px[CP0_TEXT_CAPTION] * 5 / 4;
        const int row = m.tok[CP0_TOK_ROW_H];
        const int status = m.text_px[CP0_TEXT_BODY] * 5 / 4;
        const int content = label + 4 + row + m.tok[CP0_TOK_SPACE_S] + status;
        cp0_ui_dialog_t d;

        /* no keyboard: everything, under the header */
        CHECK_EQ(cp0_ui_dialog_layout(&m, header, 0, title, content, 2, &d), 0);
        CHECK_EQ(d.buttons, 2);
        CHECK_EQ(d.scrolls, 0);
        CHECK(d.y >= header);
        CHECK(d.y + d.h <= m.h);
        CHECK(d.x >= 0 && d.x + d.w <= m.w);
        CHECK(d.button_h >= 48);

        /* keyboard up */
        CHECK_EQ(cp0_ui_dialog_layout(&m, header, k.height, title, content, 2, &d), 0);
        if (d.buttons != (sizes[i].buttons_with_keyboard ? 2 : 0))
            fprintf(stderr, "  %dx%d: buttons %d with the keyboard (%d px)\n", m.w, m.h, d.buttons, k.height);
        CHECK_EQ(d.buttons, sizes[i].buttons_with_keyboard ? 2 : 0);
        CHECK_EQ(d.covers_header, sizes[i].covers_header);
        CHECK(d.y >= 0);
        CHECK(d.y + d.h <= m.h - k.height); /* never under the keyboard */
        CHECK(d.body_h >= row);             /* the field box always shows */
        if (!d.covers_header) CHECK(d.y >= header);
    }
}

int main(void)
{
    test_tokens();
    test_centred_without_keyboard();
    test_above_keyboard();
    test_password_dialog_at_target_sizes();
    if (failures) {
        fprintf(stderr, "test_ui_dialog: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ui_dialog: ok\n");
    return 0;
}
