/*
 * SPDX-License-Identifier: MIT
 *
 * strokes-bench: time per recognition on this machine (README "Performance"). Times the whole engine path per
 * stroke (frames in, drop-out healing, clean-up, features, gates, matching, modes), and the clean-up and the
 * matcher alone. Built for aarch64 too (make aarch64), so the same command gives the real number on a board.
 */
#include "sample.h"
#include "strokes_internal.h"
#include "tools_common.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void cpu_name(char *buf, size_t size)
{
    snprintf(buf, size, "unknown CPU");
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "model name", 10) == 0 || strncmp(line, "Model", 5) == 0) {
            char *c = strchr(line, ':');
            if (c) {
                c++;
                while (*c == ' ' || *c == '\t')
                    c++;
                c[strcspn(c, "\n")] = 0;
                snprintf(buf, size, "%s", c);
                break;
            }
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    char tbuf[512];
    const char *table = NULL;
    int n = 400, reps = 10;
    unsigned long long seed = 7;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(a, "--table") == 0 && v)
            table = argv[++i];
        else if (strcmp(a, "--n") == 0 && v)
            n = atoi(argv[++i]);
        else if (strcmp(a, "--reps") == 0 && v)
            reps = atoi(argv[++i]);
        else if (strcmp(a, "--seed") == 0 && v)
            seed = strtoull(argv[++i], NULL, 0);
        else {
            fprintf(stderr, "usage: strokes-bench [--table FILE] [--n SAMPLES] [--reps R] [--seed S]\n");
            return 2;
        }
    }
    if (n < 1)
        n = 1;
    if (reps < 1)
        reps = 1;
    if (!table)
        table = tools_default_table(argv[0], tbuf, sizeof tbuf);
    strokes_table *t = tools_load_table(table, NULL, 0);
    if (!t)
        return 2;
    synth_cfg gen;
    synth_default(&gen);
    strokes_config cfg;
    strokes_config_default(&cfg);
    cfg.ppmm = gen.ppmm;

    stk_sample *s = malloc((size_t)n * sizeof *s);
    stk_vec(*fine)[STK_FINE] = malloc((size_t)n * sizeof *fine);
    int *nfine = malloc((size_t)n * sizeof *nfine);
    stk_feat *feat = malloc((size_t)n * sizeof *feat);
    int *sets = malloc((size_t)n * sizeof *sets);
    if (!s || !fine || !nfine || !feat || !sets) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    synth_rng rng;
    synth_seed(&rng, seed);
    int ng = synth_glyph_count(t), gi = 0;
    long pts = 0;
    int k = 0;
    while (k < n) {
        gi = (gi + 1) % ng;
        if (!synth_glyph_live(t, gi) || synth_glyph_set(t, gi) == STROKES_SET_PUNCT ||
            synth_glyph_set(t, gi) == STROKES_SET_EXT)
            continue;
        if (synth_glyph(t, gi, &gen, &rng, &s[k]))
            continue;
        sets[k] = s[k].box == STROKES_BOX_DIGITS ? STROKES_SET_DIGITS : STROKES_SET_LETTERS;
        pts += s[k].n;
        k++;
    }
    strokes_engine *e = strokes_engine_new(t, &cfg);
    strokes_result res[8];
    volatile int sink = 0;

    /* A: the whole engine path per stroke */
    double t0 = now_ns();
    for (int r = 0; r < reps; ++r)
        for (int i = 0; i < n; ++i) {
            strokes_set_mode(e, 0u);
            sink += replay_sample(e, &s[i], res, 8);
        }
    double full = (now_ns() - t0) / ((double)n * reps);

    /* B: clean-up + features alone (single contact, as one stroke) */
    stk_vec pts32[STK_N];
    float path_mm;
    t0 = now_ns();
    for (int r = 0; r < reps; ++r)
        for (int i = 0; i < n; ++i) {
            float dur = (float)(s[i].p[s[i].n - 1].t_us - s[i].p[0].t_us) / 1000.0f;
            sink += stk_clean(&cfg, s[i].p, s[i].n, dur, fine[i], &nfine[i], &path_mm);
            stk_resample(fine[i], nfine[i], pts32, STK_N);
            stk_features(pts32, STK_N, &feat[i]);
        }
    double clean = (now_ns() - t0) / ((double)n * reps);

    /* C: gates + matching alone, every template of the set */
    t0 = now_ns();
    for (int r = 0; r < reps; ++r)
        for (int i = 0; i < n; ++i) {
            strokes_result rr;
            memset(&rr, 0, sizeof rr);
            sink += stk_match(t, &cfg, sets[i], fine[i], nfine[i], &feat[i], &rr);
        }
    double match = (now_ns() - t0) / ((double)n * reps);
    (void)sink;

    char cpu[128];
    cpu_name(cpu, sizeof cpu);
    int ntpl = strokes_table_template_count(t);
    printf("strokes-bench %s on %s (%s)\n", STROKES_VERSION, cpu,
#ifdef __OPTIMIZE__
           "optimised build"
#else
           "unoptimised build"
#endif
    );
    printf("table %s: %d glyphs, %d templates; %d synthetic strokes (report 038 settings), %.0f raw points on average, "
           "%d repetitions\n",
           table, strokes_table_glyph_count(t), ntpl, n, (double)pts / n, reps);
    printf("whole engine path per stroke  : %8.0f ns\n", full);
    printf("  clean-up + features         : %8.0f ns\n", clean);
    printf("  resampling, gates, matching : %8.0f ns (3 aspect variants x every template of the set)\n", match);
#if defined(__x86_64__) || defined(__i386__)
    printf("estimate, Cortex-A53 boards: 10-20 x this PC (the factor of reports 015 and 037): Pi Zero 2 W (1.0 GHz) "
           "and Pi 3A+ (1.4 GHz) about %.0f-%.0f us per stroke; run build/aarch64/strokes-bench on the board to "
           "measure it\n",
           full * 10.0 / 1000.0, full * 20.0 / 1000.0);
#else
    printf("measured on this machine: %.1f us per stroke\n", full / 1000.0);
#endif
    strokes_engine_free(e);
    free(s);
    free(fine);
    free(nfine);
    free(feat);
    free(sets);
    strokes_table_free(t);
    return 0;
}
