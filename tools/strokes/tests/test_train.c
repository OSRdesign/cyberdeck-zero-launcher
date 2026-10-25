/*
 * SPDX-License-Identifier: MIT
 *
 * The training session (strokes_train.c, report 042): the order, the overlay blocks and how they merge with the
 * shipped table (added variants, personal only, hand-written lines kept, resume), the state machine (attempts,
 * verdicts, skip / back / redo / reset / finish, re-test, numbers, the log line), and a simulation: a writer who writes
 * ten letters unlike the chart (slanted and narrow) is mostly refused; after a training session with four of their own
 * strokes per letter, fresh strokes of that writer are recognised, and the chart's strokes still are.
 */
#include "strokes_internal.h"
#include "strokes_train.h"
#include "test.h"

#include <stdlib.h>

#define FAILING "b p s e m o q r y z"

static strokes_table *table_with(const char *overlay)
{
    strokes_table *t = strokes_table_new();
    strokes_error err;
    if (strokes_table_add_file(t, test_table_path(), &err) || strokes_table_add_file(t, test_table_extra_path(), &err) ||
        (overlay && strokes_table_add_text(t, overlay, "overlay", &err))) {
        printf("table: %s:%d: %s\n", err.source, err.line, err.msg);
        strokes_table_free(t);
        return NULL;
    }
    return t;
}

/* one synthetic stroke of glyph (id, set) of table t: its points, drop-outs joined (as the zone heals them) */
static int synth_points(const strokes_table *t, const char *id, int set, const synth_cfg *c, synth_rng *r,
                        strokes_point *pts, int max)
{
    const int gi = stk_find_glyph(t, id, set);
    if (gi < 0)
        return 0;
    static stk_sample s;
    if (synth_glyph(t, gi, c, r, &s) != 0)
        return 0;
    const int n = s.n < max ? s.n : max;
    memcpy(pts, s.p, (size_t)n * sizeof *pts);
    return n;
}

/* the chart template of (id, set) drawn 100 px high: a clean stroke */
static int clean_points(const strokes_table *t, const char *id, int set, strokes_point *pts, int max)
{
    float xy[2 * STK_MAX_VERTS];
    const int nv = strokes_table_template_points(t, id, set, 0, xy, STK_MAX_VERTS);
    int n = 0;
    uint64_t tm = 1000000;
    for (int i = 0; i + 1 < nv && n < max; ++i)
        for (int k = 0; k < 8 && n < max; ++k) {
            const float f = (float)k / 8.0f;
            pts[n].x = 100.0f + xy[2 * i] + f * (xy[2 * i + 2] - xy[2 * i]);
            pts[n].y = 100.0f + xy[2 * i + 1] + f * (xy[2 * i + 3] - xy[2 * i + 1]);
            pts[n].t_us = tm;
            tm += 8000;
            n++;
        }
    if (nv > 0 && n < max) {
        pts[n].x = 100.0f + xy[2 * nv - 2];
        pts[n].y = 100.0f + xy[2 * nv - 1];
        pts[n].t_us = tm;
        n++;
    }
    return n;
}

static int count_lines(const char *text, const char *prefix)
{
    int n = 0;
    for (const char *p = text; p && *p;) {
        if (strncmp(p, prefix, strlen(prefix)) == 0)
            n++;
        const char *eol = strchr(p, '\n');
        p = eol ? eol + 1 : p + strlen(p);
    }
    return n;
}

static void test_order(void)
{
    strokes_table *t = table_with(NULL);
    strokes_train *s = strokes_train_new(t, NULL, FAILING, NULL);
    CHECK(s != NULL);
    const char *first[] = {"b", "p", "s", "e", "m", "o", "q", "r", "y", "z"};
    for (int i = 0; i < 10; ++i) {
        const strokes_train_item *it = strokes_train_item_at(s, i);
        CHECKF(it && strcmp(it->id, first[i]) == 0 && it->group == STROKES_TRAIN_FAILING, "item %d", i);
    }
    /* then the other letters (x is the extended set's: not trained), the digits, gestures and punctuation */
    const strokes_train_item *a = strokes_train_item_at(s, 10);
    CHECK(a && strcmp(a->id, "a") == 0 && a->group == STROKES_TRAIN_LETTERS);
    int letters = 0, digits = 0, other = 0, taps = 0;
    for (int i = 0; i < strokes_train_count(s); ++i) {
        const strokes_train_item *it = strokes_train_item_at(s, i);
        letters += it->group <= STROKES_TRAIN_LETTERS;
        digits += it->group == STROKES_TRAIN_DIGITS;
        other += it->group == STROKES_TRAIN_OTHER;
        taps += strcmp(it->id, "tap") == 0 || strcmp(it->id, "period") == 0 || strcmp(it->id, "x") == 0;
        if (i > 0) /* groups in order */
            CHECK(it->group >= strokes_train_item_at(s, i - 1)->group);
    }
    CHECK_NEAR(letters, 27, 0); /* a-z but x, and the optional section's c cedilla and ae */
    CHECK_NEAR(digits, 10, 0);
    CHECK(other >= 30);
    CHECK_NEAR(taps, 0, 0);
    const strokes_train_item *sp = NULL;
    for (int i = 0; i < strokes_train_count(s); ++i)
        if (strcmp(strokes_train_item_at(s, i)->id, "space") == 0)
            sp = strokes_train_item_at(s, i);
    CHECK(sp && strcmp(sp->label, "space") == 0 && sp->set == STROKES_SET_ANY);
    CHECK_NEAR(strokes_train_current(s), 0, 0);
    CHECK_NEAR(strokes_train_phase(s), STROKES_TRAIN_TRAINING, 0);
    /* the guide: the chart variant's points */
    float xy[2 * STK_MAX_VERTS];
    CHECK(strokes_table_template_points(t, "b", STROKES_SET_LETTERS, 0, xy, STK_MAX_VERTS) > 10);
    CHECK(strokes_table_template_points(t, "b", STROKES_SET_LETTERS, 99, xy, STK_MAX_VERTS) == -1);
    CHECK(strokes_table_template_points(t, "period", STROKES_SET_PUNCT, 0, xy, STK_MAX_VERTS) == 0);
    CHECK(strokes_table_template_points(t, "nosuch", STROKES_SET_PUNCT, 0, xy, STK_MAX_VERTS) == -1);
    strokes_train_free(s);
    strokes_table_free(t);
}

/* An overlay with a hand-written line and two training blocks: the blocks are recognised (done, resume), the
 * hand-written line kept, the whole text still a valid overlay that adds the variants. */
static void test_overlay_merge(void)
{
    strokes_table *base = table_with(NULL);
    strokes_point pts[256];
    char line_b[1024], line_p[1024];
    int n = clean_points(base, "d", STROKES_SET_LETTERS, pts, 256); /* "my b" looks like the chart's d */
    CHECK(strokes_template_line(base, NULL, "b", STROKES_SET_LETTERS, pts, n, line_b, sizeof line_b) > 0);
    n = clean_points(base, "q", STROKES_SET_LETTERS, pts, 256);
    CHECK(strokes_template_line(base, NULL, "p", STROKES_SET_LETTERS, pts, n, line_p, sizeof line_p) > 0);
    char overlay[4096];
    snprintf(overlay, sizeof overlay,
             "# my own v\nv letters v start=tl end=tr corners=0-3 0,0 30,85 50,100 70,85 100,0\n\n"
             STROKES_TRAIN_MARK "b letters: 4 written, 0 recognised, 1 added\n%s\n"
             STROKES_TRAIN_MARK "p letters: 4 written, 0 recognised, 1 added (personal only)\nreplace p letters\n%s\n",
             line_b, line_p);
    strokes_table *t = table_with(overlay);
    CHECK(t != NULL);
    char id[STROKES_ID_MAX];
    int set, variants_base = 0, variants = 0, p_variants = -1;
    for (int i = 0; strokes_table_glyph_info(base, i, id, &set, &variants) == 0; ++i)
        if (strcmp(id, "b") == 0)
            variants_base = variants;
    for (int i = 0; t && strokes_table_glyph_info(t, i, id, &set, &variants) == 0; ++i) {
        if (strcmp(id, "b") == 0)
            CHECK_NEAR(variants, variants_base + 1, 0); /* added: the shipped b variants stay */
        if (strcmp(id, "p") == 0)
            p_variants = variants; /* personal only: the shipped p gone */
    }
    CHECK_NEAR(p_variants, 1, 0);
    /* precedence: the personal b wins the chart's d shape it was trained with; d itself is still recognised by its own
     * shipped templates as long as the personal b is not closer (it is the same shape: the margin rule refuses it) */
    strokes_result r;
    n = clean_points(base, "d", STROKES_SET_LETTERS, pts, 256);
    (void)strokes_classify(t, NULL, STROKES_SET_LETTERS, pts, n, &r);
    CHECK(strcmp(r.glyph, "b") == 0 || r.kind == STROKES_REJECT);

    strokes_train *s = strokes_train_new(t, NULL, FAILING, overlay);
    CHECK(strokes_train_is_done(s, 0) && strokes_train_is_done(s, 1) && !strokes_train_is_done(s, 2));
    CHECK_NEAR(strokes_train_current(s), 2, 0); /* resumes at s */
    const char *text = strokes_train_overlay(s);
    CHECK(strstr(text, "# my own v\nv letters v") == text); /* hand-written lines first, as they were */
    CHECK_NEAR(count_lines(text, STROKES_TRAIN_MARK), 2, 0);
    strokes_table *again = table_with(text);
    CHECK(again != NULL);
    strokes_table_free(again);
    /* reset all: the blocks go, the hand-written lines stay */
    unsigned ev = strokes_train_reset_all(s);
    CHECK(ev & STROKES_TRAIN_EV_OVERLAY);
    CHECK_NEAR(count_lines(strokes_train_overlay(s), STROKES_TRAIN_MARK), 0, 0);
    CHECK(strstr(strokes_train_overlay(s), "v letters v start=tl") != NULL);
    CHECK_NEAR(strokes_train_current(s), 0, 0);
    strokes_train_free(s);
    strokes_table_free(t);
    strokes_table_free(base);
}

static void test_session(void)
{
    strokes_table *t = table_with(NULL);
    strokes_train *s = strokes_train_new(t, NULL, "b d", NULL);
    strokes_train_set_time(s, "2026-10-25 21:04");
    strokes_point pts[512];
    /* b written as the chart's b four times: recognised, close to the shipped variant: nothing added */
    for (int k = 0; k < STROKES_TRAIN_REPS; ++k) {
        const int n = clean_points(t, "b", STROKES_SET_LETTERS, pts, 512);
        const unsigned ev = strokes_train_stroke(s, pts, n);
        CHECK(ev & STROKES_TRAIN_EV_ATTEMPT);
        CHECKF(strokes_train_last(s)->verdict == STROKES_TRAIN_OK, "b attempt %d: %s %s", k,
               strokes_train_verdict_name(strokes_train_last(s)->verdict), strokes_train_last(s)->got);
        if (k < STROKES_TRAIN_REPS - 1) {
            CHECK_NEAR(strokes_train_progress(s), k + 1, 0);
            CHECK(!(ev & STROKES_TRAIN_EV_OVERLAY));
        } else {
            CHECK((ev & STROKES_TRAIN_EV_OVERLAY) && (ev & STROKES_TRAIN_EV_MOVED));
        }
    }
    CHECK(strokes_train_is_done(s, 0));
    CHECK(strstr(strokes_train_overlay(s), STROKES_TRAIN_MARK "b letters: 4 written, 4 recognised, 0 added 2026-10-25 21:04\n\n"));
    CHECK_NEAR(strokes_train_current(s), 1, 0); /* d */
    /* the log line of the last attempt */
    char log[512];
    CHECK(strokes_train_log_comment(s, "2026-10-25T21:04:11", log, sizeof log) > 0);
    CHECKF(strncmp(log, "# training phase=train target=b set=letters attempt=4/4 verdict=ok got=b score=", 79) == 0,
           "%s", log);
    CHECK(strstr(log, " size_mm=") && strstr(log, " at=2026-10-25T21:04:11"));
    CHECK(strokes_train_log_comment(s, "x", log, 20) == -1);

    /* d written as the chart's q three times (wrong glyph), then one d: the misses are taken, near duplicates not */
    for (int k = 0; k < 3; ++k) {
        const int n = clean_points(t, "q", STROKES_SET_LETTERS, pts, 512);
        strokes_train_stroke(s, pts, n);
        CHECKF(strokes_train_last(s)->verdict == STROKES_TRAIN_WRONG && strcmp(strokes_train_last(s)->got, "q") == 0,
               "%s %s", strokes_train_verdict_name(strokes_train_last(s)->verdict), strokes_train_last(s)->got);
    }
    CHECK(strokes_train_attempt_at(s, 2) && strokes_train_attempt_at(s, 2)->verdict == STROKES_TRAIN_WRONG);
    int n = clean_points(t, "d", STROKES_SET_LETTERS, pts, 512);
    unsigned ev = strokes_train_stroke(s, pts, n);
    CHECK(ev & STROKES_TRAIN_EV_OVERLAY);
    const char *text = strokes_train_overlay(s);
    CHECK(strstr(text, STROKES_TRAIN_MARK "d letters: 4 written, 1 recognised, 1 added"));
    CHECK_NEAR(count_lines(text, "d letters d "), 1, 0); /* three identical q shapes: one template */
    strokes_table *t2 = table_with(text);
    CHECK(t2 != NULL);
    strokes_train_set_table(s, t2);

    /* back to d (already done): redo drops its block */
    CHECK(strokes_train_current(s) == 2); /* a: the first letter after the named ones */
    ev = strokes_train_back(s);
    CHECK((ev & STROKES_TRAIN_EV_MOVED) && strokes_train_current(s) == 1);
    ev = strokes_train_redo(s);
    CHECK(ev & STROKES_TRAIN_EV_OVERLAY);
    CHECK(!strokes_train_is_done(s, 1));
    CHECK(strstr(strokes_train_overlay(s), "d letters d ") == NULL);
    /* skip: d stays open, the next open glyph is p */
    ev = strokes_train_skip(s);
    CHECK(strokes_train_current(s) == 2 && !(ev & STROKES_TRAIN_EV_OVERLAY));
    /* a refused scribble counts as written */
    strokes_point zz[5] = {{0, 0, 0}, {80, 5, 20000}, {3, 60, 40000}, {90, 70, 60000}, {10, 4, 80000}};
    strokes_train_stroke(s, zz, 5);
    CHECK(strokes_train_last(s)->verdict != STROKES_TRAIN_OK);

    /* finish: the re-test checks b only (d was redone and skipped, p is unfinished) */
    ev = strokes_train_finish(s);
    CHECK((ev & STROKES_TRAIN_EV_PHASE) && strokes_train_phase(s) == STROKES_TRAIN_RETEST);
    CHECK_NEAR(strokes_train_retest_count(s), 1, 0);
    CHECK_NEAR(strokes_train_current(s), 0, 0);
    n = clean_points(t2, "b", STROKES_SET_LETTERS, pts, 512);
    ev = strokes_train_stroke(s, pts, n);
    CHECK((ev & STROKES_TRAIN_EV_PHASE) && strokes_train_phase(s) == STROKES_TRAIN_DONE);
    CHECK(strokes_train_log_comment(s, NULL, log, sizeof log) > 0 && strstr(log, "phase=retest target=b") &&
          strstr(log, "attempt=1/1"));
    strokes_train_stats st;
    strokes_train_item_stats(s, 0, &st);
    CHECK(st.trained == 1 && st.tried == 4 && st.ok_before == 4 && st.retested == 1 && st.ok_after == 1);
    strokes_train_totals(s, &st);
    CHECK(st.trained == 1 && st.tried == 4 && st.retested == 1);
    CHECK_NEAR(strokes_train_stroke(s, pts, n), 0, 0); /* done: nothing more */
    CHECK_NEAR(strokes_train_current(s), -1, 0);
    strokes_train_free(s);
    strokes_table_free(t2);

    /* personal only: the block replaces the shipped variants; an all-recognised glyph still keeps one template */
    s = strokes_train_new(t, NULL, "b", NULL);
    strokes_train_set_personal_only(s, 1);
    for (int k = 0; k < STROKES_TRAIN_REPS; ++k) {
        n = clean_points(t, "b", STROKES_SET_LETTERS, pts, 512);
        strokes_train_stroke(s, pts, n);
    }
    text = strokes_train_overlay(s);
    CHECK(strstr(text, "(personal only)\nreplace b letters\nb letters b "));
    CHECK_NEAR(count_lines(text, "b letters b "), 1, 0);
    t2 = table_with(text);
    CHECK(t2 != NULL);
    strokes_train_free(s);
    strokes_table_free(t2);

    /* a miss written once (a slip, a scribble) is not kept; written twice the same way it is */
    s = strokes_train_new(t, NULL, "e", NULL);
    strokes_point zz2[5] = {{0, 0, 0}, {80, 5, 20000}, {3, 60, 40000}, {90, 70, 60000}, {10, 4, 80000}};
    for (int k = 0; k < STROKES_TRAIN_REPS - 1; ++k) {
        n = clean_points(t, "e", STROKES_SET_LETTERS, pts, 512);
        strokes_train_stroke(s, pts, n);
    }
    strokes_train_stroke(s, zz2, 5);
    CHECK(strokes_train_last_item(s) == 0 && strokes_train_last(s)->verdict == STROKES_TRAIN_REFUSED);
    CHECK(strstr(strokes_train_overlay(s), STROKES_TRAIN_MARK "e letters: 4 written, 3 recognised, 0 added"));
    strokes_train_free(s);
    s = strokes_train_new(t, NULL, "e", NULL);
    for (int k = 0; k < STROKES_TRAIN_REPS; ++k)
        strokes_train_stroke(s, zz2, 5);
    CHECK(strstr(strokes_train_overlay(s), STROKES_TRAIN_MARK "e letters: 4 written, 0 recognised, 1 added"));
    strokes_train_free(s);

    /* a tap is no shape: it is judged, never a template */
    s = strokes_train_new(t, NULL, "e", NULL);
    strokes_point tap[2] = {{50, 50, 0}, {50.5f, 50, 60000}};
    for (int k = 0; k < STROKES_TRAIN_REPS; ++k)
        strokes_train_stroke(s, tap, 2);
    CHECK(strstr(strokes_train_overlay(s), STROKES_TRAIN_MARK "e letters: 4 written, 0 recognised, 0 added"));
    strokes_train_free(s);
    strokes_table_free(t);
}

/* The glyph picker: pick a glyph, four attempts, back to the picker; the marks; the re-test written into the block. */
static void test_picker(void)
{
    strokes_table *t = table_with(NULL);
    strokes_train *s = strokes_train_new(t, NULL, "b d", NULL);
    strokes_train_set_time(s, "2026-10-26 09:00");
    strokes_point pts[512];
    CHECK_NEAR(strokes_train_mark(s, 0), STROKES_TRAIN_UNTRAINED, 0);
    unsigned ev = strokes_train_open_picker(s);
    CHECK((ev & STROKES_TRAIN_EV_PHASE) && strokes_train_phase(s) == STROKES_TRAIN_PICK);
    CHECK_NEAR(strokes_train_current(s), -1, 0);
    CHECK_NEAR(strokes_train_stroke(s, pts, 0), 0, 0);
    /* d picked, written as the chart's d four times: trained, back to the picker */
    ev = strokes_train_pick(s, 1);
    CHECK(strokes_train_phase(s) == STROKES_TRAIN_TRAINING && strokes_train_current(s) == 1 && strokes_train_picked(s));
    for (int k = 0; k < STROKES_TRAIN_REPS; ++k) {
        const int n = clean_points(t, "d", STROKES_SET_LETTERS, pts, 512);
        ev = strokes_train_stroke(s, pts, n);
    }
    CHECK((ev & STROKES_TRAIN_EV_OVERLAY) && (ev & STROKES_TRAIN_EV_PHASE) && strokes_train_phase(s) == STROKES_TRAIN_PICK);
    CHECK_NEAR(strokes_train_mark(s, 1), STROKES_TRAIN_TRAINED, 0);
    /* b picked, written as the chart's q (read as q): hard; Back in the middle of a picked glyph: the picker again */
    strokes_train_pick(s, 0);
    int n = clean_points(t, "q", STROKES_SET_LETTERS, pts, 512);
    strokes_train_stroke(s, pts, n);
    ev = strokes_train_back(s);
    CHECK((ev & STROKES_TRAIN_EV_PHASE) && strokes_train_phase(s) == STROKES_TRAIN_PICK);
    CHECK_NEAR(strokes_train_mark(s, 0), STROKES_TRAIN_UNTRAINED, 0);
    strokes_train_pick(s, 0);
    for (int k = 0; k < STROKES_TRAIN_REPS; ++k) {
        n = clean_points(t, "q", STROKES_SET_LETTERS, pts, 512);
        strokes_train_stroke(s, pts, n);
    }
    CHECK_NEAR(strokes_train_mark(s, 0), STROKES_TRAIN_HARD, 0);
    /* the order again: the first glyph without a block (a, after b and d) */
    ev = strokes_train_resume(s);
    CHECK(strokes_train_phase(s) == STROKES_TRAIN_TRAINING && strokes_train_current(s) == 2 && !strokes_train_picked(s));
    strokes_train_open_picker(s);
    /* Finish from the picker: the re-test of b and d; each result goes into its block */
    strokes_table *t2 = table_with(strokes_train_overlay(s));
    strokes_train_set_table(s, t2);
    ev = strokes_train_finish(s);
    CHECK(strokes_train_phase(s) == STROKES_TRAIN_RETEST && strokes_train_retest_count(s) == 2);
    n = clean_points(t, "b", STROKES_SET_LETTERS, pts, 512);
    ev = strokes_train_stroke(s, pts, n);
    CHECK(ev & STROKES_TRAIN_EV_OVERLAY);
    CHECK(strstr(strokes_train_overlay(s), STROKES_TRAIN_RETEST_MARK "1/1 2026-10-26 09:00\n\n") != NULL);
    CHECK_NEAR(strokes_train_mark(s, 0), STROKES_TRAIN_TRAINED, 0); /* the latest rate: the re-test's */
    n = clean_points(t, "q", STROKES_SET_LETTERS, pts, 512);                     /* d written as q: not recognised */
    strokes_train_stroke(s, pts, n);
    CHECK(strokes_train_phase(s) == STROKES_TRAIN_DONE);
    CHECK_NEAR(strokes_train_mark(s, 1), STROKES_TRAIN_HARD, 0);
    /* a new session reads the marks back from the overlay; the blocks are still valid overlay text */
    strokes_train *again = strokes_train_new(t, NULL, "b d", strokes_train_overlay(s));
    CHECK(strokes_train_mark(again, 0) == STROKES_TRAIN_TRAINED && strokes_train_mark(again, 1) == STROKES_TRAIN_HARD);
    CHECK_NEAR(count_lines(strokes_train_overlay(again), STROKES_TRAIN_RETEST_MARK), 2, 0);
    strokes_table *t3 = table_with(strokes_train_overlay(again));
    CHECK(t3 != NULL);
    strokes_table_free(t3);
    strokes_train_free(again);
    strokes_train_free(s);
    strokes_table_free(t2);
    strokes_table_free(t);
}

/* ---- the simulation: a writer unlike the chart ---- */

/* The writer's own table: the chart stroke of each letter of `which`, turned 32 degrees clockwise and 0.62 as wide. */
static strokes_table *writer_table(const strokes_table *base, const char *which)
{
    char *text = calloc(1, 256 * 1024);
    size_t len = 0;
    char buf[64];
    snprintf(buf, sizeof buf, "%s", which);
    const float a = 32.0f * STK_PI / 180.0f, ca = cosf(a), sa = sinf(a);
    for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
        float xy[2 * STK_MAX_VERTS];
        const int nv = strokes_table_template_points(base, tok, STROKES_SET_LETTERS, 0, xy, STK_MAX_VERTS);
        len += (size_t)snprintf(text + len, 256 * 1024 - len, "%s letters %s", tok, tok);
        for (int i = 0; i < nv; ++i) {
            const float x = (xy[2 * i] - 50.0f) * 0.62f, y = xy[2 * i + 1] - 50.0f;
            len += (size_t)snprintf(text + len, 256 * 1024 - len, " %.1f,%.1f", 50.0f + x * ca - y * sa,
                                    50.0f + x * sa + y * ca);
        }
        len += (size_t)snprintf(text + len, 256 * 1024 - len, "\n");
    }
    strokes_table *w = strokes_table_new();
    strokes_error err;
    if (strokes_table_add_text(w, text, "writer", &err)) {
        printf("writer table: %s:%d: %s\n", err.source, err.line, err.msg);
        strokes_table_free(w);
        w = NULL;
    }
    free(text);
    return w;
}

/* percent of n strokes of each letter of `which`, written from table `from`, that table `t` reads as that letter */
static double accuracy(const strokes_table *t, const strokes_table *from, const char *which, int n, uint64_t seed,
                       int *wrong)
{
    synth_cfg c;
    synth_default(&c);
    synth_rng r;
    synth_seed(&r, seed);
    char buf[128];
    snprintf(buf, sizeof buf, "%s", which);
    int ok = 0, total = 0, bad = 0;
    static strokes_point pts[SAMPLE_MAX_PTS];
    for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " "))
        for (int k = 0; k < n; ++k) {
            const int np = synth_points(from, tok, STROKES_SET_LETTERS, &c, &r, pts, SAMPLE_MAX_PTS);
            if (np <= 0)
                continue;
            strokes_result res;
            const int acc = strokes_classify(t, NULL, STROKES_SET_LETTERS, pts, np, &res);
            total++;
            if (acc && strcmp(res.glyph, tok) == 0)
                ok++;
            else if (acc)
                bad++;
        }
    if (wrong)
        *wrong = bad;
    return total ? 100.0 * ok / total : 0.0;
}

/* a training session of the writer: four strokes of each letter of `which`; returns the overlay (malloc'd) */
static char *train_writer(const strokes_table *base, const strokes_table *writer, const char *which, int personal)
{
    strokes_table *t = table_with(NULL);
    strokes_train *s = strokes_train_new(t, NULL, which, NULL);
    strokes_train_set_personal_only(s, personal);
    synth_cfg c;
    synth_default(&c);
    synth_rng r;
    synth_seed(&r, 4242);
    static strokes_point pts[SAMPLE_MAX_PTS];
    int letters = 0;
    for (const char *p = which; *p; ++p)
        letters += *p != ' ';
    for (int g = 0; g < letters && strokes_train_phase(s) == STROKES_TRAIN_TRAINING; ++g) {
        const strokes_train_item *it = strokes_train_item_at(s, strokes_train_current(s));
        for (int k = 0; k < STROKES_TRAIN_REPS; ++k) {
            const int np = synth_points(writer, it->id, STROKES_SET_LETTERS, &c, &r, pts, SAMPLE_MAX_PTS);
            const unsigned ev = strokes_train_stroke(s, pts, np);
            if (ev & STROKES_TRAIN_EV_OVERLAY) { /* as the page does: the tables again */
                strokes_table_free(t);
                t = table_with(strokes_train_overlay(s));
                strokes_train_set_table(s, t);
            }
        }
    }
    (void)base;
    char *out = malloc(strlen(strokes_train_overlay(s)) + 1);
    strcpy(out, strokes_train_overlay(s));
    strokes_train_free(s);
    strokes_table_free(t);
    return out;
}

static void test_simulation(void)
{
    const char *which = FAILING;
    strokes_table *base = table_with(NULL);
    strokes_table *writer = writer_table(base, which);
    CHECK(writer != NULL);
    if (!writer) {
        strokes_table_free(base);
        return;
    }
    const char *chart_letters = "a b c d e f g h i j k l m n o p q r s t u v w y z";
    int wrong_before, wrong_after, wrong_po;
    const double before = accuracy(base, writer, which, 60, 11, &wrong_before);
    const double chart_before = accuracy(base, base, chart_letters, 40, 12, NULL);
    double after = 0, chart_after = 0, after_po = 0, chart_po = 0;
    for (int personal = 0; personal < 2; ++personal) {
        char *overlay = train_writer(base, writer, which, personal);
        strokes_table *t = table_with(overlay);
        CHECK(t != NULL);
        if (t) {
            const double a = accuracy(t, writer, which, 60, 13, personal ? &wrong_po : &wrong_after);
            const double ch = accuracy(t, base, chart_letters, 40, 12, NULL);
            if (personal) {
                after_po = a;
                chart_po = ch;
            } else {
                after = a;
                chart_after = ch;
                printf("simulation: overlay %d templates for 10 letters (%zu bytes)\n",
                       strokes_table_template_count(t) - strokes_table_template_count(base), strlen(overlay));
            }
        }
        strokes_table_free(t);
        free(overlay);
    }
    printf("simulation: the slanted narrow writer: %.1f %% recognised before (%d wrong of 600), %.1f %% after "
           "(%d wrong), personal only %.1f %% (%d wrong)\n",
           before, wrong_before, after, wrong_after, after_po, wrong_po);
    printf("simulation: the chart's letters: %.1f %% before, %.1f %% after, personal only %.1f %%\n", chart_before,
           chart_after, chart_po);
    CHECKF(before < 50.0, "the writer should be mostly refused before training: %.1f", before);
    CHECKF(after >= before + 30.0 && after >= 80.0, "training should raise the writer's accuracy: %.1f -> %.1f",
           before, after);
    CHECKF(chart_after >= chart_before - 3.0, "the chart's letters should stay: %.1f -> %.1f", chart_before,
           chart_after);
    strokes_table_free(writer);
    strokes_table_free(base);
}

void suite_train(void)
{
    RUN(test_order);
    RUN(test_overlay_merge);
    RUN(test_session);
    RUN(test_picker);
    RUN(test_simulation);
}
