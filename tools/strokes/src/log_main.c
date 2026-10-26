/*
 * SPDX-License-Identifier: MIT
 *
 * strokes-log: inspects and merges the capture logs of Settings > System > Strokes training (report 042), copied from
 * a board (~/.config/applaunch/strokes-training.txt). The log is a corpus (README "Corpus format"): each sample is the
 * stroke as the panel reported it, labelled with the glyph the user was asked for, after a "# training ..." comment line
 * that says what the board made of it then (phase, verdict, the glyph read, score, size in mm, time).
 *
 *   strokes-log LOG...              per glyph: what the board recorded (train attempts and re-test: recognised,
 *                                   refused, read as another glyph; sizes), the recorded confusions, the session's
 *                                   accuracy before (training attempts) and after (re-test); then the same strokes
 *                                   replayed with the tables now (--table, --overlay: the board's strokes-user.txt) in
 *                                   the per-glyph table of strokes-replay with its confusions
 *   strokes-log --merge OUT LOG...  one corpus with every sample of the logs (their comment lines kept)
 */
#include "sample.h"
#include "tools_common.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 256
#define MAX_OVERLAYS 8
#define MAX_ROWS 256
#define MAX_CONF 512

typedef struct {
    char id[STROKES_ID_MAX];
    int n[2], ok[2], refused[2], wrong[2]; /* [0] training attempts, [1] re-test */
    double w_sum, h_sum;
    float w_min, w_max, h_min, h_max;
    int sized;
} row;

typedef struct {
    char want[STROKES_ID_MAX], got[STROKES_ID_MAX + 1];
    int count;
} conf;

typedef struct {
    /* recorded */
    row rows[MAX_ROWS];
    int nrows;
    conf confs[MAX_CONF];
    int nconf;
    long samples, commented;
    /* replay */
    strokes_table *t;
    strokes_config cfg;
    strokes_engine *e;
    float e_ppmm;
    report rep;
    int verbose;
    const char *phase; /* NULL: every sample */
    FILE *merge;
    const char *file;
    long line_no;
} log_ctx;

static void usage(FILE *f)
{
    fprintf(f, "usage: strokes-log [options] LOG...\n"
               "  --merge OUT       write every sample of the logs to OUT as one corpus (comment lines kept)\n"
               "  --phase P         only the samples of phase P (train or retest)\n"
               "  --table FILE      glyph table for the replay (default: the shipped glyphs.txt + glyphs-extra.txt)\n"
               "  --overlay FILE    user overlay for the replay (repeatable; the board's strokes-user.txt)\n"
               "  --conf N          show the N most frequent confusions (default 20)\n"
               "  -v                one line per sample (recorded and replayed)\n");
}

/* value of "key=" in a "# training ..." comment, "" when absent */
static void field(const char *comment, const char *key, char *out, size_t size)
{
    out[0] = 0;
    char pat[32];
    snprintf(pat, sizeof pat, " %s=", key);
    const char *p = strstr(comment, pat);
    if (!p)
        return;
    p += strlen(pat);
    size_t n = 0;
    while (p[n] && !isspace((unsigned char)p[n]) && n + 1 < size)
        n++;
    memcpy(out, p, n);
    out[n] = 0;
}

static row *row_for(log_ctx *c, const char *id)
{
    for (int i = 0; i < c->nrows; ++i)
        if (strcmp(c->rows[i].id, id) == 0)
            return &c->rows[i];
    if (c->nrows >= MAX_ROWS)
        return NULL;
    row *r = &c->rows[c->nrows++];
    memset(r, 0, sizeof *r);
    snprintf(r->id, sizeof r->id, "%s", id);
    return r;
}

static void add_conf(log_ctx *c, const char *want, const char *got)
{
    for (int i = 0; i < c->nconf; ++i)
        if (strcmp(c->confs[i].want, want) == 0 && strcmp(c->confs[i].got, got) == 0) {
            c->confs[i].count++;
            return;
        }
    if (c->nconf >= MAX_CONF)
        return;
    conf *k = &c->confs[c->nconf++];
    snprintf(k->want, sizeof k->want, "%s", want);
    snprintf(k->got, sizeof k->got, "%s", got);
    k->count = 1;
}

static void record(log_ctx *c, const stk_sample *s, const char *comment)
{
    char phase[16], verdict[16], got[STROKES_ID_MAX], size[32];
    field(comment, "phase", phase, sizeof phase);
    field(comment, "verdict", verdict, sizeof verdict);
    field(comment, "got", got, sizeof got);
    field(comment, "size_mm", size, sizeof size);
    row *r = row_for(c, s->label);
    if (!r)
        return;
    const int ph = strcmp(phase, "retest") == 0 ? 1 : 0;
    r->n[ph]++;
    if (strcmp(verdict, "ok") == 0) {
        r->ok[ph]++;
    } else if (strcmp(verdict, "refused") == 0) {
        r->refused[ph]++;
        char g[STROKES_ID_MAX + 1];
        snprintf(g, sizeof g, "~%s", strcmp(got, "-") == 0 || !got[0] ? "?" : got);
        add_conf(c, s->label, g);
    } else if (strcmp(verdict, "wrong") == 0) {
        r->wrong[ph]++;
        add_conf(c, s->label, got);
    }
    float w = 0, h = 0;
    if (sscanf(size, "%fx%f", &w, &h) == 2) {
        if (!r->sized || w < r->w_min) r->w_min = w;
        if (!r->sized || w > r->w_max) r->w_max = w;
        if (!r->sized || h < r->h_min) r->h_min = h;
        if (!r->sized || h > r->h_max) r->h_max = h;
        r->w_sum += w;
        r->h_sum += h;
        r->sized++;
    }
}

static void replay(log_ctx *c, const stk_sample *s, const char *comment)
{
    if (!c->e || c->e_ppmm != s->ppmm) {
        strokes_engine_free(c->e);
        c->cfg.ppmm = s->ppmm;
        c->e = strokes_engine_new(c->t, &c->cfg);
        c->e_ppmm = s->ppmm;
        if (!c->e)
            return;
    }
    const int set = tools_label_set(c->t, s->label, s->box);
    if (set < 0)
        return;
    strokes_set_mode(c->e, replay_mode_for_set(set));
    strokes_result res[8];
    const int n = replay_sample(c->e, s, res, 8);
    report_add(&c->rep, s->label, set, res, n);
    if (c->verbose)
        printf("%s:%ld %-8s now %s%s score %.3f | then: %s\n", c->file, c->line_no, s->label,
               n == 1 && res[0].kind == STROKES_REJECT ? "~" : "", n >= 1 && res[0].glyph[0] ? res[0].glyph : "-",
               n >= 1 ? (double)res[0].confidence : 0.0, comment[0] ? comment + 2 : "(no comment)");
}

static int read_log(log_ctx *c, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    c->file = path;
    c->line_no = 0;
    static char line[1 << 16];
    static stk_sample s;
    char comment[512] = "";
    while (fgets(line, sizeof line, f)) {
        c->line_no++;
        if (strncmp(line, "# training ", 11) == 0) {
            size_t len = strcspn(line, "\r\n");
            if (len >= sizeof comment)
                len = sizeof comment - 1;
            memcpy(comment, line, len);
            comment[len] = 0;
            continue;
        }
        char msg[160];
        const int k = corpus_parse_line(line, &s, msg, sizeof msg);
        if (k < 0) {
            fprintf(stderr, "%s:%ld: %s\n", path, c->line_no, msg);
            fclose(f);
            return -1;
        }
        if (k == 0)
            continue;
        char phase[16];
        field(comment, "phase", phase, sizeof phase);
        if (!c->phase || strcmp(phase, c->phase) == 0) {
            c->samples++;
            if (comment[0])
                c->commented++;
            if (c->merge) {
                if (comment[0])
                    fprintf(c->merge, "%s\n", comment);
                corpus_write(c->merge, &s);
            } else {
                if (comment[0] && strcmp(s.label, "?") != 0)
                    record(c, &s, comment);
                if (strcmp(s.label, "?") != 0)
                    replay(c, &s, comment);
            }
        }
        comment[0] = 0;
    }
    fclose(f);
    return 0;
}

static int conf_cmp(const void *a, const void *b)
{
    return ((const conf *)b)->count - ((const conf *)a)->count;
}

static double pct(int a, int b)
{
    return b ? 100.0 * a / b : 0.0;
}

static void print_recorded(log_ctx *c, int top)
{
    printf("\nrecorded on the board (%ld sample(s), %ld with a training comment)\n", c->samples, c->commented);
    printf("%-10s %16s %16s %8s   %-14s\n", "glyph", "train ok/ref/wr", "re-test ok/n", "", "size mm (w x h)");
    int n[2] = {0, 0}, ok[2] = {0, 0};
    for (int i = 0; i < c->nrows; ++i) {
        const row *r = &c->rows[i];
        char a[32], b[32], sz[48] = "-";
        snprintf(a, sizeof a, "%d/%d/%d of %d", r->ok[0], r->refused[0], r->wrong[0], r->n[0]);
        snprintf(b, sizeof b, "%d/%d", r->ok[1], r->n[1]);
        if (r->sized)
            snprintf(sz, sizeof sz, "%.1f x %.1f (%.0f-%.0f x %.0f-%.0f)", r->w_sum / r->sized, r->h_sum / r->sized,
                     (double)r->w_min, (double)r->w_max, (double)r->h_min, (double)r->h_max);
        printf("%-10s %16s %16s %7.0f%%   %s\n", r->id, a, b, pct(r->ok[0], r->n[0]), sz);
        for (int p = 0; p < 2; ++p) {
            n[p] += r->n[p];
            ok[p] += r->ok[p];
        }
    }
    printf("session accuracy: before (training attempts) %d/%d = %.1f %%, after (re-test) %d/%d = %.1f %%\n", ok[0],
           n[0], pct(ok[0], n[0]), ok[1], n[1], pct(ok[1], n[1]));
    qsort(c->confs, (size_t)c->nconf, sizeof c->confs[0], conf_cmp);
    if (c->nconf)
        printf("recorded confusions (asked -> read; ~ = refused, its best candidate):\n");
    for (int i = 0; i < c->nconf && i < top; ++i)
        printf("  %-10s -> %-10s %d\n", c->confs[i].want, c->confs[i].got, c->confs[i].count);
}

int main(int argc, char **argv)
{
    char tbuf[512], ebuf[600];
    const char *table = NULL, *overlays[MAX_OVERLAYS + 1], *files[MAX_FILES], *merge_path = NULL;
    int n_ov = 0, n_files = 0, top = 20;
    log_ctx *c = calloc(1, sizeof *c);
    if (!c)
        return 2;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            free(c);
            return 0;
        } else if (strcmp(a, "-v") == 0) {
            c->verbose = 1;
        } else if (a[0] == '-' && a[1] == '-' && !v) {
            fprintf(stderr, "%s needs a value (or is unknown; see -h)\n", a);
            free(c);
            return 2;
        } else if (strcmp(a, "--merge") == 0) {
            merge_path = argv[++i];
        } else if (strcmp(a, "--phase") == 0) {
            c->phase = argv[++i];
        } else if (strcmp(a, "--table") == 0) {
            table = argv[++i];
        } else if (strcmp(a, "--overlay") == 0) {
            if (n_ov < MAX_OVERLAYS)
                overlays[n_ov++] = argv[i + 1];
            i++;
        } else if (strcmp(a, "--conf") == 0) {
            top = atoi(argv[++i]);
        } else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "unknown option %s\n", a);
            usage(stderr);
            free(c);
            return 2;
        } else if (n_files < MAX_FILES) {
            files[n_files++] = a;
        }
    }
    if (n_files == 0) {
        usage(stderr);
        free(c);
        return 2;
    }
    int rc = 0;
    if (merge_path) {
        c->merge = fopen(merge_path, "w");
        if (!c->merge) {
            fprintf(stderr, "cannot write %s\n", merge_path);
            free(c);
            return 2;
        }
        char note[64];
        snprintf(note, sizeof note, "merged by strokes-log from %d file(s)", n_files);
        corpus_write_header(c->merge, "strokes-log", note);
        for (int i = 0; i < n_files && rc == 0; ++i)
            if (read_log(c, files[i]) != 0)
                rc = 2;
        fclose(c->merge);
        printf("%ld sample(s) written to %s\n", c->samples, merge_path);
        free(c);
        return rc;
    }
    /* the replay uses the tables the launcher loads: glyphs.txt, glyphs-extra.txt next to it, then the overlays */
    if (!table) {
        table = tools_default_table(argv[0], tbuf, sizeof tbuf);
        snprintf(ebuf, sizeof ebuf, "%s", table);
        char *slash = strrchr(ebuf, '/');
        snprintf(slash ? slash + 1 : ebuf, sizeof ebuf - (size_t)(slash ? slash + 1 - ebuf : 0), "glyphs-extra.txt");
        FILE *probe = fopen(ebuf, "rb");
        if (probe) {
            fclose(probe);
            memmove(overlays + 1, overlays, (size_t)n_ov * sizeof overlays[0]);
            overlays[0] = ebuf;
            n_ov++;
        }
    }
    c->t = tools_load_table(table, overlays, n_ov);
    if (!c->t) {
        free(c);
        return 2;
    }
    strokes_config_default(&c->cfg);
    report_init(&c->rep);
    printf("strokes-log %s: replay with %s + %d overlay(s): %d glyphs, %d templates\n", STROKES_VERSION, table, n_ov,
           strokes_table_glyph_count(c->t), strokes_table_template_count(c->t));
    for (int i = 0; i < n_files && rc == 0; ++i)
        if (read_log(c, files[i]) != 0)
            rc = 2;
    print_recorded(c, top);
    printf("\nreplayed with the tables above:\n");
    report_print(stdout, &c->rep, top);
    report_free(&c->rep);
    strokes_engine_free(c->e);
    strokes_table_free(c->t);
    free(c);
    return rc;
}
