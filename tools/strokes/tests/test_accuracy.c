/*
 * SPDX-License-Identifier: MIT
 *
 * The synthetic regression gate (report 038 section 5): with the default generator settings and a fixed seed, at
 * least 98 % of the samples are recognised and at most 2 % rejected; random scribbles are mostly rejected. Synthetic
 * samples come from the table's own templates: this guards the code against regressions, it does not measure real
 * stylus accuracy.
 */
#include "strokes_internal.h"
#include "test.h"

#include <stdlib.h>

static void synthetic_gate(void)
{
    strokes_table *t = test_table();
    synth_cfg gen;
    synth_default(&gen);
    strokes_config cfg;
    strokes_config_default(&cfg);
    cfg.ppmm = gen.ppmm;
    strokes_engine *e = strokes_engine_new(t, &cfg);
    synth_rng rng;
    synth_seed(&rng, 1);
    stk_sample *s = malloc(sizeof *s);
    report rep;
    report_init(&rep);
    strokes_result res[8];
    for (int gi = 0; gi < synth_glyph_count(t); ++gi) {
        if (!synth_glyph_live(t, gi))
            continue;
        int set = synth_glyph_set(t, gi);
        for (int k = 0; k < 100; ++k) {
            synth_glyph(t, gi, &gen, &rng, s);
            strokes_set_mode(e, replay_mode_for_set(set));
            int n = replay_sample(e, s, res, 8);
            report_add(&rep, synth_glyph_id(t, gi), set, res, n);
        }
    }
    int n;
    double acc, rej, wrong;
    report_totals(&rep, &n, &acc, &rej, &wrong);
    printf("  synthetic gate: %d samples, correct %.2f %%, rejected %.2f %%, wrong %.2f %%\n", n, acc, rej, wrong);
    CHECK(n == strokes_table_glyph_count(t) * 100);
    CHECKF(acc >= 98.0, "correct %.2f %%", acc);
    CHECKF(rej <= 2.0, "rejected %.2f %%", rej);
    CHECKF(wrong <= 0.5, "wrong %.2f %%", wrong);
    /* no glyph is left behind */
    for (int i = 0; i < rep.nrows; ++i)
        CHECKF(rep.rows[i].ok * 100 >= rep.rows[i].n * 85, "%s: %d of %d", rep.rows[i].id, rep.rows[i].ok,
               rep.rows[i].n);
    report_free(&rep);

    int rejected = 0;
    for (int k = 0; k < 1000; ++k) {
        synth_scribble(&gen, &rng, s);
        strokes_set_mode(e, 0u);
        int nr = replay_sample(e, s, res, 8);
        rejected += nr == 1 && res[0].kind == STROKES_REJECT;
    }
    printf("  random scribbles rejected: %.1f %%\n", rejected / 10.0);
    CHECK(rejected >= 950);
    free(s);
    strokes_engine_free(e);
}

/* Natural b and V (report 041 section 12): on the first board test b and a capital V were refused or read as
 * another letter. Each shape below is written the way people write it without the chart, run through the synthetic
 * generator and read with the tables the launcher loads (glyphs.txt + glyphs-extra.txt). Some shapes are the shipped
 * variants, the ones marked unseen are in no table. Before the fix: the plain V read as u (69 % wrong), the
 * lower-case b refused (87 %, d the best candidate), the B with a bigger lower bump read as d (69 %). The controls
 * (u, d, o, p, h) keep their accuracy. Also at the sizes the taller writing zone invites (15-28 mm). */
static const struct {
    const char *what, *line;
    int min_ok; /* percent */
} natural[] = {
    {"chart B", "b letters b 0,0 1,83 2,59 5,29 11,12 21,6 34,5 45,6 57,11 60,21 58,33 50,43 37,47 27,52 16,56 "
                "27,60 37,63 47,68 57,74 61,85 58,96 45,100 35,100 22,98 9,97 4,97", 90},
    {"lower-case b", "b letters b 0,0 0,50 0,100 0,70 1,58 6,50 15,44 27,42 40,44 51,50 58,60 61,72 59,84 53,93 "
                     "42,99 28,100 14,99 4,97", 90},
    {"lower-case b, wide bowl", "b letters b 0,0 0,100 0,80 2,62 10,52 25,46 45,45 65,48 80,57 86,72 82,86 68,96 "
                                "45,100 20,100 3,97", 90},
    {"b, bowl from the foot", "b letters b 0,0 0,100 15,100 32,99 47,94 57,85 61,73 58,62 49,54 36,50 22,50 10,53 "
                              "0,57", 90},
    {"B, full stem", "b letters b 0,0 0,100 2,55 5,12 20,1 32,0 45,2 55,8 58,18 55,30 45,40 30,46 15,49 30,51 45,55 "
                     "58,63 64,75 62,88 52,96 38,100 0,100", 85},
    {"B, bigger lower bump", "b letters b 0,0 0,95 2,50 6,8 12,2 22,0 33,3 40,12 38,24 30,32 15,38 30,40 48,45 60,55 "
                             "66,70 63,85 52,95 35,100 0,98", 80},
    {"B from the bottom", "b letters b 0,100 1,50 4,10 10,3 20,0 33,0 45,3 53,10 55,20 50,30 38,38 22,44 12,47 25,50 "
                          "40,54 52,61 58,72 57,84 48,94 33,99 2,100", 90},
    {"B, retrace apart (unseen)", "b letters b 0,0 2,90 7,55 13,18 17,8 25,2 35,0 47,2 56,9 59,20 54,32 42,41 25,47 "
                                  "14,50 28,53 44,57 57,65 63,77 60,89 50,96 35,99 2,100", 90},
    {"B, ending on the stem (unseen)", "b letters b 0,0 0,90 2,45 8,8 16,2 28,0 42,3 52,11 54,22 47,33 33,41 16,46 "
                                       "33,50 48,55 58,64 62,76 58,87 46,94 30,96 12,95 3,92", 85},
    {"chart V", "v letters v 0,0 9,46 19,100 28,63 32,33 35,19 37,13 41,10 46,9 52,9 60,10 77,10", 90},
    {"plain V", "v letters v 0,0 25,100 50,0", 90},
    {"narrow V (unseen)", "v letters v 0,0 18,100 36,0", 90},
    {"wide V", "v letters v 0,0 40,100 80,0", 85},
    {"V, short flick", "v letters v 0,0 19,100 31,30 34,15 38,9 44,8 52,8", 90},
    {"V, overshoot (unseen)", "v letters v 0,0 25,100 50,4 55,0", 90},
    {"V, vertical left arm (unseen)", "v letters v 0,0 5,60 9,100 20,70 45,10 50,0", 90},
    {"V, curved left arm (unseen)", "v letters v 0,0 3,30 7,60 12,85 19,100 25,82 30,60 35,35 40,10 42,0", 85},
    {"chart u", "u letters u 1,0 0,30 0,51 2,67 6,81 15,91 31,99 38,100 52,94 60,84 65,70 66,51 66,22 65,3", 90},
    {"wide u (unseen)", "u letters u 0,0 0,40 3,70 12,90 30,100 50,100 68,90 77,70 80,40 80,0", 90},
    {"narrow u", "u letters u 0,0 0,50 2,75 8,92 20,100 32,92 38,75 40,50 40,0", 85},
    {"chart d", "d letters d 0,0 0,87 2,60 5,34 12,15 22,10 34,11 46,15 55,22 61,32 64,50 64,66 62,79 56,89 45,95 "
                "29,99 9,100 2,100", 90},
    {"chart p", "p letters p 1,0 0,100 4,76 7,46 10,22 15,9 25,5 38,4 52,6 63,13 67,26 65,41 60,53 46,57 33,58 17,58",
     90},
    {"chart h", "h letters h 0,0 0,100 4,81 8,64 16,55 27,52 39,52 51,56 60,65 66,76 69,89 70,97", 90},
    {"chart o", "o letters o 37,0 19,8 9,17 3,29 0,45 1,62 5,76 15,90 27,98 42,100 59,97 73,85 82,67 84,51 82,28 74,13 "
                "61,3 59,2", 90},
};

static void natural_variants(void)
{
    strokes_table *t = test_table_full();
    strokes_config cfg;
    strokes_config_default(&cfg);
    strokes_engine *e = strokes_engine_new(t, &cfg);
    stk_sample *s = malloc(sizeof *s);
    strokes_result res[8];
    for (int big = 0; big < 2; ++big) {
        synth_cfg gen;
        synth_default(&gen);
        if (big) {
            gen.size_min_mm = 15.0f;
            gen.size_max_mm = 28.0f;
        }
        synth_rng rng;
        synth_seed(&rng, 41);
        for (size_t i = 0; i < sizeof natural / sizeof natural[0]; ++i) {
            strokes_table *src = strokes_table_new();
            strokes_error err;
            int bad = strokes_table_add_text(src, natural[i].line, "shape", &err);
            CHECKF(bad == 0, "%s: %s", natural[i].what, err.msg);
            int ok = 0, wrong = 0, n = 100;
            for (int k = 0; !bad && k < n; ++k) {
                synth_glyph(src, 0, &gen, &rng, s);
                strokes_set_mode(e, 0u);
                int nr = replay_sample(e, s, res, 8);
                if (nr == 1 && res[0].kind != STROKES_REJECT && strcmp(res[0].glyph, s->label) == 0)
                    ok++;
                else if (nr != 1 || res[0].kind != STROKES_REJECT)
                    wrong++;
            }
            printf("  %-32s %s %3d %% correct, %d %% wrong\n", natural[i].what, big ? "15-28 mm" : " 8-16 mm", ok,
                   wrong);
            CHECKF(ok >= natural[i].min_ok, "%s (%s): %d %% correct", natural[i].what, big ? "large" : "default", ok);
            CHECKF(wrong <= 3, "%s (%s): %d %% wrong", natural[i].what, big ? "large" : "default", wrong);
            strokes_table_free(src);
        }
    }
    free(s);
    strokes_engine_free(e);
}

/* A capital V and a b through the engine as the writing zone feeds it: the shift gesture, then a plain V. */
static void capital_v_and_b(void)
{
    strokes_engine *e = strokes_engine_new(test_table_full(), NULL);
    strokes_result r[4];
    const float shift[] = {300, 650, 300, 560};
    const float vee[] = {200, 520, 240, 640, 280, 520};
    const float bee[] = {200, 520, 200, 640, 200, 590, 215, 572, 245, 568, 268, 585, 272, 615, 258, 636, 230, 640,
                         205, 637};
    CHECK(test_feed(e, STROKES_BOX_LETTERS, shift, 2, 1000000, 0.8f, 10.0f, r, 4) == 1);
    CHECK(r[0].kind == STROKES_MODE && (r[0].mode & STROKES_MODE_SHIFT));
    CHECK(test_feed(e, STROKES_BOX_LETTERS, vee, 3, 3000000, 0.8f, 10.0f, r, 4) == 1);
    CHECKF(r[0].kind == STROKES_CHAR && strcmp(r[0].utf8, "V") == 0, "got %s (%s)", r[0].glyph,
           strokes_reject_name(r[0].reject));
    CHECK(test_feed(e, STROKES_BOX_LETTERS, bee, 10, 5000000, 0.8f, 10.0f, r, 4) == 1);
    CHECKF(r[0].kind == STROKES_CHAR && strcmp(r[0].utf8, "b") == 0, "got %s (%s)", r[0].glyph,
           strokes_reject_name(r[0].reject));
    strokes_engine_free(e);
}

void suite_accuracy(void)
{
    RUN(synthetic_gate);
    RUN(natural_variants);
    RUN(capital_v_and_b);
}
