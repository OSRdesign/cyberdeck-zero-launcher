/*
 * SPDX-License-Identifier: MIT
 *
 * strokes-eval: synthetic accuracy of the Strokes recogniser, per glyph (README "Evaluation").
 */
#include "sample.h"
#include "tools_common.h"

#include <stdlib.h>
#include <string.h>

#define MAX_OVERLAYS 8
#define MAX_KV 32

static void usage(FILE *f)
{
    fprintf(f,
            "usage: strokes-eval [options]\n"
            "  --table FILE      glyph table (default: the shipped glyphs.txt)\n"
            "  --overlay FILE    user overlay, applied after the table (repeatable)\n"
            "  --seed N          generator seed (default 1)\n"
            "  --n N             samples per glyph (default 200)\n"
            "  --set NAME        only the glyphs of one set: letters, digits, punct, ext, any\n"
            "  --glyph ID        only this glyph\n"
            "  --hard            the harsher generator settings\n"
            "  --gen KEY=VALUE   one generator setting (repeatable; see README)\n"
            "  --cfg KEY=VALUE   one recogniser setting (repeatable; see README)\n"
            "  --garbage N       also N random scribbles, reported as the share rejected\n"
            "  --write FILE      also write the samples as a corpus file\n"
            "  --evtest FILE     also write them as evtest-style text, and FILE.labels with the labels in order\n"
            "  --sweep           print a threshold sweep\n"
            "  --conf N          show the N most frequent confusions (default 12)\n"
            "  --gate ACC,REJ    exit 1 unless correct >= ACC %% and rejected <= REJ %%\n"
            "  -v                one line per sample that is not correct\n");
}

static void print_result_line(FILE *f, const char *want, const strokes_result *r)
{
    fprintf(f, "  want %-6s got %s%-6s %-12s score %.3f margin %.3f healed %d corners %d turn %4.0f dirs %-10s cand", want,
            r->kind == STROKES_REJECT ? "~" : "", r->glyph[0] ? r->glyph : "-", strokes_reject_name(r->reject),
            (double)r->confidence, (double)r->margin, r->healed, r->corners, (double)r->turn_deg, r->dirs);
    for (int i = 0; i < r->n_cand; ++i)
        fprintf(f, " %s%s %.3f", r->cand[i].gated ? "!" : "", r->cand[i].glyph, (double)r->cand[i].score);
    fprintf(f, "\n");
}

int main(int argc, char **argv)
{
    char tbuf[512];
    const char *table = NULL, *overlays[MAX_OVERLAYS], *gen_kv[MAX_KV], *cfg_kv[MAX_KV];
    int n_ov = 0, n_gen = 0, n_cfg = 0;
    unsigned long long seed = 1;
    int n = 200, only_set = -1, hard = 0, garbage = 0, sweep = 0, top_conf = 12, verbose = 0;
    const char *only_glyph = NULL, *write_path = NULL, *evtest_path = NULL;
    double gate_acc = -1.0, gate_rej = -1.0;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NEED_ARG                                                                                                   \
    do {                                                                                                           \
        if (!v) {                                                                                                  \
            fprintf(stderr, "%s needs a value\n", a);                                                              \
            return 2;                                                                                              \
        }                                                                                                          \
        i++;                                                                                                       \
    } while (0)
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "--table") == 0) {
            NEED_ARG;
            table = v;
        } else if (strcmp(a, "--overlay") == 0) {
            NEED_ARG;
            if (n_ov < MAX_OVERLAYS)
                overlays[n_ov++] = v;
        } else if (strcmp(a, "--seed") == 0) {
            NEED_ARG;
            seed = strtoull(v, NULL, 0);
        } else if (strcmp(a, "--n") == 0) {
            NEED_ARG;
            n = atoi(v);
        } else if (strcmp(a, "--set") == 0) {
            NEED_ARG;
            only_set = strokes_set_from_name(v);
            if (only_set < 0) {
                fprintf(stderr, "unknown set %s\n", v);
                return 2;
            }
        } else if (strcmp(a, "--glyph") == 0) {
            NEED_ARG;
            only_glyph = v;
        } else if (strcmp(a, "--hard") == 0) {
            hard = 1;
        } else if (strcmp(a, "--gen") == 0) {
            NEED_ARG;
            if (n_gen < MAX_KV)
                gen_kv[n_gen++] = v;
        } else if (strcmp(a, "--cfg") == 0) {
            NEED_ARG;
            if (n_cfg < MAX_KV)
                cfg_kv[n_cfg++] = v;
        } else if (strcmp(a, "--garbage") == 0) {
            NEED_ARG;
            garbage = atoi(v);
        } else if (strcmp(a, "--write") == 0) {
            NEED_ARG;
            write_path = v;
        } else if (strcmp(a, "--evtest") == 0) {
            NEED_ARG;
            evtest_path = v;
        } else if (strcmp(a, "--sweep") == 0) {
            sweep = 1;
        } else if (strcmp(a, "--conf") == 0) {
            NEED_ARG;
            top_conf = atoi(v);
        } else if (strcmp(a, "--gate") == 0) {
            NEED_ARG;
            if (sscanf(v, "%lf,%lf", &gate_acc, &gate_rej) != 2) {
                fprintf(stderr, "--gate wants ACC,REJ\n");
                return 2;
            }
        } else if (strcmp(a, "-v") == 0) {
            verbose = 1;
        } else {
            fprintf(stderr, "unknown option %s\n", a);
            usage(stderr);
            return 2;
        }
#undef NEED_ARG
    }
    if (n < 1)
        n = 1;
    if (!table)
        table = tools_default_table(argv[0], tbuf, sizeof tbuf);
    strokes_table *t = tools_load_table(table, overlays, n_ov);
    if (!t)
        return 2;

    synth_cfg gen;
    if (hard)
        synth_hard(&gen);
    else
        synth_default(&gen);
    for (int i = 0; i < n_gen; ++i)
        if (synth_set(&gen, gen_kv[i])) {
            fprintf(stderr, "bad --gen %s\n", gen_kv[i]);
            return 2;
        }
    strokes_config cfg;
    strokes_config_default(&cfg);
    cfg.ppmm = gen.ppmm;
    for (int i = 0; i < n_cfg; ++i)
        if (tools_cfg_set(&cfg, cfg_kv[i])) {
            fprintf(stderr, "bad --cfg %s\n", cfg_kv[i]);
            return 2;
        }
    strokes_engine *e = strokes_engine_new(t, &cfg);
    if (!e)
        return 2;

    FILE *wf = NULL, *ef = NULL, *lf = NULL;
    if (write_path) {
        wf = fopen(write_path, "w");
        if (!wf) {
            perror(write_path);
            return 2;
        }
        corpus_write_header(wf, "strokes-eval (synthetic)", NULL);
    }
    if (evtest_path) {
        char lp[600];
        snprintf(lp, sizeof lp, "%s.labels", evtest_path);
        ef = fopen(evtest_path, "w");
        lf = fopen(lp, "w");
        if (!ef || !lf) {
            perror(evtest_path);
            return 2;
        }
        evtest_write_header(ef, 480, 800);
    }

    char desc[1024];
    printf("strokes-eval %s: table %s (%d glyphs, %d templates), %d overlay(s), seed %llu, %d samples per glyph\n",
           STROKES_VERSION, table, strokes_table_glyph_count(t), strokes_table_template_count(t), n_ov, seed, n);
    synth_describe(&gen, desc, sizeof desc);
    printf("generator%s: %s\n", hard ? " (hard)" : "", desc);
    tools_cfg_describe(&cfg, desc, sizeof desc);
    printf("recogniser: %s\n", desc);

    synth_rng rng;
    synth_seed(&rng, seed);
    stk_sample *s = malloc(sizeof *s);
    report rep;
    report_init(&rep);
    strokes_result res[8];
    int tracking = 100;
    unsigned long long k_ev = 0;
    for (int gi = 0; gi < synth_glyph_count(t); ++gi) {
        if (!synth_glyph_live(t, gi))
            continue;
        int set = synth_glyph_set(t, gi);
        const char *id = synth_glyph_id(t, gi);
        if (only_set >= 0 && set != only_set)
            continue;
        if (only_glyph && strcmp(only_glyph, id) != 0)
            continue;
        for (int k = 0; k < n; ++k) {
            if (synth_glyph(t, gi, &gen, &rng, s))
                continue;
            strokes_set_mode(e, replay_mode_for_set(set));
            int nres = replay_sample(e, s, res, 8);
            report_add(&rep, id, set, res, nres);
            if (verbose && (nres != 1 || res[0].kind == STROKES_REJECT || strcmp(res[0].glyph, id) != 0)) {
                if (nres == 1)
                    print_result_line(stdout, id, &res[0]);
                else
                    printf("  want %-6s %d results (split)\n", id, nres);
            }
            if (wf)
                corpus_write(wf, s);
            if (ef) {
                evtest_write_sample(ef, s, 1000000000ull + k_ev * 3000000ull, &tracking);
                fprintf(lf, "%s\n", id);
                k_ev++;
            }
        }
    }
    printf("\n");
    report_print(stdout, &rep, top_conf);
    if (sweep)
        report_sweep(stdout, &rep);

    if (garbage > 0) {
        int rejected = 0;
        for (int k = 0; k < garbage; ++k) {
            synth_scribble(&gen, &rng, s);
            strokes_set_mode(e, 0u);
            int nres = replay_sample(e, s, res, 8);
            if (nres == 1 && res[0].kind == STROKES_REJECT)
                rejected++;
        }
        printf("random scribbles: %d, rejected %.1f %% (a random shape can be a real glyph: 100 %% is not the goal)\n",
               garbage, 100.0 * rejected / garbage);
    }

    int total;
    double acc, rej, wrong;
    report_totals(&rep, &total, &acc, &rej, &wrong);
    int rc = 0;
    if (gate_acc >= 0.0 && (acc < gate_acc || rej > gate_rej)) {
        printf("GATE FAIL: correct %.2f %% (need >= %.2f), rejected %.2f %% (need <= %.2f)\n", acc, gate_acc, rej,
               gate_rej);
        rc = 1;
    } else if (gate_acc >= 0.0) {
        printf("GATE PASS: correct %.2f %% >= %.2f, rejected %.2f %% <= %.2f\n", acc, gate_acc, rej, gate_rej);
    }
    if (wf)
        fclose(wf);
    if (ef)
        fclose(ef);
    if (lf)
        fclose(lf);
    report_free(&rep);
    free(s);
    strokes_engine_free(e);
    strokes_table_free(t);
    return rc;
}
