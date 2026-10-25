/*
 * SPDX-License-Identifier: MIT
 *
 * PC tools of tools/strokes (not part of the recogniser library): a recorded or synthetic stroke sample, the corpus
 * file format, the synthetic generator, the replay harness and the accuracy report. See README.md.
 */
#ifndef STROKES_SAMPLE_H
#define STROKES_SAMPLE_H

#include "strokes.h"

#include <stdint.h>
#include <stdio.h>

#define SAMPLE_MAX_PTS 1024
#define SAMPLE_MAX_LIFTS 64

/* One intended stroke as the panel reported it: points of one or more contacts (drop-outs) and the lifts. */
typedef struct {
    char label[STROKES_ID_MAX]; /* expected glyph id, "?" when unknown */
    int box;                    /* enum strokes_box */
    float ppmm;
    int n;
    strokes_point p[SAMPLE_MAX_PTS];
    int nl;
    int lift_after[SAMPLE_MAX_LIFTS]; /* the pen lifts after point lift_after[i] ... */
    uint64_t lift_t[SAMPLE_MAX_LIFTS]; /* ... at this time; the last lift follows the last point */
} stk_sample;

void sample_clear(stk_sample *s);
/* Appends a point; a point after a lift is a new press. Returns -1 when full. */
int sample_add(stk_sample *s, float x, float y, uint64_t t_us);
int sample_lift(stk_sample *s, uint64_t t_us);

/* ---- corpus file: "label<TAB>box<TAB>ppmm<TAB>t,x,y t,x,y ... ^t" (README "Corpus format") ---- */

/* Parses one corpus line. Returns 1 for a sample, 0 for a blank or comment line, -1 on error (msg says why). */
int corpus_parse_line(const char *line, stk_sample *s, char *msg, size_t msz);
void corpus_write_header(FILE *f, const char *tool, const char *note);
void corpus_write(FILE *f, const stk_sample *s);
/* Reads every sample of a file; calls fn for each. Returns the number of samples, or -1 (msg says which line). */
int corpus_read_file(const char *path, int (*fn)(void *ctx, const stk_sample *s), void *ctx, char *msg, size_t msz);

/* ---- replay harness ---- */

/* Feeds one sample through the engine as the frames would arrive (press, moves, lifts, presses after drop-outs),
 * then flushes. Returns the number of results stored (at most max). */
int replay_sample(strokes_engine *e, const stk_sample *s, strokes_result *res, int max);
/* The engine mode a glyph of `set` is written in: the punctuation or extended prefix, else 0. */
unsigned replay_mode_for_set(int set);

/* ---- synthetic generator ---- */

typedef struct {
    float ppmm;                         /* coordinates written at this density */
    float size_min_mm, size_max_mm;     /* longer side of the written glyph */
    float scale_min, scale_max;         /* independent per-axis scale (aspect noise) */
    float rot_deg;                      /* rotation, uniform in +-rot_deg */
    float shear;                        /* x += shear * y, uniform in +-shear */
    float wobble;                       /* smooth per-sample shape variation, fraction of the size */
    float round_p, round_max;           /* corner rounding: probability, maximum cut fraction */
    float noise;                        /* Gaussian jitter per point, sigma as a fraction of the size */
    float rate_min_hz, rate_max_hz;     /* touch report rate */
    float speed_min, speed_max;         /* mean pen speed, mm/s */
    float hook_p, hook_min, hook_max;   /* landing / lift hook per end: probability, length (fraction of size) */
    float trunc_p, trunc_max;           /* truncation per end: probability, maximum fraction of the path */
    float drop_p, drop_min_ms, drop_max_ms; /* one capacitive drop-out per stroke: probability, gap */
    float tap_min_ms, tap_max_ms, tap_jitter_mm;
    int quantise; /* round coordinates to whole pixels, as touch controllers report them */
} synth_cfg;

typedef struct {
    uint64_t s;
} synth_rng;

void synth_default(synth_cfg *c); /* the report 038 settings (README "Synthetic generator") */
void synth_hard(synth_cfg *c);    /* harsher settings for the stress table */
int synth_set(synth_cfg *c, const char *kv); /* "key=value"; returns -1 when unknown or bad */
void synth_describe(const synth_cfg *c, char *buf, size_t size);
void synth_seed(synth_rng *r, uint64_t seed);
double synth_uniform(synth_rng *r); /* [0, 1) */
/* One sample of the glyph with internal index gi (see synth_glyph_count / synth_glyph_id). Returns 0, -1 on error. */
int synth_glyph(const strokes_table *t, int gi, const synth_cfg *c, synth_rng *r, stk_sample *s);
/* A random scribble (4-8 random vertices), not meant to be any glyph. */
void synth_scribble(const synth_cfg *c, synth_rng *r, stk_sample *s);
/* Internal glyph indices of live glyphs: count, id, set. */
int synth_glyph_count(const strokes_table *t);
const char *synth_glyph_id(const strokes_table *t, int gi);
int synth_glyph_set(const strokes_table *t, int gi);
int synth_glyph_live(const strokes_table *t, int gi);

/* ---- evtest-style text (capture tool input) from samples, for the end-to-end test ---- */
void evtest_write_header(FILE *f, int w, int h);
void evtest_write_sample(FILE *f, const stk_sample *s, uint64_t base_us, int *tracking_id);

/* ---- per-glyph accuracy report ---- */

#define REPORT_MAX_GLYPHS 256
#define REPORT_MAX_CONF 512

typedef struct {
    char id[STROKES_ID_MAX];
    int set;
    int n, ok, rej, wrong, split;
} report_row;

typedef struct {
    char want[STROKES_ID_MAX], got[STROKES_ID_MAX];
    int count;
} report_conf;

typedef struct {
    float conf, margin;
    int matchable; /* the matcher scored it (not short / dwell / no candidate) */
    int top_ok;    /* the best candidate is the expected glyph */
} report_point;

typedef struct {
    report_row rows[REPORT_MAX_GLYPHS];
    int nrows;
    report_conf conf[REPORT_MAX_CONF];
    int nconf;
    int rej_reason[8];
    report_point *pts;
    int npts, cappts;
} report;

void report_init(report *r);
void report_free(report *r);
/* Records one sample's outcome: results res[0..nres-1] for expected glyph (want, set). */
void report_add(report *r, const char *want, int set, const strokes_result *res, int nres);
/* Prints the per-glyph table, the totals per set and overall, and the top confusions. */
void report_print(FILE *f, const report *r, int top_conf);
/* Totals over every row: accuracy and rejection in percent. */
void report_totals(const report *r, int *n, double *acc, double *rej, double *wrong);
/* Accuracy / rejection for a grid of thresholds (from the recorded best scores and margins). */
void report_sweep(FILE *f, const report *r);

#endif
