/*
 * SPDX-License-Identifier: MIT
 *
 * Gates and matcher of the Strokes recogniser (report 038 section 5, steps 6 and 7): every template of the active
 * set (plus the gestures valid in it, see stk_set_matches) is scored; a glyph is a candidate only through a variant
 * whose gates the stroke passes; the best glyph is accepted when its score reaches min_score and the runner-up is
 * at least min_margin behind. Otherwise the stroke is rejected: no guessing.
 */
#include "strokes_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int cell_pass(uint16_t mask, float u, float v)
{
    for (int i = 0; i < 9; ++i) {
        if (!(mask & (1u << i)))
            continue;
        float c = (float)(i % 3), r = (float)(i / 3);
        if (u >= c / 3.0f - STK_CELL_TOL && u <= (c + 1.0f) / 3.0f + STK_CELL_TOL && v >= r / 3.0f - STK_CELL_TOL &&
            v <= (r + 1.0f) / 3.0f + STK_CELL_TOL)
            return 1;
    }
    return 0;
}

/* Each gate digit, in order, must meet a run of the stroke's chain code within 45 degrees of it. */
static int dirs_pass(const char *gate, const char *dirs)
{
    const char *d = dirs;
    for (const char *g = gate; *g; ++g) {
        float a = stk_numpad_angle(*g - '0');
        while (*d && stk_angle_diff(a, stk_numpad_angle(*d - '0')) > STK_PI / 4.0f + 1e-3f)
            d++;
        if (!*d)
            return 0;
        d++;
    }
    return 1;
}

int stk_gates_pass(const stk_gates *g, const stk_feat *tf, const stk_feat *f)
{
    if (g->start_mask) {
        if (!cell_pass(g->start_mask, f->su, f->sv))
            return 0;
    } else if (hypotf(f->su - tf->su, f->sv - tf->sv) > STK_AUTO_R) {
        return 0;
    }
    if (g->end_mask) {
        if (!cell_pass(g->end_mask, f->eu, f->ev))
            return 0;
    } else if (hypotf(f->eu - tf->eu, f->ev - tf->ev) > STK_AUTO_R) {
        return 0;
    }
    if (g->line == STK_G_YES) {
        if (f->straight < STK_LINE_MIN || stk_angle_diff(f->dir, g->line_dir) > STK_LINE_TOL)
            return 0;
    } else if (g->line == STK_G_NO) {
        if (f->straight > STK_CURVE_MAX)
            return 0;
    }
    if (g->closed == STK_G_YES && f->gap > STK_CLOSED_MAX)
        return 0;
    if (g->closed == STK_G_NO && f->gap < STK_OPEN_MIN)
        return 0;
    if (g->turn > 0 && f->turn < g->turn_min)
        return 0;
    if (g->turn < 0 && f->turn > -g->turn_min)
        return 0;
    if (g->cmin >= 0 && (f->corners < g->cmin || f->corners > g->cmax))
        return 0;
    if (g->sharp_max > 0.0f && (f->sharp < g->sharp_min || f->sharp > g->sharp_max))
        return 0;
    if (g->dirs[0] && !dirs_pass(g->dirs, f->dirs))
        return 0;
    return 1;
}

/* Indices of the k best glyphs by score (score >= 0), best first; ties keep the table order. */
static int top_k(const float *score, int n, int *idx, int k)
{
    int m = 0;
    for (int i = 0; i < n; ++i) {
        if (score[i] < 0.0f)
            continue;
        int pos = m < k ? m : k;
        while (pos > 0 && score[idx[pos - 1]] < score[i])
            pos--;
        if (pos >= k)
            continue;
        int last = m < k ? m : k - 1;
        for (int j = last; j > pos; --j)
            idx[j] = idx[j - 1];
        idx[pos] = i;
        if (m < k)
            m++;
    }
    return m;
}

int stk_match(const strokes_table *t, const strokes_config *cfg, int set, const stk_vec *fine, int nfine,
              const stk_feat *f, strokes_result *r)
{
    float pass[STK_MAX_GLYPHS], any[STK_MAX_GLYPHS];
    int ng = t->ng < STK_MAX_GLYPHS ? t->ng : STK_MAX_GLYPHS;
    for (int i = 0; i < ng; ++i)
        pass[i] = any[i] = -1.0f;
    stk_rot rot;
    stk_rot_init(&rot, cfg->rot_tol_deg);
    /* Aspect tolerance: the stroke is also tried stretched and squeezed horizontally by (1 + aspect_tol), and the
     * best of the three counts. The stretch is applied to the cleaned path before resampling, so the points follow
     * the stretched shape. Lines and shapes that differ by more than that stay apart. */
    stk_vec var[3][STK_N];
    int nvar = 0;
    if (stk_prepare(fine, nfine, 1.0f, var[0]))
        nvar = 1;
    if (nvar && cfg->aspect_tol > 0.0f) {
        float k[2] = {1.0f + cfg->aspect_tol, 1.0f / (1.0f + cfg->aspect_tol)};
        for (int v = 0; v < 2; ++v)
            if (stk_prepare(fine, nfine, k[v], var[nvar]))
                nvar++;
    }
    for (int i = 0; i < t->nt; ++i) {
        const stk_tpl *tp = &t->tp[i];
        if (tp->dead || tp->glyph >= ng)
            continue;
        const stk_glyph *g = &t->g[tp->glyph];
        if (!stk_set_matches(g, set))
            continue;
        float s = 0.0f;
        for (int v = 0; v < nvar; ++v) {
            float sv = stk_score(var[v], tp->p, STK_N, &rot);
            if (sv > s)
                s = sv;
        }
        if (s > any[tp->glyph])
            any[tp->glyph] = s;
        if (s > pass[tp->glyph] && stk_gates_pass(&tp->g, &tp->f, f))
            pass[tp->glyph] = s;
    }
    int idx[STROKES_MAX_CAND];
    int m = top_k(pass, ng, idx, STROKES_MAX_CAND);
    r->n_cand = 0;
    if (m == 0) {
        m = top_k(any, ng, idx, STROKES_MAX_CAND);
        for (int i = 0; i < m; ++i) {
            snprintf(r->cand[i].glyph, sizeof r->cand[i].glyph, "%s", t->g[idx[i]].id);
            r->cand[i].score = any[idx[i]];
            r->cand[i].gated = 1;
        }
        r->n_cand = m;
        r->glyph[0] = 0;
        r->confidence = 0.0f;
        r->margin = 0.0f;
        r->reject = STROKES_REJECT_NO_CANDIDATE;
        return -1;
    }
    for (int i = 0; i < m; ++i) {
        snprintf(r->cand[i].glyph, sizeof r->cand[i].glyph, "%s", t->g[idx[i]].id);
        r->cand[i].score = pass[idx[i]];
        r->cand[i].gated = 0;
    }
    r->n_cand = m;
    float best = pass[idx[0]];
    float second = m > 1 ? pass[idx[1]] : 0.0f;
    snprintf(r->glyph, sizeof r->glyph, "%s", t->g[idx[0]].id);
    r->confidence = best;
    r->margin = best - second;
    if (best < cfg->min_score) {
        r->reject = STROKES_REJECT_LOW_SCORE;
        return -1;
    }
    if (r->margin < cfg->min_margin) {
        r->reject = STROKES_REJECT_AMBIGUOUS;
        return -1;
    }
    r->reject = STROKES_REJECT_NONE;
    return idx[0];
}

int stk_match_tap(const strokes_table *t, int set, strokes_result *r)
{
    /* the set's own tap glyph first (the period after a tap, the bullet after the extended prefix), then a gesture
     * that is a candidate in this set (the tap that starts the punctuation prefix, in letters and digits) */
    int found = -1;
    for (int pass = 0; pass < 2 && found < 0; ++pass)
        for (int i = 0; i < t->ng; ++i) {
            const stk_glyph *g = &t->g[i];
            if (!g->is_tap || g->n_variants <= 0)
                continue;
            if (pass == 0 ? g->set == set : g->set != set && stk_set_matches(g, set)) {
                found = i;
                break;
            }
        }
    r->n_cand = 0;
    if (found < 0) {
        r->glyph[0] = 0;
        r->confidence = r->margin = 0.0f;
        r->reject = STROKES_REJECT_NO_CANDIDATE;
        return -1;
    }
    snprintf(r->glyph, sizeof r->glyph, "%s", t->g[found].id);
    snprintf(r->cand[0].glyph, sizeof r->cand[0].glyph, "%s", t->g[found].id);
    r->cand[0].score = 1.0f;
    r->cand[0].gated = 0;
    r->n_cand = 1;
    r->confidence = 1.0f;
    r->margin = 1.0f;
    r->reject = STROKES_REJECT_NONE;
    return found;
}
