/*
 * Unit test of the layout service (src/cp0_ui_metrics.c, src/cp0/cp0_ui_state.c): screen classes, density
 * sources and fallbacks, token floors, the five target sizes of report 022, odd sizes, the pinned deck and Pi 3A+
 * presets (exactly the numbers native_ui.cpp drew before P1b), the compat window rule, determinism, the screen state
 * text and its atomic write.
 */
#define _GNU_SOURCE

#include "cp0_ui_metrics.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        const long long va_ = (long long)(a), vb_ = (long long)(b);                                        \
        if (va_ != vb_) {                                                                                  \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, \
                    va_, vb_);                                                                             \
            failures++;                                                                                    \
        }                                                                                                  \
    } while (0)

static cp0_ui_metrics_t compute(int w, int h, int ppmm, int cap)
{
    cp0_ui_metrics_t m;
    memset(&m, 0xAB, sizeof(m));
    CHECK(cp0_ui_metrics_compute(w, h, 0, ppmm, ppmm > 0 ? CP0_UI_DENSITY_PANEL_MM : CP0_UI_DENSITY_DEFAULT, cap,
                                 &m) == 0);
    return m;
}

static void check_compat(const cp0_ui_metrics_t *m, int scale, int x, int y, int toolbar_y, int toolbar_h)
{
    CHECK_EQ(m->compat.scale, scale);
    CHECK_EQ(m->compat.w, 320 * scale);
    CHECK_EQ(m->compat.h, 170 * scale);
    CHECK_EQ(m->compat.x, x);
    CHECK_EQ(m->compat.y, y);
    CHECK_EQ(m->compat.toolbar_y, toolbar_y);
    CHECK_EQ(m->compat.toolbar_h, toolbar_h);
    CHECK_EQ(m->compat.toolbar_y + m->compat.toolbar_h, m->h); /* the toolbar reaches the bottom */
    if (m->compat.toolbar_h > 0) CHECK(m->compat.y + m->compat.h <= m->compat.toolbar_y); /* window above it */
}

static void check_tokens(const cp0_ui_metrics_t *m, int s, int sm, int sl, int rs, int rm, int rl, int header,
                         int status, int row, int target, int toolbar, int icon, int dialog)
{
    CHECK_EQ(m->tok[CP0_TOK_SPACE_S], s);
    CHECK_EQ(m->tok[CP0_TOK_SPACE_M], sm);
    CHECK_EQ(m->tok[CP0_TOK_SPACE_L], sl);
    CHECK_EQ(m->tok[CP0_TOK_RADIUS_S], rs);
    CHECK_EQ(m->tok[CP0_TOK_RADIUS_M], rm);
    CHECK_EQ(m->tok[CP0_TOK_RADIUS_L], rl);
    CHECK_EQ(m->tok[CP0_TOK_HEADER_H], header);
    CHECK_EQ(m->tok[CP0_TOK_STATUS_PCT], status);
    CHECK_EQ(m->tok[CP0_TOK_ROW_H], row);
    CHECK_EQ(m->tok[CP0_TOK_TARGET], target);
    CHECK_EQ(m->tok[CP0_TOK_TOOLBAR_H], toolbar);
    CHECK_EQ(m->tok[CP0_TOK_TILE_ICON], icon);
    CHECK_EQ(m->tok[CP0_TOK_DIALOG_MAX_W], dialog);
}

static void check_text(const cp0_ui_metrics_t *m, int title, int heading, int body, int caption)
{
    CHECK_EQ(m->text_px[CP0_TEXT_TITLE], title);
    CHECK_EQ(m->text_px[CP0_TEXT_HEADING], heading);
    CHECK_EQ(m->text_px[CP0_TEXT_BODY], body);
    CHECK_EQ(m->text_px[CP0_TEXT_CAPTION], caption);
    CHECK_EQ(m->text_px[CP0_TEXT_SYMBOL], title);
    for (int i = 0; i < CP0_TEXT__COUNT; i++) /* every text size is a builtin Montserrat size */
        CHECK(m->text_px[i] >= 8 && m->text_px[i] <= 48 && m->text_px[i] % 2 == 0);
}

/* ------------------------------------------------------------------ classes */

static void test_classes(void)
{
    static const struct {
        int w, h;
        cp0_ui_class_t cls;
    } cases[] = {
        /* the five targets */
        {480, 320, CP0_UI_COMPACT}, {640, 480, CP0_UI_STANDARD}, {800, 480, CP0_UI_WIDE},
        {720, 720, CP0_UI_SQUARE},  {1280, 720, CP0_UI_LARGE},
        /* other panels */
        {854, 480, CP0_UI_WIDE},    {1024, 600, CP0_UI_WIDE},    {960, 540, CP0_UI_WIDE},
        {800, 600, CP0_UI_STANDARD}, {720, 540, CP0_UI_STANDARD}, {1024, 768, CP0_UI_LARGE},
        {1280, 800, CP0_UI_LARGE},  {1920, 1080, CP0_UI_LARGE},  {1280, 400, CP0_UI_WIDE},
        {1280, 390, CP0_UI_COMPACT},
        {480, 480, CP0_UI_SQUARE},  {1080, 1080, CP0_UI_SQUARE}, {320, 320, CP0_UI_COMPACT},
        {320, 170, CP0_UI_COMPACT}, {320, 480, CP0_UI_PORTRAIT}, {480, 800, CP0_UI_PORTRAIT},
        /* boundaries: a = 1.25 is square, just above it standard; a = 1.5 is wide; a = 0.8 is square */
        {600, 480, CP0_UI_SQUARE},  {601, 480, CP0_UI_STANDARD}, {719, 480, CP0_UI_STANDARD},
        {720, 480, CP0_UI_WIDE},    {480, 600, CP0_UI_SQUARE},   {479, 600, CP0_UI_PORTRAIT},
        {600, 399, CP0_UI_COMPACT}, {600, 400, CP0_UI_WIDE},     {1100, 699, CP0_UI_WIDE},
        {1100, 700, CP0_UI_LARGE},  {1000, 699, CP0_UI_STANDARD},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const cp0_ui_class_t got = cp0_ui_classify(cases[i].w, cases[i].h);
        if (got != cases[i].cls) {
            fprintf(stderr, "classify %dx%d = %s, expected %s\n", cases[i].w, cases[i].h, cp0_ui_class_name(got),
                    cp0_ui_class_name(cases[i].cls));
            failures++;
        }
    }
    CHECK(strcmp(cp0_ui_class_name(CP0_UI_STANDARD), "standard") == 0);
    CHECK(strcmp(cp0_ui_class_name(CP0_UI_LARGE), "large") == 0);
    CHECK(strcmp(cp0_ui_density_src_name(CP0_UI_DENSITY_PANEL_MM), "panel_mm") == 0);
    cp0_ui_metrics_t m;
    CHECK(cp0_ui_metrics_compute(0, 480, 0, 0, CP0_UI_DENSITY_DEFAULT, 0, &m) == -1);
    CHECK(cp0_ui_metrics_compute(640, -1, 0, 0, CP0_UI_DENSITY_DEFAULT, 0, &m) == -1);
    CHECK(cp0_ui_metrics_compute(640, 480, 0, 0, CP0_UI_DENSITY_DEFAULT, 0, NULL) == -1);
}

/* ------------------------------------------------------------------ density */

static void test_panel_mm_parse(void)
{
    int w = 0, h = 0;
    CHECK(cp0_ui_parse_panel_mm("79x49", &w, &h) == 0 && w == 790 && h == 490);
    CHECK(cp0_ui_parse_panel_mm("56.6x42.5", &w, &h) == 0 && w == 566 && h == 425);
    CHECK(cp0_ui_parse_panel_mm("79X49", &w, &h) == 0 && w == 790 && h == 490);
    CHECK(cp0_ui_parse_panel_mm("0.5x0.5", &w, &h) == 0 && w == 5 && h == 5);
    static const char *bad[] = {"", "x", "79", "79x", "x49", "0x49", "79x0", "-79x49", "79x-49", "79x49x3",
                                "79,49", "79 x49", "79x49 ", "79.x49", "79.25x49", "79x49mm", "abc", "100001x5"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        if (cp0_ui_parse_panel_mm(bad[i], &w, &h) != -1) {
            fprintf(stderr, "parse_panel_mm accepted \"%s\"\n", bad[i]);
            failures++;
        }
    }
    CHECK(cp0_ui_parse_panel_mm(NULL, &w, &h) == -1);
}

static void test_density(void)
{
    /* mean of the two axes: the Luckfox 3.5" (79x49 mm landscape) is 6.08 and 6.53 px/mm -> 6.30 */
    CHECK_EQ(cp0_ui_ppmm_from_mm(480, 320, 790, 490), 630);
    CHECK_EQ(cp0_ui_ppmm_from_mm(720, 720, 720, 720), 1000);
    CHECK_EQ(cp0_ui_ppmm_from_mm(640, 480, 570, 430), 1120); /* the deck's 2.8" spec size, 57x43 mm */
    CHECK_EQ(cp0_ui_ppmm_from_mm(0, 480, 570, 430), 0);
    CHECK_EQ(cp0_ui_ppmm_from_mm(640, 480, 0, 430), 0);

    /* 1. APPLAUNCH_PANEL_MM wins over the framebuffer's mm */
    cp0_ui_density_t d = cp0_ui_density_resolve(480, 320, 90, "79x49", 10, 10);
    CHECK(d.src == CP0_UI_DENSITY_PANEL_MM && d.ppmm_x100 == 630 && !d.panel_mm_rejected);
    /* written portrait (as the overlay's width-mm/height-mm): turned to the canvas' orientation */
    d = cp0_ui_density_resolve(480, 320, 90, "49x79", 0, 0);
    CHECK(d.src == CP0_UI_DENSITY_PANEL_MM && d.ppmm_x100 == 630);
    /* a square panel is never swapped */
    d = cp0_ui_density_resolve(720, 720, 0, "72x72", 0, 0);
    CHECK(d.src == CP0_UI_DENSITY_PANEL_MM && d.ppmm_x100 == 1000);
    /* malformed: flagged, next source */
    d = cp0_ui_density_resolve(640, 480, 0, "57mm", 57, 43);
    CHECK(d.panel_mm_rejected && d.src == CP0_UI_DENSITY_FBDEV && d.ppmm_x100 == 1120);
    /* out of range (1x1 mm on 640x480 = 560 px/mm): flagged, falls through to the default */
    d = cp0_ui_density_resolve(640, 480, 0, "1x1", 0, 0);
    CHECK(d.panel_mm_rejected && d.src == CP0_UI_DENSITY_DEFAULT && d.ppmm_x100 == CP0_UI_DEFAULT_PPMM_X100);
    /* empty value = not set */
    d = cp0_ui_density_resolve(640, 480, 0, "", 0, 0);
    CHECK(!d.panel_mm_rejected && d.src == CP0_UI_DENSITY_DEFAULT && d.ppmm_x100 == 1130);

    /* 2. the framebuffer's mm, in buffer orientation: swapped for rot 90/270 */
    d = cp0_ui_density_resolve(480, 320, 90, NULL, 49, 79);
    CHECK(d.src == CP0_UI_DENSITY_FBDEV && d.ppmm_x100 == 630);
    d = cp0_ui_density_resolve(480, 320, 270, NULL, 49, 79);
    CHECK(d.src == CP0_UI_DENSITY_FBDEV && d.ppmm_x100 == 630);
    d = cp0_ui_density_resolve(480, 320, 0, NULL, 79, 49);
    CHECK(d.src == CP0_UI_DENSITY_FBDEV && d.ppmm_x100 == 630);
    d = cp0_ui_density_resolve(800, 480, 0, NULL, 87, 52);
    CHECK(d.src == CP0_UI_DENSITY_FBDEV && d.ppmm_x100 == 921);
    /* unknown (0, or the u32 -1 some drivers report), bogus (1 mm), absurd: the default */
    d = cp0_ui_density_resolve(640, 480, 0, NULL, 0, 0);
    CHECK(d.src == CP0_UI_DENSITY_DEFAULT && d.ppmm_x100 == 1130);
    d = cp0_ui_density_resolve(640, 480, 0, NULL, -1, -1);
    CHECK(d.src == CP0_UI_DENSITY_DEFAULT);
    d = cp0_ui_density_resolve(640, 480, 0, NULL, 1, 1);
    CHECK(d.src == CP0_UI_DENSITY_DEFAULT);
    d = cp0_ui_density_resolve(640, 480, 0, NULL, 0, 43);
    CHECK(d.src == CP0_UI_DENSITY_DEFAULT);
    d = cp0_ui_density_resolve(640, 480, 0, NULL, 2000000000, 2000000000);
    CHECK(d.src == CP0_UI_DENSITY_DEFAULT);

    /* the density reaches the metrics; no density = the default, recorded as such */
    cp0_ui_metrics_t m;
    CHECK(cp0_ui_metrics_compute(640, 480, 0, 0, CP0_UI_DENSITY_FBDEV, 0, &m) == 0);
    CHECK(m.ppmm_x100 == 1130 && m.density_src == CP0_UI_DENSITY_DEFAULT);
    CHECK(cp0_ui_metrics_compute(640, 480, 0, 1120, CP0_UI_DENSITY_FBDEV, 0, &m) == 0);
    CHECK(m.ppmm_x100 == 1120 && m.density_src == CP0_UI_DENSITY_FBDEV);
}

static void test_mm_and_fonts(void)
{
    CHECK_EQ(cp0_ui_mm_px(1130, 70, 40), 79);  /* 7 mm row on the deck */
    CHECK_EQ(cp0_ui_mm_px(1130, 90, 48), 102); /* 9 mm target */
    CHECK_EQ(cp0_ui_mm_px(610, 70, 40), 43);
    CHECK_EQ(cp0_ui_mm_px(610, 90, 48), 55);
    CHECK_EQ(cp0_ui_mm_px(300, 70, 40), 40); /* floors on a 3 px/mm panel */
    CHECK_EQ(cp0_ui_mm_px(300, 90, 48), 48);
    CHECK_EQ(cp0_ui_mm_px(1160, 90, 48), 104);

    /* floors reach the tokens */
    const cp0_ui_metrics_t low = compute(800, 480, 300, 0);
    CHECK_EQ(low.tok[CP0_TOK_ROW_H], CP0_UI_ROW_FLOOR_PX);
    CHECK_EQ(low.tok[CP0_TOK_TARGET], CP0_UI_TARGET_FLOOR_PX);
    CHECK_EQ(low.tok[CP0_TOK_TOOLBAR_H], CP0_UI_TARGET_FLOOR_PX + 2 * 10);

    CHECK_EQ(cp0_ui_font_snap(-5), 8);
    CHECK_EQ(cp0_ui_font_snap(7), 8);
    CHECK_EQ(cp0_ui_font_snap(9), 8);
    CHECK_EQ(cp0_ui_font_snap(23), 22);
    CHECK_EQ(cp0_ui_font_snap(24), 24);
    CHECK_EQ(cp0_ui_font_snap(47), 46);
    CHECK_EQ(cp0_ui_font_snap(48), 48);
    CHECK_EQ(cp0_ui_font_snap(60), 48);
}

/* ------------------------------------------------------------------ the five targets (report 022 section 3.2) */

static void test_targets(void)
{
    /* Compact 480x320 at the study's 6.1 px/mm; pinned (T3) */
    cp0_ui_metrics_t m = compute(480, 320, 610, 0);
    CHECK(m.cls == CP0_UI_COMPACT && m.preset == CP0_UI_PRESET_COMPACT);
    check_tokens(&m, 5, 10, 14, 6, 14, 20, 40, 72, 43, 55, 100, 88, 440);
    check_text(&m, 24, 20, 18, 14);
    check_compat(&m, 1, 80, 25, 220, 100);

    /* Standard 640x480, the deck at 11.3 px/mm; pinned */
    m = compute(640, 480, 0, 0);
    CHECK(m.cls == CP0_UI_STANDARD && m.preset == CP0_UI_PRESET_DECK);
    check_tokens(&m, 8, 16, 20, 8, 18, 28, 56, 100, 79, 102, 140, 128, 560);
    check_text(&m, 32, 28, 24, 18);
    check_compat(&m, 2, 0, 0, 340, 140);

    /* Wide 800x480 at 9.2 px/mm: toolbar 83 + 2 x 10 = 103 px, 2x at x 80 centred above it */
    m = compute(800, 480, 920, 0);
    CHECK(m.cls == CP0_UI_WIDE && m.preset == CP0_UI_PRESET_COMPUTED);
    check_tokens(&m, 8, 14, 20, 8, 16, 24, 56, 100, 64, 83, 103, 128, 600);
    check_text(&m, 32, 26, 22, 18);
    check_compat(&m, 2, 80, 18, 377, 103);

    /* Square 720x720 at 10 px/mm: toolbar 110 (never "the rest of the screen"), 2x centred in the 610 px above */
    m = compute(720, 720, 1000, 0);
    CHECK(m.cls == CP0_UI_SQUARE && m.preset == CP0_UI_PRESET_COMPUTED);
    check_tokens(&m, 8, 16, 24, 8, 18, 28, 64, 100, 70, 90, 110, 128, 600);
    check_text(&m, 32, 28, 24, 20);
    check_compat(&m, 2, 40, 135, 610, 110);

    /* Large 1280x720 at 11.6 px/mm: toolbar 124, 3x (not 4x with a 40 px toolbar) */
    m = compute(1280, 720, 1160, 0);
    CHECK(m.cls == CP0_UI_LARGE && m.preset == CP0_UI_PRESET_COMPUTED);
    check_tokens(&m, 10, 16, 24, 10, 20, 28, 64, 115, 81, 104, 124, 160, 720);
    check_text(&m, 36, 30, 26, 20);
    check_compat(&m, 3, 160, 43, 596, 124);
    CHECK_EQ(m.compat.scale_max, 3);

    /* computed sizes: the toolbar keys (toolbar minus the shell's vertical padding) are exactly 9 mm */
    static const int sizes[][3] = {{800, 480, 920}, {720, 720, 1000}, {1280, 720, 1160}, {1024, 600, 660},
                                   {1280, 390, 1130}, {800, 480, 300}};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        m = compute(sizes[i][0], sizes[i][1], sizes[i][2], 0);
        CHECK_EQ(m.compat.toolbar_h - 2 * m.shell.tb_pad_y, m.tok[CP0_TOK_TARGET]);
        CHECK_EQ(m.compat.toolbar_h, m.tok[CP0_TOK_TOOLBAR_H]);
    }
}

/* ------------------------------------------------------------------ odd sizes */

static void test_odd_sizes(void)
{
    /* 1024x600 7" at its real 6.6 px/mm: 3x fits above a 79 px toolbar (59 px keys) */
    cp0_ui_metrics_t m = compute(1024, 600, 660, 0);
    CHECK(m.cls == CP0_UI_WIDE);
    CHECK_EQ(m.tok[CP0_TOK_TARGET], 59);
    check_compat(&m, 3, 32, 5, 521, 79);
    /* ... at the default 11.3 px/mm the 122 px toolbar does not fit under 3x: 2x */
    m = compute(1024, 600, 0, 0);
    check_compat(&m, 2, 192, 69, 478, 122);

    /* a 1280x390 bar: compact, computed, compact home layout (toolbar padding 5): toolbar 102 + 10 */
    m = compute(1280, 390, 0, 0);
    CHECK(m.cls == CP0_UI_COMPACT && m.preset == CP0_UI_PRESET_COMPUTED);
    check_compat(&m, 1, 480, 54, 278, 112);
    CHECK(m.shell_preset == CP0_UI_PRESET_COMPACT);
    /* 1280x400 is wide by the rule (h < 400 for compact) */
    m = compute(1280, 400, 0, 0);
    CHECK(m.cls == CP0_UI_WIDE && m.shell_preset == CP0_UI_PRESET_DECK);

    /* portrait (D12: not supported, still sane) */
    m = compute(320, 480, 0, 0);
    CHECK(m.cls == CP0_UI_PORTRAIT);
    check_compat(&m, 1, 0, 94, 358, 122);
    CHECK_EQ(m.tok[CP0_TOK_DIALOG_MAX_W], 320 - 2 * 16);

    /* exactly the window, and smaller than it: never negative, toolbar 0 */
    m = compute(320, 170, 0, 0);
    check_compat(&m, 1, 0, 0, 170, 0);
    m = compute(200, 100, 0, 0);
    CHECK(m.compat.scale == 1 && m.compat.x == 0 && m.compat.y == 0 && m.compat.toolbar_h == 0);
    CHECK_EQ(m.compat.toolbar_y, 100);

    /* 1920x1080: 5x (1600x850) fits above the 122 px toolbar, 6x does not (1020 + 122 > 1080) */
    m = compute(1920, 1080, 0, 0);
    CHECK(m.cls == CP0_UI_LARGE);
    check_compat(&m, 5, 160, 54, 958, 122);
}

/* ------------------------------------------------------------------ pinned presets */

/* native_ui.cpp's Layout before P1b, deck profile */
static void check_shell_deck(const cp0_ui_shell_t *s, int w, int h)
{
    CHECK_EQ(s->cols, 3);
    CHECK_EQ(s->rows, 2);
    CHECK_EQ(s->bar_h, 56);
    CHECK_EQ(s->pad, 16);
    CHECK_EQ(s->status_pct, 100);
    CHECK_EQ(s->status_top, 8);
    CHECK_EQ(s->status_w, 320);
    CHECK_EQ(s->title_x, 20);
    CHECK_EQ(s->title_y, 10);
    CHECK_EQ(s->title_px, 32);
    CHECK_EQ(s->tile_radius, 28);
    CHECK_EQ(s->tile_border, 3);
    CHECK_EQ(s->tile_border_sel, 5);
    CHECK_EQ(s->icon, 128);
    CHECK_EQ(s->icon_top, 14);
    CHECK_EQ(s->label_px, 24);
    CHECK_EQ(s->label_bottom, 12);
    CHECK_EQ(s->label_inset, 16);
    CHECK_EQ(s->tb_pad_x, 10);
    CHECK_EQ(s->tb_pad_y, 10);
    CHECK_EQ(s->tb_gap, 8);
    CHECK_EQ(s->tb_radius, 18);
    CHECK_EQ(s->tb_text_px, 24);
    CHECK_EQ(s->tb_symbol_px, 32);
    CHECK_EQ(s->tile_w, (w - 2 * 16 - 2 * 16) / 3);
    CHECK_EQ(s->tile_h, (h - 56 - 3 * 16) / 2);
}

/* ... compact profile (task 013 T3) */
static void check_shell_compact(const cp0_ui_shell_t *s, int w, int h)
{
    CHECK_EQ(s->cols, 3);
    CHECK_EQ(s->rows, 2);
    CHECK_EQ(s->bar_h, 40);
    CHECK_EQ(s->pad, 10);
    CHECK_EQ(s->status_pct, 72);
    CHECK_EQ(s->status_top, 5);
    CHECK_EQ(s->status_w, 230);
    CHECK_EQ(s->title_x, 14);
    CHECK_EQ(s->title_y, 6);
    CHECK_EQ(s->title_px, 24);
    CHECK_EQ(s->tile_radius, 20);
    CHECK_EQ(s->tile_border, 2);
    CHECK_EQ(s->tile_border_sel, 4);
    CHECK_EQ(s->icon, 88);
    CHECK_EQ(s->icon_top, 6);
    CHECK_EQ(s->label_px, 18);
    CHECK_EQ(s->label_bottom, 5);
    CHECK_EQ(s->label_inset, 12);
    CHECK_EQ(s->tb_pad_x, 6);
    CHECK_EQ(s->tb_pad_y, 5);
    CHECK_EQ(s->tb_gap, 6);
    CHECK_EQ(s->tb_radius, 14);
    CHECK_EQ(s->tb_text_px, 18);
    CHECK_EQ(s->tb_symbol_px, 24);
    CHECK_EQ(s->tile_w, (w - 2 * 10 - 2 * 10) / 3);
    CHECK_EQ(s->tile_h, (h - 40 - 3 * 10) / 2);
}

/* The tokens that stand for something the shell draws today equal the shell's numbers, so P1c can switch the grid
 * to tokens without moving a pixel on the two shipped boards. */
static void check_token_shell_agreement(const cp0_ui_metrics_t *m)
{
    const cp0_ui_shell_t *s = &m->shell;
    CHECK_EQ(m->tok[CP0_TOK_HEADER_H], s->bar_h);
    CHECK_EQ(m->tok[CP0_TOK_SPACE_M], s->pad);
    CHECK_EQ(m->tok[CP0_TOK_STATUS_PCT], s->status_pct);
    CHECK_EQ(m->tok[CP0_TOK_RADIUS_L], s->tile_radius);
    CHECK_EQ(m->tok[CP0_TOK_RADIUS_M], s->tb_radius);
    CHECK_EQ(m->tok[CP0_TOK_TILE_ICON], s->icon);
    CHECK_EQ(m->text_px[CP0_TEXT_TITLE], s->title_px);
    CHECK_EQ(m->text_px[CP0_TEXT_BODY], s->label_px);
    CHECK_EQ(m->text_px[CP0_TEXT_BODY], s->tb_text_px);
    CHECK_EQ(m->text_px[CP0_TEXT_SYMBOL], s->tb_symbol_px);
    CHECK_EQ(m->tok[CP0_TOK_TOOLBAR_H], m->compat.toolbar_h);
}

static void test_pinned(void)
{
    static const int densities[] = {0, 300, 610, 630, 1120, 1130, 2500}; /* the density never moves a pinned pixel */
    for (size_t i = 0; i < sizeof(densities) / sizeof(densities[0]); i++) {
        cp0_ui_metrics_t m = compute(640, 480, densities[i], 0);
        CHECK(m.preset == CP0_UI_PRESET_DECK && m.shell_preset == CP0_UI_PRESET_DECK);
        check_shell_deck(&m.shell, 640, 480);
        CHECK_EQ(m.shell.tile_w, 192);
        CHECK_EQ(m.shell.tile_h, 188);
        check_compat(&m, 2, 0, 0, 340, 140);
        CHECK_EQ(m.compat.scale_max, 2);
        check_token_shell_agreement(&m);

        m = compute(480, 320, densities[i], 0);
        CHECK(m.preset == CP0_UI_PRESET_COMPACT && m.shell_preset == CP0_UI_PRESET_COMPACT);
        check_shell_compact(&m.shell, 480, 320);
        CHECK_EQ(m.shell.tile_w, 146);
        CHECK_EQ(m.shell.tile_h, 125);
        check_compat(&m, 1, 80, 25, 220, 100);
        check_token_shell_agreement(&m);
        /* the board profile's APPLAUNCH_COMPAT_SCALE=1 changes nothing */
        cp0_ui_metrics_t capped = compute(480, 320, densities[i], 1);
        CHECK(memcmp(&capped, &m, sizeof(m)) == 0);
    }
    /* the Pi 3A+ as installed: APPLAUNCH_PANEL_MM=79x49 */
    cp0_ui_metrics_t m = compute(480, 320, 630, 1);
    CHECK_EQ(m.tok[CP0_TOK_ROW_H], 44);
    CHECK_EQ(m.tok[CP0_TOK_TARGET], 57);

    /* the deck with APPLAUNCH_COMPAT_SCALE=1: today's 1x window at (160, 0), toolbar from y 170 */
    m = compute(640, 480, 0, 1);
    check_compat(&m, 1, 160, 0, 170, 310);
    CHECK_EQ(m.compat.scale_max, 2);
    /* a cap above the rule is ignored */
    m = compute(640, 480, 0, 3);
    check_compat(&m, 2, 0, 0, 340, 140);

    /* other sizes keep today's two home layouts: deck from 400 px of height, compact below */
    m = compute(800, 480, 920, 0);
    CHECK(m.shell_preset == CP0_UI_PRESET_DECK);
    check_shell_deck(&m.shell, 800, 480);
    m = compute(1280, 720, 1160, 0);
    check_shell_deck(&m.shell, 1280, 720);
    m = compute(720, 720, 1000, 0);
    check_shell_deck(&m.shell, 720, 720);
    m = compute(800, 320, 0, 0);
    CHECK(m.shell_preset == CP0_UI_PRESET_COMPACT);
    check_shell_compact(&m.shell, 800, 320);
    m = compute(800, 400, 0, 0);
    CHECK(m.shell_preset == CP0_UI_PRESET_DECK);

    /* a cap on a computed size lowers the scale; the window is re-centred above the toolbar */
    m = compute(1280, 720, 1160, 2);
    check_compat(&m, 2, 320, 128, 596, 124);
    CHECK_EQ(m.compat.scale_max, 3);
}

/* ------------------------------------------------------------------ determinism, current */

static void test_determinism(void)
{
    static const int sizes[][2] = {{480, 320}, {640, 480}, {800, 480}, {720, 720}, {1280, 720}, {1024, 600}};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        cp0_ui_metrics_t a, b;
        memset(&a, 0x11, sizeof(a));
        memset(&b, 0xEE, sizeof(b));
        CHECK(cp0_ui_metrics_compute(sizes[i][0], sizes[i][1], 90, 1000, CP0_UI_DENSITY_FBDEV, 0, &a) == 0);
        CHECK(cp0_ui_metrics_compute(sizes[i][0], sizes[i][1], 90, 1000, CP0_UI_DENSITY_FBDEV, 0, &b) == 0);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        CHECK(a.rot == 90);
    }
    CHECK(cp0_ui_metrics_current() == NULL);
    cp0_ui_metrics_t m = compute(800, 480, 920, 0);
    cp0_ui_metrics_set_current(&m);
    CHECK(cp0_ui_metrics_current() != NULL && memcmp(cp0_ui_metrics_current(), &m, sizeof(m)) == 0);
    m.w = 1; /* a copy is kept */
    CHECK(cp0_ui_metrics_current()->w == 800);
    cp0_ui_metrics_set_current(NULL);
    CHECK(cp0_ui_metrics_current() == NULL);
}

/* ------------------------------------------------------------------ state file */

static void test_state_text(void)
{
    char buf[256];
    CHECK(cp0_ui_state_path("/run/user/1000", buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/run/user/1000/applaunch/screen.state") == 0);
    CHECK(cp0_ui_state_path(NULL, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/tmp/applaunch-screen.state") == 0);
    CHECK(cp0_ui_state_path("", buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/tmp/applaunch-screen.state") == 0);
    char small[10];
    CHECK(cp0_ui_state_path("/run/user/1000", small, sizeof(small)) == -1);

    cp0_ui_metrics_t m = compute(640, 480, 0, 0);
    char text[512];
    const int n = cp0_ui_state_format(&m, 1, text, sizeof(text));
    CHECK(n > 0);
    CHECK(strcmp(text, "v=1\nw=640\nh=480\nrot=0\nppmm=1130\ndensity=default\nclass=standard\ncompat=0,0,640,340\n"
                       "scale=2\ntoolbar=140\nkb_inset=0\ngen=1\n") == 0);
    CHECK(cp0_ui_state_parse_gen(text, (size_t)n) == 1);
    char tiny[16];
    CHECK(cp0_ui_state_format(&m, 1, tiny, sizeof(tiny)) == -1);

    m = compute(480, 320, 630, 1);
    m.rot = 90;
    CHECK(cp0_ui_state_format(&m, 42, text, sizeof(text)) > 0);
    CHECK(strcmp(text, "v=1\nw=480\nh=320\nrot=90\nppmm=630\ndensity=panel_mm\nclass=compact\ncompat=80,25,320,170\n"
                       "scale=1\ntoolbar=100\nkb_inset=0\ngen=42\n") == 0);
    CHECK(cp0_ui_state_parse_gen(text, strlen(text)) == 42);

    const char *crlf = "v=1\r\nw=640\r\ngen=7\r\n";
    CHECK(cp0_ui_state_parse_gen(crlf, strlen(crlf)) == 7);
    const char *v2 = "v=2\ngen=7\n";
    CHECK(cp0_ui_state_parse_gen(v2, strlen(v2)) == 0);
    const char *nov = "gen=7\n";
    CHECK(cp0_ui_state_parse_gen(nov, strlen(nov)) == 0);
    const char *badgen = "v=1\ngen=x\n";
    CHECK(cp0_ui_state_parse_gen(badgen, strlen(badgen)) == 0);
    CHECK(cp0_ui_state_parse_gen(NULL, 4) == 0);

    char desc[1024];
    m = compute(1280, 720, 1160, 0);
    CHECK(cp0_ui_metrics_describe(&m, desc, sizeof(desc)) > 0);
    CHECK(strstr(desc, "class large (computed)") != NULL);
    CHECK(strstr(desc, "density 11.60 px/mm") != NULL);
    CHECK(strstr(desc, "window 960x510 at (160,43)") != NULL);
    CHECK(cp0_ui_metrics_describe(&m, tiny, sizeof(tiny)) == -1);
}

static int count_entries(const char *dir)
{
    DIR *d = opendir(dir);
    CHECK(d != NULL);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) n++;
    closedir(d);
    return n;
}

static void read_file(const char *path, char *text, size_t size)
{
    text[0] = '\0';
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (!f) return;
    const size_t len = fread(text, 1, size - 1, f);
    fclose(f);
    text[len] = '\0';
}

static void test_publish(void)
{
    char root[] = "/tmp/cp0-ui-metrics-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char path[512], dir[512];
    CHECK(cp0_ui_state_path(root, path, sizeof(path)) == 0);
    snprintf(dir, sizeof(dir), "%s/applaunch", root);

    cp0_ui_metrics_t m = compute(800, 480, 921, 0);
    m.density_src = CP0_UI_DENSITY_FBDEV;
    const mode_t old_umask = umask(077);
    CHECK(cp0_ui_metrics_publish(&m, root) == 0); /* creates <runtime>/applaunch */
    umask(old_umask);
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0644);
    CHECK(stat(dir, &st) == 0 && S_ISDIR(st.st_mode));
    char text[512];
    read_file(path, text, sizeof(text));
    CHECK(strcmp(text, "v=1\nw=800\nh=480\nrot=0\nppmm=921\ndensity=fbdev\nclass=wide\ncompat=80,18,640,340\n"
                       "scale=2\ntoolbar=103\nkb_inset=0\ngen=1\n") == 0);
    CHECK(getenv("APPLAUNCH_SCREEN_W") && strcmp(getenv("APPLAUNCH_SCREEN_W"), "800") == 0);
    CHECK(getenv("APPLAUNCH_SCREEN_H") && strcmp(getenv("APPLAUNCH_SCREEN_H"), "480") == 0);
    CHECK(getenv("APPLAUNCH_SCREEN_ROTATE") && strcmp(getenv("APPLAUNCH_SCREEN_ROTATE"), "0") == 0);
    CHECK(getenv("APPLAUNCH_SCREEN_PPMM") && strcmp(getenv("APPLAUNCH_SCREEN_PPMM"), "921") == 0);
    CHECK(getenv("APPLAUNCH_SCREEN_CLASS") && strcmp(getenv("APPLAUNCH_SCREEN_CLASS"), "wide") == 0);

    /* a restarted launcher continues gen; the file is replaced, no temporary is left */
    CHECK(cp0_ui_metrics_publish(&m, root) == 0);
    read_file(path, text, sizeof(text));
    CHECK(strstr(text, "\ngen=2\n") != NULL);
    CHECK(count_entries(dir) == 1);

    /* a file of another version restarts gen at 1 */
    CHECK(cp0_ui_state_write(path, "v=9\ngen=50\n", 11) == 0);
    CHECK(cp0_ui_metrics_publish(&m, root) == 0);
    read_file(path, text, sizeof(text));
    CHECK(strstr(text, "\ngen=1\n") != NULL);

    /* unwritable place: error, no crash, environment still set */
    unsetenv("APPLAUNCH_SCREEN_W");
    CHECK(cp0_ui_metrics_publish(&m, "/proc/applaunch-test") == -1);
    CHECK(getenv("APPLAUNCH_SCREEN_W") && strcmp(getenv("APPLAUNCH_SCREEN_W"), "800") == 0);
    CHECK(cp0_ui_state_write("/proc/applaunch-test/screen.state", "x", 1) == -1);
    CHECK(cp0_ui_metrics_publish(NULL, root) == -1);

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

int main(void)
{
    test_classes();
    test_panel_mm_parse();
    test_density();
    test_mm_and_fonts();
    test_targets();
    test_odd_sizes();
    test_pinned();
    test_determinism();
    test_state_text();
    test_publish();
    if (failures) {
        fprintf(stderr, "test_ui_metrics: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ui_metrics: PASS\n");
    return 0;
}
