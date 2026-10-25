/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal test harness of tools/strokes (make test, make asan).
 */
#ifndef STROKES_TEST_H
#define STROKES_TEST_H

#include "sample.h"
#include "strokes.h"
#include "tools_common.h" /* STROKES_SHARE_DIR: the shipped tables, relative to tools/strokes */

#include <math.h>
#include <stdio.h>
#include <string.h>

extern int t_checks, t_fails, t_tests;

#define CHECK(cond)                                                                                                \
    do {                                                                                                           \
        ++t_checks;                                                                                                \
        if (!(cond)) {                                                                                             \
            ++t_fails;                                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                 \
        }                                                                                                          \
    } while (0)

#define CHECKF(cond, ...)                                                                                          \
    do {                                                                                                           \
        ++t_checks;                                                                                                \
        if (!(cond)) {                                                                                             \
            ++t_fails;                                                                                             \
            printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);                                                 \
            printf(__VA_ARGS__);                                                                                   \
            printf("\n");                                                                                          \
        }                                                                                                          \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECKF(fabs((double)(a) - (double)(b)) <= (eps), "%g vs %g", (double)(a), (double)(b))

#define RUN(fn)                                                                                                    \
    do {                                                                                                           \
        ++t_tests;                                                                                                 \
        fn();                                                                                                      \
    } while (0)

/* helpers (test_main.c) */
strokes_table *test_table(void);            /* the shipped table, loaded once */
strokes_table *test_table_full(void);       /* the shipped table plus its optional section, loaded once */
const char *test_table_path(void);
const char *test_table_extra_path(void);
/* Feeds a polyline (x,y pairs in px) as frames at speed px/ms, one frame every frame_ms, then lifts and flushes.
 * Returns the number of results (stored in res, at most max). */
int test_feed(strokes_engine *e, int box, const float *xy, int npts, uint64_t t0_us, float speed, float frame_ms,
              strokes_result *res, int max);
/* One stroke through a fresh engine with the default config (ppmm 8.54) in the box; returns the first result. */
strokes_result test_recognise(int box, const float *xy, int npts);
/* Writes the first template of glyph (id, set) of table t into the engine as a 100 px (11.7 mm) stroke in the box,
 * then flushes; returns the first result (kind STROKES_NONE when the glyph has no drawn template). */
strokes_result test_write_glyph(strokes_engine *e, const strokes_table *t, const char *id, int set, int box);

void suite_geom(void);
void suite_table(void);
void suite_match(void);
void suite_engine(void);
void suite_tools(void);
void suite_fuzz(void);
void suite_accuracy(void);

#endif
