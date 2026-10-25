/*
 * SPDX-License-Identifier: MIT
 *
 * The Strokes engine: raw frames of one contact in, one result per stroke out. Heals capacitive drop-outs (a lift
 * then a press within heal_ms, near the lift point or within reach at the pen speed), runs clean-up and the
 * matcher in the set of the box (or the punctuation / extended set after its prefix), and the mode machine:
 *
 *   shift gesture   off -> one-shot shift -> caps lock -> off
 *   one-shot shift  consumed by the next character (letters are upper-cased; digits and punctuation ignore it)
 *   tap             punctuation mode: strokes are read in the punctuation set (a tap there: the period)
 *   down-right line extended mode: strokes are read in the extended set (a tap there: the bullet)
 *   prefix modes    stay on across glyphs (and keys such as tab) until the backspace gesture, which leaves the mode
 *                   without deleting anything, or a stroke starting in the other box (or strokes_set_mode); a
 *                   rejection keeps the mode; the one exception is a plain letter typed through a prefix (the X of
 *                   the extended set): it is a letter, so it ends the mode
 *   dead accent     (optional table section) one letter only: waits for the next character: a e i o u y n get the
 *                   accent, other characters and keys drop it; backspace drops it without deleting anything
 *   keys            space, backspace, enter, tab leave shift and caps alone
 */
#include "strokes_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QCAP 8
#define COORD_MAX 1.0e6f
#define SPEED_WINDOW_US 30000u /* pen speed measured over the last 30 ms before the lift */
#define SPEED_SLACK 1.25f      /* the pen may speed up a little during the gap */
#define SPEED_MAX_MM 15.0f

enum { ST_IDLE = 0, ST_DOWN, ST_LIFTED };

struct strokes_engine {
    const strokes_table *t;
    strokes_config cfg;
    int state, box, healed;
    uint64_t t_down, t_up;
    unsigned mode;
    int accent; /* pending dead accent (enum strokes_accent) */
    int pbox;   /* box a prefix mode is bound to (-1 = bound by the next stroke) */
    int n;
    strokes_point raw[STK_MAX_RAW];
    strokes_result q[QCAP];
    int qh, qn;
};

void strokes_config_default(strokes_config *c)
{
    c->ppmm = 8.54f;
    c->heal_ms = 50.0f;
    c->heal_mm = 3.0f;
    c->heal_speed = 1;
    c->tap_max_mm = 1.5f;
    c->tap_max_ms = 250.0f;
    c->min_stroke_mm = 3.0f;
    c->min_score = 0.80f;
    c->min_margin = 0.04f;
    c->rot_tol_deg = 15.0f;
    c->aspect_tol = 0.2f;
    c->smooth = 1;
    c->hook_trim = 1;
}

static float finite_or(float v, float dflt)
{
    return isfinite(v) ? v : dflt;
}

static void sanitise(strokes_config *c)
{
    strokes_config d;
    strokes_config_default(&d);
    c->ppmm = finite_or(c->ppmm, d.ppmm);
    if (c->ppmm <= 0.1f)
        c->ppmm = d.ppmm;
    c->heal_ms = fmaxf(0.0f, finite_or(c->heal_ms, d.heal_ms));
    c->heal_mm = fmaxf(0.0f, finite_or(c->heal_mm, d.heal_mm));
    c->tap_max_mm = fmaxf(0.0f, finite_or(c->tap_max_mm, d.tap_max_mm));
    c->tap_max_ms = fmaxf(0.0f, finite_or(c->tap_max_ms, d.tap_max_ms));
    c->min_stroke_mm = fmaxf(0.0f, finite_or(c->min_stroke_mm, d.min_stroke_mm));
    c->min_score = finite_or(c->min_score, d.min_score);
    c->min_margin = finite_or(c->min_margin, d.min_margin);
    c->rot_tol_deg = fminf(45.0f, fmaxf(0.0f, finite_or(c->rot_tol_deg, d.rot_tol_deg)));
    c->aspect_tol = fminf(0.5f, fmaxf(0.0f, finite_or(c->aspect_tol, d.aspect_tol)));
}

/* ---- modes ---- */

#define PREFIXES (STROKES_MODE_PUNCT | STROKES_MODE_EXT)

/* Letters a dead accent applies to (Latin-1, plus Y with diaeresis), lower and upper case. */
static const struct {
    unsigned char accent;
    char base;
    unsigned short lower, upper;
} composed[] = {
    {STROKES_ACCENT_GRAVE, 'a', 0xE0, 0xC0},      {STROKES_ACCENT_GRAVE, 'e', 0xE8, 0xC8},
    {STROKES_ACCENT_GRAVE, 'i', 0xEC, 0xCC},      {STROKES_ACCENT_GRAVE, 'o', 0xF2, 0xD2},
    {STROKES_ACCENT_GRAVE, 'u', 0xF9, 0xD9},      {STROKES_ACCENT_ACUTE, 'a', 0xE1, 0xC1},
    {STROKES_ACCENT_ACUTE, 'e', 0xE9, 0xC9},      {STROKES_ACCENT_ACUTE, 'i', 0xED, 0xCD},
    {STROKES_ACCENT_ACUTE, 'o', 0xF3, 0xD3},      {STROKES_ACCENT_ACUTE, 'u', 0xFA, 0xDA},
    {STROKES_ACCENT_ACUTE, 'y', 0xFD, 0xDD},      {STROKES_ACCENT_CIRCUMFLEX, 'a', 0xE2, 0xC2},
    {STROKES_ACCENT_CIRCUMFLEX, 'e', 0xEA, 0xCA}, {STROKES_ACCENT_CIRCUMFLEX, 'i', 0xEE, 0xCE},
    {STROKES_ACCENT_CIRCUMFLEX, 'o', 0xF4, 0xD4}, {STROKES_ACCENT_CIRCUMFLEX, 'u', 0xFB, 0xDB},
    {STROKES_ACCENT_TILDE, 'a', 0xE3, 0xC3},      {STROKES_ACCENT_TILDE, 'n', 0xF1, 0xD1},
    {STROKES_ACCENT_TILDE, 'o', 0xF5, 0xD5},      {STROKES_ACCENT_DIAERESIS, 'a', 0xE4, 0xC4},
    {STROKES_ACCENT_DIAERESIS, 'e', 0xEB, 0xCB},  {STROKES_ACCENT_DIAERESIS, 'i', 0xEF, 0xCF},
    {STROKES_ACCENT_DIAERESIS, 'o', 0xF6, 0xD6},  {STROKES_ACCENT_DIAERESIS, 'u', 0xFC, 0xDC},
    {STROKES_ACCENT_DIAERESIS, 'y', 0xFF, 0x178}, {STROKES_ACCENT_RING, 'a', 0xE5, 0xC5},
};

/* Upper case of a single letter: a-z and the Latin-1 letters (y with diaeresis to U+0178); others unchanged. */
static unsigned long upper_cp(unsigned long cp)
{
    if (cp >= 'a' && cp <= 'z')
        return cp - 'a' + 'A';
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)
        return cp - 0x20;
    if (cp == 0xFF)
        return 0x178;
    return cp;
}

/* The character typed for glyph text `in` with a pending accent and the case flags. */
static void char_out(const char *in, int accent, int upper, char out[8])
{
    memcpy(out, in, 8);
    unsigned long cp = stk_utf8_single(in);
    if (accent && cp >= 'a' && cp <= 'z')
        for (size_t i = 0; i < sizeof composed / sizeof composed[0]; ++i)
            if (composed[i].accent == accent && (unsigned long)composed[i].base == cp) {
                stk_utf8_encode(upper ? composed[i].upper : composed[i].lower, out);
                return;
            }
    if (upper && cp) {
        unsigned long up = upper_cp(cp);
        if (up != cp)
            stk_utf8_encode(up, out);
    }
}

static void finish_mode(unsigned *mode, int *accent, strokes_result *r)
{
    if (*accent)
        *mode |= STROKES_MODE_ACCENT;
    else
        *mode &= ~(unsigned)STROKES_MODE_ACCENT;
    r->mode = *mode;
    r->accent = *accent;
}

void stk_modes_apply(unsigned *mode, int *accent, const stk_glyph *g, strokes_result *r)
{
    r->utf8[0] = 0;
    r->key = STROKES_KEY_NONE;
    switch (g->out) {
    case STK_OUT_MODE:
        r->kind = STROKES_MODE;
        if (g->mode_op == STROKES_MODE_SHIFT) {
            if (*mode & STROKES_MODE_CAPS)
                *mode &= ~(unsigned)STROKES_MODE_CAPS;
            else if (*mode & STROKES_MODE_SHIFT)
                *mode = (*mode & ~(unsigned)STROKES_MODE_SHIFT) | STROKES_MODE_CAPS;
            else
                *mode |= STROKES_MODE_SHIFT;
            *mode &= ~(unsigned)PREFIXES;
        } else if (g->mode_op == STROKES_MODE_CAPS) {
            *mode ^= STROKES_MODE_CAPS;
            *mode &= ~(unsigned)(STROKES_MODE_SHIFT | PREFIXES);
        } else { /* a prefix (punctuation, extended); the same prefix again cancels it */
            unsigned had = *mode & g->mode_op;
            *mode &= ~(unsigned)PREFIXES;
            if (!had)
                *mode |= g->mode_op & PREFIXES;
        }
        break;
    case STK_OUT_ACCENT:
        r->kind = STROKES_MODE;
        *accent = g->accent;
        *mode &= ~(unsigned)PREFIXES;
        break;
    case STK_OUT_KEY:
        if (g->key == STROKES_KEY_BACKSPACE && ((*mode & PREFIXES) || *accent)) {
            /* backspace leaves a pending prefix or accent; nothing is deleted */
            r->kind = STROKES_MODE;
            *mode &= ~(unsigned)PREFIXES;
            *accent = STROKES_ACCENT_NONE;
            break;
        }
        r->kind = STROKES_KEY;
        r->key = g->key;
        if (g->key == STROKES_KEY_SPACE)
            strcpy(r->utf8, " ");
        else if (g->key == STROKES_KEY_ENTER)
            strcpy(r->utf8, "\n");
        else if (g->key == STROKES_KEY_TAB)
            strcpy(r->utf8, "\t");
        *accent = STROKES_ACCENT_NONE; /* a key keeps a prefix mode (the tab of the punctuation set) */
        break;
    default:
        r->kind = STROKES_CHAR;
        char_out(g->utf8, *accent, (*mode & (STROKES_MODE_SHIFT | STROKES_MODE_CAPS)) != 0, r->utf8);
        *mode &= ~(unsigned)STROKES_MODE_SHIFT;
        {
            /* a prefix mode stays on; a plain letter typed through it (the X) ends it */
            unsigned long cp = stk_utf8_single(g->utf8);
            if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
                *mode &= ~(unsigned)PREFIXES;
        }
        *accent = STROKES_ACCENT_NONE;
        break;
    }
    finish_mode(mode, accent, r);
}

void stk_modes_reject(const unsigned *mode, int accent, strokes_result *r)
{
    /* a rejection changes nothing: a pending prefix or accent waits for the stroke written again */
    r->kind = STROKES_REJECT;
    r->utf8[0] = 0;
    r->key = STROKES_KEY_NONE;
    r->mode = *mode;
    r->accent = accent;
}

/* ---- one stroke ---- */

/* Clean-up + matching of one healed stroke; fills r (except kind/mode). Returns the accepted glyph index or -1. */
static int classify_core(const strokes_table *t, const strokes_config *cfg, int set, const strokes_point *raw, int n,
                         float duration_ms, strokes_result *r)
{
    stk_vec fine[STK_FINE];
    int nfine;
    r->set = set;
    r->duration_ms = duration_ms;
    int c = stk_clean(cfg, raw, n, duration_ms, fine, &nfine, &r->path_mm);
    if (c == STK_CLEAN_TAP)
        return stk_match_tap(t, set, r);
    if (c == STK_CLEAN_SHORT || c == STK_CLEAN_DWELL) {
        r->reject = c == STK_CLEAN_SHORT ? STROKES_REJECT_SHORT : STROKES_REJECT_DWELL;
        return -1;
    }
    stk_vec pts[STK_N];
    stk_resample(fine, nfine, pts, STK_N);
    stk_feat f;
    stk_features(pts, STK_N, &f);
    memcpy(r->dirs, f.dirs, sizeof r->dirs);
    r->corners = f.corners;
    r->turn_deg = f.turn * 180.0f / STK_PI;
    return stk_match(t, cfg, set, fine, nfine, &f, r);
}

int strokes_classify(const strokes_table *t, const strokes_config *cfg, int set, const strokes_point *pts, int n,
                     strokes_result *out)
{
    strokes_config c;
    if (cfg)
        c = *cfg;
    else
        strokes_config_default(&c);
    sanitise(&c);
    memset(out, 0, sizeof *out);
    if (!t || !pts || n <= 0 || set < 0 || set >= STK_NSETS) {
        out->kind = STROKES_REJECT;
        out->reject = STROKES_REJECT_SHORT;
        return 0;
    }
    if (n > STK_MAX_RAW)
        n = STK_MAX_RAW;
    out->n_raw = n;
    out->t_down_us = pts[0].t_us;
    out->t_up_us = pts[n - 1].t_us;
    float dur = pts[n - 1].t_us >= pts[0].t_us ? (float)(pts[n - 1].t_us - pts[0].t_us) / 1000.0f : 0.0f;
    int gi = classify_core(t, &c, set, pts, n, dur, out);
    unsigned mode = 0;
    int accent = STROKES_ACCENT_NONE;
    if (gi < 0) {
        stk_modes_reject(&mode, accent, out);
        return 0;
    }
    stk_modes_apply(&mode, &accent, &t->g[gi], out);
    out->mode = 0;
    return 1;
}

int strokes_template_line(const strokes_table *t, const strokes_config *cfg, const char *id, int set,
                          const strokes_point *pts, int n, char *buf, size_t size)
{
    if (!t || !id || !pts || n <= 0 || !buf || size == 0)
        return -1;
    int gi = stk_find_glyph(t, id, set);
    if (gi < 0 || t->g[gi].is_tap)
        return -1;
    strokes_config c;
    if (cfg)
        c = *cfg;
    else
        strokes_config_default(&c);
    sanitise(&c);
    if (n > STK_MAX_RAW)
        n = STK_MAX_RAW;
    stk_vec fine[STK_FINE], p[STK_N];
    float path_mm;
    int nfine;
    if (stk_clean(&c, pts, n, 0.0f, fine, &nfine, &path_mm) != STK_CLEAN_STROKE)
        return -1;
    stk_resample(fine, nfine, p, STK_N);
    float minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
    for (int i = 1; i < STK_N; ++i) {
        minx = fminf(minx, p[i].x);
        maxx = fmaxf(maxx, p[i].x);
        miny = fminf(miny, p[i].y);
        maxy = fmaxf(maxy, p[i].y);
    }
    float side = fmaxf(maxx - minx, maxy - miny);
    if (side <= 1e-6f)
        return -1;
    char ob[16];
    int len = snprintf(buf, size, "%s %s %s", t->g[gi].id, strokes_set_name(set), stk_out_token(&t->g[gi], ob));
    if (len < 0 || (size_t)len >= size)
        return -1;
    for (int i = 0; i < STK_N; ++i) {
        /* tenths written as integers: the output never depends on the C locale's decimal separator */
        long x = lroundf((p[i].x - minx) * 1000.0f / side), y = lroundf((p[i].y - miny) * 1000.0f / side);
        int w = snprintf(buf + len, size - (size_t)len, " %ld.%ld,%ld.%ld", x / 10, x % 10, y / 10, y % 10);
        if (w < 0 || (size_t)(len + w) >= size)
            return -1;
        len += w;
    }
    if ((size_t)len + 1 >= size)
        return -1;
    buf[len++] = '\n';
    buf[len] = 0;
    return len;
}

/* ---- engine ---- */

strokes_engine *strokes_engine_new(const strokes_table *t, const strokes_config *cfg)
{
    if (!t)
        return NULL;
    strokes_engine *e = calloc(1, sizeof *e);
    if (!e)
        return NULL;
    e->t = t;
    if (cfg)
        e->cfg = *cfg;
    else
        strokes_config_default(&e->cfg);
    sanitise(&e->cfg);
    e->pbox = -1;
    return e;
}

void strokes_engine_free(strokes_engine *e)
{
    free(e);
}

static void push(strokes_engine *e, const strokes_result *r)
{
    if (e->qn == QCAP) { /* never polled: drop the oldest */
        e->qh = (e->qh + 1) % QCAP;
        e->qn--;
    }
    e->q[(e->qh + e->qn) % QCAP] = *r;
    e->qn++;
}

static int pop(strokes_engine *e, strokes_result *out)
{
    if (e->qn == 0) {
        if (out)
            memset(out, 0, sizeof *out);
        return 0;
    }
    if (out)
        *out = e->q[e->qh];
    e->qh = (e->qh + 1) % QCAP;
    e->qn--;
    return 1;
}

static void finalise(strokes_engine *e)
{
    strokes_result r;
    memset(&r, 0, sizeof r);
    r.box = e->box;
    r.healed = e->healed;
    r.n_raw = e->n;
    r.t_down_us = e->t_down;
    r.t_up_us = e->t_up;
    float dur = e->t_up >= e->t_down ? (float)(e->t_up - e->t_down) / 1000.0f : 0.0f;
    int set = (e->mode & STROKES_MODE_PUNCT) ? STROKES_SET_PUNCT
              : (e->mode & STROKES_MODE_EXT) ? STROKES_SET_EXT
              : e->box == STROKES_BOX_DIGITS ? STROKES_SET_DIGITS
                                             : STROKES_SET_LETTERS;
    int gi = classify_core(e->t, &e->cfg, set, e->raw, e->n, dur, &r);
    if (gi >= 0)
        stk_modes_apply(&e->mode, &e->accent, &e->t->g[gi], &r);
    else
        stk_modes_reject(&e->mode, e->accent, &r);
    e->pbox = (e->mode & PREFIXES) ? e->box : -1;
    push(e, &r);
    e->state = ST_IDLE;
    e->n = 0;
    e->healed = 0;
}

static int sane_xy(float *x, float *y)
{
    if (!isfinite(*x) || !isfinite(*y))
        return 0;
    *x = fmaxf(-COORD_MAX, fminf(COORD_MAX, *x));
    *y = fmaxf(-COORD_MAX, fminf(COORD_MAX, *y));
    return 1;
}

static void add_point(strokes_engine *e, float x, float y, uint64_t t)
{
    if (e->n > 0) {
        strokes_point *last = &e->raw[e->n - 1];
        if (t < last->t_us)
            t = last->t_us; /* the clock went back: keep the order */
        if (last->x == x && last->y == y)
            return; /* duplicate frame */
    }
    if (e->n == STK_MAX_RAW) {
        int k = 0;
        for (int i = 0; i < e->n; i += 2)
            e->raw[k++] = e->raw[i];
        e->n = k;
    }
    e->raw[e->n].x = x;
    e->raw[e->n].y = y;
    e->raw[e->n].t_us = t;
    e->n++;
}

/* A press after a short lift continues the stroke when it lands within heal_mm of the lift point, plus (heal_speed)
 * the distance the pen covers at its last speed during the gap: a fast stylus keeps moving while the panel has lost
 * it. A new stroke cannot start within heal_ms of a lift, so the window is what separates strokes; the distance
 * only refuses presses far away (another box, a second finger). */
static int heals(const strokes_engine *e, float x, float y, uint64_t t)
{
    if (e->n == 0)
        return 0;
    float gap_ms = t >= e->t_up ? (float)(t - e->t_up) / 1000.0f : 0.0f;
    if (gap_ms > e->cfg.heal_ms)
        return 0;
    const strokes_point *last = &e->raw[e->n - 1];
    float lim = e->cfg.heal_mm * e->cfg.ppmm;
    float d = hypotf(x - last->x, y - last->y);
    if (d <= lim)
        return 1;
    if (!e->cfg.heal_speed)
        return 0;
    int j = e->n - 1;
    float path = 0.0f;
    /* the last 30 ms, and at least the last segment (a slow panel reports every 33 ms or more) */
    while (j > 0 && (j == e->n - 1 || last->t_us - e->raw[j - 1].t_us <= SPEED_WINDOW_US)) {
        path += hypotf(e->raw[j].x - e->raw[j - 1].x, e->raw[j].y - e->raw[j - 1].y);
        j--;
    }
    uint64_t dt = last->t_us - e->raw[j].t_us;
    if (dt < 4000u)
        return 0;
    float ahead = t >= last->t_us ? (float)(t - last->t_us) : 0.0f;
    float reach = SPEED_SLACK * path / (float)dt * ahead;
    float cap = SPEED_MAX_MM * e->cfg.ppmm;
    return d <= lim + (reach < cap ? reach : cap);
}

void strokes_down(strokes_engine *e, int box, float x, float y, uint64_t t_us)
{
    if (!e || !sane_xy(&x, &y))
        return;
    if (e->state == ST_DOWN) { /* a second press without a lift: treat as a move */
        add_point(e, x, y, t_us);
        return;
    }
    if (e->state == ST_LIFTED) {
        if (heals(e, x, y, t_us)) {
            e->state = ST_DOWN;
            e->healed++;
            add_point(e, x, y, t_us);
            return;
        }
        finalise(e);
    }
    e->state = ST_DOWN;
    e->box = box == STROKES_BOX_DIGITS ? STROKES_BOX_DIGITS : STROKES_BOX_LETTERS;
    if (e->mode & PREFIXES) { /* a prefix mode belongs to its box: a stroke in the other box leaves it */
        if (e->pbox >= 0 && e->pbox != e->box)
            e->mode &= ~(unsigned)PREFIXES;
        e->pbox = (e->mode & PREFIXES) ? e->box : -1;
    }
    e->n = 0;
    e->healed = 0;
    e->t_down = t_us;
    add_point(e, x, y, t_us);
}

void strokes_move(strokes_engine *e, float x, float y, uint64_t t_us)
{
    if (!e || e->state != ST_DOWN || !sane_xy(&x, &y))
        return;
    add_point(e, x, y, t_us);
}

void strokes_up(strokes_engine *e, uint64_t t_us)
{
    if (!e || e->state != ST_DOWN)
        return;
    uint64_t last = e->n > 0 ? e->raw[e->n - 1].t_us : e->t_down;
    e->t_up = t_us > last ? t_us : last;
    e->state = ST_LIFTED;
}

int strokes_poll(strokes_engine *e, uint64_t now_us, strokes_result *out)
{
    if (!e)
        return 0;
    if (e->state == ST_LIFTED && now_us >= e->t_up && (float)(now_us - e->t_up) / 1000.0f > e->cfg.heal_ms)
        finalise(e);
    return pop(e, out);
}

int strokes_flush(strokes_engine *e, strokes_result *out)
{
    if (!e)
        return 0;
    if (e->state == ST_LIFTED)
        finalise(e);
    return pop(e, out);
}

void strokes_cancel(strokes_engine *e)
{
    if (!e)
        return;
    e->state = ST_IDLE;
    e->n = 0;
    e->healed = 0;
}

int strokes_busy(const strokes_engine *e)
{
    return e && e->state != ST_IDLE;
}

unsigned strokes_get_mode(const strokes_engine *e)
{
    return e ? e->mode : 0u;
}

void strokes_set_mode(strokes_engine *e, unsigned mode)
{
    if (!e)
        return;
    mode &= STROKES_MODE_SHIFT | STROKES_MODE_CAPS | PREFIXES | STROKES_MODE_ACCENT;
    if ((mode & STROKES_MODE_SHIFT) && (mode & STROKES_MODE_CAPS))
        mode &= ~(unsigned)STROKES_MODE_SHIFT;
    if ((mode & STROKES_MODE_PUNCT) && (mode & STROKES_MODE_EXT))
        mode &= ~(unsigned)STROKES_MODE_EXT;
    if (!(mode & STROKES_MODE_ACCENT))
        e->accent = STROKES_ACCENT_NONE;
    else if (e->accent == STROKES_ACCENT_NONE)
        mode &= ~(unsigned)STROKES_MODE_ACCENT; /* no accent is invented */
    e->mode = mode;
    e->pbox = -1; /* a prefix set here belongs to the box of the next stroke */
}

/* ---- names ---- */

const char *strokes_kind_name(int kind)
{
    static const char *n[] = {"none", "char", "key", "mode", "reject"};
    return kind >= 0 && kind <= 4 ? n[kind] : "?";
}

const char *strokes_reject_name(int reject)
{
    static const char *n[] = {"none", "short", "dwell", "no-candidate", "low-score", "ambiguous"};
    return reject >= 0 && reject <= 5 ? n[reject] : "?";
}
