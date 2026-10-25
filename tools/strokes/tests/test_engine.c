/*
 * SPDX-License-Identifier: MIT
 *
 * Engine: capture clean-up (tap, dwell, too short, drop-out healing), timing of results, the mode machine, the
 * queue, robustness to odd event orders, determinism.
 */
#include "strokes_internal.h"
#include "test.h"

#include <stdlib.h>

static strokes_engine *eng(void)
{
    return strokes_engine_new(test_table(), NULL);
}

/* points around (x, y) within r px for dur_ms, one every 10 ms */
static void hold(strokes_engine *e, int box, float x, float y, float r, float dur_ms, uint64_t t0)
{
    strokes_down(e, box, x, y, t0);
    int n = (int)(dur_ms / 10.0f);
    for (int i = 1; i <= n; ++i)
        strokes_move(e, x + ((i % 2) ? r : -r), y + ((i % 3) ? r : 0), t0 + (uint64_t)i * 10000);
    strokes_up(e, t0 + (uint64_t)(dur_ms * 1000.0f));
}

static strokes_result flush1(strokes_engine *e, int *count)
{
    strokes_result r, first;
    memset(&first, 0, sizeof first);
    int n = 0;
    while (strokes_flush(e, &r)) {
        if (n == 0)
            first = r;
        n++;
    }
    if (count)
        *count = n;
    return first;
}

static void tap_and_period(void)
{
    strokes_engine *e = eng();
    int n;
    hold(e, STROKES_BOX_LETTERS, 250, 550, 2, 100, 1000000);
    strokes_result r = flush1(e, &n);
    CHECK(n == 1 && r.kind == STROKES_MODE && strcmp(r.glyph, "tap") == 0 && (r.mode & STROKES_MODE_PUNCT));
    CHECK(strokes_get_mode(e) == STROKES_MODE_PUNCT);
    hold(e, STROKES_BOX_LETTERS, 250, 550, 2, 80, 2000000);
    r = flush1(e, &n);
    CHECK(n == 1 && r.kind == STROKES_CHAR && strcmp(r.utf8, ".") == 0 && strcmp(r.glyph, "period") == 0);
    CHECK(r.set == STROKES_SET_PUNCT && r.mode == STROKES_MODE_PUNCT && strokes_get_mode(e) == STROKES_MODE_PUNCT);
    /* a tap in the digits box leaves the letters box's mode and enters the same punctuation mode there */
    hold(e, STROKES_BOX_DIGITS, 250, 550, 1, 60, 3000000);
    r = flush1(e, &n);
    CHECK(r.kind == STROKES_MODE && strcmp(r.glyph, "tap") == 0 && strokes_get_mode(e) == STROKES_MODE_PUNCT);
    strokes_engine_free(e);
}

static void dwell_and_short(void)
{
    strokes_engine *e = eng();
    int n;
    hold(e, STROKES_BOX_LETTERS, 250, 550, 2, 400, 1000000); /* a rest, not a tap */
    strokes_result r = flush1(e, &n);
    CHECK(n == 1 && r.kind == STROKES_REJECT && r.reject == STROKES_REJECT_DWELL && strokes_get_mode(e) == 0);
    float shortline[] = {250, 550, 268, 550}; /* 2.1 mm */
    strokes_result rr[2];
    n = test_feed(e, STROKES_BOX_LETTERS, shortline, 2, 2000000, 0.5f, 10.0f, rr, 2);
    CHECK(n == 1 && rr[0].kind == STROKES_REJECT && rr[0].reject == STROKES_REJECT_SHORT);
    strokes_engine_free(e);
}

/* a vertical line down, 100 px, at 1 px/ms, frames every 10 ms, with a lift between t_cut and t_cut + gap_ms; the
 * press after the gap lands at the position the pen reached plus `jump` px sideways */
static int line_with_gap(strokes_engine *e, float gap_ms, float jump, float speed, strokes_result *res)
{
    uint64_t t = 1000000;
    float y = 500;
    strokes_down(e, STROKES_BOX_LETTERS, 250, y, t);
    int lifted = 0;
    float cut = 50;
    while (y < 600) {
        t += 10000;
        y += speed * 10.0f;
        if (!lifted && y >= 500 + cut) {
            strokes_up(e, t);
            t += (uint64_t)(gap_ms * 1000.0f);
            y += speed * gap_ms;
            strokes_down(e, STROKES_BOX_LETTERS, 250 + jump, y, t);
            lifted = 1;
            continue;
        }
        strokes_move(e, 250 + (lifted ? jump : 0), y, t);
    }
    strokes_up(e, t);
    int n = 0;
    strokes_result r;
    while (strokes_flush(e, &r))
        if (n < 4)
            res[n++] = r;
    return n;
}

static void healing(void)
{
    strokes_engine *e = eng();
    strokes_result r[4];
    int n = line_with_gap(e, 30, 5, 0.5f, r); /* slow pen, 30 ms gap, 0.6 mm sideways */
    CHECK(n == 1 && r[0].healed == 1 && strcmp(r[0].glyph, "i") == 0);
    n = line_with_gap(e, 80, 0, 0.5f, r); /* gap longer than heal_ms: two strokes */
    CHECK(n == 2);
    n = line_with_gap(e, 20, 250, 0.5f, r); /* far away: a new stroke */
    CHECK(n == 2);
    n = line_with_gap(e, 45, 0, 2.0f, r); /* fast pen (234 mm/s): it travels 9 mm during the gap */
    CHECK(n == 1 && r[0].healed == 1);
    strokes_engine_free(e);
    strokes_config cfg;
    strokes_config_default(&cfg);
    cfg.heal_speed = 0; /* report 038's plain rule: 3 mm from the lift point only */
    e = strokes_engine_new(test_table(), &cfg);
    n = line_with_gap(e, 45, 0, 2.0f, r);
    CHECK(n == 2);
    n = line_with_gap(e, 30, 5, 0.5f, r);
    CHECK(n == 1);
    strokes_engine_free(e);
}

static void timing(void)
{
    strokes_engine *e = eng();
    strokes_result r;
    float xy[] = {250, 500, 250, 600};
    strokes_down(e, STROKES_BOX_LETTERS, xy[0], xy[1], 1000000);
    for (int i = 1; i <= 10; ++i)
        strokes_move(e, 250, 500 + 10.0f * i, 1000000 + (uint64_t)i * 10000);
    CHECK(strokes_busy(e));
    CHECK(strokes_poll(e, 1200000, &r) == 0); /* still down */
    strokes_up(e, 1110000);
    CHECK(strokes_busy(e));
    CHECK(strokes_poll(e, 1150000, &r) == 0); /* inside the heal window */
    CHECK(strokes_poll(e, 1170000, &r) == 1 && strcmp(r.glyph, "i") == 0);
    CHECK(!strokes_busy(e) && strokes_poll(e, 1200000, &r) == 0 && r.kind == STROKES_NONE);
    /* a press after the window finalises the previous stroke at once */
    strokes_down(e, STROKES_BOX_LETTERS, 250, 500, 2000000);
    for (int i = 1; i <= 10; ++i)
        strokes_move(e, 250, 500 + 10.0f * i, 2000000 + (uint64_t)i * 10000);
    strokes_up(e, 2110000);
    strokes_down(e, STROKES_BOX_LETTERS, 200, 550, 2300000);
    CHECK(strokes_poll(e, 2300000, &r) == 1 && strcmp(r.glyph, "i") == 0 && r.t_up_us == 2110000);
    strokes_cancel(e);
    CHECK(!strokes_busy(e) && strokes_flush(e, &r) == 0);
    strokes_engine_free(e);
}

static uint64_t draw_clock = 10000000;

static uint64_t clock_next(void)
{
    draw_clock += 2000000;
    return draw_clock;
}

static strokes_result draw(strokes_engine *e, int box, const float *xy, int n)
{
    strokes_result r[4];
    memset(r, 0, sizeof r);
    test_feed(e, box, xy, n, clock_next(), 0.8f, 10.0f, r, 4);
    return r[0];
}

static const float A[] = {200, 600, 250, 500, 300, 600};
static const float B1[] = {250, 500, 250, 600}; /* the digit 1 in the digits box */
static const float SHIFT[] = {250, 600, 250, 500};
static const float SPACE[] = {200, 550, 300, 550};
static const float BKSP[] = {300, 550, 200, 550};
static const float ENTER[] = {300, 500, 200, 600};

static void modes(void)
{
    strokes_engine *e = eng();
    strokes_result r = draw(e, STROKES_BOX_LETTERS, A, 3);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "a") == 0 && r.mode == 0);
    r = draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    CHECK(r.kind == STROKES_MODE && r.mode == STROKES_MODE_SHIFT && r.utf8[0] == 0);
    r = draw(e, STROKES_BOX_LETTERS, A, 3);
    CHECK(strcmp(r.utf8, "A") == 0 && r.mode == 0); /* one-shot */
    r = draw(e, STROKES_BOX_LETTERS, A, 3);
    CHECK(strcmp(r.utf8, "a") == 0);
    /* caps lock: two shifts; a third ends it */
    draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    r = draw(e, STROKES_BOX_DIGITS, SHIFT, 2); /* the gesture works in both boxes */
    CHECK(r.mode == STROKES_MODE_CAPS);
    CHECK(strcmp(draw(e, STROKES_BOX_LETTERS, A, 3).utf8, "A") == 0);
    CHECK(strcmp(draw(e, STROKES_BOX_LETTERS, A, 3).utf8, "A") == 0);
    r = draw(e, STROKES_BOX_DIGITS, B1, 2);
    CHECK(strcmp(r.utf8, "1") == 0 && r.mode == STROKES_MODE_CAPS);
    r = draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    CHECK(r.mode == 0);
    CHECK(strcmp(draw(e, STROKES_BOX_LETTERS, A, 3).utf8, "a") == 0);
    /* keys leave a pending shift alone; the next character takes it */
    draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    r = draw(e, STROKES_BOX_LETTERS, SPACE, 2);
    CHECK(r.kind == STROKES_KEY && r.key == STROKES_KEY_SPACE && strcmp(r.utf8, " ") == 0 &&
          r.mode == STROKES_MODE_SHIFT);
    r = draw(e, STROKES_BOX_DIGITS, BKSP, 2);
    CHECK(r.kind == STROKES_KEY && r.key == STROKES_KEY_BACKSPACE && r.utf8[0] == 0 && r.mode == STROKES_MODE_SHIFT);
    r = draw(e, STROKES_BOX_LETTERS, ENTER, 2);
    CHECK(r.kind == STROKES_KEY && r.key == STROKES_KEY_ENTER && strcmp(r.utf8, "\n") == 0);
    r = draw(e, STROKES_BOX_DIGITS, B1, 2); /* a digit takes the shift, unchanged */
    CHECK(strcmp(r.utf8, "1") == 0 && r.mode == 0);
    /* a rejection keeps the shift */
    draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    float diag[] = {200, 600, 300, 500}; /* up-right: no glyph of the letters box */
    r = draw(e, STROKES_BOX_LETTERS, diag, 2);
    CHECK(r.kind == STROKES_REJECT && r.mode == STROKES_MODE_SHIFT);
    /* punctuation: a stroke that is no punctuation is rejected and the mode waits for the stroke written again */
    strokes_set_mode(e, STROKES_MODE_PUNCT | STROKES_MODE_SHIFT);
    r = draw(e, STROKES_BOX_LETTERS, diag, 2);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "/") == 0 && r.set == STROKES_SET_PUNCT &&
          r.mode == STROKES_MODE_PUNCT); /* the shift is used up, the mode stays */
    strokes_set_mode(e, STROKES_MODE_PUNCT | STROKES_MODE_SHIFT);
    float zig[] = {200, 500, 300, 520, 210, 540, 290, 560, 220, 580, 280, 600};
    r = draw(e, STROKES_BOX_LETTERS, zig, 6);
    CHECK(r.kind == STROKES_REJECT && r.set == STROKES_SET_PUNCT &&
          r.mode == (STROKES_MODE_PUNCT | STROKES_MODE_SHIFT));
    /* backspace leaves the mode and deletes nothing; the shift stays */
    r = draw(e, STROKES_BOX_LETTERS, BKSP, 2);
    CHECK(r.kind == STROKES_MODE && r.key == STROKES_KEY_NONE && strcmp(r.glyph, "bksp") == 0 &&
          r.mode == STROKES_MODE_SHIFT);
    /* in the punctuation set the gestures' lines are punctuation: a line right is the hyphen (the mode stays) */
    strokes_set_mode(e, STROKES_MODE_PUNCT);
    r = draw(e, STROKES_BOX_LETTERS, SPACE, 2);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "-") == 0 && r.mode == STROKES_MODE_PUNCT);
    strokes_set_mode(e, STROKES_MODE_SHIFT | STROKES_MODE_CAPS | 0x80);
    CHECK(strokes_get_mode(e) == STROKES_MODE_CAPS);
    strokes_set_mode(e, STROKES_MODE_PUNCT | STROKES_MODE_EXT | STROKES_MODE_ACCENT);
    CHECK(strokes_get_mode(e) == STROKES_MODE_PUNCT); /* PUNCT wins, no accent is invented */
    strokes_set_mode(e, 0);
    CHECK(strokes_get_mode(e) == 0 && strokes_get_mode(NULL) == 0);
    strokes_engine_free(e);
}

static const float EXT[] = {200, 500, 300, 600};  /* the extended prefix: a line down-right */
static const float X2[] = {300, 500, 200, 600};   /* the second stroke of X: a line down-left */

static void extended_and_x(void)
{
    strokes_engine *e = eng();
    /* X is two strokes: the prefix, then the line down-left */
    strokes_result r = draw(e, STROKES_BOX_LETTERS, EXT, 2);
    CHECK(r.kind == STROKES_MODE && strcmp(r.glyph, "ext") == 0 && r.mode == STROKES_MODE_EXT);
    r = draw(e, STROKES_BOX_LETTERS, X2, 2);
    /* x is a letter: it ends the extended mode */
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "x") == 0 && r.set == STROKES_SET_EXT && r.mode == 0);
    /* the line down-left without the prefix is enter */
    r = draw(e, STROKES_BOX_LETTERS, X2, 2);
    CHECK(r.kind == STROKES_KEY && r.key == STROKES_KEY_ENTER);
    /* shift applies across the prefix */
    draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    draw(e, STROKES_BOX_LETTERS, EXT, 2);
    CHECK(strokes_get_mode(e) == (STROKES_MODE_SHIFT | STROKES_MODE_EXT));
    r = draw(e, STROKES_BOX_LETTERS, X2, 2);
    CHECK(strcmp(r.utf8, "X") == 0 && r.mode == 0);
    /* a tap in the extended mode is the bullet; the prefix line is no glyph of the ext set (rejected, the mode
     * waits); backspace leaves it */
    draw(e, STROKES_BOX_DIGITS, EXT, 2);
    int n;
    hold(e, STROKES_BOX_DIGITS, 250, 550, 2, 100, clock_next());
    r = flush1(e, &n);
    CHECK(n == 1 && r.kind == STROKES_CHAR && strcmp(r.utf8, "\xe2\x80\xa2") == 0 && r.mode == STROKES_MODE_EXT);
    r = draw(e, STROKES_BOX_DIGITS, EXT, 2);
    CHECK(r.kind == STROKES_REJECT && r.mode == STROKES_MODE_EXT); /* no down-right glyph in the ext set */
    r = draw(e, STROKES_BOX_DIGITS, BKSP, 2);
    CHECK(r.kind == STROKES_MODE && r.key == STROKES_KEY_NONE && r.mode == 0);
    /* extended characters: the c shape is the copyright sign, upper-casing leaves it alone */
    strokes_table *t = test_table();
    draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    draw(e, STROKES_BOX_LETTERS, EXT, 2);
    r = test_write_glyph(e, t, "copy", STROKES_SET_EXT, STROKES_BOX_LETTERS);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "\xc2\xa9") == 0 && r.mode == STROKES_MODE_EXT);
    strokes_engine_free(e);
}

/* Punctuation and extended modes stay on until backspace or a box change (owner decision 2026-10-25). */
static void sticky_modes(void)
{
    strokes_engine *e = eng();
    strokes_table *t = test_table();
    float diag[] = {200, 600, 300, 500}; /* the slash in the punctuation set */
    float zig[] = {200, 500, 300, 520, 210, 540, 290, 560, 220, 580, 280, 600};
    int n;
    /* several glyphs in a row: tap, then . / - ? tab, all in the punctuation set */
    hold(e, STROKES_BOX_LETTERS, 250, 550, 2, 100, clock_next());
    strokes_result r = flush1(e, &n);
    CHECK(r.kind == STROKES_MODE && r.mode == STROKES_MODE_PUNCT);
    hold(e, STROKES_BOX_LETTERS, 250, 550, 2, 80, clock_next());
    r = flush1(e, &n);
    CHECK(strcmp(r.utf8, ".") == 0 && r.mode == STROKES_MODE_PUNCT);
    r = draw(e, STROKES_BOX_LETTERS, diag, 2);
    CHECK(strcmp(r.utf8, "/") == 0 && r.set == STROKES_SET_PUNCT && r.mode == STROKES_MODE_PUNCT);
    r = draw(e, STROKES_BOX_LETTERS, SPACE, 2);
    CHECK(strcmp(r.utf8, "-") == 0 && r.mode == STROKES_MODE_PUNCT);
    r = test_write_glyph(e, t, "question", STROKES_SET_PUNCT, STROKES_BOX_LETTERS);
    CHECK(strcmp(r.utf8, "?") == 0 && r.mode == STROKES_MODE_PUNCT);
    r = test_write_glyph(e, t, "tab", STROKES_SET_PUNCT, STROKES_BOX_LETTERS);
    CHECK(r.kind == STROKES_KEY && r.key == STROKES_KEY_TAB && r.mode == STROKES_MODE_PUNCT); /* a key keeps it */
    /* a rejected stroke keeps the mode */
    r = draw(e, STROKES_BOX_LETTERS, zig, 6);
    CHECK(r.kind == STROKES_REJECT && r.mode == STROKES_MODE_PUNCT && strokes_get_mode(e) == STROKES_MODE_PUNCT);
    /* backspace leaves without deleting; the next stroke is a letter again */
    r = draw(e, STROKES_BOX_LETTERS, BKSP, 2);
    CHECK(r.kind == STROKES_MODE && r.key == STROKES_KEY_NONE && r.utf8[0] == 0 && r.mode == 0);
    CHECK(strokes_get_mode(e) == 0);
    r = draw(e, STROKES_BOX_LETTERS, A, 3);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "a") == 0 && r.set == STROKES_SET_LETTERS);
    /* a second backspace is a real one */
    r = draw(e, STROKES_BOX_LETTERS, BKSP, 2);
    CHECK(r.kind == STROKES_KEY && r.key == STROKES_KEY_BACKSPACE);
    /* the extended mode: the same rule */
    draw(e, STROKES_BOX_LETTERS, EXT, 2);
    r = test_write_glyph(e, t, "copy", STROKES_SET_EXT, STROKES_BOX_LETTERS);
    CHECK(strcmp(r.utf8, "\xc2\xa9") == 0 && r.mode == STROKES_MODE_EXT);
    r = test_write_glyph(e, t, "euro", STROKES_SET_EXT, STROKES_BOX_LETTERS);
    CHECK(strcmp(r.utf8, "\xe2\x82\xac") == 0 && r.mode == STROKES_MODE_EXT);
    r = draw(e, STROKES_BOX_LETTERS, zig, 6);
    CHECK(r.kind == STROKES_REJECT && r.mode == STROKES_MODE_EXT);
    hold(e, STROKES_BOX_LETTERS, 250, 550, 2, 80, clock_next());
    r = flush1(e, &n);
    CHECK(strcmp(r.utf8, "\xe2\x80\xa2") == 0 && r.mode == STROKES_MODE_EXT);
    r = draw(e, STROKES_BOX_LETTERS, BKSP, 2);
    CHECK(r.kind == STROKES_MODE && r.key == STROKES_KEY_NONE && r.mode == 0);
    /* a box change clears the mode as soon as the pen lands, before the stroke's result */
    draw(e, STROKES_BOX_LETTERS, EXT, 2);
    strokes_down(e, STROKES_BOX_DIGITS, 250, 500, clock_next());
    CHECK(strokes_get_mode(e) == 0);
    strokes_cancel(e);
    draw(e, STROKES_BOX_LETTERS, EXT, 2);
    r = draw(e, STROKES_BOX_DIGITS, B1, 2);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "1") == 0 && r.set == STROKES_SET_DIGITS && r.mode == 0);
    hold(e, STROKES_BOX_DIGITS, 250, 550, 2, 100, clock_next()); /* punctuation in the digits box ... */
    flush1(e, &n);
    r = draw(e, STROKES_BOX_LETTERS, A, 3); /* ... a letter in the letters box leaves it */
    CHECK(strcmp(r.utf8, "a") == 0 && r.mode == 0);
    /* the tap in the other box is a new punctuation mode, not the bullet */
    draw(e, STROKES_BOX_DIGITS, EXT, 2);
    hold(e, STROKES_BOX_LETTERS, 250, 550, 2, 100, clock_next());
    r = flush1(e, &n);
    CHECK(r.kind == STROKES_MODE && strcmp(r.glyph, "tap") == 0 && r.mode == STROKES_MODE_PUNCT);
    /* a mode set by the UI belongs to the box of the next stroke */
    strokes_set_mode(e, STROKES_MODE_PUNCT);
    r = draw(e, STROKES_BOX_DIGITS, diag, 2);
    CHECK(strcmp(r.utf8, "/") == 0 && r.mode == STROKES_MODE_PUNCT);
    r = draw(e, STROKES_BOX_LETTERS, A, 3);
    CHECK(strcmp(r.utf8, "a") == 0 && r.mode == 0);
    strokes_engine_free(e);
    /* the dead accent stays one letter */
    t = test_table_full();
    e = strokes_engine_new(t, NULL);
    test_write_glyph(e, t, "acc_acute", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(strcmp(test_write_glyph(e, t, "e", STROKES_SET_LETTERS, STROKES_BOX_LETTERS).utf8, "\xc3\xa9") == 0);
    r = test_write_glyph(e, t, "e", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(strcmp(r.utf8, "e") == 0 && r.mode == 0 && r.accent == 0);
    strokes_engine_free(e);
}

static void accents(void)
{
    strokes_table *t = test_table_full();
    strokes_engine *e = strokes_engine_new(t, NULL);
    /* a dead accent, then the letter */
    strokes_result r = test_write_glyph(e, t, "acc_acute", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(r.kind == STROKES_MODE && r.accent == STROKES_ACCENT_ACUTE && r.mode == STROKES_MODE_ACCENT);
    r = test_write_glyph(e, t, "e", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "\xc3\xa9") == 0 && r.accent == 0 && r.mode == 0);
    /* with shift, before or after the accent */
    draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    test_write_glyph(e, t, "acc_grave", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(strokes_get_mode(e) == (STROKES_MODE_SHIFT | STROKES_MODE_ACCENT));
    r = test_write_glyph(e, t, "a", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(strcmp(r.utf8, "\xc3\x80") == 0 && r.mode == 0);
    test_write_glyph(e, t, "acc_uml", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    r = test_write_glyph(e, t, "y", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(strcmp(r.utf8, "\xc5\xb8") == 0); /* Y with diaeresis, U+0178 */
    test_write_glyph(e, t, "acc_tilde", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(strcmp(test_write_glyph(e, t, "n", STROKES_SET_LETTERS, STROKES_BOX_LETTERS).utf8, "\xc3\xb1") == 0);
    test_write_glyph(e, t, "acc_circ", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(strcmp(test_write_glyph(e, t, "o", STROKES_SET_LETTERS, STROKES_BOX_LETTERS).utf8, "\xc3\xb4") == 0);
    test_write_glyph(e, t, "acc_ring", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(strcmp(test_write_glyph(e, t, "a", STROKES_SET_LETTERS, STROKES_BOX_LETTERS).utf8, "\xc3\xa5") == 0);
    /* a letter without that accent is typed plain; the accent is dropped */
    test_write_glyph(e, t, "acc_grave", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    r = test_write_glyph(e, t, "b", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(strcmp(r.utf8, "b") == 0 && r.accent == 0 && r.mode == 0);
    /* backspace drops a pending accent and deletes nothing; a rejection keeps it; a key drops it */
    test_write_glyph(e, t, "acc_acute", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    float zig[] = {200, 500, 300, 520, 210, 540, 290, 560, 220, 580, 280, 600};
    r = draw(e, STROKES_BOX_LETTERS, zig, 6);
    CHECK(r.kind == STROKES_REJECT && r.accent == STROKES_ACCENT_ACUTE);
    r = draw(e, STROKES_BOX_LETTERS, BKSP, 2);
    CHECK(r.kind == STROKES_MODE && r.key == STROKES_KEY_NONE && r.accent == 0 && r.mode == 0);
    test_write_glyph(e, t, "acc_acute", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    r = draw(e, STROKES_BOX_LETTERS, SPACE, 2);
    CHECK(r.kind == STROKES_KEY && r.key == STROKES_KEY_SPACE && r.accent == 0 && r.mode == 0);
    /* c cedilla and ae are letters of their own, upper-cased by shift */
    r = test_write_glyph(e, t, "ccedil", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "\xc3\xa7") == 0);
    draw(e, STROKES_BOX_LETTERS, SHIFT, 2);
    r = test_write_glyph(e, t, "aelig", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "\xc3\x86") == 0);
    /* the extended prefix keeps a pending accent; the ext character then drops it */
    test_write_glyph(e, t, "acc_ring", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    draw(e, STROKES_BOX_LETTERS, EXT, 2);
    CHECK(strokes_get_mode(e) == (STROKES_MODE_EXT | STROKES_MODE_ACCENT));
    r = test_write_glyph(e, t, "oslash", STROKES_SET_EXT, STROKES_BOX_LETTERS);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.utf8, "\xc3\xb8") == 0 && r.mode == STROKES_MODE_EXT && r.accent == 0);
    draw(e, STROKES_BOX_LETTERS, BKSP, 2); /* leaves the extended mode */
    /* set_mode keeps a pending accent only when asked */
    test_write_glyph(e, t, "acc_grave", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    strokes_set_mode(e, STROKES_MODE_ACCENT | STROKES_MODE_CAPS);
    CHECK(strokes_get_mode(e) == (STROKES_MODE_ACCENT | STROKES_MODE_CAPS));
    CHECK(strcmp(test_write_glyph(e, t, "e", STROKES_SET_LETTERS, STROKES_BOX_LETTERS).utf8, "\xc3\x88") == 0);
    test_write_glyph(e, t, "acc_grave", STROKES_SET_LETTERS, STROKES_BOX_LETTERS);
    strokes_set_mode(e, 0);
    CHECK(strcmp(test_write_glyph(e, t, "e", STROKES_SET_LETTERS, STROKES_BOX_LETTERS).utf8, "e") == 0);
    strokes_engine_free(e);
}

static void queue_and_odd_events(void)
{
    strokes_engine *e = eng();
    strokes_result r;
    /* ten strokes, never polled: the eight newest are kept */
    for (int i = 0; i < 10; ++i) {
        uint64_t t = 1000000 + (uint64_t)i * 1000000;
        strokes_down(e, STROKES_BOX_LETTERS, 250, 500, t);
        for (int k = 1; k <= 10; ++k)
            strokes_move(e, 250, 500 + 10.0f * k, t + (uint64_t)k * 10000);
        strokes_up(e, t + 110000);
    }
    int n = 0;
    uint64_t first_up = 0;
    while (strokes_flush(e, &r)) {
        if (n == 0)
            first_up = r.t_up_us;
        n++;
    }
    CHECK(n == 8 && first_up == 3000000 + 110000);
    /* moves and lifts without a press are ignored; a second press is a move */
    strokes_move(e, 1, 1, 100);
    strokes_up(e, 200);
    CHECK(!strokes_busy(e) && strokes_flush(e, &r) == 0);
    strokes_down(e, STROKES_BOX_LETTERS, 250, 500, 20000000);
    strokes_down(e, STROKES_BOX_LETTERS, 250, 550, 20050000);
    strokes_move(e, 250, 600, 20100000);
    strokes_up(e, 20110000);
    CHECK(strokes_flush(e, &r) == 1 && strcmp(r.glyph, "i") == 0);
    /* non-finite coordinates are dropped, huge ones clamped, the clock going back is tolerated */
    strokes_down(e, STROKES_BOX_LETTERS, NAN, 3, 30000000);
    CHECK(!strokes_busy(e));
    strokes_down(e, 99, 250, 500, 30000000); /* unknown box: letters */
    strokes_move(e, INFINITY, 520, 30010000);
    strokes_move(e, 250, 550, 29000000);
    strokes_move(e, 250, 1e30f, 30020000);
    strokes_up(e, 30030000);
    CHECK(strokes_flush(e, &r) == 1 && r.box == STROKES_BOX_LETTERS);
    /* NULL engine calls are harmless */
    strokes_down(NULL, 0, 1, 1, 1);
    strokes_move(NULL, 1, 1, 1);
    strokes_up(NULL, 1);
    strokes_cancel(NULL);
    CHECK(strokes_poll(NULL, 1, &r) == 0 && strokes_flush(NULL, &r) == 0 && !strokes_busy(NULL));
    CHECK(strokes_engine_new(NULL, NULL) == NULL);
    strokes_engine_free(NULL);
    strokes_engine_free(e);
}

static void long_stroke(void)
{
    strokes_engine *e = eng();
    /* 5000 frames of a slow down-stroke: the buffer halves, the stroke is still an i */
    strokes_down(e, STROKES_BOX_LETTERS, 250, 500, 1000000);
    for (int i = 1; i < 5000; ++i)
        strokes_move(e, 250 + (float)(i % 3), 500 + 0.02f * i, 1000000 + (uint64_t)i * 1000);
    strokes_up(e, 7000000);
    strokes_result r;
    CHECK(strokes_flush(e, &r) == 1 && strcmp(r.glyph, "i") == 0 && r.n_raw <= STK_MAX_RAW);
    strokes_engine_free(e);
}

static void density_independent(void)
{
    /* the same 12 mm "a" at 4, 8.54 and 16 px/mm */
    const float ppmm[3] = {4.0f, 8.54f, 16.0f};
    for (int k = 0; k < 3; ++k) {
        strokes_config cfg;
        strokes_config_default(&cfg);
        cfg.ppmm = ppmm[k];
        strokes_engine *e = strokes_engine_new(test_table(), &cfg);
        float s = 12.0f * ppmm[k];
        float xy[] = {100, 100 + s, 100 + s / 2, 100, 100 + s, 100 + s};
        strokes_result r[2];
        int n = test_feed(e, STROKES_BOX_LETTERS, xy, 3, 1000000, 0.1f * ppmm[k], 10.0f, r, 2);
        CHECKF(n == 1 && strcmp(r[0].glyph, "a") == 0, "ppmm %.2f -> %s", (double)ppmm[k], r[0].glyph);
        strokes_engine_free(e);
    }
}

static void deterministic(void)
{
    strokes_result a[64], b[64];
    for (int pass = 0; pass < 2; ++pass) {
        strokes_engine *e = eng();
        srand(1234);
        strokes_result *out = pass ? b : a;
        for (int s = 0; s < 64; ++s) {
            uint64_t t = 1000000 + (uint64_t)s * 1000000;
            float x = 200, y = 500;
            strokes_down(e, s % 2, x, y, t);
            for (int k = 0; k < 20; ++k) {
                x += (float)(rand() % 21 - 10);
                y += (float)(rand() % 21 - 5);
                strokes_move(e, x, y, t + (uint64_t)(k + 1) * 10000);
            }
            strokes_up(e, t + 220000);
            memset(&out[s], 0, sizeof out[s]);
            strokes_flush(e, &out[s]);
        }
        strokes_engine_free(e);
    }
    int same = 1;
    for (int s = 0; s < 64; ++s)
        same &= a[s].kind == b[s].kind && strcmp(a[s].glyph, b[s].glyph) == 0 && a[s].confidence == b[s].confidence &&
                a[s].margin == b[s].margin && strcmp(a[s].dirs, b[s].dirs) == 0 && a[s].mode == b[s].mode &&
                a[s].n_cand == b[s].n_cand;
    CHECK(same);
}

void suite_engine(void)
{
    RUN(tap_and_period);
    RUN(dwell_and_short);
    RUN(healing);
    RUN(timing);
    RUN(modes);
    RUN(extended_and_x);
    RUN(sticky_modes);
    RUN(accents);
    RUN(queue_and_odd_events);
    RUN(long_stroke);
    RUN(density_independent);
    RUN(deterministic);
}
