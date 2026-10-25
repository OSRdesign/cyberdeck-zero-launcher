/*
 * SPDX-License-Identifier: MIT
 *
 * strokes-replay: the PC harness that replays corpus files (recorded with strokes-capture, or written by
 * strokes-eval --write) through the engine, frame by frame with their timestamps, and reports per glyph.
 */
#include "sample.h"
#include "tools_common.h"

#include <stdlib.h>
#include <string.h>

#define MAX_OVERLAYS 8
#define MAX_KV 32

typedef struct {
    strokes_table *t;
    strokes_config cfg;
    float ppmm_override;
    strokes_engine *e;
    float e_ppmm;
    report rep;
    int verbose, errors_only;
    long unlabelled, unknown;
    const char *file;
    long index;
} replay_ctx;

static void usage(FILE *f)
{
    fprintf(f,
            "usage: strokes-replay [options] CORPUS...\n"
            "  --table FILE      glyph table (default: share/glyphs.txt)\n"
            "  --overlay FILE    user overlay, applied after the table (repeatable)\n"
            "  --cfg KEY=VALUE   one recogniser setting (repeatable; see README)\n"
            "  --ppmm P          override the density written in the corpus\n"
            "  -v                one line per sample\n"
            "  --errors          one line per sample that is not correct\n"
            "  --sweep           print a threshold sweep\n"
            "  --conf N          show the N most frequent confusions (default 12)\n"
            "  --gate ACC,REJ    exit 1 unless correct >= ACC %% and rejected <= REJ %%\n");
}

static void line_for(const replay_ctx *c, const stk_sample *s, const strokes_result *res, int n, int ok)
{
    printf("%s:%ld %-6s %-7s %s", c->file, c->index, s->label, s->box == STROKES_BOX_DIGITS ? "digits" : "letters",
           ok ? "ok   " : "FAIL ");
    for (int i = 0; i < n; ++i) {
        const strokes_result *r = &res[i];
        printf("%s%s%s %s score %.3f margin %.3f healed %d corners %d turn %.0f dirs %s cand", i ? " | " : "",
               r->kind == STROKES_REJECT ? "~" : "", r->glyph[0] ? r->glyph : "-", strokes_reject_name(r->reject),
               (double)r->confidence, (double)r->margin, r->healed, r->corners, (double)r->turn_deg, r->dirs);
        for (int k = 0; k < r->n_cand; ++k)
            printf(" %s%s %.3f", r->cand[k].gated ? "!" : "", r->cand[k].glyph, (double)r->cand[k].score);
    }
    printf("\n");
}

static int on_sample(void *ctx, const stk_sample *s)
{
    replay_ctx *c = ctx;
    c->index++;
    float ppmm = c->ppmm_override > 0.0f ? c->ppmm_override : s->ppmm;
    if (!c->e || c->e_ppmm != ppmm) {
        strokes_engine_free(c->e);
        c->cfg.ppmm = ppmm;
        c->e = strokes_engine_new(c->t, &c->cfg);
        c->e_ppmm = ppmm;
        if (!c->e)
            return 1;
    }
    int labelled = strcmp(s->label, "?") != 0;
    int set = labelled ? tools_label_set(c->t, s->label, s->box) : -1;
    strokes_set_mode(c->e, replay_mode_for_set(set));
    strokes_result res[8];
    int n = replay_sample(c->e, s, res, 8);
    int ok = n == 1 && res[0].kind != STROKES_REJECT && strcmp(res[0].glyph, s->label) == 0;
    if (!labelled)
        c->unlabelled++;
    else if (set < 0)
        c->unknown++;
    else
        report_add(&c->rep, s->label, set, res, n);
    if (c->verbose || (c->errors_only && labelled && !ok))
        line_for(c, s, res, n, ok);
    return 0;
}

int main(int argc, char **argv)
{
    char tbuf[512];
    const char *table = NULL, *overlays[MAX_OVERLAYS], *cfg_kv[MAX_KV], *files[256];
    int n_ov = 0, n_cfg = 0, n_files = 0, sweep = 0, top_conf = 12;
    double gate_acc = -1.0, gate_rej = -1.0;
    replay_ctx c;
    memset(&c, 0, sizeof c);
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "-v") == 0) {
            c.verbose = 1;
        } else if (strcmp(a, "--errors") == 0) {
            c.errors_only = 1;
        } else if (strcmp(a, "--sweep") == 0) {
            sweep = 1;
        } else if (a[0] == '-' && a[1] == '-' && !v) {
            fprintf(stderr, "%s needs a value (or is unknown; see -h)\n", a);
            return 2;
        } else if (strcmp(a, "--table") == 0) {
            table = argv[++i];
        } else if (strcmp(a, "--overlay") == 0) {
            if (n_ov < MAX_OVERLAYS)
                overlays[n_ov++] = argv[i + 1];
            i++;
        } else if (strcmp(a, "--cfg") == 0) {
            if (n_cfg < MAX_KV)
                cfg_kv[n_cfg++] = argv[i + 1];
            i++;
        } else if (strcmp(a, "--ppmm") == 0) {
            c.ppmm_override = (float)atof(argv[++i]);
        } else if (strcmp(a, "--conf") == 0) {
            top_conf = atoi(argv[++i]);
        } else if (strcmp(a, "--gate") == 0) {
            if (sscanf(argv[++i], "%lf,%lf", &gate_acc, &gate_rej) != 2) {
                fprintf(stderr, "--gate wants ACC,REJ\n");
                return 2;
            }
        } else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "unknown option %s\n", a);
            usage(stderr);
            return 2;
        } else if (n_files < 256) {
            files[n_files++] = a;
        }
    }
    if (n_files == 0) {
        usage(stderr);
        return 2;
    }
    if (!table)
        table = tools_default_table(argv[0], tbuf, sizeof tbuf);
    c.t = tools_load_table(table, overlays, n_ov);
    if (!c.t)
        return 2;
    strokes_config_default(&c.cfg);
    for (int i = 0; i < n_cfg; ++i)
        if (tools_cfg_set(&c.cfg, cfg_kv[i])) {
            fprintf(stderr, "bad --cfg %s\n", cfg_kv[i]);
            return 2;
        }
    report_init(&c.rep);
    char desc[512];
    tools_cfg_describe(&c.cfg, desc, sizeof desc);
    printf("strokes-replay %s: table %s (%d glyphs, %d templates), %d overlay(s)\nrecogniser: %s (ppmm from the "
           "corpus%s)\n",
           STROKES_VERSION, table, strokes_table_glyph_count(c.t), strokes_table_template_count(c.t), n_ov, desc,
           c.ppmm_override > 0.0f ? ", overridden" : "");
    int rc = 0;
    long total = 0;
    for (int i = 0; i < n_files; ++i) {
        char msg[256];
        c.file = files[i];
        c.index = 0;
        int n = corpus_read_file(files[i], on_sample, &c, msg, sizeof msg);
        if (n < 0) {
            fprintf(stderr, "%s\n", msg);
            rc = 2;
            break;
        }
        total += n;
    }
    printf("\n%ld sample(s) read, %ld unlabelled (\"?\"), %ld with a label the table does not know\n", total,
           c.unlabelled, c.unknown);
    report_print(stdout, &c.rep, top_conf);
    if (sweep)
        report_sweep(stdout, &c.rep);
    int n;
    double acc, rej, wrong;
    report_totals(&c.rep, &n, &acc, &rej, &wrong);
    if (rc == 0 && gate_acc >= 0.0 && n > 0) {
        int pass = acc >= gate_acc && rej <= gate_rej;
        printf("GATE %s: correct %.2f %% (need >= %.2f), rejected %.2f %% (need <= %.2f)\n", pass ? "PASS" : "FAIL",
               acc, gate_acc, rej, gate_rej);
        rc = pass ? 0 : 1;
    }
    report_free(&c.rep);
    strokes_engine_free(c.e);
    strokes_table_free(c.t);
    return rc;
}
