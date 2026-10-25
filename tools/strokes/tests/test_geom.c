/*
 * SPDX-License-Identifier: MIT
 *
 * Geometry: resampling, smoothing, hook trimming, normalisation, features, chain code, the bounded-rotation score.
 */
#include "strokes_internal.h"
#include "test.h"

static void resample_equidistant(void)
{
    stk_vec in[3] = {{0, 0}, {100, 0}, {100, 50}};
    stk_vec out[STK_N];
    stk_resample(in, 3, out, STK_N);
    CHECK_NEAR(out[0].x, 0, 1e-4);
    CHECK_NEAR(out[STK_N - 1].x, 100, 1e-3);
    CHECK_NEAR(out[STK_N - 1].y, 50, 1e-3);
    /* equidistant along the path: point i is i * 150 / 31 from the start, on the polyline itself */
    float step = 150.0f / (STK_N - 1);
    for (int i = 0; i < STK_N; ++i) {
        float s = step * (float)i;
        float ex = s <= 100.0f ? s : 100.0f, ey = s <= 100.0f ? 0.0f : s - 100.0f;
        CHECK_NEAR(out[i].x, ex, 0.01);
        CHECK_NEAR(out[i].y, ey, 0.01);
    }
    stk_vec straight[4] = {{0, 0}, {10, 0}, {60, 0}, {62, 0}};
    stk_resample(straight, 4, out, STK_N);
    for (int i = 1; i < STK_N; ++i)
        CHECK_NEAR(out[i].x - out[i - 1].x, 62.0 / (STK_N - 1), 1e-3);
}

static void resample_degenerate(void)
{
    stk_vec one = {5, 7};
    stk_vec out[STK_N];
    stk_resample(&one, 1, out, STK_N);
    CHECK(out[0].x == 5 && out[STK_N - 1].y == 7);
    stk_vec same[4] = {{3, 3}, {3, 3}, {3, 3}, {3, 3}};
    stk_resample(same, 4, out, STK_N);
    CHECK(out[10].x == 3 && out[10].y == 3);
    stk_vec two[2] = {{0, 0}, {0, 31}};
    stk_resample(two, 2, out, STK_N);
    for (int i = 0; i < STK_N; ++i)
        CHECK_NEAR(out[i].y, (float)i, 1e-3);
    stk_resample(two, 0, out, STK_N); /* no input: zeros, no crash */
    CHECK(out[0].x == 0 && out[0].y == 0);
}

static void smooth_keeps_ends(void)
{
    stk_vec p[5] = {{0, 0}, {10, 10}, {20, 0}, {30, 10}, {40, 0}};
    stk_smooth(p, 5);
    CHECK(p[0].x == 0 && p[0].y == 0 && p[4].x == 40 && p[4].y == 0);
    CHECK_NEAR(p[1].y, 5.0, 1e-4); /* 0.25*0 + 0.5*10 + 0.25*0 */
}

static void normalise_unit(void)
{
    stk_vec in[STK_N], out[STK_N];
    for (int i = 0; i < STK_N; ++i) {
        in[i].x = 50.0f + 3.0f * i;
        in[i].y = 10.0f + 0.5f * i * i;
    }
    CHECK(stk_normalise(in, STK_N, out) == 1);
    double sx = 0, sy = 0, ss = 0;
    for (int i = 0; i < STK_N; ++i) {
        sx += out[i].x;
        sy += out[i].y;
        ss += out[i].x * out[i].x + out[i].y * out[i].y;
    }
    CHECK_NEAR(sx, 0, 1e-4);
    CHECK_NEAR(sy, 0, 1e-4);
    CHECK_NEAR(ss, 1, 1e-4);
    stk_vec flat[STK_N];
    for (int i = 0; i < STK_N; ++i)
        flat[i].x = flat[i].y = 4.0f;
    CHECK(stk_normalise(flat, STK_N, out) == 0);
}

static void line_from(float x0, float y0, float x1, float y1, stk_vec *out)
{
    stk_vec in[2] = {{x0, y0}, {x1, y1}};
    stk_resample(in, 2, out, STK_N);
}

static void features_lines(void)
{
    stk_vec p[STK_N];
    stk_feat f;
    line_from(0, 0, 0, 100, p); /* down */
    stk_features(p, STK_N, &f);
    CHECK_NEAR(f.straight, 1, 1e-4);
    CHECK_NEAR(f.dir, STK_PI / 2, 1e-4);
    CHECK(stk_numpad_dir(f.dir) == 2);
    CHECK(strcmp(f.dirs, "2") == 0);
    CHECK_NEAR(f.su, 0.5, 1e-4); /* aspect kept: a vertical line sits in the middle column */
    CHECK_NEAR(f.sv, 0.0, 1e-4);
    CHECK_NEAR(f.ev, 1.0, 1e-4);
    CHECK(f.corners == 0);
    line_from(100, 0, 0, 100, p); /* down-left */
    stk_features(p, STK_N, &f);
    CHECK(stk_numpad_dir(f.dir) == 1);
    line_from(0, 50, 100, 50, p);
    stk_features(p, STK_N, &f);
    CHECK(strcmp(f.dirs, "6") == 0 && f.sv > 0.49f && f.sv < 0.51f);
    CHECK(stk_numpad_dir(-STK_PI / 2) == 8 && stk_numpad_dir(STK_PI) == 4 && stk_numpad_dir(-STK_PI) == 4);
    CHECK(stk_numpad_dir(-STK_PI / 4) == 9 && stk_numpad_dir(-3 * STK_PI / 4) == 7 && stk_numpad_dir(STK_PI / 4) == 3);
}

static void features_shapes(void)
{
    stk_vec in[3] = {{0, 0}, {0, 100}, {70, 100}}; /* L */
    stk_vec p[STK_N];
    stk_feat f;
    stk_resample(in, 3, p, STK_N);
    stk_features(p, STK_N, &f);
    CHECK(f.corners == 1);
    CHECK(strcmp(f.dirs, "26") == 0);
    CHECK(f.straight < 0.8f);
    CHECK(f.turn < -1.3f && f.turn > -1.8f); /* down then right: a quarter turn counter-clockwise on screen */
    stk_vec v[3] = {{0, 0}, {50, 100}, {100, 0}};
    stk_resample(v, 3, p, STK_N);
    stk_features(p, STK_N, &f);
    CHECK(f.corners == 1);
    CHECK(strcmp(f.dirs, "39") == 0);
    stk_vec z[4] = {{0, 0}, {100, 0}, {0, 100}, {100, 100}};
    stk_resample(z, 4, p, STK_N);
    stk_features(p, STK_N, &f);
    CHECK(f.corners == 2);
    CHECK(strcmp(f.dirs, "616") == 0);
    CHECK(f.gap > 1.3f); /* far from closed */
    /* a counter-clockwise circle from the top */
    stk_vec c[STK_N];
    for (int i = 0; i < STK_N; ++i) {
        float a = -STK_PI / 2 - 2 * STK_PI * (float)i / (STK_N - 1);
        c[i].x = 50 + 50 * cosf(a);
        c[i].y = 50 + 50 * sinf(a);
    }
    stk_features(c, STK_N, &f);
    CHECK(f.gap < 0.01f);
    CHECK(f.corners == 0);
    CHECK_NEAR(f.turn, -2 * STK_PI, 0.25);
}

static void hooks_trimmed(void)
{
    /* a line down with a 4 % landing hook: the pen lands low and flicks up before the stroke */
    stk_vec in[3] = {{1, 4}, {0, 0}, {0, 100}};
    stk_vec p[STK_FINE];
    stk_resample(in, 3, p, STK_FINE);
    int n = stk_trim_hooks(p, STK_FINE, 8.0f);
    CHECK(n < STK_FINE);
    CHECK(fabsf(p[0].x) < 0.5f);
    /* no hook on a clean L: nothing trimmed */
    stk_vec l[3] = {{0, 0}, {0, 100}, {70, 100}};
    stk_resample(l, 3, p, STK_FINE);
    CHECK(stk_trim_hooks(p, STK_FINE, 8.0f) == STK_FINE);
    /* a lift hook flicking back up at the end of a line right */
    stk_vec e[3] = {{0, 50}, {100, 50}, {97, 46}};
    stk_resample(e, 3, p, STK_FINE);
    n = stk_trim_hooks(p, STK_FINE, 8.0f);
    CHECK(n < STK_FINE);
    CHECK(p[n - 1].x > 98.0f);
    /* a short last stroke of the glyph itself (the tail of q, the bar of g): it turns back further than a hook can
     * be long, so it stays */
    stk_vec tail[3] = {{0, 0}, {0, 100}, {3, 88}};
    stk_resample(tail, 3, p, STK_FINE);
    CHECK(stk_trim_hooks(p, STK_FINE, 8.0f) == STK_FINE);
    /* the same turn within the hook length is a hook */
    stk_vec flick[3] = {{0, 0}, {0, 100}, {1, 95}};
    stk_resample(flick, 3, p, STK_FINE);
    n = stk_trim_hooks(p, STK_FINE, 8.0f);
    CHECK(n < STK_FINE && p[n - 1].y > 98.0f);
}

static void score_rotation_bound(void)
{
    stk_vec a[STK_N], b[STK_N], na[STK_N], nb[STK_N];
    stk_vec in[3] = {{0, 0}, {0, 100}, {70, 100}};
    stk_resample(in, 3, a, STK_N);
    stk_normalise(a, STK_N, na);
    stk_rot none, r15;
    stk_rot_init(&none, 0.0f);
    stk_rot_init(&r15, 15.0f);
    CHECK_NEAR(stk_score(na, na, STK_N, &none), 1.0, 1e-3);
    for (int deg = 0; deg <= 60; deg += 10) {
        float t = (float)deg * STK_PI / 180.0f;
        for (int i = 0; i < STK_N; ++i) {
            b[i].x = cosf(t) * a[i].x - sinf(t) * a[i].y;
            b[i].y = sinf(t) * a[i].x + cosf(t) * a[i].y;
        }
        stk_normalise(b, STK_N, nb);
        float s0 = stk_score(nb, na, STK_N, &none), s15 = stk_score(nb, na, STK_N, &r15);
        /* without tolerance the score falls linearly with the angle; with it, slants up to 15 degrees are free */
        CHECK_NEAR(s0, 1.0 - deg / 90.0, 0.01);
        if (deg <= 10)
            CHECK_NEAR(s15, 1.0, 0.01);
        else
            CHECK_NEAR(s15, 1.0 - (deg - 15) / 90.0, 0.01);
    }
    /* reversed direction scores 0: direction matters */
    for (int i = 0; i < STK_N; ++i)
        b[i] = a[STK_N - 1 - i];
    stk_normalise(b, STK_N, nb);
    CHECK(stk_score(nb, na, STK_N, &r15) < 0.5f);
}

void suite_geom(void)
{
    RUN(resample_equidistant);
    RUN(resample_degenerate);
    RUN(smooth_keeps_ends);
    RUN(normalise_unit);
    RUN(features_lines);
    RUN(features_shapes);
    RUN(hooks_trimmed);
    RUN(score_rotation_bound);
}
