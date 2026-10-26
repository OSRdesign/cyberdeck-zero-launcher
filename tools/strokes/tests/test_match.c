/*
 * SPDX-License-Identifier: MIT
 *
 * Gates and matcher: every template recognises as itself, aspect and direction keep lines apart, gates, rejection
 * (low score, ambiguous, no candidate), candidate lists.
 */
#include "strokes_internal.h"
#include "test.h"

static void feat_at(stk_feat *f, float su, float sv, float eu, float ev)
{
    memset(f, 0, sizeof *f);
    f->su = su;
    f->sv = sv;
    f->eu = eu;
    f->ev = ev;
    f->straight = 0.5f;
    f->gap = 0.8f;
}

static void gates_cells(void)
{
    stk_gates g;
    memset(&g, 0, sizeof g);
    g.line = g.closed = STK_G_ANY;
    g.cmin = g.cmax = -1;
    g.start_mask = 1u << 0; /* tl */
    g.end_mask = 1u << 8;   /* br */
    stk_feat tf, f;
    feat_at(&tf, 0, 0, 1, 1);
    feat_at(&f, 0.1f, 0.2f, 0.9f, 0.8f);
    CHECK(stk_gates_pass(&g, &tf, &f));
    feat_at(&f, 0.42f, 0.2f, 0.9f, 0.8f); /* tl widened by the tolerance: up to 0.433 */
    CHECK(stk_gates_pass(&g, &tf, &f));
    feat_at(&f, 0.45f, 0.2f, 0.9f, 0.8f);
    CHECK(!stk_gates_pass(&g, &tf, &f));
    feat_at(&f, 0.1f, 0.2f, 0.5f, 0.5f); /* ends in the middle */
    CHECK(!stk_gates_pass(&g, &tf, &f));
    /* implicit gates: within STK_AUTO_R of the template's own start and end */
    g.start_mask = g.end_mask = 0;
    feat_at(&f, 0.3f, 0.2f, 0.8f, 0.8f);
    CHECK(stk_gates_pass(&g, &tf, &f));
    feat_at(&f, 0.6f, 0.2f, 0.8f, 0.8f);
    CHECK(!stk_gates_pass(&g, &tf, &f));
}

static void gates_shape(void)
{
    stk_gates g;
    memset(&g, 0, sizeof g);
    g.line = STK_G_YES;
    g.line_dir = STK_PI / 2; /* down */
    g.closed = STK_G_ANY;
    g.cmin = g.cmax = -1;
    stk_feat tf, f;
    feat_at(&tf, 0.5f, 0, 0.5f, 1);
    feat_at(&f, 0.5f, 0, 0.5f, 1);
    f.straight = 0.95f;
    f.dir = STK_PI / 2 + 0.5f; /* 29 degrees off */
    CHECK(stk_gates_pass(&g, &tf, &f));
    f.dir = STK_PI / 2 + 0.7f; /* 40 degrees off */
    CHECK(!stk_gates_pass(&g, &tf, &f));
    f.dir = STK_PI / 2;
    f.straight = 0.85f; /* too curved for a line glyph */
    CHECK(!stk_gates_pass(&g, &tf, &f));
    g.line = STK_G_NO;
    f.straight = 0.99f; /* too straight for a curve glyph */
    CHECK(!stk_gates_pass(&g, &tf, &f));
    f.straight = 0.95f; /* the overlap band: both kinds stay candidates */
    CHECK(stk_gates_pass(&g, &tf, &f));
    g.line = STK_G_ANY;
    g.closed = STK_G_YES;
    f.gap = 0.3f;
    CHECK(stk_gates_pass(&g, &tf, &f));
    f.gap = 0.4f;
    CHECK(!stk_gates_pass(&g, &tf, &f));
    g.closed = STK_G_NO;
    CHECK(stk_gates_pass(&g, &tf, &f));
    f.gap = 0.1f;
    CHECK(!stk_gates_pass(&g, &tf, &f));
    g.closed = STK_G_ANY;
    g.turn = -1;
    g.turn_min = 170.0f * STK_PI / 180.0f;
    f.turn = -3.5f;
    CHECK(stk_gates_pass(&g, &tf, &f));
    f.turn = -2.0f;
    CHECK(!stk_gates_pass(&g, &tf, &f));
    f.turn = 3.5f;
    CHECK(!stk_gates_pass(&g, &tf, &f));
    g.turn = 0;
    g.cmin = 1;
    g.cmax = 2;
    f.corners = 0;
    CHECK(!stk_gates_pass(&g, &tf, &f));
    f.corners = 2;
    CHECK(stk_gates_pass(&g, &tf, &f));
    f.corners = 3;
    CHECK(!stk_gates_pass(&g, &tf, &f));
    g.cmin = g.cmax = -1;
    strcpy(g.dirs, "41");
    strcpy(f.dirs, "7412369");
    CHECK(stk_gates_pass(&g, &tf, &f));
    strcpy(f.dirs, "3"); /* 3 is 135 degrees from 4 */
    CHECK(!stk_gates_pass(&g, &tf, &f));
    strcpy(f.dirs, "71"); /* 7 is within 45 degrees of 4, then 1 */
    CHECK(stk_gates_pass(&g, &tf, &f));
    strcpy(g.dirs, "26");
    strcpy(f.dirs, "62"); /* order matters */
    CHECK(!stk_gates_pass(&g, &tf, &f));
    strcpy(f.dirs, "3626");
    CHECK(stk_gates_pass(&g, &tf, &f));
    /* sharp=: the sharpest turn (a V's point against a u's round bottom) */
    g.dirs[0] = 0;
    g.sharp_min = 120.0f * STK_PI / 180.0f; /* sharp=120: at least */
    g.sharp_max = 3.0f * STK_PI;
    f.sharp = 2.5f; /* 143 degrees */
    CHECK(stk_gates_pass(&g, &tf, &f));
    f.sharp = 1.6f; /* 92 degrees */
    CHECK(!stk_gates_pass(&g, &tf, &f));
    g.sharp_min = 0.0f; /* sharp=0-120 */
    g.sharp_max = 120.0f * STK_PI / 180.0f;
    CHECK(stk_gates_pass(&g, &tf, &f));
    f.sharp = 2.5f;
    CHECK(!stk_gates_pass(&g, &tf, &f));
    g.sharp_max = 0.0f; /* not set: any */
    CHECK(stk_gates_pass(&g, &tf, &f));
}

static void templates_recognise_themselves(void)
{
    /* the shipped table with its optional section: every template, written cleanly, is read as its own glyph */
    strokes_table *t = test_table_full();
    strokes_engine *e = strokes_engine_new(t, NULL);
    for (int i = 0; i < t->nt; ++i) {
        const stk_tpl *tp = &t->tp[i];
        if (tp->dead)
            continue;
        const stk_glyph *g = &t->g[tp->glyph];
        float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
        for (int k = 0; k < tp->nv; ++k) {
            stk_vec v = t->v[tp->v0 + k];
            minx = fminf(minx, v.x);
            maxx = fmaxf(maxx, v.x);
            miny = fminf(miny, v.y);
            maxy = fmaxf(maxy, v.y);
        }
        float side = fmaxf(maxx - minx, maxy - miny), k = 100.0f / side; /* 11.7 mm */
        float xy[2 * STK_MAX_VERTS];
        for (int j = 0; j < tp->nv; ++j) {
            xy[2 * j] = 200.0f + (t->v[tp->v0 + j].x - minx) * k;
            xy[2 * j + 1] = 500.0f + (t->v[tp->v0 + j].y - miny) * k;
        }
        int box = g->set == STROKES_SET_DIGITS ? STROKES_BOX_DIGITS : STROKES_BOX_LETTERS;
        strokes_set_mode(e, replay_mode_for_set(g->set));
        strokes_result r[4];
        int n = test_feed(e, box, xy, tp->nv, 1000000, 0.8f, 10.0f, r, 4);
        CHECKF(n == 1 && r[0].kind != STROKES_REJECT && strcmp(r[0].glyph, g->id) == 0,
               "template %s (line %d) -> %s %s %.3f margin %.3f", g->id, tp->line, r[0].glyph,
               strokes_reject_name(r[0].reject), (double)r[0].confidence, (double)r[0].margin);
        CHECKF(r[0].confidence > 0.93f, "%s line %d scores %.3f", g->id, tp->line, (double)r[0].confidence);
    }
    strokes_engine_free(e);
}

static void lines_stay_apart(void)
{
    static const struct {
        int box;
        float x0, y0, x1, y1;
        const char *want;
    } cases[] = {
        {STROKES_BOX_LETTERS, 250, 500, 250, 600, "i"},     {STROKES_BOX_LETTERS, 200, 550, 300, 550, "space"},
        {STROKES_BOX_LETTERS, 300, 550, 200, 550, "bksp"},  {STROKES_BOX_LETTERS, 300, 500, 200, 600, "enter"},
        {STROKES_BOX_LETTERS, 250, 600, 250, 500, "shift"}, {STROKES_BOX_DIGITS, 250, 500, 250, 600, "1"},
        {STROKES_BOX_DIGITS, 200, 550, 300, 550, "space"},  {STROKES_BOX_LETTERS, 250, 500, 262, 600, "i"},
        {STROKES_BOX_LETTERS, 250, 500, 250, 530, "i"},     {STROKES_BOX_LETTERS, 200, 550, 230, 553, "space"},
        {STROKES_BOX_LETTERS, 200, 500, 300, 600, "ext"},   {STROKES_BOX_DIGITS, 200, 500, 290, 610, "ext"},
        {STROKES_BOX_LETTERS, 250, 500, 270, 600, "i"},     {STROKES_BOX_LETTERS, 300, 500, 210, 610, "enter"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        float xy[4] = {cases[i].x0, cases[i].y0, cases[i].x1, cases[i].y1};
        strokes_result r = test_recognise(cases[i].box, xy, 2);
        CHECKF(r.kind != STROKES_REJECT && strcmp(r.glyph, cases[i].want) == 0, "line %zu -> %s (%s) want %s", i,
               r.glyph, strokes_reject_name(r.reject), cases[i].want);
    }
    /* the L (l) is not a line, and a line is not an L */
    float l[] = {200, 500, 200, 600, 270, 600};
    strokes_result r = test_recognise(STROKES_BOX_LETTERS, l, 3);
    CHECK(r.kind == STROKES_CHAR && strcmp(r.glyph, "l") == 0);
    /* a diagonal up-right is no glyph of the letters box: rejected, not forced onto shift or space */
    float d[] = {200, 600, 300, 500};
    r = test_recognise(STROKES_BOX_LETTERS, d, 2);
    CHECK(r.kind == STROKES_REJECT);
    /* after the prefixes, the gestures' lines are glyphs of the set: hyphen, exclamation mark, comma, backslash */
    strokes_point pts[2];
    static const struct {
        int set;
        float x0, y0, x1, y1;
        const char *want;
    } pre[] = {{STROKES_SET_PUNCT, 200, 550, 300, 550, "hyphen"}, {STROKES_SET_PUNCT, 250, 600, 250, 500, "excl"},
               {STROKES_SET_PUNCT, 300, 500, 200, 600, "comma"},  {STROKES_SET_PUNCT, 200, 500, 300, 600, "backslash"},
               {STROKES_SET_PUNCT, 300, 550, 200, 550, "bksp"},   {STROKES_SET_EXT, 300, 500, 200, 600, "x"},
               {STROKES_SET_EXT, 200, 550, 300, 550, "ext_minus"}, {STROKES_SET_EXT, 250, 600, 250, 500, "iexcl"},
               {STROKES_SET_EXT, 300, 550, 200, 550, "bksp"}};
    for (size_t i = 0; i < sizeof pre / sizeof pre[0]; ++i) {
        pts[0].x = pre[i].x0;
        pts[0].y = pre[i].y0;
        pts[0].t_us = 1000000;
        pts[1].x = pre[i].x1;
        pts[1].y = pre[i].y1;
        pts[1].t_us = 1100000;
        strokes_result c;
        int ok = strokes_classify(test_table(), NULL, pre[i].set, pts, 2, &c);
        CHECKF(ok && strcmp(c.glyph, pre[i].want) == 0, "%s line %zu -> %s want %s", strokes_set_name(pre[i].set), i,
               c.glyph, pre[i].want);
    }
}

static void rejects(void)
{
    /* a clockwise circle: o is counter-clockwise, nothing else is a circle */
    float cw[2 * 25];
    for (int i = 0; i < 25; ++i) {
        float a = -STK_PI / 2 + 2 * STK_PI * (float)i / 24.0f * 0.97f;
        cw[2 * i] = 250 + 50 * cosf(a);
        cw[2 * i + 1] = 550 + 50 * sinf(a);
    }
    strokes_result r = test_recognise(STROKES_BOX_LETTERS, cw, 25);
    CHECKF(r.kind == STROKES_REJECT, "cw circle -> %s", r.glyph);
    /* a scribble */
    float sc[] = {200, 500, 300, 520, 210, 540, 290, 560, 220, 580, 280, 600, 230, 520};
    r = test_recognise(STROKES_BOX_LETTERS, sc, 7);
    CHECK(r.kind == STROKES_REJECT);
    CHECK(r.n_cand >= 1 && r.n_cand <= STROKES_MAX_CAND);
    for (int i = 1; i < r.n_cand; ++i)
        CHECK(r.cand[i].score <= r.cand[i - 1].score);

    /* ambiguous: two glyphs with one shape */
    strokes_table *t = strokes_table_new();
    strokes_error err;
    CHECK(strokes_table_add_text(t, "p letters p 0,0 0,100 100,100\nq letters q 0,0 0,100 100,100\n", "amb", &err) ==
          0);
    strokes_point pts[40];
    for (int i = 0; i < 40; ++i) {
        float s = (float)i / 39.0f * 200.0f;
        pts[i].x = 200 + (s < 100 ? 0 : s - 100);
        pts[i].y = 500 + (s < 100 ? s : 100);
        pts[i].t_us = 1000000 + (uint64_t)i * 10000;
    }
    CHECK(strokes_classify(t, NULL, STROKES_SET_LETTERS, pts, 40, &r) == 0);
    CHECK(r.reject == STROKES_REJECT_AMBIGUOUS && r.margin < 0.01f && r.confidence > 0.95f);
    CHECK(r.n_cand == 2 && strcmp(r.glyph, "p") == 0); /* the best is still named, for diagnostics */
    /* low score: only one glyph, far from the stroke */
    strokes_table_free(t);
    t = strokes_table_new();
    CHECK(strokes_table_add_text(t, "u letters u 0,0 0,100 100,100 100,0\n", "low", &err) == 0);
    for (int i = 0; i < 40; ++i) { /* a right-angle 7 */
        float s = (float)i / 39.0f * 200.0f;
        pts[i].x = 200 + (s < 100 ? s : 100);
        pts[i].y = 500 + (s < 100 ? 0 : s - 100);
    }
    strokes_config cfg;
    strokes_config_default(&cfg);
    CHECK(strokes_classify(t, &cfg, STROKES_SET_LETTERS, pts, 40, &r) == 0);
    CHECK(r.reject == STROKES_REJECT_NO_CANDIDATE || r.reject == STROKES_REJECT_LOW_SCORE);
    /* no candidate: a line table and a curve */
    strokes_table_free(t);
    t = strokes_table_new();
    CHECK(strokes_table_add_text(t, "space any KEY_SPACE 0,50 100,50\n", "line", &err) == 0);
    for (int i = 0; i < 40; ++i) {
        float a = STK_PI * (float)i / 39.0f;
        pts[i].x = 250 - 50 * cosf(a);
        pts[i].y = 550 + 50 * sinf(a);
    }
    CHECK(strokes_classify(t, &cfg, STROKES_SET_LETTERS, pts, 40, &r) == 0);
    CHECK(r.reject == STROKES_REJECT_NO_CANDIDATE && r.n_cand == 1 && r.cand[0].gated == 1 && r.glyph[0] == 0);
    /* thresholds are honoured */
    for (int i = 0; i < 40; ++i) {
        pts[i].x = 200 + 100.0f * (float)i / 39.0f;
        pts[i].y = 550;
    }
    CHECK(strokes_classify(t, &cfg, STROKES_SET_LETTERS, pts, 40, &r) == 1 && r.kind == STROKES_KEY);
    cfg.min_score = 1.01f;
    CHECK(strokes_classify(t, &cfg, STROKES_SET_LETTERS, pts, 40, &r) == 0 && r.reject == STROKES_REJECT_LOW_SCORE);
    strokes_table_free(t);
}

static void set_any_only_gestures(void)
{
    strokes_table *t = test_table();
    strokes_point pts[30];
    for (int i = 0; i < 30; ++i) { /* an L */
        float s = (float)i / 29.0f * 170.0f;
        pts[i].x = 200 + (s < 100 ? 0 : s - 100);
        pts[i].y = 500 + (s < 100 ? s : 100);
        pts[i].t_us = 1000000 + (uint64_t)i * 10000;
    }
    strokes_result r;
    CHECK(strokes_classify(t, NULL, STROKES_SET_LETTERS, pts, 30, &r) == 1 && strcmp(r.glyph, "l") == 0);
    CHECK(strokes_classify(t, NULL, STROKES_SET_ANY, pts, 30, &r) == 0);
    for (int i = 0; i < 30; ++i) { /* the same move, 70 down and 100 across: the digits box reads 4 */
        float s = (float)i / 29.0f * 170.0f;
        pts[i].x = 200 + (s < 70 ? 0 : s - 70);
        pts[i].y = 500 + (s < 70 ? s : 70);
    }
    CHECK(strokes_classify(t, NULL, STROKES_SET_DIGITS, pts, 30, &r) == 1 && strcmp(r.glyph, "4") == 0);
    CHECK(strokes_classify(t, NULL, 7, pts, 30, &r) == 0);
    CHECK(strokes_classify(NULL, NULL, STROKES_SET_LETTERS, pts, 30, &r) == 0);
}

void suite_match(void)
{
    RUN(gates_cells);
    RUN(gates_shape);
    RUN(templates_recognise_themselves);
    RUN(lines_stay_apart);
    RUN(rejects);
    RUN(set_any_only_gestures);
}
