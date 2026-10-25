/*
 * SPDX-License-Identifier: MIT
 *
 * Robustness: random event streams, random point clouds, mutated glyph tables, corpus lines and evtest text never
 * crash and always give well-formed results (run under ASan + UBSan by make asan).
 */
#include "evparse.h"
#include "strokes_internal.h"
#include "test.h"

#include <stdlib.h>

static uint64_t fz = 0x1234567887654321ull;

static uint64_t rnd(void)
{
    fz ^= fz << 13;
    fz ^= fz >> 7;
    fz ^= fz << 17;
    return fz;
}

static float rf(float a, float b)
{
    return a + (b - a) * (float)(rnd() % 1000001) / 1000000.0f;
}

static float odd_coord(void)
{
    switch (rnd() % 40) {
    case 0: return NAN;
    case 1: return INFINITY;
    case 2: return -INFINITY;
    case 3: return 1e30f;
    case 4: return -1e30f;
    case 5: return 0.0f;
    default: return rf(-50.0f, 900.0f);
    }
}

static int valid(const strokes_result *r)
{
    if (r->kind < STROKES_NONE || r->kind > STROKES_REJECT)
        return 0;
    if (memchr(r->utf8, 0, sizeof r->utf8) == NULL || memchr(r->glyph, 0, sizeof r->glyph) == NULL)
        return 0;
    if (memchr(r->dirs, 0, sizeof r->dirs) == NULL)
        return 0;
    if (!(r->confidence >= 0.0f && r->confidence <= 1.0f) || !isfinite(r->margin))
        return 0;
    if (r->n_cand < 0 || r->n_cand > STROKES_MAX_CAND)
        return 0;
    if (r->mode & ~31u)
        return 0;
    if ((r->mode & STROKES_MODE_SHIFT) && (r->mode & STROKES_MODE_CAPS))
        return 0;
    if ((r->mode & STROKES_MODE_PUNCT) && (r->mode & STROKES_MODE_EXT))
        return 0;
    if (r->accent < STROKES_ACCENT_NONE || r->accent > STROKES_ACCENT_RING ||
        !(r->mode & STROKES_MODE_ACCENT) != (r->accent == STROKES_ACCENT_NONE))
        return 0;
    if (r->kind == STROKES_REJECT && (r->reject < STROKES_REJECT_SHORT || r->reject > STROKES_REJECT_AMBIGUOUS))
        return 0;
    if ((r->kind == STROKES_CHAR || r->kind == STROKES_KEY || r->kind == STROKES_MODE) &&
        r->reject != STROKES_REJECT_NONE)
        return 0;
    return 1;
}

static void random_event_streams(void)
{
    strokes_engine *e = strokes_engine_new(test_table_full(), NULL); /* with the accents and every prefix */
    uint64_t t = 1000;
    int bad = 0;
    long results = 0;
    strokes_result r;
    for (int i = 0; i < 200000; ++i) {
        switch (rnd() % 8) {
        case 0: t -= rnd() % 5000; break;                   /* the clock goes back */
        case 1: t += rnd() % 2000000; break;                /* a long pause */
        case 2: t += (uint64_t)1 << (rnd() % 63); break;    /* a wild jump */
        default: t += rnd() % 20000; break;
        }
        switch (rnd() % 12) {
        case 0: strokes_down(e, (int)(rnd() % 4) - 1, odd_coord(), odd_coord(), t); break;
        case 1: strokes_up(e, t); break;
        case 2:
            while (strokes_poll(e, t, &r)) {
                bad += !valid(&r);
                results++;
            }
            break;
        case 3:
            while (strokes_flush(e, &r)) {
                bad += !valid(&r);
                results++;
            }
            break;
        case 4: strokes_cancel(e); break;
        case 5: strokes_set_mode(e, (unsigned)rnd()); break;
        default: strokes_move(e, odd_coord(), odd_coord(), t); break;
        }
    }
    while (strokes_flush(e, &r)) {
        bad += !valid(&r);
        results++;
    }
    CHECK(bad == 0);
    CHECK(results > 1000);
    strokes_engine_free(e);
}

static void random_strokes(void)
{
    strokes_engine *e = strokes_engine_new(test_table(), NULL);
    int bad = 0, n = 0, rejected = 0;
    strokes_result r;
    for (int s = 0; s < 3000; ++s) {
        uint64_t t = 1000000ull * (uint64_t)(s + 1);
        float x = rf(100, 400), y = rf(400, 700);
        int npts = 1 + (int)(rnd() % 300);
        strokes_down(e, (int)(rnd() % 2), x, y, t);
        for (int k = 0; k < npts; ++k) {
            x += rf(-15, 15);
            y += rf(-15, 15);
            t += rnd() % 30000;
            if (rnd() % 50 == 0) { /* drop-outs of any length */
                strokes_up(e, t);
                t += rnd() % 120000;
                strokes_down(e, 0, x + rf(-40, 40), y + rf(-40, 40), t);
            } else {
                strokes_move(e, x, y, t);
            }
        }
        strokes_up(e, t);
        while (strokes_flush(e, &r)) {
            bad += !valid(&r);
            rejected += r.kind == STROKES_REJECT;
            n++;
        }
    }
    CHECK(bad == 0);
    CHECK(n >= 3000);
    CHECKF(rejected * 100 >= n * 90, "random walks rejected: %d of %d", rejected, n); /* reject, do not guess */
    strokes_engine_free(e);
}

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *buf = malloc(1 << 20);
    size_t n = fread(buf, 1, (1 << 20) - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void mutated_tables(void)
{
    char *orig = read_all(test_table_path());
    CHECK(orig != NULL);
    if (!orig)
        return;
    size_t len = strlen(orig);
    char *buf = malloc(len + 64);
    static const char junk[] = " ,=#\n\t-+.0123456789eExyzKEY_MODE_replaceremovetapstart=line=closed=turn=corners=dirs=";
    int loaded = 0, refused = 0;
    for (int it = 0; it < 400; ++it) {
        memcpy(buf, orig, len + 1);
        int muts = 1 + (int)(rnd() % 8);
        for (int m = 0; m < muts; ++m) {
            size_t pos = rnd() % len;
            switch (rnd() % 4) {
            case 0: buf[pos] = junk[rnd() % (sizeof junk - 1)]; break;
            case 1: buf[pos] = (char)(rnd() % 255 + 1); break;
            case 2: buf[pos] = '\n'; break;
            default: buf[pos] = 0; break; /* truncate */
            }
        }
        strokes_table *t = strokes_table_new();
        strokes_error err;
        int rc = strokes_table_add_text(t, buf, "mut", &err);
        if (rc == 0) {
            loaded++;
            strokes_engine *e = strokes_engine_new(t, NULL);
            float xy[] = {200, 600, 250, 500, 300, 600};
            strokes_result r[4];
            int n = test_feed(e, STROKES_BOX_LETTERS, xy, 3, 1000000, 0.8f, 10.0f, r, 4);
            CHECK(n == 1 && valid(&r[0]));
            strokes_engine_free(e);
        } else {
            refused++;
            CHECK(strokes_table_glyph_count(t) == 0 && err.msg[0] != 0);
        }
        strokes_table_free(t);
    }
    CHECK(loaded + refused == 400 && loaded > 0 && refused > 0);
    free(buf);
    free(orig);
}

static void random_text_inputs(void)
{
    stk_sample *s = malloc(sizeof *s);
    evp p;
    evp_init(&p);
    char line[600];
    static const char alpha[] = "0123456789.,-^ \tabcdefghijklmnopqrstuvwxyzEvent: time , type code value()SYN_REPORT";
    int ok = 1;
    for (int it = 0; it < 20000; ++it) {
        int n = (int)(rnd() % 500);
        for (int i = 0; i < n; ++i)
            line[i] = (rnd() % 10 == 0) ? (char)(rnd() % 255 + 1) : alpha[rnd() % (sizeof alpha - 1)];
        line[n] = 0;
        if (it % 3 == 0 && n > 20) { /* plausible prefixes */
            memcpy(line, it % 2 ? "a\tletters\t8.54\t" : "Event: time 12.5, ", it % 2 ? 15 : 18);
        }
        char msg[96];
        int r = corpus_parse_line(line, s, msg, sizeof msg);
        ok &= r >= -1 && r <= 1;
        if (r == 1)
            ok &= s->n > 0 && s->nl > 0 && s->n <= SAMPLE_MAX_PTS;
        evp_evtest_line(&p, line);
        evp_event(&p, rnd(), (int)(rnd() % 5), (int)(rnd() % 400) - 20, (int)rnd());
    }
    evp_finish(&p);
    CHECK(ok);
    evp_free(&p);
    free(s);
}

void suite_fuzz(void)
{
    RUN(random_event_streams);
    RUN(random_strokes);
    RUN(mutated_tables);
    RUN(random_text_inputs);
}
