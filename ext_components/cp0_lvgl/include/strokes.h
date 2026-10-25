/*
 * SPDX-License-Identifier: MIT
 *
 * strokes.h: the "Strokes" single-stroke handwriting recogniser of the PDA profile (report 038 section 5, phase S1).
 *
 * Plain C99, no LVGL, no allocation after start-up, no global state, deterministic. Two layers:
 *
 *   - a glyph table (strokes_table): the shipped text file plus optional user overlays, see tools/strokes/README.md;
 *   - an engine (strokes_engine): feed it the raw touch frames of one contact (logical px, microseconds) with pen
 *     down / move / up; it heals capacitive drop-outs, cleans the stroke, matches it against the active glyph set,
 *     runs the mode machine (one-shot shift, caps lock, punctuation and extended modes, dead accents) and queues
 *     one strokes_result per stroke.
 *     Results are read with strokes_poll() (call it from a timer: a lift is final only after the heal window) or
 *     strokes_flush().
 *
 * Every result either names a glyph with its confidence and runner-up candidates, or is a rejection with a reason:
 * the recogniser never guesses below its thresholds (strokes_config.min_score / min_margin).
 */
#ifndef STROKES_H
#define STROKES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STROKES_VERSION "0.2.1"
#define STROKES_ID_MAX 16  /* glyph id incl. the terminating NUL */
#define STROKES_MAX_CAND 4 /* candidates kept in a result */
#define STROKES_DIRS_MAX 24 /* chain code string incl. NUL */

/* The writing box a stroke starts in (report 038 section 4: letters box and digits box). */
enum strokes_box { STROKES_BOX_LETTERS = 0, STROKES_BOX_DIGITS = 1 };

/* The glyph set column of the table. "any" glyphs (gestures) are candidates in the letters and digits sets; in the
 * punctuation or extended mode only the mode's set and the backspace gesture (which leaves the mode) are. */
enum strokes_set {
    STROKES_SET_LETTERS = 0,
    STROKES_SET_DIGITS = 1,
    STROKES_SET_PUNCT = 2, /* after a tap */
    STROKES_SET_ANY = 3,
    STROKES_SET_EXT = 4 /* after the extended prefix (a line down-right) */
};

enum strokes_kind {
    STROKES_NONE = 0, /* no result (empty queue) */
    STROKES_CHAR,     /* utf8 holds the text to type (case and a pending accent already applied) */
    STROKES_KEY,      /* key holds a strokes_key; utf8 holds its text when it has one (" ", "\n", "\t") */
    STROKES_MODE,     /* a mode changed (shift, caps lock, a prefix, an accent, a prefix left); nothing to type */
    STROKES_REJECT    /* not recognised: reject holds the reason; nothing to type */
};

enum strokes_key { STROKES_KEY_NONE = 0, STROKES_KEY_SPACE, STROKES_KEY_BACKSPACE, STROKES_KEY_ENTER, STROKES_KEY_TAB };

/* Mode flags (strokes_result.mode, strokes_get_mode). SHIFT = one-shot, CAPS = locked; never both. PUNCT (entered
 * by a tap) and EXT (entered by the line down-right) stay on across glyphs (never both) until the backspace gesture
 * (leaves the mode, deletes nothing), a stroke in the other box, or strokes_set_mode; a rejection keeps them; a plain
 * letter typed through one (the X of the ext set) ends it. ACCENT = a dead accent waits for the next letter only
 * (strokes_result.accent). */
enum strokes_mode {
    STROKES_MODE_SHIFT = 1u,
    STROKES_MODE_CAPS = 2u,
    STROKES_MODE_PUNCT = 4u,
    STROKES_MODE_EXT = 8u,
    STROKES_MODE_ACCENT = 16u
};

/* Dead accents (optional table section): composed with the next letter (a e i o u y n), dropped otherwise. */
enum strokes_accent {
    STROKES_ACCENT_NONE = 0,
    STROKES_ACCENT_GRAVE,
    STROKES_ACCENT_ACUTE,
    STROKES_ACCENT_CIRCUMFLEX,
    STROKES_ACCENT_TILDE,
    STROKES_ACCENT_DIAERESIS,
    STROKES_ACCENT_RING
};

enum strokes_reject {
    STROKES_REJECT_NONE = 0,
    STROKES_REJECT_SHORT,        /* longer than a tap, shorter than min_stroke_mm */
    STROKES_REJECT_DWELL,        /* the pen rested without moving for longer than a tap */
    STROKES_REJECT_NO_CANDIDATE, /* no glyph of the active set passed the gates */
    STROKES_REJECT_LOW_SCORE,    /* best score under min_score */
    STROKES_REJECT_AMBIGUOUS     /* the two best glyphs are closer than min_margin */
};

typedef struct {
    float x, y;    /* logical px, any origin, y down */
    uint64_t t_us; /* CLOCK_MONOTONIC (or any monotonic clock) in microseconds */
} strokes_point;

typedef struct {
    char glyph[STROKES_ID_MAX];
    float score; /* 0..1, 1 = identical to a template */
    int gated;   /* 1 = this glyph failed a gate (listed only when no glyph passed) */
} strokes_candidate;

typedef struct {
    int kind;      /* enum strokes_kind */
    char utf8[8];  /* text to type, NUL-terminated ("" when none) */
    int key;       /* enum strokes_key for STROKES_KEY */
    unsigned mode; /* mode flags after this result */
    int accent;    /* enum strokes_accent pending after this result (STROKES_MODE_ACCENT set when not NONE) */
    char glyph[STROKES_ID_MAX]; /* accepted glyph id; on REJECT the best candidate ("" before matching) */
    float confidence; /* score of the best glyph (0 when none) */
    float margin;     /* best score minus the runner-up glyph's score (= confidence when no runner-up) */
    int reject;       /* enum strokes_reject */
    int set;          /* enum strokes_set the stroke was matched in */
    int box;          /* enum strokes_box the stroke started in */
    int n_cand;
    strokes_candidate cand[STROKES_MAX_CAND]; /* best first; cand[0] is the best even when rejected */
    int healed;       /* drop-outs healed into this stroke */
    int n_raw;        /* raw points kept for this stroke */
    float path_mm;    /* cleaned path length */
    float duration_ms;
    char dirs[STROKES_DIRS_MAX]; /* chain code of the cleaned stroke, numpad notation (6 = right, 2 = down, ...) */
    int corners;      /* sharp turns of the cleaned stroke */
    float turn_deg;   /* net turning of the cleaned stroke, degrees, + = clockwise */
    uint64_t t_down_us, t_up_us;
} strokes_result;

/* Tunables. strokes_config_default() gives the S1 defaults (README "Tunables"); every length is in millimetres on
 * the glass, converted with ppmm, so the same table works at any density. */
typedef struct {
    float ppmm;          /* pixels per millimetre of the coordinates fed in (reference: 8.54, the 4.3" DSI) */
    float heal_ms;       /* a lift then a press within this time ... */
    float heal_mm;       /* ... and this distance from the lift point heals the drop-out ... */
    int heal_speed;      /* ... 1 = plus the distance the pen covers at its last speed during the gap */
    float tap_max_mm;    /* a tap moves less than this ... */
    float tap_max_ms;    /* ... and lasts less than this; longer still = a dwell (rejected) */
    float min_stroke_mm; /* shorter paths that are not taps are rejected (SHORT) */
    float min_score;     /* reject when the best score is under this */
    float min_margin;    /* reject when the runner-up glyph is within this */
    float rot_tol_deg;   /* a slant up to this is forgiven by the matcher (0 = none; at most 45) */
    float aspect_tol;    /* an aspect change up to this fraction is forgiven (0 = none; at most 0.5) */
    int smooth;          /* 1 = 3-tap smoothing of the fine-resampled path */
    int hook_trim;       /* 1 = drop landing / lift hooks */
} strokes_config;

typedef struct {
    char source[64]; /* file name or the name given to strokes_table_add_text */
    int line;        /* 1-based, 0 when not tied to a line */
    char msg[160];
} strokes_error;

typedef struct strokes_table strokes_table;
typedef struct strokes_engine strokes_engine;

void strokes_config_default(strokes_config *cfg);

/* ---- glyph table ---------------------------------------------------------------------------------------------- */

strokes_table *strokes_table_new(void);
void strokes_table_free(strokes_table *t);
/* Adds one source (the shipped table first, then overlays). Atomic: on error the table is unchanged and err (may be
 * NULL) says where. Returns 0 on success, -1 on error. */
int strokes_table_add_text(strokes_table *t, const char *text, const char *name, strokes_error *err);
int strokes_table_add_file(strokes_table *t, const char *path, strokes_error *err);
int strokes_table_glyph_count(const strokes_table *t);    /* glyphs that have at least one variant */
int strokes_table_template_count(const strokes_table *t); /* variants (tap glyphs count one) */
/* Glyph i (0 <= i < strokes_table_glyph_count): id, set, and the number of variants. Returns -1 when out of range. */
int strokes_table_glyph_info(const strokes_table *t, int i, char id[STROKES_ID_MAX], int *set, int *variants);

/* ---- one complete stroke, no drop-out healing, no modes (eval, training preview) ------------------------------ */

/* Classifies n points of one contact in the given set (enum strokes_set; ANY = the gestures only; PUNCT and EXT
 * also accept the backspace gesture). Fills out (kind/utf8/key from the glyph's table entry, unshifted; mode = 0;
 * accent = the glyph's own accent for an accent glyph). Returns 1 when a glyph was accepted, 0 on rejection. */
int strokes_classify(const strokes_table *t, const strokes_config *cfg, int set, const strokes_point *pts, int n,
                     strokes_result *out);

/* Formats one overlay line for a user variant of glyph `id` from a raw stroke (cleaned, resampled, scaled to
 * 0..100): "id set out x,y x,y ...\n". Returns the length written (excluding NUL), or -1 when the stroke is too short
 * or the buffer too small (S6 training uses this; the output is a valid strokes_table_add_text line). */
int strokes_template_line(const strokes_table *t, const strokes_config *cfg, const char *id, int set,
                          const strokes_point *pts, int n, char *buf, size_t size);

/* ---- engine --------------------------------------------------------------------------------------------------- */

/* The table must outlive the engine and must not change while it is used. cfg NULL = defaults. */
strokes_engine *strokes_engine_new(const strokes_table *t, const strokes_config *cfg);
void strokes_engine_free(strokes_engine *e);

void strokes_down(strokes_engine *e, int box, float x, float y, uint64_t t_us);
void strokes_move(strokes_engine *e, float x, float y, uint64_t t_us);
void strokes_up(strokes_engine *e, uint64_t t_us);
/* Finalises a lifted stroke once now_us is past the heal window, then pops one result. Returns 1 and fills out
 * when a result was ready, 0 otherwise. Call until it returns 0. */
int strokes_poll(strokes_engine *e, uint64_t now_us, strokes_result *out);
/* As poll, but finalises a lifted stroke at once (end of a replay, zone closing). */
int strokes_flush(strokes_engine *e, strokes_result *out);
/* Drops the stroke in progress (down or within the heal window) without a result. Modes are kept. */
void strokes_cancel(strokes_engine *e);
/* 1 while a contact is down or a lifted stroke waits for the heal window. */
int strokes_busy(const strokes_engine *e);
/* The mode flags now, without polling (for the mode line): they change when a result is queued, and a press in the
 * other box clears PUNCT / EXT at once. */
unsigned strokes_get_mode(const strokes_engine *e);
/* e.g. 0 when the zone opens (or closes). A PUNCT / EXT set here belongs to the box of the next stroke. STROKES_MODE_ACCENT can only be kept or cleared (no accent is invented); PUNCT wins
 * over EXT and CAPS over SHIFT. */
void strokes_set_mode(strokes_engine *e, unsigned mode);

/* ---- names ---------------------------------------------------------------------------------------------------- */

const char *strokes_kind_name(int kind);
const char *strokes_reject_name(int reject);
const char *strokes_set_name(int set);
int strokes_set_from_name(const char *name); /* -1 when unknown */
const char *strokes_key_name(int key);       /* "KEY_SPACE", ... */
const char *strokes_accent_name(int accent); /* "grave", ...; "" for none */
/* The Linux input-event code of a strokes_key (KEY_SPACE 57, KEY_BACKSPACE 14, KEY_ENTER 28, KEY_TAB 15), 0 for none.
 * Characters go through the on-screen keyboard's US table instead (S3). */
int strokes_key_linux_code(int key);

#ifdef __cplusplus
}
#endif

#endif /* STROKES_H */
