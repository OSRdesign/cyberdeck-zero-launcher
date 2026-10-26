/*
 * Unit test of the on-screen keyboard core (src/cp0_ui_osk.c, task 014 P2b): the layout from the tokens at the five
 * target sizes (key pitch, 10 mm cap and centring, key height from the 6.5 mm token under the class height cap,
 * utility row, number pad), the key placement of every page, the one-shot / lock state machine of Shift and Ctrl,
 * the key event sequences (characters, Ctrl+C, Tab, Enter, hold keys and their owed release), the terminal's Alt key
 * (its layout at the target sizes and the FZ-M1's, Alt+Tab and Alt+key sequences, report 028 X4), the presence policy
 * gate (keyboards present, asleep Bluetooth keyboard, grace, Off setting, lock, desktop, APPLAUNCH_OSK) and the dialog
 * placement above the keyboard.
 */
#include "cp0_ui_osk.h"

#include "input_keys.h"
#include "keyboard_input.h"

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

static int imin_t(int a, int b) { return a < b ? a : b; }

static cp0_ui_metrics_t metrics(int w, int h, int ppmm)
{
    cp0_ui_metrics_t m;
    memset(&m, 0, sizeof(m));
    CHECK_EQ(cp0_ui_metrics_compute(w, h, 0, ppmm, CP0_UI_DENSITY_PANEL_MM, 0, &m), 0);
    return m;
}

/* the densities of the render harness profiles (Pi 3A+ panel 79x49 mm, deck default, HyperPixel, 4" square, 5") */
static int ppmm_pi3a(void) { return cp0_ui_ppmm_from_mm(480, 320, 790, 490); }

typedef struct {
    int w, h, ppmm;
    int key_w, key_h, util_h, util_key_w, gap, row_x, row_w, pad_x, pad_w, height, cap, label_px, small_px;
} expected_t;

static void test_layout_sizes(void)
{
    const expected_t cases[] = {
        /*  w     h    ppmm  key_w key_h util_h util_w gap row_x row_w pad_x pad_w height cap label small */
        {480, 320, 0, 42, 33, 26, 57, 5, 7, 465, 172, 136, 189, 192, 14, 12},
        {640, 480, 1130, 55, 52, 42, 102, 8, 9, 622, 229, 181, 299, 300, 24, 18},
        {800, 480, 920, 71, 39, 31, 83, 8, 9, 782, 285, 229, 236, 240, 18, 14},
        {720, 720, 1000, 63, 64, 51, 90, 8, 9, 702, 257, 205, 356, 360, 28, 22}, /* Square: grows to half the height */
        {1280, 720, 1160, 116, 54, 43, 116, 10, 15, 1250, 456, 368, 320, 324, 24, 18},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const expected_t *e = &cases[i];
        const cp0_ui_metrics_t m = metrics(e->w, e->h, e->ppmm ? e->ppmm : ppmm_pi3a());
        cp0_ui_osk_layout_t l;
        CHECK_EQ(cp0_ui_osk_layout(&m, &l), 0);
        CHECK_EQ(l.key_w, e->key_w);
        CHECK_EQ(l.key_h, e->key_h);
        CHECK_EQ(l.util_h, e->util_h);
        CHECK_EQ(l.util_key_w, e->util_key_w);
        CHECK_EQ(l.gap, e->gap);
        CHECK_EQ(l.row_x, e->row_x);
        CHECK_EQ(l.row_w, e->row_w);
        CHECK_EQ(l.pad_x, e->pad_x);
        CHECK_EQ(l.pad_w, e->pad_w);
        CHECK_EQ(l.height, e->height);
        CHECK_EQ(l.cap, e->cap);
        CHECK_EQ(l.label_px, e->label_px);
        CHECK_EQ(l.small_px, e->small_px);
        CHECK(l.arrows);
        /* invariants: full width, at the bottom, under the class cap, the key area centred and inside the margins,
         * the height made of the rows, the utility row 80 % of a letter row, the 10 mm width cap */
        CHECK_EQ(l.y + l.height, e->h);
        CHECK(l.height <= l.cap);
        CHECK_EQ(l.row_w, 10 * l.key_w + 9 * l.gap);
        CHECK(l.row_x >= l.margin && l.row_x + l.row_w <= e->w - l.margin);
        CHECK(l.row_x == (e->w - l.row_w) / 2);
        CHECK_EQ(l.pad_w, 3 * l.key_w + 2 * l.gap);
        CHECK_EQ(l.pad_x, (e->w - l.pad_w) / 2);
        CHECK_EQ(l.height, l.border + 2 * l.pad_y + l.util_h + 4 * (l.gap + l.key_h));
        CHECK_EQ(l.util_h, (l.key_h * 80 + 50) / 100);
        CHECK(l.key_w <= l.key_w_cap);
        if (m.cls != CP0_UI_SQUARE) CHECK(l.key_h <= l.key_h_token); /* Square grows past the token */
        CHECK(l.util_key_w >= m.tok[CP0_TOK_TARGET] || l.util_key_w == 2 * l.key_w + l.gap ||
              l.util_key_w == l.key_w);
        CHECK(l.util_key_w >= l.key_w);
    }

    /* the deck: about 300 px (F1 rule 280-320), keys 55 x 52 (4.9 x 4.6 mm) */
    cp0_ui_metrics_t deck = metrics(640, 480, 1130);
    cp0_ui_osk_layout_t l;
    cp0_ui_osk_layout(&deck, &l);
    CHECK(l.height >= 280 && l.height <= 320);

    /* Large: the 10 mm cap applies (116 px at 11.6 px/mm) and the key area is centred with more than the margin */
    cp0_ui_metrics_t large = metrics(1280, 720, 1160);
    cp0_ui_osk_layout(&large, &l);
    CHECK_EQ(l.key_w, l.key_w_cap);
    CHECK(l.row_x > l.margin);
    /* a wider Large canvas: same keys, more room each side */
    cp0_ui_metrics_t wider = metrics(1600, 720, 1160);
    cp0_ui_osk_layout_t lw;
    cp0_ui_osk_layout(&wider, &lw);
    CHECK_EQ(lw.key_w, 116);
    CHECK_EQ(lw.row_x, (1600 - lw.row_w) / 2);
    CHECK_EQ(lw.row_w, l.row_w);

    /* the class caps (report 022 section 3.2) */
    CHECK_EQ(cp0_ui_osk_height_cap(CP0_UI_COMPACT, 320), 192);
    CHECK_EQ(cp0_ui_osk_height_cap(CP0_UI_STANDARD, 480), 300);
    CHECK_EQ(cp0_ui_osk_height_cap(CP0_UI_WIDE, 480), 240);
    CHECK_EQ(cp0_ui_osk_height_cap(CP0_UI_SQUARE, 720), 360);
    CHECK_EQ(cp0_ui_osk_height_cap(CP0_UI_LARGE, 720), 324);
    CHECK_EQ(cp0_ui_osk_height_cap(CP0_UI_CLASS__COUNT, 720), 0);

    /* Square (HackBerry 720x720 test, 2026-10-11): the keyboard reaches about half the screen; keys grow as tall as
     * that allows (past the 6.5 mm token on 1080x1080), their width is unchanged (10 mm cap), the utility row follows
     * (80 %), and the room above keeps the header and one field with its label */
    const int squares[][5] = {
        /* side ppmm key_w key_h height */
        {600, 1000, 51, 52, 299},
        {720, 1000, 63, 64, 356},
        {1080, 1000, 99, 102, 539},
    };
    for (size_t i = 0; i < sizeof(squares) / sizeof(squares[0]); ++i) {
        const int side = squares[i][0];
        cp0_ui_metrics_t sq = metrics(side, side, squares[i][1]);
        CHECK_EQ(sq.cls, CP0_UI_SQUARE);
        cp0_ui_osk_layout(&sq, &l);
        CHECK_EQ(l.key_w, squares[i][2]);
        CHECK_EQ(l.key_h, squares[i][3]);
        CHECK_EQ(l.height, squares[i][4]);
        CHECK(l.height <= side / 2);                                   /* at most the target ... */
        CHECK(side / 2 - l.height < 6);                                /* ... and within one key step of it */
        /* the tallest key that fits: one pixel more would pass the target (not limited by the 6.5 mm token) */
        CHECK(l.border + 2 * l.pad_y + ((l.key_h + 1) * 80 + 50) / 100 + 4 * (l.gap + l.key_h + 1) > side / 2);
        CHECK(l.key_w <= l.key_w_cap);
        CHECK_EQ(l.key_w, imin_t((side - 2 * l.margin - 9 * l.gap) / 10, l.key_w_cap)); /* width rule unchanged */
        CHECK_EQ(l.util_h, (l.key_h * 80 + 50) / 100);
        CHECK(side - l.height >= cp0_ui_osk_field_reserve(&sq)); /* the field and its label stay above */
        CHECK(l.arrows);
    }
    /* 720x720: what is above the keyboard (720 - 356 = 364) holds the 64 px header and one field with its label */
    cp0_ui_metrics_t hb = metrics(720, 720, 1000);
    cp0_ui_osk_layout(&hb, &l);
    CHECK_EQ(cp0_ui_osk_field_reserve(&hb), 64 + 16 + 26 + 5 + 70 + 16);
    CHECK(720 - l.height - hb.shell.bar_h >= 16 + 26 + 5 + 70);
    /* a small square (400x400 at 11.3 px/mm): the field reserve, not the 50 % target, limits the keyboard */
    cp0_ui_metrics_t small = metrics(400, 400, 1130);
    CHECK_EQ(small.cls, CP0_UI_SQUARE);
    cp0_ui_osk_layout(&small, &l);
    CHECK_EQ(l.cap, 400 - cp0_ui_osk_field_reserve(&small));
    CHECK(l.cap < 200);
    CHECK(l.height <= l.cap);
    CHECK(400 - l.height >= cp0_ui_osk_field_reserve(&small));

    /* when the cap leaves room (a tall portrait canvas, not Square) the key height is the 6.5 mm token */
    cp0_ui_metrics_t tall = metrics(700, 1000, 1000);
    CHECK_EQ(tall.cls, CP0_UI_PORTRAIT);
    cp0_ui_osk_layout(&tall, &l);
    CHECK_EQ(l.key_h, 65);
    CHECK_EQ(l.key_h_token, 65);

    CHECK_EQ(cp0_ui_osk_layout(NULL, &l), -1);
    cp0_ui_metrics_t empty;
    memset(&empty, 0, sizeof(empty));
    CHECK_EQ(cp0_ui_osk_layout(&empty, &l), -1);
}

static const cp0_ui_osk_place_t *find_key(const cp0_ui_osk_place_t *p, int n, cp0_ui_osk_kind_t kind, uint32_t code,
                                          const char *text)
{
    for (int i = 0; i < n; ++i) {
        if (p[i].key->kind != kind) continue;
        if (code && p[i].key->code != code) continue;
        if (text && (!p[i].key->text || strcmp(p[i].key->text, text) != 0)) continue;
        return &p[i];
    }
    return NULL;
}

static void test_place_keys(void)
{
    const cp0_ui_metrics_t m = metrics(640, 480, 1130);
    cp0_ui_osk_layout_t l;
    cp0_ui_osk_layout(&m, &l);
    cp0_ui_osk_place_t p[CP0_UI_OSK_MAX_KEYS];

    /* letters: utility row 3 + 4 arrows, then 10, 9, 9, 5 */
    int n = cp0_ui_osk_place_keys(&l, CP0_UI_OSK_PAGE_LETTERS, p, CP0_UI_OSK_MAX_KEYS);
    CHECK_EQ(n, 7 + 10 + 9 + 9 + 5);
    int per_row[5] = {0};
    for (int i = 0; i < n; ++i) per_row[p[i].row]++;
    CHECK_EQ(per_row[0], 7);
    CHECK_EQ(per_row[1], 10);
    CHECK_EQ(per_row[2], 9);
    CHECK_EQ(per_row[3], 9);
    CHECK_EQ(per_row[4], 5);
    /* row 0: Esc, Tab, Ctrl from the left, TARGET wide; the arrows end at the key area's right edge */
    CHECK(p[0].key->kind == CP0_UI_OSK_K_ESC && p[1].key->kind == CP0_UI_OSK_K_TAB && p[2].key->kind == CP0_UI_OSK_K_CTRL);
    CHECK_EQ(p[0].x, l.row_x);
    CHECK_EQ(p[0].w, 102);
    CHECK_EQ(p[1].x, l.row_x + 102 + l.gap);
    CHECK_EQ(p[0].h, l.util_h);
    CHECK(p[3].key->code == KEY_LEFT && p[6].key->code == KEY_RIGHT);
    CHECK_EQ(p[6].x + p[6].w, l.row_x + l.row_w);
    CHECK_EQ(p[0].y, l.border + l.pad_y);
    /* row 1 fills the key area exactly; row 2 is centred (half a key in) */
    CHECK_EQ(p[7].x, l.row_x);
    CHECK_EQ(p[16].x + p[16].w, l.row_x + l.row_w);
    CHECK_EQ(p[7].y, l.border + l.pad_y + l.util_h + l.gap);
    CHECK_EQ(p[17].x, l.row_x + (l.key_w + l.gap) / 2);
    /* row 3: Shift and Backspace 1.5 keys at the ends; row 4: 123 1.5, ',', space 4.5, '.', Enter 2 */
    const cp0_ui_osk_place_t *shift = find_key(p, n, CP0_UI_OSK_K_SHIFT, 0, NULL);
    const cp0_ui_osk_place_t *bs = find_key(p, n, CP0_UI_OSK_K_BACKSPACE, 0, NULL);
    const cp0_ui_osk_place_t *space = find_key(p, n, CP0_UI_OSK_K_CHAR, KEY_SPACE, NULL);
    const cp0_ui_osk_place_t *enter = find_key(p, n, CP0_UI_OSK_K_ENTER, 0, NULL);
    const cp0_ui_osk_place_t *page = find_key(p, n, CP0_UI_OSK_K_PAGE, 0, NULL);
    CHECK(shift && bs && space && enter && page);
    if (shift && bs && space && enter && page) {
        CHECK_EQ(shift->x, l.row_x);
        CHECK_EQ(bs->x + bs->w, l.row_x + l.row_w);
        CHECK(shift->w >= 86 && shift->w <= 87);
        CHECK_EQ(space->w, 275);
        CHECK_EQ(enter->w, 2 * l.key_w + l.gap);
        CHECK_EQ(enter->x + enter->w, l.row_x + l.row_w);
        CHECK(strcmp(page->key->label, "123") == 0 && page->key->target == CP0_UI_OSK_PAGE_SYMBOLS);
        CHECK_EQ(enter->row, 4);
        CHECK_EQ(enter->y + enter->h + l.pad_y, l.height);
    }
    /* no overlap inside a row, nothing outside the keyboard */
    for (int i = 0; i < n; ++i) {
        CHECK(p[i].x >= 0 && p[i].x + p[i].w <= l.w && p[i].y >= l.border && p[i].y + p[i].h <= l.height);
        if (i + 1 < n && p[i + 1].row == p[i].row) CHECK(p[i].x + p[i].w + l.gap <= p[i + 1].x + 1);
    }

    /* 123: 1..0 on row 1, symbols below, '#+=' and 'abc' */
    n = cp0_ui_osk_place_keys(&l, CP0_UI_OSK_PAGE_SYMBOLS, p, CP0_UI_OSK_MAX_KEYS);
    CHECK_EQ(n, 7 + 10 + 10 + 7 + 5);
    CHECK(strcmp(p[7].key->text, "1") == 0 && strcmp(p[16].key->text, "0") == 0);
    CHECK(find_key(p, n, CP0_UI_OSK_K_CHAR, KEY_2, "@") != NULL);
    const cp0_ui_osk_place_t *at = find_key(p, n, CP0_UI_OSK_K_CHAR, KEY_2, "@");
    CHECK(at && at->key->shift);
    page = find_key(p, n, CP0_UI_OSK_K_PAGE, 0, NULL);
    CHECK(page && strcmp(page->key->label, "#+=") == 0 && page->key->target == CP0_UI_OSK_PAGE_SYMBOLS2);
    /* #+=: a second symbol page with a way back to 123 and abc */
    n = cp0_ui_osk_place_keys(&l, CP0_UI_OSK_PAGE_SYMBOLS2, p, CP0_UI_OSK_MAX_KEYS);
    CHECK_EQ(n, 7 + 10 + 10 + 7 + 5);
    CHECK(find_key(p, n, CP0_UI_OSK_K_CHAR, 0, "\xe2\x82\xac") != NULL); /* euro: text only, no key */
    page = find_key(p, n, CP0_UI_OSK_K_PAGE, 0, NULL);
    CHECK(page && page->key->target == CP0_UI_OSK_PAGE_SYMBOLS);

    /* number pad: 3 x 4 (Backspace 0 Enter at the bottom), 3 key widths, centred, under the same utility row */
    n = cp0_ui_osk_place_keys(&l, CP0_UI_OSK_PAGE_NUMPAD, p, CP0_UI_OSK_MAX_KEYS);
    CHECK_EQ(n, 7 + 12);
    CHECK_EQ(p[7].x, l.pad_x);
    CHECK_EQ(p[9].x + p[9].w, l.pad_x + l.pad_w);
    CHECK(p[16].key->kind == CP0_UI_OSK_K_BACKSPACE && strcmp(p[17].key->text, "0") == 0 &&
          p[18].key->kind == CP0_UI_OSK_K_ENTER);
    CHECK_EQ(p[17].x, l.pad_x + l.key_w + l.gap);

    /* portrait canvases: the pad is the full key area wide (3 keys of (area - 2 gaps) / 3), landscape keeps 3 letter keys */
    const int pw[][3] = {{480, 800, 854}, {480, 640, 904}};
    for (size_t s = 0; s < sizeof(pw) / sizeof(pw[0]); ++s) {
        const cp0_ui_metrics_t mp = metrics(pw[s][0], pw[s][1], pw[s][2]);
        cp0_ui_osk_layout_t lp;
        CHECK_EQ(cp0_ui_osk_layout(&mp, &lp), 0);
        CHECK_EQ(lp.pad_key_w, (lp.row_w - 2 * lp.gap) / 3);
        CHECK(lp.pad_key_w >= 140 && lp.pad_key_w <= 152);
        CHECK(lp.pad_key_w > lp.key_w);
        CHECK_EQ(lp.pad_w, 3 * lp.pad_key_w + 2 * lp.gap);
        CHECK(lp.pad_w <= lp.row_w && lp.row_w - lp.pad_w <= 2);
        CHECK_EQ(lp.pad_x, (lp.w - lp.pad_w) / 2);
        n = cp0_ui_osk_place_keys(&lp, CP0_UI_OSK_PAGE_NUMPAD, p, CP0_UI_OSK_MAX_KEYS);
        CHECK_EQ(n, 7 + 12);
        CHECK_EQ(p[7].x, lp.pad_x);
        CHECK_EQ(p[7].w, lp.pad_key_w);
        CHECK_EQ(p[9].x + p[9].w, lp.pad_x + lp.pad_w);
        CHECK_EQ(p[17].x, lp.pad_x + lp.pad_key_w + lp.gap);
        CHECK_EQ(lp.height, lp.border + 2 * lp.pad_y + lp.util_h + 4 * (lp.gap + lp.key_h));
        /* the letter pages are untouched by the pad */
        n = cp0_ui_osk_place_keys(&lp, CP0_UI_OSK_PAGE_LETTERS, p, CP0_UI_OSK_MAX_KEYS);
        CHECK(n > 0);
    }
    {   /* landscape and square: pad_key_w is the letter key */
        const cp0_ui_metrics_t ml = metrics(800, 480, 920), mq = metrics(720, 720, 1000);
        cp0_ui_osk_layout_t a, b;
        cp0_ui_osk_layout(&ml, &a);
        cp0_ui_osk_layout(&mq, &b);
        CHECK_EQ(a.pad_key_w, a.key_w);
        CHECK_EQ(b.pad_key_w, b.key_w);
    }

    /* every page at every size fits the key table */
    const int sizes[][3] = {{480, 320, 0}, {800, 480, 920}, {720, 720, 1000}, {1280, 720, 1160}, {480, 800, 854}, {480, 640, 904}};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        const cp0_ui_metrics_t ms = metrics(sizes[s][0], sizes[s][1], sizes[s][2] ? sizes[s][2] : ppmm_pi3a());
        cp0_ui_osk_layout_t ls;
        cp0_ui_osk_layout(&ms, &ls);
        for (int pg = 0; pg < CP0_UI_OSK_PAGE__COUNT; ++pg) {
            const int count = cp0_ui_osk_place_keys(&ls, (cp0_ui_osk_page_t)pg, p, CP0_UI_OSK_MAX_KEYS);
            CHECK(count > 0);
            for (int i = 0; i < count; ++i)
                CHECK(p[i].x >= 0 && p[i].x + p[i].w <= ls.w && p[i].y + p[i].h <= ls.height && p[i].w > 0);
        }
    }
    CHECK_EQ(cp0_ui_osk_place_keys(&l, CP0_UI_OSK_PAGE_LETTERS, p, 5), -1); /* too small an array */
    CHECK_EQ(cp0_ui_osk_place_keys(&l, CP0_UI_OSK_PAGE__COUNT, p, CP0_UI_OSK_MAX_KEYS), -1);
}

static void test_modifiers(void)
{
    cp0_ui_osk_mod_t m;
    memset(&m, 0, sizeof(m));
    /* one tap: armed; a key uses it */
    cp0_ui_osk_mod_tap(&m, 1000);
    CHECK_EQ(m.state, CP0_UI_OSK_MOD_ARMED);
    CHECK(cp0_ui_osk_mod_on(&m));
    cp0_ui_osk_mod_used(&m);
    CHECK_EQ(m.state, CP0_UI_OSK_MOD_OFF);
    /* double tap within 400 ms: locked; keys keep it; a tap turns it off */
    cp0_ui_osk_mod_tap(&m, 2000);
    cp0_ui_osk_mod_tap(&m, 2400);
    CHECK_EQ(m.state, CP0_UI_OSK_MOD_LOCKED);
    cp0_ui_osk_mod_used(&m);
    cp0_ui_osk_mod_used(&m);
    CHECK_EQ(m.state, CP0_UI_OSK_MOD_LOCKED);
    cp0_ui_osk_mod_tap(&m, 5000);
    CHECK_EQ(m.state, CP0_UI_OSK_MOD_OFF);
    /* two slow taps: armed, then off */
    cp0_ui_osk_mod_tap(&m, 6000);
    cp0_ui_osk_mod_tap(&m, 6401);
    CHECK_EQ(m.state, CP0_UI_OSK_MOD_OFF);
    cp0_ui_osk_mod_tap(&m, 7000);
    cp0_ui_osk_mod_reset(&m);
    CHECK_EQ(m.state, CP0_UI_OSK_MOD_OFF);
    CHECK(!cp0_ui_osk_mod_on(NULL));
}

/* a key of the given kind / code / text on a page (row 0 included) */
static const cp0_ui_osk_key_t *key_of(cp0_ui_osk_page_t page, cp0_ui_osk_kind_t kind, uint32_t code, const char *text)
{
    static cp0_ui_osk_layout_t l;
    static int ready;
    if (!ready) {
        const cp0_ui_metrics_t m = metrics(640, 480, 1130);
        cp0_ui_osk_layout(&m, &l);
        ready = 1;
    }
    static cp0_ui_osk_place_t p[CP0_UI_OSK_MAX_KEYS];
    const int n = cp0_ui_osk_place_keys(&l, page, p, CP0_UI_OSK_MAX_KEYS);
    const cp0_ui_osk_place_t *found = find_key(p, n, kind, code, text);
    CHECK(found != NULL);
    return found ? found->key : NULL;
}

static int is_pair(const cp0_ui_osk_event_t *e, int n, uint32_t code, uint32_t mods, const char *utf8)
{
    return n == 2 && e[0].code == code && e[0].state == KBD_KEY_PRESSED && e[0].mods == mods &&
           strcmp(e[0].utf8, utf8) == 0 && e[1].code == code && e[1].state == KBD_KEY_RELEASED && e[1].mods == mods &&
           strcmp(e[1].utf8, utf8) == 0;
}

static void test_events(void)
{
    cp0_ui_osk_state_t s;
    cp0_ui_osk_event_t e[CP0_UI_OSK_MAX_EVENTS];
    cp0_ui_osk_state_init(&s, 0, 0);
    CHECK_EQ(s.page, CP0_UI_OSK_PAGE_LETTERS);

    const cp0_ui_osk_key_t *a = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CHAR, KEY_A, NULL);
    const cp0_ui_osk_key_t *c = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CHAR, KEY_C, NULL);
    const cp0_ui_osk_key_t *shift = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_SHIFT, 0, NULL);
    const cp0_ui_osk_key_t *ctrl = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CTRL, 0, NULL);
    const cp0_ui_osk_key_t *esc = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_ESC, KEY_ESC, NULL);
    const cp0_ui_osk_key_t *tab = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_TAB, KEY_TAB, NULL);
    const cp0_ui_osk_key_t *enter = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_ENTER, KEY_ENTER, NULL);
    const cp0_ui_osk_key_t *bs = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_BACKSPACE, KEY_BACKSPACE, NULL);
    const cp0_ui_osk_key_t *left = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_ARROW, KEY_LEFT, NULL);
    const cp0_ui_osk_key_t *space = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CHAR, KEY_SPACE, NULL);
    const cp0_ui_osk_key_t *to123 = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_PAGE, 0, NULL);
    if (!a || !c || !shift || !ctrl || !esc || !tab || !enter || !bs || !left || !space || !to123) return;

    /* a character acts on the click only (finger up on the key): press + release, code and text */
    CHECK_EQ(cp0_ui_osk_key_down(&s, a, e, 4), 0);
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, a, 100, e, 4), KEY_A, 0, "a"));
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, space, 100, e, 4), KEY_SPACE, 0, " "));

    /* Shift armed: one capital with KBD_MOD_SHIFT, then lower case again */
    CHECK_EQ(cp0_ui_osk_key_up(&s, shift, 1000, e, 4), 0);
    CHECK_EQ(s.shift.state, CP0_UI_OSK_MOD_ARMED);
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, a, 1500, e, 4), KEY_A, KBD_MOD_SHIFT, "A"));
    CHECK_EQ(s.shift.state, CP0_UI_OSK_MOD_OFF);
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, a, 1600, e, 4), KEY_A, 0, "a"));
    /* double tap: Caps lock (KBD_MOD_CAPS) until tapped again */
    cp0_ui_osk_key_up(&s, shift, 2000, e, 4);
    cp0_ui_osk_key_up(&s, shift, 2200, e, 4);
    CHECK_EQ(s.shift.state, CP0_UI_OSK_MOD_LOCKED);
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, a, 2300, e, 4), KEY_A, KBD_MOD_CAPS, "A"));
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, c, 2400, e, 4), KEY_C, KBD_MOD_CAPS, "C"));
    cp0_ui_osk_key_up(&s, shift, 3000, e, 4);
    CHECK_EQ(s.shift.state, CP0_UI_OSK_MOD_OFF);

    /* Ctrl armed: Ctrl+C = KEY_C with CTRL, press then release, no text; then plain again */
    cp0_ui_osk_key_up(&s, ctrl, 4000, e, 4);
    CHECK_EQ(s.ctrl.state, CP0_UI_OSK_MOD_ARMED);
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, c, 4100, e, 4), KEY_C, KBD_MOD_CTRL, ""));
    CHECK_EQ(e[0].utf8[0], '\0');
    CHECK_EQ(s.ctrl.state, CP0_UI_OSK_MOD_OFF);
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, c, 4200, e, 4), KEY_C, 0, "c"));
    /* Ctrl locked: Ctrl+D, Ctrl+Z, Ctrl+L in a row (terminal use), until tapped again */
    cp0_ui_osk_key_up(&s, ctrl, 5000, e, 4);
    cp0_ui_osk_key_up(&s, ctrl, 5100, e, 4);
    CHECK_EQ(s.ctrl.state, CP0_UI_OSK_MOD_LOCKED);
    const cp0_ui_osk_key_t *d = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CHAR, KEY_D, NULL);
    const cp0_ui_osk_key_t *z = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CHAR, KEY_Z, NULL);
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, d, 5200, e, 4), KEY_D, KBD_MOD_CTRL, ""));
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, z, 5300, e, 4), KEY_Z, KBD_MOD_CTRL, ""));
    cp0_ui_osk_key_up(&s, ctrl, 6000, e, 4);
    CHECK_EQ(s.ctrl.state, CP0_UI_OSK_MOD_OFF);
    /* Ctrl + Shift + C */
    cp0_ui_osk_key_up(&s, ctrl, 7000, e, 4);
    cp0_ui_osk_key_up(&s, shift, 7500, e, 4);
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, c, 7600, e, 4), KEY_C, KBD_MOD_CTRL | KBD_MOD_SHIFT, ""));
    CHECK(!cp0_ui_osk_mod_on(&s.ctrl) && !cp0_ui_osk_mod_on(&s.shift));

    /* Tab: the key code alone in a single-line field, a tab character in a multi-line one */
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, tab, 8000, e, 4), KEY_TAB, 0, ""));
    cp0_ui_osk_state_t multi;
    cp0_ui_osk_state_init(&multi, 0, 1);
    CHECK(is_pair(e, cp0_ui_osk_key_up(&multi, tab, 8000, e, 4), KEY_TAB, 0, "\t"));
    /* Enter: the physical key's control text */
    CHECK(is_pair(e, cp0_ui_osk_key_up(&s, enter, 8100, e, 4), KEY_ENTER, 0, "\r"));

    /* Esc is a hold key like the toolbar's: press on finger down (with "\x1b"), release on finger up */
    CHECK_EQ(cp0_ui_osk_key_down(&s, esc, e, 4), 1);
    CHECK(e[0].code == KEY_ESC && e[0].state == KBD_KEY_PRESSED && strcmp(e[0].utf8, "\x1b") == 0);
    CHECK_EQ(s.held_code, KEY_ESC);
    CHECK_EQ(cp0_ui_osk_key_repeat(&s, esc, e, 4), 1);
    CHECK_EQ(e[0].state, KBD_KEY_REPEATED);
    CHECK_EQ(cp0_ui_osk_key_up(&s, esc, 9000, e, 4), 1);
    CHECK(e[0].code == KEY_ESC && e[0].state == KBD_KEY_RELEASED);
    CHECK_EQ(s.held_code, 0);
    CHECK_EQ(cp0_ui_osk_key_up(&s, esc, 9000, e, 4), 0); /* never twice */
    /* the release is always owed: finger lost ... */
    cp0_ui_osk_key_down(&s, esc, e, 4);
    CHECK_EQ(cp0_ui_osk_key_cancel(&s, e, 4), 1);
    CHECK(e[0].code == KEY_ESC && e[0].state == KBD_KEY_RELEASED);
    CHECK_EQ(cp0_ui_osk_key_cancel(&s, e, 4), 0);
    /* ... or the keyboard destroyed mid-press (release_all), which also drops latched modifiers */
    cp0_ui_osk_key_up(&s, shift, 10000, e, 4);
    cp0_ui_osk_key_up(&s, ctrl, 10000, e, 4);
    cp0_ui_osk_key_down(&s, esc, e, 4);
    CHECK_EQ(cp0_ui_osk_release_all(&s, e, 4), 1);
    CHECK(e[0].code == KEY_ESC && e[0].state == KBD_KEY_RELEASED);
    CHECK(!cp0_ui_osk_mod_on(&s.shift) && !cp0_ui_osk_mod_on(&s.ctrl));
    CHECK_EQ(cp0_ui_osk_release_all(&s, e, 4), 0);
    /* Esc never takes Ctrl (it stays the toolbar's Esc) and leaves an armed Ctrl armed */
    cp0_ui_osk_key_up(&s, ctrl, 11000, e, 4);
    cp0_ui_osk_key_down(&s, esc, e, 4);
    CHECK(e[0].mods == 0 && strcmp(e[0].utf8, "\x1b") == 0);
    cp0_ui_osk_key_up(&s, esc, 11100, e, 4);
    CHECK_EQ(s.ctrl.state, CP0_UI_OSK_MOD_ARMED);
    cp0_ui_osk_release_all(&s, e, 4);

    /* Backspace and arrows: hold keys too, with the control text; a second hold key releases the first */
    CHECK_EQ(cp0_ui_osk_key_down(&s, bs, e, 4), 1);
    CHECK(e[0].code == KEY_BACKSPACE && strcmp(e[0].utf8, "\x7f") == 0);
    CHECK_EQ(cp0_ui_osk_key_down(&s, left, e, 4), 2);
    CHECK(e[0].code == KEY_BACKSPACE && e[0].state == KBD_KEY_RELEASED);
    CHECK(e[1].code == KEY_LEFT && e[1].state == KBD_KEY_PRESSED && strcmp(e[1].utf8, "\033[D") == 0);
    CHECK_EQ(cp0_ui_osk_key_up(&s, left, 12000, e, 4), 1);
    /* Ctrl + arrow: the code with CTRL, no text; the armed Ctrl is used at the release */
    cp0_ui_osk_key_up(&s, ctrl, 13000, e, 4);
    cp0_ui_osk_key_down(&s, left, e, 4);
    CHECK(e[0].mods == KBD_MOD_CTRL && e[0].utf8[0] == '\0');
    cp0_ui_osk_key_up(&s, left, 13100, e, 4);
    CHECK(e[0].mods == KBD_MOD_CTRL && e[0].state == KBD_KEY_RELEASED);
    CHECK_EQ(s.ctrl.state, CP0_UI_OSK_MOD_OFF);

    /* pages: 123 -> symbols (shifted ones carry KBD_MOD_SHIFT), #+= -> second page, abc -> letters */
    cp0_ui_osk_key_up(&s, to123, 14000, e, 4);
    CHECK_EQ(s.page, CP0_UI_OSK_PAGE_SYMBOLS);
    const cp0_ui_osk_key_t *at = key_of(CP0_UI_OSK_PAGE_SYMBOLS, CP0_UI_OSK_K_CHAR, KEY_2, "@");
    const cp0_ui_osk_key_t *one = key_of(CP0_UI_OSK_PAGE_SYMBOLS, CP0_UI_OSK_K_CHAR, KEY_1, "1");
    if (at && one) {
        CHECK(is_pair(e, cp0_ui_osk_key_up(&s, at, 14100, e, 4), KEY_2, KBD_MOD_SHIFT, "@"));
        CHECK(is_pair(e, cp0_ui_osk_key_up(&s, one, 14200, e, 4), KEY_1, 0, "1"));
    }
    const cp0_ui_osk_key_t *euro = key_of(CP0_UI_OSK_PAGE_SYMBOLS2, CP0_UI_OSK_K_CHAR, 0, "\xe2\x82\xac");
    if (euro) {
        /* a character without a key: its text alone, and Ctrl does not apply to it (stays armed) */
        cp0_ui_osk_key_up(&s, ctrl, 15000, e, 4);
        CHECK(is_pair(e, cp0_ui_osk_key_up(&s, euro, 15100, e, 4), 0, 0, "\xe2\x82\xac"));
        CHECK_EQ(s.ctrl.state, CP0_UI_OSK_MOD_ARMED);
    }

    /* a Number field starts on the number pad */
    cp0_ui_osk_state_t num;
    cp0_ui_osk_state_init(&num, 1, 0);
    CHECK_EQ(num.page, CP0_UI_OSK_PAGE_NUMPAD);
    CHECK_EQ(num.home, CP0_UI_OSK_PAGE_NUMPAD);
    const cp0_ui_osk_key_t *seven = key_of(CP0_UI_OSK_PAGE_NUMPAD, CP0_UI_OSK_K_CHAR, KEY_7, "7");
    if (seven) CHECK(is_pair(e, cp0_ui_osk_key_up(&num, seven, 0, e, 4), KEY_7, 0, "7"));
}

static cp0_ui_osk_policy_t base_policy(void)
{
    cp0_ui_osk_policy_t p;
    memset(&p, 0, sizeof(p));
    p.now_s = 100000;
    return p;
}

static void test_policy(void)
{
    cp0_ui_osk_policy_t p = base_policy();
    CHECK(cp0_ui_osk_policy_allows(&p)); /* no keyboard ever seen: shown */
    /* a physical keyboard present: never shown */
    p.keyboards = 1;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p.keyboards = 3;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    /* an asleep Bluetooth keyboard counts as present for N = 10 minutes after it went */
    p = base_policy();
    p.bt_gone_s = p.now_s - 1;
    p.any_gone_s = p.bt_gone_s;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p.bt_gone_s = p.any_gone_s = p.now_s - (CP0_UI_OSK_BT_ASLEEP_S - 1);
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p.bt_gone_s = p.any_gone_s = p.now_s - CP0_UI_OSK_BT_ASLEEP_S;
    CHECK(cp0_ui_osk_policy_allows(&p));
    /* a USB keyboard unplugged: only the short grace */
    p = base_policy();
    p.any_gone_s = p.now_s - 1;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p.any_gone_s = p.now_s - CP0_UI_OSK_GRACE_S;
    CHECK(cp0_ui_osk_policy_allows(&p));
    /* Settings On (report 042): shown even with a physical keyboard, never under the lock screen nor in a desktop
     * session; the lock screen, a desktop session */
    p = base_policy();
    p.setting_on = 1;
    p.keyboards = 1;
    CHECK(cp0_ui_osk_policy_allows(&p));
    p.bt_gone_s = p.any_gone_s = p.now_s - 1;
    CHECK(cp0_ui_osk_policy_allows(&p));
    p.locked = 1;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p.locked = 0;
    p.desktop_session = 1;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p = base_policy();
    p.locked = 1;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p = base_policy();
    p.desktop_session = 1;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    /* APPLAUNCH_OSK: force shows even with a keyboard (never under the lock), off never shows (the setting On
     * included), auto is the normal rule */
    p = base_policy();
    p.keyboards = 1;
    p.env = CP0_UI_OSK_ENV_FORCE;
    CHECK(cp0_ui_osk_policy_allows(&p));
    p.locked = 1;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p = base_policy();
    p.env = CP0_UI_OSK_ENV_OFF;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p.setting_on = 1;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    p.setting_on = 0;
    p.env = CP0_UI_OSK_ENV_AUTO;
    CHECK(cp0_ui_osk_policy_allows(&p));
    p.keyboards = 1;
    CHECK(!cp0_ui_osk_policy_allows(&p));
    CHECK(!cp0_ui_osk_policy_allows(NULL));

    /* the stored setting (report 042): the old Off reads as Auto, Handwriting + keyboard only where offered */
    CHECK_EQ(cp0_ui_osk_setting_default(0), CP0_UI_OSK_SETTING_AUTO);
    CHECK_EQ(cp0_ui_osk_setting_default(1), CP0_UI_OSK_SETTING_HAND);
    for (int hand = 0; hand < 2; hand++) {
        CHECK_EQ(cp0_ui_osk_setting_read(0, hand), CP0_UI_OSK_SETTING_AUTO);
        CHECK_EQ(cp0_ui_osk_setting_read(CP0_UI_OSK_SETTING_OLD_OFF, hand), CP0_UI_OSK_SETTING_AUTO);
        CHECK_EQ(cp0_ui_osk_setting_read(2, hand), CP0_UI_OSK_SETTING_ON);
        CHECK_EQ(cp0_ui_osk_setting_read(3, hand), hand ? CP0_UI_OSK_SETTING_HAND : CP0_UI_OSK_SETTING_AUTO);
        CHECK_EQ(cp0_ui_osk_setting_read(4, hand), cp0_ui_osk_setting_default(hand));
        CHECK_EQ(cp0_ui_osk_setting_read(-1, hand), cp0_ui_osk_setting_default(hand));
    }

    CHECK_EQ(cp0_ui_osk_env_parse("force"), CP0_UI_OSK_ENV_FORCE);
    CHECK_EQ(cp0_ui_osk_env_parse("auto"), CP0_UI_OSK_ENV_AUTO);
    CHECK_EQ(cp0_ui_osk_env_parse("off"), CP0_UI_OSK_ENV_OFF);
    CHECK_EQ(cp0_ui_osk_env_parse("1"), CP0_UI_OSK_ENV_UNSET);
    CHECK_EQ(cp0_ui_osk_env_parse(NULL), CP0_UI_OSK_ENV_UNSET);
}

static void test_dialog(void)
{
    /* deck: a 120 px dialog centred above a 299 px keyboard (181 px of room) */
    CHECK_EQ(cp0_ui_osk_dialog_y(480, 0, 299, 120), 30);
    /* no keyboard: centred on the screen */
    CHECK_EQ(cp0_ui_osk_dialog_y(480, 0, 0, 120), 180);
    /* under a header */
    CHECK_EQ(cp0_ui_osk_dialog_y(480, 56, 0, 120), 56 + (424 - 120) / 2);
    /* too tall for the room: top-aligned */
    CHECK_EQ(cp0_ui_osk_dialog_y(320, 0, 189, 200), 0);
    CHECK_EQ(cp0_ui_osk_dialog_y(320, 40, 400, 50), 40);
}

/* The terminal's keyboard (cp0_ui_osk_layout_alt): Alt after Ctrl; the arrows stay where they fit with narrower
 * utility keys; the text fields' keyboard (cp0_ui_osk_layout) has no Alt and is unchanged. */
static void test_alt_layout(void)
{
    const int sizes[][3] = {{480, 320, 0}, {640, 480, 1130}, {800, 480, 920}, {720, 720, 1000}, {1280, 720, 1160},
                            {1280, 800, 847} /* the FZ-M1 tablet (report 028, x-a) */};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        const cp0_ui_metrics_t m = metrics(sizes[i][0], sizes[i][1], sizes[i][2] ? sizes[i][2] : ppmm_pi3a());
        cp0_ui_osk_layout_t plain, alt;
        CHECK_EQ(cp0_ui_osk_layout(&m, &plain), 0);
        CHECK_EQ(cp0_ui_osk_layout_alt(&m, &alt), 0);
        CHECK_EQ(plain.alt, 0);
        CHECK_EQ(alt.alt, 1);
        /* only the utility row differs */
        CHECK_EQ(alt.height, plain.height);
        CHECK_EQ(alt.key_w, plain.key_w);
        CHECK_EQ(alt.row_x, plain.row_x);
        CHECK(alt.util_key_w >= alt.key_w && alt.util_key_w <= plain.util_key_w);
        cp0_ui_osk_place_t p[CP0_UI_OSK_MAX_KEYS], q[CP0_UI_OSK_MAX_KEYS];
        const int n = cp0_ui_osk_place_keys(&alt, CP0_UI_OSK_PAGE_LETTERS, p, CP0_UI_OSK_MAX_KEYS);
        const int np = cp0_ui_osk_place_keys(&plain, CP0_UI_OSK_PAGE_LETTERS, q, CP0_UI_OSK_MAX_KEYS);
        CHECK_EQ(n, np + 1 + (alt.arrows - plain.arrows) * 4);
        CHECK(p[2].key->kind == CP0_UI_OSK_K_CTRL && p[3].key->kind == CP0_UI_OSK_K_ALT);
        CHECK(strcmp(p[3].key->label, "Alt") == 0);
        CHECK_EQ(p[3].x, p[2].x + p[2].w + alt.gap);
        /* row 0 inside the key area, no overlap, the arrows ending at its right edge */
        int row0 = 0;
        for (int k = 0; k < n && p[k].row == 0; ++k) {
            row0++;
            CHECK(p[k].x >= alt.row_x && p[k].x + p[k].w <= alt.row_x + alt.row_w);
            if (k > 0) CHECK(p[k].x >= p[k - 1].x + p[k - 1].w + alt.gap);
        }
        CHECK_EQ(row0, 4 + 4 * alt.arrows);
        if (alt.arrows) CHECK_EQ(p[7].x + p[7].w, alt.row_x + alt.row_w);
        /* the target sizes keep their arrows (the deck's, 640x480: 4 utility keys narrowed from 102 px) */
        CHECK_EQ(alt.arrows, plain.arrows);
        /* the other rows are the text fields' */
        for (int k = 0; k < np; ++k) {
            if (q[k].row == 0) continue;
            const int j = k + (n - np);
            CHECK(q[k].x == p[j].x && q[k].y == p[j].y && q[k].w == p[j].w && q[k].key == p[j].key);
        }
    }
    /* the deck: the arrows stay, the utility keys narrower than their 9 mm (still wider than a letter key) */
    const cp0_ui_metrics_t deck = metrics(640, 480, 1130);
    cp0_ui_osk_layout_t l;
    cp0_ui_osk_layout_alt(&deck, &l);
    CHECK(l.arrows && l.util_key_w > l.key_w && l.util_key_w < 102);
    /* the FZ-M1: room for 9 mm utility keys and the arrows */
    const cp0_ui_metrics_t tablet = metrics(1280, 800, 847);
    cp0_ui_osk_layout_t lp;
    cp0_ui_osk_layout(&tablet, &lp);
    cp0_ui_osk_layout_alt(&tablet, &l);
    CHECK(l.arrows && l.util_key_w == lp.util_key_w);
    CHECK_EQ(cp0_ui_osk_layout_alt(NULL, &l), -1);
}

/* The close cross (report 042, the PDA profile: the keyboard's way back to the writing zone): one key wide at the right
 * end of the utility row, the arrows left of it, nothing overlapping, on the text fields' and the terminal's keyboards
 * and the number pad; it sends nothing. */
static void test_close_layout(void)
{
    const int sizes[][3] = {{480, 800, 857}, {480, 640, 900}, {640, 480, 1130}, {480, 320, 0}};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        const cp0_ui_metrics_t m = metrics(sizes[i][0], sizes[i][1], sizes[i][2] ? sizes[i][2] : ppmm_pi3a());
        for (int terminal = 0; terminal < 2; ++terminal) {
            cp0_ui_osk_layout_t l, before;
            CHECK_EQ(terminal ? cp0_ui_osk_layout_alt(&m, &l) : cp0_ui_osk_layout(&m, &l), 0);
            before = l;
            CHECK_EQ(l.close, 0);
            CHECK_EQ(cp0_ui_osk_layout_add_close(&l), 0);
            CHECK_EQ(l.close, 1);
            CHECK(l.util_key_w >= l.key_w && l.util_key_w <= before.util_key_w);
            CHECK(l.arrows <= before.arrows);
            CHECK_EQ(l.height, before.height);
            const cp0_ui_osk_page_t pages[] = {CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_PAGE_NUMPAD};
            for (size_t pg = 0; pg < 2; ++pg) {
                cp0_ui_osk_place_t p[CP0_UI_OSK_MAX_KEYS];
                const int n = cp0_ui_osk_place_keys(&l, pages[pg], p, CP0_UI_OSK_MAX_KEYS);
                CHECK(n > 0);
                int row0 = 0, close_at = -1;
                for (int k = 0; k < n && p[k].row == 0; ++k) {
                    row0++;
                    CHECK(p[k].x >= l.row_x && p[k].x + p[k].w <= l.row_x + l.row_w);
                    if (k > 0) CHECK(p[k].x >= p[k - 1].x + p[k - 1].w + l.gap);
                    if (p[k].key->kind == CP0_UI_OSK_K_CLOSE) close_at = k;
                }
                CHECK_EQ(row0, (terminal ? 4 : 3) + 4 * l.arrows + 1);
                CHECK_EQ(close_at, row0 - 1);
                if (close_at < 0) continue;
                const cp0_ui_osk_place_t *c = &p[close_at];
                CHECK_EQ(c->w, l.key_w);
                CHECK_EQ(c->x + c->w, l.row_x + l.row_w);
                if (l.arrows) CHECK_EQ(p[close_at - 1].x + p[close_at - 1].w + l.gap, c->x);
                cp0_ui_osk_state_t st;
                memset(&st, 0, sizeof(st));
                cp0_ui_osk_event_t e[CP0_UI_OSK_MAX_EVENTS];
                CHECK_EQ(cp0_ui_osk_key_down(&st, c->key, e, CP0_UI_OSK_MAX_EVENTS), 0);
                CHECK_EQ(cp0_ui_osk_key_up(&st, c->key, 1000, e, CP0_UI_OSK_MAX_EVENTS), 0);
            }
            /* the PDA panels keep their arrows beside the cross */
            if (sizes[i][0] == 480 && sizes[i][1] >= 640) CHECK(l.arrows);
            printf("close %dx%d %s: util %d -> %d px, arrows %d\n", sizes[i][0], sizes[i][1],
                   terminal ? "terminal" : "fields", before.util_key_w, l.util_key_w, l.arrows);
        }
    }
    CHECK_EQ(cp0_ui_osk_layout_add_close(NULL), -1);
}

static int is_alt(const cp0_ui_osk_event_t *e, int state)
{
    return e->code == KEY_LEFTALT && e->state == state && e->utf8[0] == '\0';
}

static void test_alt_events(void)
{
    /* Alt is on the terminal's keyboard only */
    const cp0_ui_metrics_t m = metrics(640, 480, 1130);
    cp0_ui_osk_layout_t l;
    cp0_ui_osk_place_t places[CP0_UI_OSK_MAX_KEYS];
    cp0_ui_osk_layout(&m, &l);
    int count = cp0_ui_osk_place_keys(&l, CP0_UI_OSK_PAGE_LETTERS, places, CP0_UI_OSK_MAX_KEYS);
    CHECK(find_key(places, count, CP0_UI_OSK_K_ALT, KEY_LEFTALT, NULL) == NULL);
    cp0_ui_osk_layout_alt(&m, &l);
    count = cp0_ui_osk_place_keys(&l, CP0_UI_OSK_PAGE_LETTERS, places, CP0_UI_OSK_MAX_KEYS);
    const cp0_ui_osk_place_t *alt_place = find_key(places, count, CP0_UI_OSK_K_ALT, KEY_LEFTALT, NULL);
    CHECK(alt_place != NULL);
    const cp0_ui_osk_key_t *alt = alt_place ? alt_place->key : NULL;
    const cp0_ui_osk_key_t *tab = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_TAB, KEY_TAB, NULL);
    const cp0_ui_osk_key_t *b = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CHAR, KEY_B, NULL);
    const cp0_ui_osk_key_t *c = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CHAR, KEY_C, NULL);
    const cp0_ui_osk_key_t *ctrl = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CTRL, 0, NULL);
    const cp0_ui_osk_key_t *esc = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_ESC, KEY_ESC, NULL);
    const cp0_ui_osk_key_t *left = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_ARROW, KEY_LEFT, NULL);
    const cp0_ui_osk_key_t *enter = key_of(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_ENTER, KEY_ENTER, NULL);
    if (!alt || !tab || !b || !c || !ctrl || !esc || !left || !enter) return;
    cp0_ui_osk_state_t s;
    cp0_ui_osk_event_t e[CP0_UI_OSK_MAX_EVENTS];
    cp0_ui_osk_state_init(&s, 0, 1); /* the terminal: multi-line */

    /* a quick Alt+Tab: Alt's press when armed, then Tab (code only, ALT), then Alt's release (it was used) */
    int n = cp0_ui_osk_key_up(&s, alt, 1000, e, 4);
    CHECK_EQ(n, 1);
    CHECK(is_alt(&e[0], KBD_KEY_PRESSED) && e[0].mods == KBD_MOD_ALT);
    CHECK_EQ(s.alt.state, CP0_UI_OSK_MOD_ARMED);
    n = cp0_ui_osk_key_up(&s, tab, 2000, e, 4);
    CHECK_EQ(n, 3);
    CHECK(e[0].code == KEY_TAB && e[0].state == KBD_KEY_PRESSED && e[0].mods == KBD_MOD_ALT && e[0].utf8[0] == '\0');
    CHECK(e[1].code == KEY_TAB && e[1].state == KBD_KEY_RELEASED && e[1].mods == KBD_MOD_ALT);
    CHECK(is_alt(&e[2], KBD_KEY_RELEASED));
    CHECK_EQ(s.alt.state, CP0_UI_OSK_MOD_OFF);

    /* an Alt+Tab session: Alt locked (double tap: one press), Tab, Tab, then Alt off (its release commits) */
    n = cp0_ui_osk_key_up(&s, alt, 3000, e, 4);
    CHECK(n == 1 && is_alt(&e[0], KBD_KEY_PRESSED));
    n = cp0_ui_osk_key_up(&s, alt, 3100, e, 4);
    CHECK_EQ(n, 0);
    CHECK_EQ(s.alt.state, CP0_UI_OSK_MOD_LOCKED);
    CHECK_EQ(cp0_ui_osk_key_up(&s, tab, 3500, e, 4), 2);
    CHECK_EQ(cp0_ui_osk_key_up(&s, tab, 3600, e, 4), 2);
    CHECK_EQ(s.alt.state, CP0_UI_OSK_MOD_LOCKED);
    /* Esc while Alt is down: the toolbar's Esc (cancels the session), Alt stays */
    n = cp0_ui_osk_key_down(&s, esc, e, 4);
    CHECK(n == 1 && e[0].code == KEY_ESC && e[0].mods == 0);
    n = cp0_ui_osk_key_up(&s, esc, 3700, e, 4);
    CHECK(n == 1 && e[0].code == KEY_ESC && e[0].state == KBD_KEY_RELEASED);
    CHECK_EQ(s.alt.state, CP0_UI_OSK_MOD_LOCKED);
    n = cp0_ui_osk_key_up(&s, alt, 4000, e, 4);
    CHECK(n == 1 && is_alt(&e[0], KBD_KEY_RELEASED));
    CHECK_EQ(s.alt.state, CP0_UI_OSK_MOD_OFF);

    /* armed, then tapped again too late: off, its release */
    cp0_ui_osk_key_up(&s, alt, 5000, e, 4);
    n = cp0_ui_osk_key_up(&s, alt, 6000, e, 4);
    CHECK(n == 1 && is_alt(&e[0], KBD_KEY_RELEASED));

    /* Alt+b: the key with ALT and its text, as a physical Alt+key; Alt released after it */
    cp0_ui_osk_key_up(&s, alt, 7000, e, 4);
    n = cp0_ui_osk_key_up(&s, b, 7500, e, 4);
    CHECK_EQ(n, 3);
    CHECK(e[0].code == KEY_B && e[0].mods == KBD_MOD_ALT && strcmp(e[0].utf8, "b") == 0);
    CHECK(is_alt(&e[2], KBD_KEY_RELEASED));

    /* Ctrl+Alt+c: the code with both, no text; both one-shots used */
    cp0_ui_osk_key_up(&s, ctrl, 8000, e, 4);
    cp0_ui_osk_key_up(&s, alt, 8100, e, 4);
    n = cp0_ui_osk_key_up(&s, c, 8200, e, 4);
    CHECK_EQ(n, 3);
    CHECK(e[0].code == KEY_C && e[0].mods == (KBD_MOD_CTRL | KBD_MOD_ALT) && e[0].utf8[0] == '\0');
    CHECK(is_alt(&e[2], KBD_KEY_RELEASED));
    CHECK(!cp0_ui_osk_mod_on(&s.ctrl) && !cp0_ui_osk_mod_on(&s.alt));

    /* Alt + a hold key (an arrow): pressed with ALT, Alt released after its release */
    cp0_ui_osk_key_up(&s, alt, 9000, e, 4);
    n = cp0_ui_osk_key_down(&s, left, e, 4);
    CHECK(n == 1 && e[0].code == KEY_LEFT && e[0].mods == KBD_MOD_ALT);
    n = cp0_ui_osk_key_up(&s, left, 9100, e, 4);
    CHECK(n == 2 && e[0].code == KEY_LEFT && e[0].state == KBD_KEY_RELEASED && is_alt(&e[1], KBD_KEY_RELEASED));

    /* Alt + Enter carries ALT and its control text; Alt released */
    cp0_ui_osk_key_up(&s, alt, 10000, e, 4);
    n = cp0_ui_osk_key_up(&s, enter, 10100, e, 4);
    CHECK(n == 3 && e[0].code == KEY_ENTER && e[0].mods == KBD_MOD_ALT && strcmp(e[0].utf8, "\r") == 0);

    /* the keyboard goes away with Alt locked and an arrow held: both releases owed go out */
    cp0_ui_osk_key_up(&s, alt, 11000, e, 4);
    cp0_ui_osk_key_up(&s, alt, 11100, e, 4);
    CHECK_EQ(s.alt.state, CP0_UI_OSK_MOD_LOCKED);
    cp0_ui_osk_key_down(&s, left, e, 4);
    n = cp0_ui_osk_release_all(&s, e, 4);
    CHECK(n == 2 && e[0].code == KEY_LEFT && e[0].state == KBD_KEY_RELEASED && is_alt(&e[1], KBD_KEY_RELEASED));
    CHECK_EQ(s.alt.state, CP0_UI_OSK_MOD_OFF);
    CHECK_EQ(cp0_ui_osk_release_all(&s, e, 4), 0);

    /* a text field's Tab (no Alt) is unchanged: the code alone, no tab character in a single-line field */
    cp0_ui_osk_state_init(&s, 0, 0);
    n = cp0_ui_osk_key_up(&s, tab, 12000, e, 4);
    CHECK(n == 2 && e[0].mods == 0 && e[0].utf8[0] == '\0');
}

int main(void)
{
    test_layout_sizes();
    test_place_keys();
    test_alt_layout();
    test_close_layout();
    test_alt_events();
    test_modifiers();
    test_events();
    test_policy();
    test_dialog();
    if (failures) {
        fprintf(stderr, "test_ui_osk: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ui_osk: OK\n");
    return 0;
}
