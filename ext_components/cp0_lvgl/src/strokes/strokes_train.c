/*
 * SPDX-License-Identifier: MIT
 *
 * The Strokes training session (see strokes_train.h; report 042, PDA phase S6).
 */
#include "strokes_train.h"

#include "strokes_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char id[STROKES_ID_MAX];
    int set;
    char *text;   /* the whole block: its header, the lines, the blank line after */
    int lines;    /* template lines in it */
    int written, recognised; /* from its header (-1: not written there) */
    int retested, retest_ok; /* from its "# re-test" line (0 / 0: none) */
} block;

typedef struct {
    int tried, ok_before, retested, ok_after, added, trained;
} item_stats;

struct strokes_train {
    const strokes_table *t;
    strokes_config cfg;
    strokes_train_item items[STROKES_TRAIN_MAX_ITEMS];
    int n_items;
    char *foreign; /* the overlay's lines outside the blocks */
    block *blocks;
    int n_blocks, cap_blocks;
    char *overlay; /* composed by compose() */
    int phase, cur;
    strokes_point pts[STROKES_TRAIN_REPS][STROKES_TRAIN_MAX_PTS];
    int npts[STROKES_TRAIN_REPS];
    strokes_train_attempt att[STROKES_TRAIN_REPS];
    int n_att;
    strokes_train_attempt last;
    int last_phase, last_pos, last_total, last_item;
    int retest[STROKES_TRAIN_MAX_ITEMS];
    int n_retest, retest_pos;
    item_stats st[STROKES_TRAIN_MAX_ITEMS];
    int personal_only;
    int picked; /* the glyph being trained was picked: the picker comes back after it */
    char when[40];
};

/* ---- text helpers ---- */

static char *dup_str(const char *s)
{
    size_t n = strlen(s ? s : "");
    char *d = malloc(n + 1);
    if (d)
        memcpy(d, s ? s : "", n + 1);
    return d;
}

typedef struct {
    char *p;
    size_t n, cap;
    int oom;
} sbuf;

static void sb_add(sbuf *b, const char *s, size_t n)
{
    if (b->oom)
        return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n + 1)
            cap *= 2;
        char *p = realloc(b->p, cap);
        if (!p) {
            b->oom = 1;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

static void sb_str(sbuf *b, const char *s)
{
    sb_add(b, s, strlen(s));
}

/* ---- the overlay: hand-written lines and the training blocks ---- */

static int find_block(const strokes_train *s, const char *id, int set)
{
    for (int i = 0; i < s->n_blocks; ++i)
        if (s->blocks[i].set == set && strcmp(s->blocks[i].id, id) == 0)
            return i;
    return -1;
}

static void drop_block(strokes_train *s, int bi)
{
    free(s->blocks[bi].text);
    memmove(&s->blocks[bi], &s->blocks[bi + 1], (size_t)(s->n_blocks - bi - 1) * sizeof *s->blocks);
    s->n_blocks--;
}

/* The numbers a block's text says: its header's "<n> written, <k> recognised" and its last "# re-test <a>/<b>". */
static void block_stats(block *b)
{
    b->written = b->recognised = -1;
    b->retested = b->retest_ok = 0;
    const char *colon = strchr(b->text, ':');
    const char *eol = strchr(b->text, '\n');
    int n = 0, k = 0;
    if (colon && (!eol || colon < eol) && sscanf(colon + 1, " %d written, %d recognised", &n, &k) == 2 && n > 0) {
        b->written = n;
        b->recognised = k;
    }
    const size_t ml = strlen(STROKES_TRAIN_RETEST_MARK);
    for (const char *p = b->text; (p = strstr(p, STROKES_TRAIN_RETEST_MARK)) != NULL; p += ml) {
        int a = 0, c = 0;
        if (sscanf(p + ml, "%d/%d", &a, &c) == 2 && c > 0 && a >= 0 && a <= c) {
            b->retest_ok = a;
            b->retested = c;
        }
    }
}

/* Puts a block (replacing the glyph's earlier one in place, else at the end). 0, -1 out of memory. */
static int put_block(strokes_train *s, const char *id, int set, const char *text, int lines)
{
    char *copy = dup_str(text);
    if (!copy)
        return -1;
    int bi = find_block(s, id, set);
    if (bi >= 0) {
        free(s->blocks[bi].text);
        s->blocks[bi].text = copy;
        s->blocks[bi].lines = lines;
        block_stats(&s->blocks[bi]);
        return 0;
    }
    if (s->n_blocks == s->cap_blocks) {
        int cap = s->cap_blocks ? 2 * s->cap_blocks : 32;
        block *nb = realloc(s->blocks, (size_t)cap * sizeof *nb);
        if (!nb) {
            free(copy);
            return -1;
        }
        s->blocks = nb;
        s->cap_blocks = cap;
    }
    block *b = &s->blocks[s->n_blocks++];
    memset(b, 0, sizeof *b);
    snprintf(b->id, sizeof b->id, "%s", id);
    b->set = set;
    b->text = copy;
    b->lines = lines;
    block_stats(b);
    return 0;
}

/* The block's re-test line becomes "# re-test <ok>/<n> <time>" (an earlier one dropped). 0, -1. */
static int set_retest(strokes_train *s, int bi, int ok, int n)
{
    block *b = &s->blocks[bi];
    sbuf out = {0};
    sb_str(&out, "");
    for (const char *p = b->text; *p;) {
        const char *eol = strchr(p, '\n');
        const size_t len = eol ? (size_t)(eol - p) + 1 : strlen(p);
        const int blank = len <= 1 || (len == 2 && p[0] == '\r');
        if (strncmp(p, STROKES_TRAIN_RETEST_MARK, strlen(STROKES_TRAIN_RETEST_MARK)) != 0 && !blank)
            sb_add(&out, p, len);
        p += len;
    }
    if (out.n > 0 && out.p[out.n - 1] != '\n')
        sb_str(&out, "\n");
    char line[96];
    snprintf(line, sizeof line, "%s%d/%d%s%s\n\n", STROKES_TRAIN_RETEST_MARK, ok, n, s->when[0] ? " " : "", s->when);
    sb_str(&out, line);
    if (out.oom) {
        free(out.p);
        return -1;
    }
    free(b->text);
    b->text = out.p;
    block_stats(b);
    return 0;
}

static void compose(strokes_train *s)
{
    sbuf b = {0};
    sb_str(&b, "");
    sb_str(&b, s->foreign ? s->foreign : "");
    if (b.n > 0 && b.p[b.n - 1] != '\n')
        sb_str(&b, "\n");
    if (b.n > 0 && s->n_blocks > 0 && !(b.n >= 2 && b.p[b.n - 2] == '\n'))
        sb_str(&b, "\n");
    for (int i = 0; i < s->n_blocks; ++i)
        sb_str(&b, s->blocks[i].text);
    if (b.oom) {
        free(b.p);
        return; /* the previous text stays */
    }
    free(s->overlay);
    s->overlay = b.p;
}

/* "# strokes-training <id> <set>..." -> id, set; 0 when the line is not a block header */
static int parse_mark(const char *line, char id[STROKES_ID_MAX], int *set)
{
    size_t ml = strlen(STROKES_TRAIN_MARK);
    if (strncmp(line, STROKES_TRAIN_MARK, ml) != 0)
        return 0;
    char a[64], b[64];
    if (sscanf(line + ml, "%63s %63[^: \t\r\n]", a, b) != 2)
        return 0;
    const size_t al = strlen(a);
    int st = strokes_set_from_name(b);
    if (st < 0 || al >= STROKES_ID_MAX)
        return 0;
    memcpy(id, a, al + 1);
    *set = st;
    return 1;
}

static int is_blank(const char *line)
{
    for (; *line; ++line)
        if (*line != ' ' && *line != '\t' && *line != '\r' && *line != '\n')
            return 0;
    return 1;
}

/* Splits an overlay into the hand-written lines and the blocks. */
static void parse_overlay(strokes_train *s, const char *text)
{
    sbuf foreign = {0}, cur = {0};
    sb_str(&foreign, "");
    char id[STROKES_ID_MAX] = "";
    int set = -1, in_block = 0, lines = 0;
    const char *p = text ? text : "";
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) + 1 : strlen(p);
        char line[1024];
        size_t cl = len < sizeof line - 1 ? len : sizeof line - 1;
        memcpy(line, p, cl);
        line[cl] = 0;
        char nid[STROKES_ID_MAX];
        int nset;
        if (parse_mark(line, nid, &nset)) {
            if (in_block && cur.p)
                put_block(s, id, set, cur.p, lines);
            free(cur.p);
            memset(&cur, 0, sizeof cur);
            snprintf(id, sizeof id, "%s", nid);
            set = nset;
            in_block = 1;
            lines = 0;
            sb_add(&cur, p, len);
            if (!eol)
                sb_str(&cur, "\n");
        } else if (in_block) {
            sb_add(&cur, p, len);
            if (!eol)
                sb_str(&cur, "\n");
            if (is_blank(line)) {
                put_block(s, id, set, cur.p, lines);
                free(cur.p);
                memset(&cur, 0, sizeof cur);
                in_block = 0;
            } else if (line[0] != '#' && strncmp(line, "replace", 7) != 0) {
                lines++;
            }
        } else {
            sb_add(&foreign, p, len);
        }
        p += len;
    }
    if (in_block && cur.p) {
        sb_str(&cur, "\n"); /* the blank line that ends a block */
        put_block(s, id, set, cur.p, lines);
    }
    free(cur.p);
    s->foreign = foreign.oom ? dup_str("") : foreign.p;
    if (foreign.oom)
        free(foreign.p);
}

/* ---- the order ---- */

static void item_label(const stk_glyph *g, char out[16])
{
    if (g->out == STK_OUT_CHAR) {
        snprintf(out, 16, "%s", g->utf8);
    } else if (g->out == STK_OUT_KEY) {
        const char *w = g->key == STROKES_KEY_SPACE       ? "space"
                        : g->key == STROKES_KEY_BACKSPACE ? "backspace"
                        : g->key == STROKES_KEY_ENTER     ? "enter"
                        : g->key == STROKES_KEY_TAB       ? "tab"
                                                          : "key";
        snprintf(out, 16, "%s", w);
    } else if (g->out == STK_OUT_MODE) {
        const char *w = g->mode_op == STROKES_MODE_SHIFT   ? "shift"
                        : g->mode_op == STROKES_MODE_CAPS  ? "caps"
                        : g->mode_op == STROKES_MODE_PUNCT ? "punct"
                                                           : "ext";
        snprintf(out, 16, "%s", w);
    } else {
        snprintf(out, 16, "%s", g->id);
    }
}

static int trainable(const stk_glyph *g)
{
    return g->n_variants > 0 && !g->is_tap && g->out != STK_OUT_ACCENT;
}

static void add_item(strokes_train *s, const stk_glyph *g, int group)
{
    if (s->n_items >= STROKES_TRAIN_MAX_ITEMS)
        return;
    for (int i = 0; i < s->n_items; ++i)
        if (s->items[i].set == g->set && strcmp(s->items[i].id, g->id) == 0)
            return;
    strokes_train_item *it = &s->items[s->n_items++];
    memset(it, 0, sizeof *it);
    snprintf(it->id, sizeof it->id, "%s", g->id);
    it->set = g->set;
    it->group = group;
    item_label(g, it->label);
}

static void build_order(strokes_train *s, const char *failing)
{
    const strokes_table *t = s->t;
    s->n_items = 0;
    if (!t)
        return;
    for (const char *p = failing ? failing : ""; *p;) {
        while (*p == ' ' || *p == ',' || *p == '\t')
            p++;
        const char *e = p;
        while (*e && *e != ' ' && *e != ',' && *e != '\t')
            e++;
        if (e > p && (size_t)(e - p) < STROKES_ID_MAX) {
            char tok[STROKES_ID_MAX];
            memcpy(tok, p, (size_t)(e - p));
            tok[e - p] = 0;
            const int gi = stk_find_glyph(t, tok, STROKES_SET_LETTERS);
            if (gi >= 0 && trainable(&t->g[gi]))
                add_item(s, &t->g[gi], STROKES_TRAIN_FAILING);
        }
        p = e;
    }
    static const struct {
        int set, group;
    } pass[] = {{STROKES_SET_LETTERS, STROKES_TRAIN_LETTERS},
                {STROKES_SET_DIGITS, STROKES_TRAIN_DIGITS},
                {STROKES_SET_ANY, STROKES_TRAIN_OTHER},
                {STROKES_SET_PUNCT, STROKES_TRAIN_OTHER}};
    for (size_t k = 0; k < sizeof pass / sizeof pass[0]; ++k)
        for (int gi = 0; gi < t->ng; ++gi)
            if (t->g[gi].set == pass[k].set && trainable(&t->g[gi]))
                add_item(s, &t->g[gi], pass[k].group);
}

/* the set a glyph is judged in: as it is written (the gestures in the letters box; a punctuation glyph after its tap) */
static int judge_set(int set)
{
    return set == STROKES_SET_ANY ? STROKES_SET_LETTERS : set;
}

/* ---- the session ---- */

strokes_train *strokes_train_new(const strokes_table *t, const strokes_config *cfg, const char *failing,
                                 const char *overlay)
{
    strokes_train *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->t = t;
    if (cfg)
        s->cfg = *cfg;
    else
        strokes_config_default(&s->cfg);
    parse_overlay(s, overlay);
    compose(s);
    if (!s->foreign || !s->overlay) {
        strokes_train_free(s);
        return NULL;
    }
    build_order(s, failing);
    s->phase = STROKES_TRAIN_TRAINING;
    s->cur = -1;
    for (int i = 0; i < s->n_items; ++i)
        if (!strokes_train_is_done(s, i)) {
            s->cur = i;
            break;
        }
    if (s->cur < 0)
        s->cur = 0; /* everything trained before: start again from the first glyph */
    if (s->n_items == 0)
        s->phase = STROKES_TRAIN_DONE;
    return s;
}

void strokes_train_free(strokes_train *s)
{
    if (!s)
        return;
    for (int i = 0; i < s->n_blocks; ++i)
        free(s->blocks[i].text);
    free(s->blocks);
    free(s->foreign);
    free(s->overlay);
    free(s);
}

void strokes_train_set_table(strokes_train *s, const strokes_table *t)
{
    if (s)
        s->t = t;
}

int strokes_train_count(const strokes_train *s)
{
    return s ? s->n_items : 0;
}

const strokes_train_item *strokes_train_item_at(const strokes_train *s, int i)
{
    return s && i >= 0 && i < s->n_items ? &s->items[i] : NULL;
}

int strokes_train_is_done(const strokes_train *s, int i)
{
    if (!s || i < 0 || i >= s->n_items)
        return 0;
    return find_block(s, s->items[i].id, s->items[i].set) >= 0;
}

int strokes_train_phase(const strokes_train *s)
{
    return s ? s->phase : STROKES_TRAIN_DONE;
}

int strokes_train_current(const strokes_train *s)
{
    return s && s->phase != STROKES_TRAIN_DONE && s->phase != STROKES_TRAIN_PICK ? s->cur : -1;
}

int strokes_train_picked(const strokes_train *s)
{
    return s && s->phase == STROKES_TRAIN_TRAINING && s->picked;
}

int strokes_train_mark(const strokes_train *s, int i)
{
    if (!s || i < 0 || i >= s->n_items)
        return STROKES_TRAIN_UNTRAINED;
    const int bi = find_block(s, s->items[i].id, s->items[i].set);
    if (bi < 0)
        return STROKES_TRAIN_UNTRAINED;
    const block *b = &s->blocks[bi];
    /* the latest rate known: the re-test's, else the training attempts' */
    int ok = b->recognised, n = b->written;
    if (b->retested > 0) {
        ok = b->retest_ok;
        n = b->retested;
    }
    if (n > 0 && ok * 100 < STROKES_TRAIN_HARD_PCT * n)
        return STROKES_TRAIN_HARD;
    return STROKES_TRAIN_TRAINED;
}

int strokes_train_progress(const strokes_train *s)
{
    if (!s)
        return 0;
    return s->phase == STROKES_TRAIN_RETEST ? s->retest_pos : s->n_att;
}

int strokes_train_retest_count(const strokes_train *s)
{
    return s ? s->n_retest : 0;
}

const strokes_train_attempt *strokes_train_attempt_at(const strokes_train *s, int k)
{
    return s && s->phase == STROKES_TRAIN_TRAINING && k >= 0 && k < s->n_att ? &s->att[k] : NULL;
}

const strokes_train_attempt *strokes_train_last(const strokes_train *s)
{
    return s && s->last.verdict != STROKES_TRAIN_NONE ? &s->last : NULL;
}

int strokes_train_last_item(const strokes_train *s)
{
    return s && s->last.verdict != STROKES_TRAIN_NONE ? s->last_item : -1;
}

void strokes_train_set_personal_only(strokes_train *s, int on)
{
    if (s)
        s->personal_only = on != 0;
}

const char *strokes_train_overlay(const strokes_train *s)
{
    return s && s->overlay ? s->overlay : "";
}

void strokes_train_set_time(strokes_train *s, const char *when)
{
    if (s)
        snprintf(s->when, sizeof s->when, "%s", when ? when : "");
}

const char *strokes_train_verdict_name(int verdict)
{
    switch (verdict) {
    case STROKES_TRAIN_OK: return "ok";
    case STROKES_TRAIN_REFUSED: return "refused";
    case STROKES_TRAIN_WRONG: return "wrong";
    default: return "none";
    }
}

/* Judges one stroke for item i; fills a. */
static void judge(const strokes_train *s, int i, const strokes_point *pts, int n, strokes_train_attempt *a)
{
    memset(a, 0, sizeof *a);
    const strokes_train_item *it = &s->items[i];
    strokes_result r;
    const int ok = strokes_classify(s->t, &s->cfg, judge_set(it->set), pts, n, &r);
    a->score = r.confidence;
    a->margin = r.margin;
    a->reject = r.reject;
    if (ok) {
        snprintf(a->got, sizeof a->got, "%s", r.glyph);
        a->verdict = strcmp(r.glyph, it->id) == 0 ? STROKES_TRAIN_OK : STROKES_TRAIN_WRONG;
    } else {
        snprintf(a->got, sizeof a->got, "%s", r.n_cand > 0 ? r.cand[0].glyph : r.glyph);
        a->verdict = STROKES_TRAIN_REFUSED;
    }
    float minx = 0, maxx = 0, miny = 0, maxy = 0;
    for (int k = 0; k < n; ++k) {
        if (k == 0 || pts[k].x < minx) minx = pts[k].x;
        if (k == 0 || pts[k].x > maxx) maxx = pts[k].x;
        if (k == 0 || pts[k].y < miny) miny = pts[k].y;
        if (k == 0 || pts[k].y > maxy) maxy = pts[k].y;
    }
    const float ppmm = s->cfg.ppmm > 0.1f ? s->cfg.ppmm : 8.54f;
    a->w_mm = (maxx - minx) / ppmm;
    a->h_mm = (maxy - miny) / ppmm;
}

static void remember(strokes_train *s, const strokes_train_attempt *a, int item, int phase, int pos, int total)
{
    s->last = *a;
    s->last_item = item;
    s->last_phase = phase;
    s->last_pos = pos;
    s->last_total = total;
}

/* The next item after `from` without a block, -1 when none. */
static int next_open(const strokes_train *s, int from)
{
    for (int i = from + 1; i < s->n_items; ++i)
        if (!strokes_train_is_done(s, i))
            return i;
    return -1;
}

/* The score of pts against the templates in `chosen` (a table of the glyph's lines taken so far), 0 when none. */
static float score_against(const strokes_table *chosen, const strokes_config *cfg, const strokes_train_item *it,
                           const strokes_point *pts, int n)
{
    if (!chosen || strokes_table_template_count(chosen) == 0)
        return 0.0f;
    strokes_result r;
    (void)strokes_classify(chosen, cfg, judge_set(it->set), pts, n, &r);
    if (strcmp(r.glyph, it->id) == 0)
        return r.confidence;
    for (int k = 0; k < r.n_cand; ++k)
        if (strcmp(r.cand[k].glyph, it->id) == 0 && !r.cand[k].gated)
            return r.cand[k].score;
    return 0.0f;
}

/* 1 when another attempt of the glyph has the shape of attempt k (it would read k as the glyph): a miss the user wrote
 * once only (a slip, a scribble) is not a way they write the glyph. */
static int supported(const strokes_train *s, const strokes_train_item *it, int k, char lines[][STK_N * 24 + 96],
                     const int *usable)
{
    for (int j = 0; j < s->n_att; ++j) {
        if (j == k || !usable[j])
            continue;
        strokes_table *one = strokes_table_new();
        strokes_error err;
        float sc = 0.0f;
        if (one && strokes_table_add_text(one, lines[j], "attempt", &err) == 0)
            sc = score_against(one, &s->cfg, it, s->pts[k], s->npts[k]);
        strokes_table_free(one);
        if (sc >= s->cfg.min_score)
            return 1;
    }
    return 0;
}

/* The glyph's attempts become its block: the misses the user wrote at least twice the same way, and the recognised
 * attempts only when they add a shape (not near a shipped variant nor an attempt already taken). */
static unsigned close_glyph(strokes_train *s)
{
    const int i = s->cur;
    const strokes_train_item *it = &s->items[i];
    strokes_table *chosen = strokes_table_new();
    sbuf lines = {0};
    sb_str(&lines, "");
    int added = 0, recognised = 0;
    char line_of[STROKES_TRAIN_REPS][STK_N * 24 + 96];
    int usable[STROKES_TRAIN_REPS];
    for (int k = 0; k < s->n_att; ++k) {
        /* a tap, too short: no shape */
        usable[k] = strokes_template_line(s->t, &s->cfg, it->id, it->set, s->pts[k], s->npts[k], line_of[k],
                                          sizeof line_of[k]) >= 0;
        recognised += s->att[k].verdict == STROKES_TRAIN_OK;
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int k = 0; k < s->n_att && chosen; ++k) {
            const strokes_train_attempt *a = &s->att[k];
            if (!usable[k])
                continue;
            const int ok = a->verdict == STROKES_TRAIN_OK;
            if (strokes_table_template_count(s->t) + added >= STROKES_TRAIN_MAX_TEMPLATES)
                break; /* the table's limit: nothing more is kept */
            if (pass == 0) {
                if (ok && a->score >= STROKES_TRAIN_DUP_SCORE)
                    continue; /* the shipped variants know this shape */
                if (!ok && !supported(s, it, k, line_of, usable))
                    continue; /* written once that way */
                if (score_against(chosen, &s->cfg, it, s->pts[k], s->npts[k]) >= STROKES_TRAIN_DUP_SCORE)
                    continue; /* near an attempt already taken */
            } else if (added > 0 || !s->personal_only) {
                break; /* the second pass only fills an empty personal-only block */
            } else if (!ok && !supported(s, it, k, line_of, usable)) {
                continue;
            }
            const char *line = line_of[k];
            strokes_error err;
            if (strokes_table_add_text(chosen, line, "attempt", &err) != 0)
                continue;
            sb_str(&lines, line);
            added++;
        }
    }
    strokes_table_free(chosen);
    const int personal = s->personal_only && added > 0;
    char head[256];
    snprintf(head, sizeof head, "%s%s %s: %d written, %d recognised, %d added%s%s%s\n", STROKES_TRAIN_MARK, it->id,
             strokes_set_name(it->set), s->n_att, recognised, added, personal ? " (personal only)" : "",
             s->when[0] ? " " : "", s->when);
    sbuf block_text = {0};
    sb_str(&block_text, head);
    if (personal) {
        char rep[64];
        snprintf(rep, sizeof rep, "replace %s %s\n", it->id, strokes_set_name(it->set));
        sb_str(&block_text, rep);
    }
    sb_str(&block_text, lines.p ? lines.p : "");
    sb_str(&block_text, "\n");
    unsigned ev = 0;
    if (!block_text.oom && !lines.oom && put_block(s, it->id, it->set, block_text.p, added) == 0) {
        compose(s);
        ev |= STROKES_TRAIN_EV_OVERLAY;
    }
    free(block_text.p);
    free(lines.p);
    item_stats *st = &s->st[i];
    st->tried = s->n_att;
    st->ok_before = recognised;
    st->added = added;
    st->trained = 1;
    st->retested = st->ok_after = 0;
    return ev;
}

static unsigned go_to(strokes_train *s, int i)
{
    s->n_att = 0;
    if (i < 0)
        return strokes_train_finish(s) | STROKES_TRAIN_EV_MOVED;
    s->cur = i;
    return STROKES_TRAIN_EV_MOVED;
}

unsigned strokes_train_stroke(strokes_train *s, const strokes_point *pts, int n)
{
    if (!s || !pts || n <= 0 || s->phase == STROKES_TRAIN_DONE || s->cur < 0 || !s->t)
        return 0;
    strokes_train_attempt a;
    if (s->phase == STROKES_TRAIN_RETEST) {
        judge(s, s->cur, pts, n, &a);
        remember(s, &a, s->cur, STROKES_TRAIN_RETEST, s->retest_pos + 1, s->n_retest);
        s->st[s->cur].retested++;
        if (a.verdict == STROKES_TRAIN_OK)
            s->st[s->cur].ok_after++;
        unsigned ev = STROKES_TRAIN_EV_ATTEMPT | STROKES_TRAIN_EV_MOVED;
        /* the re-test goes into the glyph's block (the picker marks a glyph by its latest rate) */
        const int bi = find_block(s, s->items[s->cur].id, s->items[s->cur].set);
        if (bi >= 0 && set_retest(s, bi, s->st[s->cur].ok_after, s->st[s->cur].retested) == 0) {
            compose(s);
            ev |= STROKES_TRAIN_EV_OVERLAY;
        }
        s->retest_pos++;
        if (s->retest_pos >= s->n_retest) {
            s->phase = STROKES_TRAIN_DONE;
            return ev | STROKES_TRAIN_EV_PHASE;
        }
        s->cur = s->retest[s->retest_pos];
        return ev;
    }
    if (s->n_att >= STROKES_TRAIN_REPS)
        return 0;
    judge(s, s->cur, pts, n, &a);
    const int k = s->n_att;
    /* the points kept (beyond the limit, evenly thinned; the last one kept) */
    const int step = (n + STROKES_TRAIN_MAX_PTS - 1) / STROKES_TRAIN_MAX_PTS;
    int m = 0;
    for (int j = 0; j < n && m < STROKES_TRAIN_MAX_PTS; j += step)
        s->pts[k][m++] = pts[j];
    if (step > 1 && m > 0)
        s->pts[k][m - 1] = pts[n - 1];
    s->npts[k] = m;
    s->att[k] = a;
    s->n_att++;
    remember(s, &a, s->cur, STROKES_TRAIN_TRAINING, s->n_att, STROKES_TRAIN_REPS);
    unsigned ev = STROKES_TRAIN_EV_ATTEMPT;
    if (s->n_att >= STROKES_TRAIN_REPS) {
        ev |= close_glyph(s);
        if (s->picked) /* a picked glyph: back to the picker */
            ev |= strokes_train_open_picker(s);
        else
            ev |= go_to(s, next_open(s, s->cur));
    }
    return ev;
}

unsigned strokes_train_open_picker(strokes_train *s)
{
    if (!s || (s->phase != STROKES_TRAIN_TRAINING && s->phase != STROKES_TRAIN_PICK) || s->n_items == 0)
        return 0;
    s->n_att = 0;
    s->picked = 0;
    const int was = s->phase;
    s->phase = STROKES_TRAIN_PICK;
    return STROKES_TRAIN_EV_MOVED | (was != STROKES_TRAIN_PICK ? STROKES_TRAIN_EV_PHASE : 0u);
}

unsigned strokes_train_pick(strokes_train *s, int i)
{
    if (!s || i < 0 || i >= s->n_items || (s->phase != STROKES_TRAIN_PICK && s->phase != STROKES_TRAIN_TRAINING))
        return 0;
    const int was = s->phase;
    s->phase = STROKES_TRAIN_TRAINING;
    s->cur = i;
    s->n_att = 0;
    s->picked = 1;
    return STROKES_TRAIN_EV_MOVED | (was != STROKES_TRAIN_TRAINING ? STROKES_TRAIN_EV_PHASE : 0u);
}

unsigned strokes_train_resume(strokes_train *s)
{
    if (!s || s->phase != STROKES_TRAIN_PICK)
        return 0;
    s->phase = STROKES_TRAIN_TRAINING;
    s->picked = 0;
    s->n_att = 0;
    const int open = next_open(s, -1);
    s->cur = open >= 0 ? open : 0;
    return STROKES_TRAIN_EV_MOVED | STROKES_TRAIN_EV_PHASE;
}

unsigned strokes_train_skip(strokes_train *s)
{
    if (!s || s->phase == STROKES_TRAIN_DONE)
        return 0;
    if (s->phase == STROKES_TRAIN_RETEST) {
        s->retest_pos++;
        if (s->retest_pos >= s->n_retest) {
            s->phase = STROKES_TRAIN_DONE;
            return STROKES_TRAIN_EV_MOVED | STROKES_TRAIN_EV_PHASE;
        }
        s->cur = s->retest[s->retest_pos];
        return STROKES_TRAIN_EV_MOVED;
    }
    if (s->phase == STROKES_TRAIN_PICK)
        return 0;
    if (s->picked)
        return strokes_train_open_picker(s);
    return go_to(s, next_open(s, s->cur));
}

unsigned strokes_train_back(strokes_train *s)
{
    if (!s || s->phase != STROKES_TRAIN_TRAINING)
        return 0;
    if (s->picked)
        return strokes_train_open_picker(s);
    if (s->cur <= 0)
        return 0;
    return go_to(s, s->cur - 1);
}

unsigned strokes_train_redo(strokes_train *s)
{
    if (!s || s->phase != STROKES_TRAIN_TRAINING || s->cur < 0)
        return 0;
    unsigned ev = 0;
    s->n_att = 0;
    const int bi = find_block(s, s->items[s->cur].id, s->items[s->cur].set);
    if (bi >= 0) {
        drop_block(s, bi);
        compose(s);
        ev |= STROKES_TRAIN_EV_OVERLAY;
    }
    memset(&s->st[s->cur], 0, sizeof s->st[s->cur]);
    return ev | STROKES_TRAIN_EV_MOVED;
}

unsigned strokes_train_reset_all(strokes_train *s)
{
    if (!s)
        return 0;
    unsigned ev = STROKES_TRAIN_EV_MOVED;
    if (s->n_blocks > 0) {
        while (s->n_blocks > 0)
            drop_block(s, s->n_blocks - 1);
        compose(s);
        ev |= STROKES_TRAIN_EV_OVERLAY;
    }
    if (s->phase != STROKES_TRAIN_TRAINING)
        ev |= STROKES_TRAIN_EV_PHASE;
    memset(s->st, 0, sizeof s->st);
    memset(&s->last, 0, sizeof s->last);
    s->phase = s->n_items > 0 ? STROKES_TRAIN_TRAINING : STROKES_TRAIN_DONE;
    s->cur = 0;
    s->n_att = 0;
    s->picked = 0;
    s->n_retest = s->retest_pos = 0;
    return ev;
}

unsigned strokes_train_finish(strokes_train *s)
{
    if (!s || s->phase == STROKES_TRAIN_DONE)
        return 0;
    if (s->phase == STROKES_TRAIN_RETEST) { /* finish during the re-test: the summary */
        s->phase = STROKES_TRAIN_DONE;
        return STROKES_TRAIN_EV_PHASE | STROKES_TRAIN_EV_MOVED;
    }
    s->n_att = 0;
    s->picked = 0;
    s->n_retest = 0;
    for (int i = 0; i < s->n_items; ++i)
        if (s->st[i].trained)
            s->retest[s->n_retest++] = i;
    s->retest_pos = 0;
    if (s->n_retest == 0) {
        s->phase = STROKES_TRAIN_DONE;
    } else {
        s->phase = STROKES_TRAIN_RETEST;
        s->cur = s->retest[0];
    }
    return STROKES_TRAIN_EV_PHASE | STROKES_TRAIN_EV_MOVED;
}

void strokes_train_item_stats(const strokes_train *s, int i, strokes_train_stats *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof *out);
    if (!s || i < 0 || i >= s->n_items)
        return;
    const item_stats *st = &s->st[i];
    out->tried = st->tried;
    out->ok_before = st->ok_before;
    out->retested = st->retested;
    out->ok_after = st->ok_after;
    out->added = st->added;
    out->trained = st->trained;
}

void strokes_train_totals(const strokes_train *s, strokes_train_stats *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof *out);
    for (int i = 0; s && i < s->n_items; ++i) {
        out->tried += s->st[i].tried;
        out->ok_before += s->st[i].ok_before;
        out->retested += s->st[i].retested;
        out->ok_after += s->st[i].ok_after;
        out->added += s->st[i].added;
        out->trained += s->st[i].trained;
    }
}

int strokes_train_log_comment(const strokes_train *s, const char *when, char *buf, size_t size)
{
    if (!s || !buf || size == 0 || s->last.verdict == STROKES_TRAIN_NONE || s->last_item < 0 ||
        s->last_item >= s->n_items)
        return -1;
    const strokes_train_item *it = &s->items[s->last_item];
    const strokes_train_attempt *a = &s->last;
    /* hundredths as integers: the line never depends on the C locale's decimal separator */
    const long sc = lroundf(a->score * 1000.0f), mg = lroundf(a->margin * 1000.0f);
    const long w = lroundf(a->w_mm * 10.0f), h = lroundf(a->h_mm * 10.0f);
    const int len = snprintf(buf, size,
                             "# training phase=%s target=%s set=%s attempt=%d/%d verdict=%s got=%s score=%ld.%03ld "
                             "margin=%s%ld.%03ld reject=%s size_mm=%ld.%ldx%ld.%ld at=%s",
                             s->last_phase == STROKES_TRAIN_RETEST ? "retest" : "train", it->id,
                             strokes_set_name(it->set), s->last_pos, s->last_total,
                             strokes_train_verdict_name(a->verdict), a->got[0] ? a->got : "-", sc / 1000, sc % 1000,
                             mg < 0 ? "-" : "", labs(mg) / 1000, labs(mg) % 1000, strokes_reject_name(a->reject),
                             w / 10, w % 10, h / 10, h % 10, when && when[0] ? when : "-");
    return len < 0 || (size_t)len >= size ? -1 : len;
}
