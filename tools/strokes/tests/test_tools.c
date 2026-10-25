/*
 * SPDX-License-Identifier: MIT
 *
 * PC tools: corpus format, replay harness, synthetic generator, evdev / evtest parser of the capture tool, report.
 */
#include "evparse.h"
#include "strokes_internal.h"
#include "test.h"

#include <stdlib.h>

static int same_sample(const stk_sample *a, const stk_sample *b, float eps)
{
    if (a->n != b->n || a->nl != b->nl || strcmp(a->label, b->label) != 0 || a->box != b->box)
        return 0;
    uint64_t a0 = a->p[0].t_us, b0 = b->p[0].t_us;
    for (int i = 0; i < a->n; ++i) {
        if (fabsf(a->p[i].x - b->p[i].x) > eps || fabsf(a->p[i].y - b->p[i].y) > eps)
            return 0;
        long long da = (long long)(a->p[i].t_us - a0), db = (long long)(b->p[i].t_us - b0);
        if (llabs(da - db) > 1)
            return 0;
    }
    for (int i = 0; i < a->nl; ++i)
        if (a->lift_after[i] != b->lift_after[i] ||
            llabs((long long)(a->lift_t[i] - a0) - (long long)(b->lift_t[i] - b0)) > 1)
            return 0;
    return 1;
}

static void corpus_round_trip(void)
{
    strokes_table *t = test_table();
    synth_cfg gen;
    synth_default(&gen);
    gen.drop_p = 1.0f; /* every sample has a drop-out: two contacts */
    gen.quantise = 0;  /* sub-pixel coordinates: the format keeps 0.1 px */
    synth_rng rng;
    synth_seed(&rng, 3);
    static stk_sample orig[16];
    stk_sample *back = malloc(sizeof *back);
    FILE *f = tmpfile();
    CHECK(f != NULL);
    int written = 0;
    corpus_write_header(f, "test", "note");
    for (int k = 0; k < 200 && written < 16; ++k) {
        int gi = (k * 3) % synth_glyph_count(t);
        if (!synth_glyph_live(t, gi) || synth_glyph(t, gi, &gen, &rng, &orig[written]))
            continue;
        corpus_write(f, &orig[written]);
        written++;
    }
    rewind(f);
    static char line[65536];
    int read_back = 0, ok = 1;
    while (fgets(line, sizeof line, f)) {
        char msg[96];
        int r = corpus_parse_line(line, back, msg, sizeof msg);
        if (r == 0)
            continue;
        CHECKF(r == 1, "%s", msg);
        if (read_back < written)
            ok &= same_sample(&orig[read_back], back, 0.051f);
        read_back++;
    }
    CHECK(ok);
    CHECK(read_back == written && written == 16);
    CHECK(orig[0].nl == 2 || orig[1].nl == 2);
    fclose(f);
    free(back);
}

static void corpus_errors(void)
{
    stk_sample *s = malloc(sizeof *s);
    char msg[96];
    CHECK(corpus_parse_line("# comment", s, msg, sizeof msg) == 0);
    CHECK(corpus_parse_line("   ", s, msg, sizeof msg) == 0);
    CHECK(corpus_parse_line("", s, msg, sizeof msg) == 0);
    CHECK(corpus_parse_line("a letters 8.54 0,1,2", s, msg, sizeof msg) == -1);         /* spaces, not tabs */
    CHECK(corpus_parse_line("a\tboxes\t8.54\t0,1,2", s, msg, sizeof msg) == -1);        /* bad box */
    CHECK(corpus_parse_line("a\tletters\t-1\t0,1,2", s, msg, sizeof msg) == -1);        /* bad ppmm */
    CHECK(corpus_parse_line("a\tletters\t8.54\t0,1", s, msg, sizeof msg) == -1);        /* two fields */
    CHECK(corpus_parse_line("a\tletters\t8.54\t^5 0,1,2", s, msg, sizeof msg) == -1);   /* lift first */
    CHECK(corpus_parse_line("a\tletters\t8.54\t0,x,2", s, msg, sizeof msg) == -1);
    CHECK(corpus_parse_line("a\tletters\t8.54\t0,1e9,2", s, msg, sizeof msg) == -1);
    CHECK(corpus_parse_line("a\tletters\t8.54\t", s, msg, sizeof msg) == -1); /* no points */
    CHECK(corpus_parse_line("\tletters\t8.54\t0,1,2", s, msg, sizeof msg) == -1);
    CHECK(corpus_parse_line("a\tdigits\t8.54\t0,1,2 10,1,5", s, msg, sizeof msg) == 1);
    CHECK(s->n == 2 && s->nl == 1 && s->lift_after[0] == 1 && s->box == STROKES_BOX_DIGITS); /* final lift added */
    CHECK(corpus_parse_line("a\tletters\t8.54\t0,1,2 10,1,5 ^12 40,1,9 50,1,12 ^60\r\n", s, msg, sizeof msg) == 1);
    CHECK(s->n == 4 && s->nl == 2 && s->lift_after[0] == 1 && s->lift_t[0] == 12000 && s->lift_t[1] == 60000);
    CHECK(corpus_parse_line("a\tletters\t8.54\t10,1,2 5,1,5", s, msg, sizeof msg) == 1);
    CHECK(s->p[1].t_us == s->p[0].t_us); /* time never runs backwards */
    char m2[64];
    CHECK(corpus_read_file("/nonexistent/corpus.txt", NULL, NULL, m2, sizeof m2) == -1);
    free(s);
}

static void replay_heals(void)
{
    stk_sample *s = malloc(sizeof *s);
    char msg[96];
    /* an i with a 30 ms drop-out in the middle, 1 mm sideways */
    CHECK(corpus_parse_line("i\tletters\t8.54\t0,250,500 10,250,510 20,250,520 30,250,530 40,250,540 ^50 "
                            "80,258,570 90,258,580 100,258,590 110,258,600 ^115",
                            s, msg, sizeof msg) == 1);
    strokes_engine *e = strokes_engine_new(test_table(), NULL);
    strokes_result r[4];
    CHECK(replay_sample(e, s, r, 4) == 1 && strcmp(r[0].glyph, "i") == 0 && r[0].healed == 1);
    /* the same with a 200 ms gap: two strokes */
    CHECK(corpus_parse_line("i\tletters\t8.54\t0,250,500 10,250,510 20,250,520 30,250,530 40,250,540 ^50 "
                            "250,258,570 260,258,580 270,258,590 280,258,600 ^285",
                            s, msg, sizeof msg) == 1);
    CHECK(replay_sample(e, s, r, 4) == 2);
    strokes_engine_free(e);
    free(s);
}

static void synth_deterministic(void)
{
    strokes_table *t = test_table();
    synth_cfg gen;
    synth_default(&gen);
    stk_sample *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    synth_rng r1, r2;
    synth_seed(&r1, 42);
    synth_seed(&r2, 42);
    int same = 1;
    for (int gi = 0; gi < synth_glyph_count(t); ++gi) {
        if (!synth_glyph_live(t, gi))
            continue;
        synth_glyph(t, gi, &gen, &r1, a);
        synth_glyph(t, gi, &gen, &r2, b);
        same &= same_sample(a, b, 0.0f);
    }
    CHECK(same);
    synth_seed(&r2, 43);
    synth_glyph(t, 0, &gen, &r1, a);
    synth_glyph(t, 0, &gen, &r2, b);
    CHECK(!same_sample(a, b, 0.0f));
    CHECK(synth_glyph(t, -1, &gen, &r1, a) == -1 && synth_glyph(t, 9999, &gen, &r1, a) == -1);
    CHECK(synth_set(&gen, "rot=20") == 0 && gen.rot_deg == 20.0f);
    CHECK(synth_set(&gen, "quantise=0") == 0 && gen.quantise == 0);
    CHECK(synth_set(&gen, "nosuch=1") == -1 && synth_set(&gen, "rot") == -1 && synth_set(&gen, "rot=x") == -1);
    synth_scribble(&gen, &r1, a);
    CHECK(a->n > 2 && strcmp(a->label, "?") == 0);
    free(a);
    free(b);
}

/* ---- evdev parser ---- */

typedef struct {
    stk_sample s[8];
    int n;
} collect;

static void on_sample(void *ctx, stk_sample *s)
{
    collect *c = ctx;
    if (c->n < 8)
        c->s[c->n++] = *s;
}

static void ev(evp *p, uint64_t t, int type, int code, int value)
{
    evp_event(p, t, type, code, value);
}

static void syn(evp *p, uint64_t t)
{
    evp_event(p, t, EVP_EV_SYN, EVP_SYN_REPORT, 0);
}

static void mt_contact(evp *p, uint64_t t, int slot, int tid, int x, int y)
{
    ev(p, t, EVP_EV_ABS, EVP_ABS_MT_SLOT, slot);
    if (tid != -2)
        ev(p, t, EVP_EV_ABS, EVP_ABS_MT_TRACKING_ID, tid);
    if (tid != -1) {
        ev(p, t, EVP_EV_ABS, EVP_ABS_MT_POSITION_X, x);
        ev(p, t, EVP_EV_ABS, EVP_ABS_MT_POSITION_Y, y);
    }
}

static void evparse_mt(void)
{
    static collect c;
    memset(&c, 0, sizeof c);
    evp p;
    CHECK(evp_init(&p) == 0);
    p.on_sample = on_sample;
    p.ctx = &c;
    evp_set_axis(&p, EVP_ABS_MT_POSITION_X, 0, 479);
    evp_set_axis(&p, EVP_ABS_MT_POSITION_Y, 0, 799);
    uint64_t t = 5000000;
    mt_contact(&p, t, 0, 10, 100, 200);
    ev(&p, t, EVP_EV_KEY, EVP_BTN_TOUCH, 1);
    syn(&p, t);
    for (int i = 1; i <= 5; ++i) {
        t += 10000;
        mt_contact(&p, t, 0, -2, 100, 200 + 10 * i);
        if (i == 3)
            mt_contact(&p, t, 1, 11, 400, 700); /* a second finger: ignored */
        if (i == 4)
            mt_contact(&p, t, 1, -1, 0, 0);
        syn(&p, t);
    }
    t += 10000;
    mt_contact(&p, t, 0, -1, 0, 0);
    syn(&p, t);
    /* a second contact 30 ms later: the same sample (a drop-out) */
    t += 30000;
    mt_contact(&p, t, 0, 12, 101, 260);
    syn(&p, t);
    t += 10000;
    mt_contact(&p, t, 0, -2, 101, 270);
    syn(&p, t);
    t += 10000;
    mt_contact(&p, t, 0, -1, 0, 0);
    syn(&p, t);
    evp_tick(&p, t + 100000); /* inside the group window: nothing yet */
    CHECK(c.n == 0);
    evp_tick(&p, t + 200000);
    CHECK(c.n == 1);
    CHECK(c.s[0].n == 8 && c.s[0].nl == 2 && c.s[0].lift_after[0] == 5);
    CHECK(c.s[0].p[0].x == 100 && c.s[0].p[0].y == 200 && c.s[0].p[5].y == 250 && c.s[0].p[7].y == 270);
    CHECK(c.s[0].p[1].t_us - c.s[0].p[0].t_us == 10000);
    /* SYN_DROPPED: the next frame is not trusted */
    t += 1000000;
    mt_contact(&p, t, 0, 20, 50, 50);
    syn(&p, t);
    t += 10000;
    ev(&p, t, EVP_EV_SYN, EVP_SYN_DROPPED, 0);
    mt_contact(&p, t, 0, -2, 999, 999);
    syn(&p, t);
    t += 10000;
    mt_contact(&p, t, 0, -2, 60, 60);
    syn(&p, t);
    mt_contact(&p, t + 10000, 0, -1, 0, 0);
    syn(&p, t + 10000);
    evp_finish(&p);
    CHECK(c.n == 2 && c.s[1].n == 2 && c.s[1].p[1].x == 60);
    evp_free(&p);
}

static void evparse_single_touch_and_rotation(void)
{
    static collect c;
    memset(&c, 0, sizeof c);
    evp p;
    CHECK(evp_init(&p) == 0);
    p.on_sample = on_sample;
    p.ctx = &c;
    evp_set_axis(&p, EVP_ABS_X, 0, 4095);
    evp_set_axis(&p, EVP_ABS_Y, 0, 4095);
    p.out_w = 800;
    p.out_h = 480;
    p.rotate = 90;
    uint64_t t = 1000000;
    ev(&p, t, EVP_EV_KEY, EVP_BTN_TOUCH, 1);
    ev(&p, t, EVP_EV_ABS, EVP_ABS_X, 0);
    ev(&p, t, EVP_EV_ABS, EVP_ABS_Y, 0);
    syn(&p, t);
    ev(&p, t + 10000, EVP_EV_ABS, EVP_ABS_X, 2048);
    syn(&p, t + 10000);
    ev(&p, t + 20000, EVP_EV_KEY, EVP_BTN_TOUCH, 0);
    syn(&p, t + 20000);
    evp_finish(&p);
    CHECK(c.n == 1 && c.s[0].n == 2 && c.s[0].nl == 1);
    /* 800x480 turned clockwise into 480x800: the raw top-left corner is the logical top-right */
    CHECK_NEAR(c.s[0].p[0].x, 479, 1e-3);
    CHECK_NEAR(c.s[0].p[0].y, 0, 1e-3);
    CHECK_NEAR(c.s[0].p[1].y, 400, 0.01); /* raw x half-way -> logical y half-way */
    float x, y;
    p.rotate = 180;
    evp_map(&p, 0, 0, 0, &x, &y);
    CHECK_NEAR(x, 799, 1e-3);
    CHECK_NEAR(y, 479, 1e-3);
    p.rotate = 270;
    evp_map(&p, 0, 0, 0, &x, &y);
    CHECK_NEAR(x, 0, 1e-3);
    CHECK_NEAR(y, 799, 1e-3);
    p.out_w = p.out_h = 0; /* raw units */
    p.rotate = 0;
    evp_map(&p, 0, 123, 456, &x, &y);
    CHECK(x == 123 && y == 456);
    evp_free(&p);
}

static void evtest_text_round_trip(void)
{
    strokes_table *t = test_table();
    synth_cfg gen;
    synth_default(&gen);
    gen.drop_p = 0.5f;
    synth_rng rng;
    synth_seed(&rng, 11);
    static stk_sample orig[6];
    FILE *f = tmpfile();
    evtest_write_header(f, 480, 800);
    int tid = 1, n = 0;
    for (int gi = 0; gi < synth_glyph_count(t) && n < 6; gi += 7) {
        if (!synth_glyph_live(t, gi) || synth_glyph(t, gi, &gen, &rng, &orig[n]))
            continue;
        for (int i = 0; i < orig[n].n; ++i) { /* evtest has whole pixels inside the panel */
            orig[n].p[i].x = fmaxf(0, fminf(479, orig[n].p[i].x));
            orig[n].p[i].y = fmaxf(0, fminf(799, orig[n].p[i].y));
        }
        evtest_write_sample(f, &orig[n], 1000000000ull + (uint64_t)n * 800000ull, &tid);
        n++;
    }
    rewind(f);
    static collect c;
    memset(&c, 0, sizeof c);
    evp p;
    evp_init(&p);
    p.on_sample = on_sample;
    p.ctx = &c;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        unsigned long long sec, usec;
        if (sscanf(line, "Event: time %llu.%llu,", &sec, &usec) == 2)
            evp_tick(&p, sec * 1000000ull + usec);
        evp_evtest_line(&p, line);
    }
    evp_finish(&p);
    fclose(f);
    CHECK(strcmp(p.device, "strokes synthetic touch") == 0);
    CHECK(p.mx.valid && p.mx.max == 479 && p.my.max == 799);
    CHECK(c.n == n);
    int ok = 1;
    for (int i = 0; i < c.n && i < n; ++i) {
        memcpy(c.s[i].label, orig[i].label, sizeof c.s[i].label);
        c.s[i].box = orig[i].box;
        ok &= same_sample(&orig[i], &c.s[i], 0.0f);
    }
    CHECK(ok);
    evp_free(&p);
}

static void report_counts(void)
{
    report r;
    report_init(&r);
    strokes_result res[2];
    memset(res, 0, sizeof res);
    res[0].kind = STROKES_CHAR;
    strcpy(res[0].glyph, "a");
    report_add(&r, "a", STROKES_SET_LETTERS, res, 1);
    strcpy(res[0].glyph, "o");
    report_add(&r, "a", STROKES_SET_LETTERS, res, 1);
    res[0].kind = STROKES_REJECT;
    res[0].reject = STROKES_REJECT_LOW_SCORE;
    report_add(&r, "a", STROKES_SET_LETTERS, res, 1);
    report_add(&r, "a", STROKES_SET_LETTERS, res, 2);
    int n;
    double acc, rej, wrong;
    report_totals(&r, &n, &acc, &rej, &wrong);
    CHECK(n == 4);
    CHECK_NEAR(acc, 25.0, 1e-9);
    CHECK_NEAR(rej, 25.0, 1e-9);
    CHECK_NEAR(wrong, 50.0, 1e-9); /* the wrong one and the split one */
    CHECK(r.rows[0].split == 1 && r.rej_reason[STROKES_REJECT_LOW_SCORE] == 1);
    report_free(&r);
}

void suite_tools(void)
{
    RUN(corpus_round_trip);
    RUN(corpus_errors);
    RUN(replay_heals);
    RUN(synth_deterministic);
    RUN(evparse_mt);
    RUN(evparse_single_touch_and_rotation);
    RUN(evtest_text_round_trip);
    RUN(report_counts);
}
