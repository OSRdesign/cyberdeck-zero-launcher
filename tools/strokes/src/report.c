/*
 * SPDX-License-Identifier: MIT
 *
 * Per-glyph accuracy report of strokes-eval and strokes-replay: accuracy, rejections, wrong answers, split strokes
 * (a drop-out that was not healed gives two results for one sample), confusions, and a threshold sweep.
 */
#include "sample.h"

#include <stdlib.h>
#include <string.h>

void report_init(report *r)
{
    memset(r, 0, sizeof *r);
}

void report_free(report *r)
{
    free(r->pts);
    r->pts = NULL;
    r->npts = r->cappts = 0;
}

static report_row *row_for(report *r, const char *id, int set)
{
    for (int i = 0; i < r->nrows; ++i)
        if (r->rows[i].set == set && strcmp(r->rows[i].id, id) == 0)
            return &r->rows[i];
    if (r->nrows >= REPORT_MAX_GLYPHS)
        return NULL;
    report_row *w = &r->rows[r->nrows++];
    memset(w, 0, sizeof *w);
    snprintf(w->id, sizeof w->id, "%s", id);
    w->set = set;
    return w;
}

static void add_conf(report *r, const char *want, const char *got)
{
    for (int i = 0; i < r->nconf; ++i)
        if (strcmp(r->conf[i].want, want) == 0 && strcmp(r->conf[i].got, got) == 0) {
            r->conf[i].count++;
            return;
        }
    if (r->nconf >= REPORT_MAX_CONF)
        return;
    report_conf *c = &r->conf[r->nconf++];
    snprintf(c->want, sizeof c->want, "%s", want);
    snprintf(c->got, sizeof c->got, "%s", got);
    c->count = 1;
}

void report_add(report *r, const char *want, int set, const strokes_result *res, int nres)
{
    report_row *w = row_for(r, want, set);
    if (!w)
        return;
    w->n++;
    report_point pt = {0.0f, 0.0f, 0, 0};
    if (nres != 1) {
        w->split++;
        add_conf(r, want, nres == 0 ? "(none)" : "(split)");
        pt.conf = pt.margin = 1.0f; /* the sweep counts a split as wrong at every threshold, as the table does */
        pt.matchable = 1;
    } else {
        const strokes_result *x = &res[0];
        int matched = x->reject != STROKES_REJECT_SHORT && x->reject != STROKES_REJECT_DWELL &&
                      x->reject != STROKES_REJECT_NO_CANDIDATE;
        pt.conf = x->confidence;
        pt.margin = x->margin;
        pt.matchable = matched;
        pt.top_ok = strcmp(x->glyph, want) == 0;
        if (x->kind == STROKES_REJECT) {
            w->rej++;
            if (x->reject >= 0 && x->reject < 8)
                r->rej_reason[x->reject]++;
            char got[STROKES_ID_MAX + 2];
            snprintf(got, sizeof got, "~%s", x->glyph[0] ? x->glyph : strokes_reject_name(x->reject));
            add_conf(r, want, got);
        } else if (strcmp(x->glyph, want) == 0) {
            w->ok++;
        } else {
            w->wrong++;
            add_conf(r, want, x->glyph);
        }
    }
    if (r->npts == r->cappts) {
        int nc = r->cappts ? r->cappts * 2 : 1024;
        report_point *np = realloc(r->pts, (size_t)nc * sizeof *np);
        if (!np)
            return;
        r->pts = np;
        r->cappts = nc;
    }
    r->pts[r->npts++] = pt;
}

static double pct(int a, int n)
{
    return n ? 100.0 * a / n : 0.0;
}

void report_totals(const report *r, int *n, double *acc, double *rej, double *wrong)
{
    int tn = 0, ok = 0, rj = 0, wr = 0;
    for (int i = 0; i < r->nrows; ++i) {
        tn += r->rows[i].n;
        ok += r->rows[i].ok;
        rj += r->rows[i].rej;
        wr += r->rows[i].wrong + r->rows[i].split;
    }
    if (n)
        *n = tn;
    if (acc)
        *acc = pct(ok, tn);
    if (rej)
        *rej = pct(rj, tn);
    if (wrong)
        *wrong = pct(wr, tn);
}

static int cmp_conf(const void *a, const void *b)
{
    const report_conf *x = a, *y = b;
    if (x->count != y->count)
        return y->count - x->count;
    int c = strcmp(x->want, y->want);
    return c ? c : strcmp(x->got, y->got);
}

void report_print(FILE *f, const report *r, int top_conf)
{
    fprintf(f, "%-7s %-8s %6s %7s %7s %7s %6s  %s\n", "glyph", "set", "n", "ok%", "rej%", "wrong%", "split", "worst");
    static const int order[] = {STROKES_SET_LETTERS, STROKES_SET_DIGITS, STROKES_SET_PUNCT, STROKES_SET_EXT,
                                STROKES_SET_ANY};
    for (size_t oi = 0; oi < sizeof order / sizeof order[0]; ++oi) {
        int set = order[oi];
        int sn = 0, sok = 0, srej = 0, swr = 0, ssp = 0;
        for (int i = 0; i < r->nrows; ++i) {
            const report_row *w = &r->rows[i];
            if (w->set != set)
                continue;
            /* the most frequent confusion of this glyph */
            const report_conf *worst = NULL;
            for (int k = 0; k < r->nconf; ++k)
                if (strcmp(r->conf[k].want, w->id) == 0 && (!worst || r->conf[k].count > worst->count))
                    worst = &r->conf[k];
            char wbuf[48] = "";
            if (worst)
                snprintf(wbuf, sizeof wbuf, "%s x%d", worst->got, worst->count);
            fprintf(f, "%-7s %-8s %6d %7.1f %7.1f %7.1f %6d  %s\n", w->id, strokes_set_name(w->set), w->n,
                    pct(w->ok, w->n), pct(w->rej, w->n), pct(w->wrong, w->n), w->split, wbuf);
            sn += w->n;
            sok += w->ok;
            srej += w->rej;
            swr += w->wrong;
            ssp += w->split;
        }
        if (sn)
            fprintf(f, "%-7s %-8s %6d %7.1f %7.1f %7.1f %6d\n", "= set", strokes_set_name(set), sn, pct(sok, sn),
                    pct(srej, sn), pct(swr, sn), ssp);
    }
    int n;
    double acc, rej, wrong;
    report_totals(r, &n, &acc, &rej, &wrong);
    fprintf(f, "ALL: %d samples, correct %.2f %%, rejected %.2f %%, wrong %.2f %% (wrong includes split strokes)\n", n,
            acc, rej, wrong);
    fprintf(f, "rejections by reason: low-score %d, ambiguous %d, no-candidate %d, short %d, dwell %d\n",
            r->rej_reason[STROKES_REJECT_LOW_SCORE], r->rej_reason[STROKES_REJECT_AMBIGUOUS],
            r->rej_reason[STROKES_REJECT_NO_CANDIDATE], r->rej_reason[STROKES_REJECT_SHORT],
            r->rej_reason[STROKES_REJECT_DWELL]);
    if (top_conf <= 0 || r->nconf == 0)
        return;
    report_conf *sorted = malloc((size_t)r->nconf * sizeof *sorted);
    if (!sorted)
        return;
    memcpy(sorted, r->conf, (size_t)r->nconf * sizeof *sorted);
    qsort(sorted, (size_t)r->nconf, sizeof *sorted, cmp_conf);
    fprintf(f, "top confusions (want -> got; ~x = rejected, best candidate x):");
    for (int i = 0; i < r->nconf && i < top_conf; ++i)
        fprintf(f, "%s %s->%s %d", i ? "," : "", sorted[i].want, sorted[i].got, sorted[i].count);
    fprintf(f, "\n");
    free(sorted);
}

void report_sweep(FILE *f, const report *r)
{
    static const float scores[] = {0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f};
    static const float margins[] = {0.0f, 0.02f, 0.04f, 0.06f, 0.08f};
    fprintf(f, "threshold sweep (correct / rejected / wrong %%, from the recorded scores):\n%-10s", "min_score");
    for (size_t m = 0; m < sizeof margins / sizeof margins[0]; ++m)
        fprintf(f, "  margin %.2f        ", (double)margins[m]);
    fprintf(f, "\n");
    for (size_t s = 0; s < sizeof scores / sizeof scores[0]; ++s) {
        fprintf(f, "%-10.2f", (double)scores[s]);
        for (size_t m = 0; m < sizeof margins / sizeof margins[0]; ++m) {
            int ok = 0, rej = 0, wr = 0;
            for (int i = 0; i < r->npts; ++i) {
                const report_point *p = &r->pts[i];
                int acc = p->matchable && p->conf >= scores[s] && p->margin >= margins[m];
                if (!acc)
                    rej++;
                else if (p->top_ok)
                    ok++;
                else
                    wr++;
            }
            fprintf(f, "  %5.1f/%5.1f/%5.2f  ", pct(ok, r->npts), pct(rej, r->npts), pct(wr, r->npts));
        }
        fprintf(f, "\n");
    }
}
