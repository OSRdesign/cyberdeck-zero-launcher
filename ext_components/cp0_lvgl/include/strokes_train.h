/*
 * SPDX-License-Identifier: MIT
 *
 * strokes_train.h: the Strokes training session (PDA phase S6, report 042), the pure part of Settings > System >
 * Strokes training. Plain C99, no LVGL, no clock, no file access: the caller feeds it strokes, saves the overlay text it
 * returns and reloads the recogniser's tables.
 *
 * The session walks an order of glyphs: first the failing letters the caller names (the owner's), then the rest of the
 * letters, the digits, then the gestures and the punctuation (optional: the user may finish before). For each glyph the
 * user writes it STROKES_TRAIN_REPS times; every attempt is classified in the glyph's own set (strokes_classify, the
 * recogniser's own path: a punctuation glyph without its tap) and judged: recognised, refused, or another glyph. After
 * the last attempt the attempts become PERSONAL templates of that glyph in the user overlay (the overlay format of
 * tools/strokes/README.md, lines from strokes_template_line()): the attempts the recogniser missed (refused or read as
 * another glyph) that the user wrote at least twice the same way (another attempt reads it as the glyph: a shape
 * written once is a slip or a scribble, not kept), and the recognised ones only when they are not near an existing
 * variant (score under STROKES_TRAIN_DUP_SCORE); none near an attempt already taken, so the overlay stays small. The
 * templates are added to the
 * shipped ones (the glyph's score is its best variant's); with personal_only the block starts with "replace <id> <set>"
 * and only the user's templates stay (report 042 section 6: not the default).
 *
 * The overlay keeps one block per trained glyph:
 *
 *   # strokes-training <id> <set>: <n> written, <k> recognised, <a> added[ (personal only)] <time>
 *   [replace <id> <set>]
 *   <id> <set> <out> x,y x,y ...      (0 to STROKES_TRAIN_REPS lines)
 *   <blank line>
 *
 * Lines outside such blocks (hand-written ones) are kept as they are, before the blocks. A glyph that has a block is
 * done: a session started on an overlay resumes at the first glyph without one (progress = the overlay's content).
 * Finish starts a quick re-test (each glyph trained in this session once more, with the new tables), then the summary:
 * per glyph recognised x/N before (its training attempts) and the re-test, and the totals before / after.
 */
#ifndef STROKES_TRAIN_H
#define STROKES_TRAIN_H

#include "strokes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STROKES_TRAIN_REPS 4            /* attempts per glyph */
#define STROKES_TRAIN_DUP_SCORE 0.92f   /* a recognised attempt this close to a variant adds nothing */
#define STROKES_TRAIN_MAX_ITEMS 128
#define STROKES_TRAIN_MAX_PTS 1024      /* points kept per attempt (more: every second one is dropped) */
#define STROKES_TRAIN_MARK "# strokes-training "

enum strokes_train_group {
    STROKES_TRAIN_FAILING = 0, /* the letters the caller names first */
    STROKES_TRAIN_LETTERS,     /* the rest of the letters */
    STROKES_TRAIN_DIGITS,
    STROKES_TRAIN_OTHER        /* gestures and punctuation (optional) */
};

enum strokes_train_verdict {
    STROKES_TRAIN_NONE = 0,
    STROKES_TRAIN_OK,      /* recognised as the target */
    STROKES_TRAIN_REFUSED, /* refused (got: the best candidate, may be "") */
    STROKES_TRAIN_WRONG    /* recognised as another glyph (got) */
};

enum strokes_train_phase {
    STROKES_TRAIN_TRAINING = 0,
    STROKES_TRAIN_RETEST,
    STROKES_TRAIN_DONE
};

/* What a call did (bit mask). */
enum strokes_train_event {
    STROKES_TRAIN_EV_ATTEMPT = 1u, /* an attempt was judged (strokes_train_last) */
    STROKES_TRAIN_EV_MOVED = 2u,   /* the current glyph changed */
    STROKES_TRAIN_EV_OVERLAY = 4u, /* the overlay text changed: save it and reload the tables */
    STROKES_TRAIN_EV_PHASE = 8u    /* the phase changed (re-test, done) */
};

typedef struct {
    char id[STROKES_ID_MAX];
    int set;            /* enum strokes_set */
    int group;          /* enum strokes_train_group */
    char label[16];     /* what to show: the character, or a word for a key / mode ("space", "shift") */
} strokes_train_item;

typedef struct {
    int verdict;                /* enum strokes_train_verdict */
    char got[STROKES_ID_MAX];   /* the glyph recognised, or the best candidate of a refusal */
    float score, margin;
    int reject;                 /* enum strokes_reject */
    float w_mm, h_mm;           /* the stroke's size */
} strokes_train_attempt;

typedef struct {
    int tried, ok_before;   /* training attempts of this session and how many were recognised */
    int retested, ok_after; /* the re-test */
    int added;              /* templates added to the overlay in this session */
    int trained;            /* glyphs trained in this session */
} strokes_train_stats;

typedef struct strokes_train strokes_train;

/* A session on table t (the recogniser's, the overlay included) with cfg (NULL: defaults; its ppmm gives the sizes).
 * failing: ids of the letters to train first, separated by spaces (may be NULL). overlay: the overlay file's text now
 * (NULL or "": none). NULL when out of memory. */
strokes_train *strokes_train_new(const strokes_table *t, const strokes_config *cfg, const char *failing,
                                 const char *overlay);
void strokes_train_free(strokes_train *s);
/* The tables were reloaded: judge with this one from now on. */
void strokes_train_set_table(strokes_train *s, const strokes_table *t);

int strokes_train_count(const strokes_train *s);
const strokes_train_item *strokes_train_item_at(const strokes_train *s, int i); /* NULL out of range */
/* 1 when item i has a block in the overlay (trained, now or before). */
int strokes_train_is_done(const strokes_train *s, int i);
int strokes_train_phase(const strokes_train *s);
/* The current item (TRAINING: the glyph being trained; RETEST: the glyph being checked), -1 in DONE. */
int strokes_train_current(const strokes_train *s);
/* TRAINING: attempts written for the current glyph (0..STROKES_TRAIN_REPS); RETEST: glyphs checked so far. */
int strokes_train_progress(const strokes_train *s);
/* RETEST: how many glyphs it checks. */
int strokes_train_retest_count(const strokes_train *s);
/* TRAINING: attempt k (0..progress-1) of the current glyph. */
const strokes_train_attempt *strokes_train_attempt_at(const strokes_train *s, int k);
/* The last attempt judged (training or re-test), and the item it was for (-1 before the first). */
const strokes_train_attempt *strokes_train_last(const strokes_train *s);
int strokes_train_last_item(const strokes_train *s);

/* One stroke for the current glyph (the points of one contact, drop-outs healed into it). TRAINING: judged and kept;
 * the last one adds the glyph's templates and moves on (EV_OVERLAY | EV_MOVED). RETEST: judged, next glyph (the last:
 * DONE). Returns the events; 0 in DONE. */
unsigned strokes_train_stroke(strokes_train *s, const strokes_point *pts, int n);
/* The next glyph without a block (its attempts dropped; the end of the order: finish). */
unsigned strokes_train_skip(strokes_train *s);
/* The previous glyph of the order (written again: its block is replaced once its attempts are done). */
unsigned strokes_train_back(strokes_train *s);
/* The current glyph again from its first attempt, its block dropped (EV_OVERLAY when it had one). */
unsigned strokes_train_redo(strokes_train *s);
/* Every training block dropped (hand-written lines kept), the session from the first glyph, its numbers cleared. */
unsigned strokes_train_reset_all(strokes_train *s);
/* To the re-test of the glyphs trained in this session (none: DONE). */
unsigned strokes_train_finish(strokes_train *s);
/* Personal only for the glyphs trained from now on (0: added to the shipped variants, the default). */
void strokes_train_set_personal_only(strokes_train *s, int on);

/* The overlay text (owned by the session, valid until the next call that changes it). */
const char *strokes_train_overlay(const strokes_train *s);
/* The time written in the next blocks' headers (e.g. "2026-10-25 21:04"; the session has no clock). */
void strokes_train_set_time(strokes_train *s, const char *when);

/* This session's numbers for item i, and the totals. */
void strokes_train_item_stats(const strokes_train *s, int i, strokes_train_stats *out);
void strokes_train_totals(const strokes_train *s, strokes_train_stats *out);

/* The comment line of the capture log for the last attempt (README "Corpus format": a "#" line before the sample):
 * "# training phase=<train|retest> target=<id> set=<set> attempt=<k>/<n> verdict=<ok|refused|wrong> got=<id> score=..
 * margin=.. reject=<name> size_mm=<w>x<h> at=<when>". Returns its length, -1 when it does not fit. */
int strokes_train_log_comment(const strokes_train *s, const char *when, char *buf, size_t size);

const char *strokes_train_verdict_name(int verdict);

#ifdef __cplusplus
}
#endif

#endif /* STROKES_TRAIN_H */
