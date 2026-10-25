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

void suite_accuracy(void)
{
    RUN(synthetic_gate);
}
