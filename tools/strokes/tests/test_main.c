/*
 * SPDX-License-Identifier: MIT
 *
 * Unit tests of the Strokes recogniser and its PC tools: make test (build/host/test_strokes), make asan.
 * Run from tools/strokes (the shipped table is ../../projects/APPLaunch/APPLaunch/strokes/glyphs.txt, STROKES_SHARE_DIR;
 * STROKES_TABLE overrides).
 */
#include "strokes_internal.h"
#include "test.h"

#include <stdlib.h>

int t_checks, t_fails, t_tests;

static strokes_table *shipped, *full;

const char *test_table_path(void)
{
    const char *env = getenv("STROKES_TABLE");
    return env && *env ? env : STROKES_SHARE_DIR "/glyphs.txt";
}

const char *test_table_extra_path(void)
{
    const char *env = getenv("STROKES_TABLE_EXTRA");
    return env && *env ? env : STROKES_SHARE_DIR "/glyphs-extra.txt";
}

static strokes_table *load(const char *a, const char *b)
{
    strokes_table *t = strokes_table_new();
    strokes_error err;
    if (strokes_table_add_file(t, a, &err) || (b && strokes_table_add_file(t, b, &err))) {
        printf("cannot load %s:%d: %s (run from tools/strokes)\n", err.source, err.line, err.msg);
        exit(2);
    }
    return t;
}

strokes_table *test_table(void)
{
    if (!shipped)
        shipped = load(test_table_path(), NULL);
    return shipped;
}

strokes_table *test_table_full(void)
{
    if (!full)
        full = load(test_table_path(), test_table_extra_path());
    return full;
}

int test_feed(strokes_engine *e, int box, const float *xy, int npts, uint64_t t0_us, float speed, float frame_ms,
              strokes_result *res, int max)
{
    uint64_t t = t0_us;
    int nres = 0;
    strokes_down(e, box, xy[0], xy[1], t);
    float carry = 0.0f, step = speed * frame_ms;
    float px = xy[0], py = xy[1];
    for (int i = 1; i < npts; ++i) {
        float ax = px, ay = py, bx = xy[2 * i], by = xy[2 * i + 1];
        float len = hypotf(bx - ax, by - ay);
        float pos = step - carry;
        while (pos <= len) {
            float f = len > 0.0f ? pos / len : 1.0f;
            t += (uint64_t)(frame_ms * 1000.0f);
            strokes_move(e, ax + f * (bx - ax), ay + f * (by - ay), t);
            pos += step;
        }
        carry = len - (pos - step);
        px = bx;
        py = by;
    }
    t += (uint64_t)(frame_ms * 1000.0f);
    strokes_move(e, xy[2 * (npts - 1)], xy[2 * (npts - 1) + 1], t);
    strokes_up(e, t + 1000);
    strokes_result r;
    while (strokes_flush(e, &r))
        if (nres < max)
            res[nres++] = r;
    return nres;
}

strokes_result test_write_glyph(strokes_engine *e, const strokes_table *t, const char *id, int set, int box)
{
    static uint64_t clock = 500000000;
    strokes_result r[4];
    memset(r, 0, sizeof r);
    int gi = stk_find_glyph(t, id, set);
    for (int i = 0; gi >= 0 && i < t->nt; ++i) {
        const stk_tpl *tp = &t->tp[i];
        if (tp->dead || tp->glyph != gi)
            continue;
        float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
        for (int k = 0; k < tp->nv; ++k) {
            minx = fminf(minx, t->v[tp->v0 + k].x);
            maxx = fmaxf(maxx, t->v[tp->v0 + k].x);
            miny = fminf(miny, t->v[tp->v0 + k].y);
            maxy = fmaxf(maxy, t->v[tp->v0 + k].y);
        }
        float k = 100.0f / fmaxf(maxx - minx, maxy - miny);
        float xy[2 * STK_MAX_VERTS];
        for (int j = 0; j < tp->nv; ++j) {
            xy[2 * j] = 200.0f + (t->v[tp->v0 + j].x - minx) * k;
            xy[2 * j + 1] = 500.0f + (t->v[tp->v0 + j].y - miny) * k;
        }
        clock += 2000000;
        test_feed(e, box, xy, tp->nv, clock, 0.8f, 10.0f, r, 4);
        break;
    }
    return r[0];
}

strokes_result test_recognise(int box, const float *xy, int npts)
{
    strokes_engine *e = strokes_engine_new(test_table(), NULL);
    strokes_result r[4];
    memset(r, 0, sizeof r);
    test_feed(e, box, xy, npts, 1000000, 0.8f, 10.0f, r, 4);
    strokes_engine_free(e);
    return r[0];
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    struct {
        const char *name;
        void (*fn)(void);
    } suites[] = {{"geometry", suite_geom}, {"table", suite_table},   {"match", suite_match},
                  {"engine", suite_engine}, {"tools", suite_tools},   {"fuzz", suite_fuzz},
                  {"accuracy", suite_accuracy}};
    for (size_t i = 0; i < sizeof suites / sizeof suites[0]; ++i) {
        int c0 = t_checks, f0 = t_fails, n0 = t_tests;
        suites[i].fn();
        printf("%-9s %3d tests %6d checks %s\n", suites[i].name, t_tests - n0, t_checks - c0,
               t_fails == f0 ? "ok" : "FAILED");
    }
    printf("test_strokes: %d tests, %d checks, %d failed\n", t_tests, t_checks, t_fails);
    strokes_table_free(shipped);
    strokes_table_free(full);
    return t_fails ? 1 : 0;
}
