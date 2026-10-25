/*
 * SPDX-License-Identifier: MIT
 *
 * Internal types of the Strokes recogniser, shared by the library files (strokes_*.c) and by the PC tools of
 * tools/strokes (the synthetic generator reads the table's template vertices). Not a public API.
 */
#ifndef STROKES_INTERNAL_H
#define STROKES_INTERNAL_H

#include "strokes.h"

#define STK_N 32            /* resampled points of a stroke and of a template (report 038: N = 32) */
#define STK_FINE 64         /* fine resampling used for smoothing and hook trimming */
#define STK_MAX_RAW 2048    /* raw points kept per stroke; beyond, the buffer is halved (shape kept) */
#define STK_MAX_VERTS 256   /* vertices of one template line */
#define STK_MAX_GLYPHS 256
#define STK_MAX_TEMPLATES 4096
#define STK_PI 3.14159265358979f

/* Gate thresholds (README "Gates"). Frame units: the stroke's bounding box seen in a square of its longer side. */
#define STK_LINE_TPL 0.97f   /* a template this straight is a line glyph */
#define STK_LINE_MIN 0.90f   /* an input must be at least this straight to match a line glyph */
#define STK_CURVE_MAX 0.975f /* ... and at most this straight to match any other glyph */
#define STK_LINE_TOL (35.0f * STK_PI / 180.0f) /* direction tolerance of line glyphs */
#define STK_CLOSED_TPL 0.15f /* template gap (end to start / frame) under this: closed glyph */
#define STK_OPEN_TPL 0.40f   /* ... over this: open glyph */
#define STK_CLOSED_MAX 0.32f /* input gap allowed for a closed glyph */
#define STK_OPEN_MIN 0.15f   /* input gap required for an open glyph */
#define STK_CELL_TOL 0.10f   /* explicit start/end cells are widened by this */
#define STK_AUTO_R 0.40f     /* implicit start/end gate: within this of the template's start/end */
#define STK_CORNER_DEG 65.0f /* turning over 3 vertices above this is a corner */

#define STK_NSETS 5 /* enum strokes_set: letters, digits, punct, any, ext */

enum { STK_G_AUTO = -1, STK_G_NO = 0, STK_G_YES = 1, STK_G_ANY = 2 };
enum { STK_OUT_CHAR = 0, STK_OUT_KEY = 1, STK_OUT_MODE = 2, STK_OUT_ACCENT = 3 };

typedef struct {
    float x, y;
} stk_vec;

typedef struct {
    float straight; /* chord / path length, 0..1 */
    float dir;      /* chord angle, radians, y down (0 = right, +pi/2 = down) */
    float gap;      /* |end - start| / frame side */
    float turn;     /* signed net turning, radians, + = clockwise on screen */
    int corners;
    float su, sv, eu, ev; /* start and end in the square frame, 0..1 */
    char dirs[STROKES_DIRS_MAX];
} stk_feat;

typedef struct {
    uint16_t start_mask, end_mask; /* 9-bit cell masks (bit r*3+c, r = 0 top, c = 0 left); 0 = implicit */
    int8_t line;                   /* after load: STK_G_NO (curve), STK_G_YES (line, line_dir), STK_G_ANY */
    int8_t closed;                 /* after load: STK_G_NO (open), STK_G_YES (closed), STK_G_ANY */
    int8_t turn;                   /* 0 any, +1 cw, -1 ccw (net turning of at least turn_min) */
    int8_t cmin, cmax;             /* corners range, -1 = any */
    float line_dir;
    float turn_min; /* radians */
    char dirs[12]; /* numpad direction subsequence, "" = none */
} stk_gates;

typedef struct {
    char id[STROKES_ID_MAX];
    int set;
    int out;          /* STK_OUT_* */
    char utf8[8];     /* STK_OUT_CHAR */
    int key;          /* STK_OUT_KEY: enum strokes_key */
    unsigned mode_op; /* STK_OUT_MODE: STROKES_MODE_SHIFT, _CAPS, _PUNCT or _EXT */
    int accent;       /* STK_OUT_ACCENT: enum strokes_accent */
    int is_tap;
    int n_variants; /* live variants (tap glyphs: 1) */
} stk_glyph;

typedef struct {
    int glyph;
    int source, line; /* where it was defined */
    int dead;         /* removed by a later replace/remove */
    int v0, nv;       /* raw vertices in the table's pool */
    stk_vec p[STK_N]; /* resampled, centroid 0, unit norm (as one 2N vector) */
    stk_feat f;
    stk_gates g;
} stk_tpl;

struct strokes_table {
    stk_glyph *g;
    int ng, capg;
    stk_tpl *tp;
    int nt, capt;
    stk_vec *v;
    int nv, capv;
    int nsrc;
};

/* ---- geometry (strokes_geom.c) ---- */
float stk_path_len(const stk_vec *p, int n);
/* m equidistant points along the polyline (m >= 2); a zero-length input fills out with in[0]. */
void stk_resample(const stk_vec *in, int n, stk_vec *out, int m);
void stk_smooth(stk_vec *p, int n);                  /* one [1 2 1]/4 pass, endpoints kept */
int stk_trim_hooks(stk_vec *p, int n, float max_len); /* returns the new count */
void stk_features(const stk_vec *p, int n, stk_feat *f);
int stk_normalise(const stk_vec *in, int n, stk_vec *out); /* 0 when degenerate */
typedef struct {
    float c, s, t; /* cos, sin, tan of the rotation bound; t = 0: no rotation */
} stk_rot;
void stk_rot_init(stk_rot *r, float max_deg);
/* The matcher's vector of a cleaned path: stretched horizontally by xstretch (aspect tolerance, before resampling so
 * the points spread along the stretched path), resampled to STK_N, centred, unit norm. 0 when degenerate. */
int stk_prepare(const stk_vec *fine, int n, float xstretch, stk_vec out[STK_N]);
float stk_score(const stk_vec *a, const stk_vec *b, int n, const stk_rot *rot); /* 0..1 */
int stk_numpad_dir(float angle);                            /* 1..9 without 5 */
float stk_numpad_angle(int digit);                          /* radians, y down; NAN-free: 0 for bad input */
float stk_angle_diff(float a, float b);                     /* |a - b| wrapped to 0..pi */

/* ---- gates and matching (strokes_match.c) ---- */
int stk_gates_pass(const stk_gates *g, const stk_feat *tf, const stk_feat *f);
/* Fills r->glyph/confidence/margin/cand/reject for a cleaned, normalised stroke in `set`. Returns the glyph index
 * accepted, or -1 (r->reject says why). */
int stk_match(const strokes_table *t, const strokes_config *cfg, int set, const stk_vec *fine, int nfine,
              const stk_feat *f, strokes_result *r);
int stk_match_tap(const strokes_table *t, int set, strokes_result *r);

/* ---- clean-up (strokes_capture.c) ---- */
enum { STK_CLEAN_STROKE = 0, STK_CLEAN_TAP, STK_CLEAN_SHORT, STK_CLEAN_DWELL };
/* Raw points of one stroke (drop-outs already healed) to the cleaned fine path (at most STK_FINE points, *nfine).
 * Returns STK_CLEAN_*. */
int stk_clean(const strokes_config *cfg, const strokes_point *raw, int n, float duration_ms, stk_vec fine[STK_FINE],
              int *nfine, float *path_mm);

/* ---- modes (strokes_engine.c) ---- */
/* Applies an accepted glyph to the mode flags and the pending accent; fills r->kind/utf8/key/mode/accent. */
void stk_modes_apply(unsigned *mode, int *accent, const stk_glyph *g, strokes_result *r);
void stk_modes_reject(const unsigned *mode, int accent, strokes_result *r);

/* ---- table helpers (strokes_table.c) ---- */
int stk_find_glyph(const strokes_table *t, const char *id, int set);
const char *stk_out_token(const stk_glyph *g, char buf[16]);
/* 1 when glyph g is a candidate in the active set: its own set; the "any" gestures in letters and digits; only the
 * backspace gesture after a punctuation or extended prefix (it leaves the prefix). */
int stk_set_matches(const stk_glyph *g, int active_set);
int stk_utf8_encode(unsigned long cp, char out[8]); /* 0 on success */
/* The code point of s when s is exactly one well-formed UTF-8 character, else 0. */
unsigned long stk_utf8_single(const char *s);

#endif
