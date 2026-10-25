/*
 * SPDX-License-Identifier: MIT
 *
 * Glyph table: the shipped file, the parser's errors, atomic adds, overlays (variants, replace, remove), outputs.
 */
#include "strokes_internal.h"
#include "test.h"

#include <locale.h>
#include <stdlib.h>

static int has_glyph(const strokes_table *t, const char *id, int set, int *variants)
{
    char gid[STROKES_ID_MAX];
    int s, v;
    for (int i = 0; strokes_table_glyph_info(t, i, gid, &s, &v) == 0; ++i)
        if (s == set && strcmp(gid, id) == 0) {
            if (variants)
                *variants = v;
            return 1;
        }
    return 0;
}

static strokes_table *fresh(void)
{
    strokes_table *t = strokes_table_new();
    strokes_error err;
    if (strokes_table_add_file(t, test_table_path(), &err))
        printf("load: %s:%d %s\n", err.source, err.line, err.msg);
    return t;
}

static void shipped_table(void)
{
    /* 25 letters (x is two strokes: the extended prefix, then x of the ext set), 10 digits, 6 gestures, the 33
     * punctuation glyphs and the 19 common extended ones: 93 glyphs, one template each (the chart shows one way),
     * plus the natural variants of b (6), v (3) and u (1) added after the first board test (report 041 section 12) */
    strokes_table *t = test_table();
    CHECK(strokes_table_glyph_count(t) == 93);
    CHECK(strokes_table_template_count(t) == 93 + 6 + 3 + 1);
    for (int i = 0; i < strokes_table_glyph_count(t); ++i) {
        char id[STROKES_ID_MAX];
        int set, nv;
        strokes_table_glyph_info(t, i, id, &set, &nv);
        int want = strcmp(id, "b") == 0 ? 7 : strcmp(id, "v") == 0 ? 4 : strcmp(id, "u") == 0 ? 2 : 1;
        CHECKF(nv == want, "%s: %d variants", id, nv);
    }
    for (char c = 'a'; c <= 'z'; ++c) {
        char id[2] = {c, 0};
        CHECKF(has_glyph(t, id, c == 'x' ? STROKES_SET_EXT : STROKES_SET_LETTERS, NULL), "letter %s", id);
    }
    CHECK(!has_glyph(t, "x", STROKES_SET_LETTERS, NULL));
    for (char c = '0'; c <= '9'; ++c) {
        char id[2] = {c, 0};
        CHECKF(has_glyph(t, id, STROKES_SET_DIGITS, NULL), "digit %s", id);
    }
    const char *any[] = {"space", "bksp", "enter", "shift", "tap", "ext"};
    for (int i = 0; i < 6; ++i)
        CHECKF(has_glyph(t, any[i], STROKES_SET_ANY, NULL), "gesture %s", any[i]);
    const char *punct[] = {"question", "period",   "comma",    "apos",      "backtick", "hyphen", "underscore",
                           "dquote",   "colon",    "lparen",   "rparen",    "lbrace",   "rbrace", "lbracket",
                           "rbracket", "less",     "greater",  "semicolon", "at",       "hash",   "dollar",
                           "percent",  "caret",    "amp",      "star",      "excl",     "tilde",  "plus",
                           "equals",   "backslash", "bar",     "slash",     "tab"};
    for (size_t i = 0; i < sizeof punct / sizeof punct[0]; ++i)
        CHECKF(has_glyph(t, punct[i], STROKES_SET_PUNCT, NULL), "punct %s", punct[i]);
    const char *ext[] = {"bullet",     "lsquo", "rsquo",  "ldquo", "rdquo", "degree", "ext_plus",
                         "ext_minus",  "pound", "iexcl",  "ext_equals", "szlig", "iquest", "copy",
                         "trade",      "reg",   "euro",   "yen"};
    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; ++i)
        CHECKF(has_glyph(t, ext[i], STROKES_SET_EXT, NULL), "ext %s", ext[i]);
    CHECK(strokes_table_glyph_info(t, 93, NULL, NULL, NULL) == -1);
    CHECK(strokes_table_glyph_info(t, -1, NULL, NULL, NULL) == -1);
    /* the optional section: the rarer extended characters and the accents, 14 glyphs */
    strokes_table *f = test_table_full();
    CHECK(strokes_table_glyph_count(f) == 93 + 14);
    const char *extra_ext[] = {"divide", "oslash", "section", "micro", "integral", "cent"};
    for (size_t i = 0; i < sizeof extra_ext / sizeof extra_ext[0]; ++i)
        CHECKF(has_glyph(f, extra_ext[i], STROKES_SET_EXT, NULL), "extra %s", extra_ext[i]);
    const char *accents[] = {"ccedil", "aelig", "acc_grave", "acc_acute", "acc_circ", "acc_tilde", "acc_uml",
                             "acc_ring"};
    for (size_t i = 0; i < sizeof accents / sizeof accents[0]; ++i)
        CHECKF(has_glyph(f, accents[i], STROKES_SET_LETTERS, NULL), "accent %s", accents[i]);
    /* glyph ids are unique across the sets, so a corpus label names one glyph */
    char a[STROKES_ID_MAX], b[STROKES_ID_MAX];
    int dup = 0;
    for (int i = 0; strokes_table_glyph_info(f, i, a, NULL, NULL) == 0; ++i)
        for (int j = i + 1; strokes_table_glyph_info(f, j, b, NULL, NULL) == 0; ++j)
            dup += strcmp(a, b) == 0;
    CHECK(dup == 0);
}

static void bad_lines(void)
{
    static const struct {
        const char *text, *needle;
    } cases[] = {
        {"x letters\n", "needs"},
        {"x nosuch x 0,0 1,1\n", "unknown set"},
        {"x letters KEY_NOPE 0,0 1,1\n", "bad output"},
        {"x letters MODE_FOO 0,0 1,1\n", "bad output"},
        {"x letters ACCENT_HOOK 0,0 1,1\n", "bad output"},
        {"x extended x 0,0 1,1\n", "unknown set"},
        {"x letters x foo=1 0,0 1,1\n", "unknown gate"},
        {"x letters x start=zz 0,0 1,1\n", "bad value"},
        {"x letters x start=tl, 0,0 1,1\n", "bad value"},
        {"x letters x line=sideways 0,0 1,1\n", "bad value"},
        {"x letters x closed=maybe 0,0 1,1\n", "bad value"},
        {"x letters x turn=cw:abc 0,0 1,1\n", "bad value"},
        {"x letters x corners=3-1 0,0 1,1\n", "bad value"},
        {"x letters x dirs=55 0,0 1,1\n", "bad value"},
        {"x letters x sharp=120-90 0,0 1,1\n", "bad value"},
        {"x letters x sharp=0-0 0,0 1,1\n", "bad value"},
        {"x letters x sharp=600 0,0 1,1\n", "bad value"},
        {"x letters x sharp=abc 0,0 1,1\n", "bad value"},
        {"x letters x sharp=-5 0,0 1,1\n", "bad value"},
        {"v letters v sharp=150 0,0 50,0 50,100\n", "fails its own gates"},
        {"x letters x 0,0\n", "at least 2"},
        {"x letters x 0,0 0,0\n", "no length"},
        {"x letters x 1,2,3 4,5\n", "bad point"},
        {"x letters x nan,1 4,5\n", "bad point"},
        {"x letters x 1e9,1 4,5\n", "bad point"},
        {"x letters x tap 0,0 1,1\n", "takes no points"},
        {"abcdefghijklmnopq letters x 0,0 1,1\n", "longer"},
        {"a letters b 0,100 50,0 100,100\n", "output differs"},
        {"a letters a tap\n", "mixes"},
        {"a letters a start=tr 0,100 50,0 100,100\n", "fails its own gates"},
        {"replace a\n", "needs"},
        {"remove a nosuch\n", "unknown set"},
    };
    strokes_table *t = fresh();
    int g0 = strokes_table_glyph_count(t), t0 = strokes_table_template_count(t);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        strokes_error err;
        memset(&err, 0x55, sizeof err);
        int rc = strokes_table_add_text(t, cases[i].text, "case", &err);
        CHECKF(rc == -1, "accepted: %s", cases[i].text);
        CHECKF(rc == -1 && strstr(err.msg, cases[i].needle) != NULL, "%s -> '%s' (want '%s')", cases[i].text,
               err.msg, cases[i].needle);
        CHECK(rc == -1 && err.line == 1 && strcmp(err.source, "case") == 0);
    }
    CHECK(strokes_table_glyph_count(t) == g0 && strokes_table_template_count(t) == t0);
    /* too many points on one line */
    char *big = malloc(16 * 300 + 64);
    int n = sprintf(big, "x letters x");
    for (int i = 0; i < 300; ++i)
        n += sprintf(big + n, " %d,%d", i, i % 7);
    strcpy(big + n, "\n");
    strokes_error err;
    CHECK(strokes_table_add_text(t, big, "big", &err) == -1 && strstr(err.msg, "more than"));
    free(big);
    /* the error names the right line */
    CHECK(strokes_table_add_text(t, "# comment\n\nq2 letters q 0,0 1,1\nbad\n", "multi", &err) == -1);
    CHECK(err.line == 4);
    strokes_table_free(t);
}

static void atomic_add(void)
{
    strokes_table *t = fresh();
    int g0 = strokes_table_glyph_count(t);
    strokes_error err;
    CHECK(strokes_table_add_text(t, "newg letters n 0,0 50,50 100,0\nbroken letters\n", "o", &err) == -1);
    CHECK(strokes_table_glyph_count(t) == g0);
    CHECK(!has_glyph(t, "newg", STROKES_SET_LETTERS, NULL));
    CHECK(strokes_table_add_text(t, "replace a letters\nbroken letters\n", "o", &err) == -1);
    int v = 0;
    CHECK(has_glyph(t, "a", STROKES_SET_LETTERS, &v) && v == 1); /* the replace was rolled back */
    CHECK(strokes_table_add_file(t, "/nonexistent/strokes.txt", &err) == -1 && strstr(err.msg, "cannot open"));
    CHECK(strokes_table_add_text(NULL, "x", "n", &err) == -1);
    CHECK(strokes_table_add_text(t, NULL, "n", &err) == -1);
    strokes_table_free(t);
}

static void overlay_variants(void)
{
    strokes_table *t = fresh();
    strokes_error err;
    int v = 0;
    /* a second variant of a */
    CHECK(strokes_table_add_text(t, "a letters a 0,100 40,10 60,10 100,100\n", "user", &err) == 0);
    CHECK(has_glyph(t, "a", STROKES_SET_LETTERS, &v) && v == 2);
    /* replace: only the new variants (two lines in the same overlay) remain */
    CHECK(strokes_table_add_text(t,
                                 "replace a letters\n"
                                 "a letters a start=bl 0,100 0,0 100,0 100,100\n"
                                 "a letters a start=bl 0,100 5,0 95,0 100,100\n",
                                 "user2", &err) == 0);
    CHECK(has_glyph(t, "a", STROKES_SET_LETTERS, &v) && v == 2);
    float shape[] = {200, 600, 200, 500, 300, 500, 300, 600};
    strokes_engine *e = strokes_engine_new(t, NULL);
    strokes_result r[4];
    int n = test_feed(e, STROKES_BOX_LETTERS, shape, 4, 1000000, 0.8f, 10.0f, r, 4);
    CHECK(n == 1 && r[0].kind == STROKES_CHAR && strcmp(r[0].utf8, "a") == 0);
    strokes_engine_free(e);
    /* replace may change the output */
    CHECK(strokes_table_add_text(t, "replace a letters\na letters @ 0,100 50,0 100,100\n", "user3", &err) == 0);
    e = strokes_engine_new(t, NULL);
    float lambda[] = {200, 600, 250, 500, 300, 600};
    n = test_feed(e, STROKES_BOX_LETTERS, lambda, 3, 1000000, 0.8f, 10.0f, r, 4);
    CHECK(n == 1 && r[0].kind == STROKES_CHAR && strcmp(r[0].utf8, "@") == 0);
    strokes_engine_free(e);
    /* remove */
    int g0 = strokes_table_glyph_count(t);
    CHECK(strokes_table_add_text(t, "remove z letters\nremove nothere letters\n", "user4", &err) == 0);
    CHECK(strokes_table_glyph_count(t) == g0 - 1 && !has_glyph(t, "z", STROKES_SET_LETTERS, NULL));
    float zed[] = {200, 500, 300, 500, 200, 600, 300, 600};
    e = strokes_engine_new(t, NULL);
    n = test_feed(e, STROKES_BOX_LETTERS, zed, 4, 1000000, 0.8f, 10.0f, r, 4);
    CHECK(n == 1 && strcmp(r[0].glyph, "z") != 0);
    strokes_engine_free(e);
    /* a removed glyph can come back, with another output */
    CHECK(strokes_table_add_text(t, "z letters Z 0,0 100,0 0,100 100,100\n", "user5", &err) == 0);
    CHECK(has_glyph(t, "z", STROKES_SET_LETTERS, &v) && v == 1);
    /* the tap glyph can be redefined after a replace */
    CHECK(strokes_table_add_text(t, "replace tap any\ntap any MODE_PUNCT tap\n", "user6", &err) == 0);
    CHECK(strokes_table_add_text(t, "tap any MODE_PUNCT tap\n", "user7", &err) == -1); /* one definition only */
    strokes_table_free(t);
}

static void outputs(void)
{
    strokes_table *t = strokes_table_new();
    strokes_error err;
    CHECK(strokes_table_add_text(t,
                                 "eacute letters U+00E9 0,0 50,100 100,0 100,100\n"
                                 "hash punct # 0,0 100,100\n"
                                 "euro punct U+20AC 0,0 100,0 100,100\n"
                                 "tab any KEY_TAB 0,100 100,0\n"
                                 "caps any MODE_CAPS 50,100 50,0 60,0\n"
                                 "pre any MODE_EXT 0,0 100,100\n"
                                 "til letters ACCENT_TILDE 100,0 60,100 40,0 0,100\n"
                                 "yen ext U+00A5 0,0 50,100 100,0\n"
                                 "word letters th 0,0 0,100 100,100 100,0\r\n",
                                 "out", &err) == 0);
    int gi = stk_find_glyph(t, "eacute", STROKES_SET_LETTERS);
    CHECK(gi >= 0 && strcmp(t->g[gi].utf8, "\xc3\xa9") == 0);
    char buf[16];
    CHECK(strcmp(stk_out_token(&t->g[gi], buf), "\xc3\xa9") == 0);
    gi = stk_find_glyph(t, "hash", STROKES_SET_PUNCT);
    CHECK(gi >= 0 && strcmp(t->g[gi].utf8, "#") == 0 && strcmp(stk_out_token(&t->g[gi], buf), "U+0023") == 0);
    gi = stk_find_glyph(t, "euro", STROKES_SET_PUNCT);
    CHECK(gi >= 0 && strcmp(t->g[gi].utf8, "\xe2\x82\xac") == 0);
    gi = stk_find_glyph(t, "tab", STROKES_SET_ANY);
    CHECK(gi >= 0 && t->g[gi].out == STK_OUT_KEY && t->g[gi].key == STROKES_KEY_TAB);
    CHECK(strcmp(stk_out_token(&t->g[gi], buf), "KEY_TAB") == 0);
    gi = stk_find_glyph(t, "caps", STROKES_SET_ANY);
    CHECK(gi >= 0 && t->g[gi].out == STK_OUT_MODE && t->g[gi].mode_op == STROKES_MODE_CAPS);
    gi = stk_find_glyph(t, "word", STROKES_SET_LETTERS);
    CHECK(gi >= 0 && strcmp(t->g[gi].utf8, "th") == 0);
    gi = stk_find_glyph(t, "pre", STROKES_SET_ANY);
    CHECK(gi >= 0 && t->g[gi].out == STK_OUT_MODE && t->g[gi].mode_op == STROKES_MODE_EXT);
    CHECK(gi >= 0 && strcmp(stk_out_token(&t->g[gi], buf), "MODE_EXT") == 0);
    gi = stk_find_glyph(t, "til", STROKES_SET_LETTERS);
    CHECK(gi >= 0 && t->g[gi].out == STK_OUT_ACCENT && t->g[gi].accent == STROKES_ACCENT_TILDE);
    CHECK(gi >= 0 && strcmp(stk_out_token(&t->g[gi], buf), "ACCENT_TILDE") == 0);
    CHECK(stk_find_glyph(t, "yen", STROKES_SET_EXT) >= 0 && stk_find_glyph(t, "yen", STROKES_SET_LETTERS) < 0);
    /* an accent glyph's output is part of its definition */
    CHECK(strokes_table_add_text(t, "til letters ACCENT_GRAVE 100,0 60,100 40,0 0,100\n", "o", &err) == -1 &&
          strstr(err.msg, "output differs"));
    CHECK(strcmp(strokes_accent_name(STROKES_ACCENT_DIAERESIS), "diaeresis") == 0 &&
          strcmp(strokes_accent_name(0), "") == 0 && strcmp(strokes_accent_name(99), "") == 0);
    CHECK(strokes_set_from_name("ext") == STROKES_SET_EXT && strcmp(strokes_set_name(STROKES_SET_EXT), "ext") == 0);
    CHECK(strokes_table_add_text(t, "bad letters U+D800 0,0 1,1\n", "o", &err) == -1);
    CHECK(strokes_table_add_text(t, "bad letters U+110000 0,0 1,1\n", "o", &err) == -1);
    CHECK(strokes_table_add_text(t, "bad letters abcdefgh 0,0 1,1\n", "o", &err) == -1);
    CHECK(strokes_key_linux_code(STROKES_KEY_SPACE) == 57 && strokes_key_linux_code(STROKES_KEY_BACKSPACE) == 14);
    CHECK(strokes_key_linux_code(STROKES_KEY_ENTER) == 28 && strokes_key_linux_code(STROKES_KEY_TAB) == 15);
    CHECK(strokes_key_linux_code(99) == 0 && strcmp(strokes_key_name(STROKES_KEY_ENTER), "KEY_ENTER") == 0);
    CHECK(strokes_set_from_name("punct") == STROKES_SET_PUNCT && strokes_set_from_name("x") == -1);
    CHECK(strcmp(strokes_set_name(9), "?") == 0 && strcmp(strokes_kind_name(STROKES_REJECT), "reject") == 0);
    CHECK(strcmp(strokes_reject_name(STROKES_REJECT_AMBIGUOUS), "ambiguous") == 0);
    strokes_table_free(t);
}

static void load_is_deterministic(void)
{
    strokes_table *a = fresh(), *b = fresh();
    CHECK(a->nt == b->nt && a->ng == b->ng && a->nv == b->nv);
    CHECK(memcmp(a->tp, b->tp, (size_t)a->nt * sizeof *a->tp) == 0);
    CHECK(memcmp(a->g, b->g, (size_t)a->ng * sizeof *a->g) == 0);
    strokes_table_free(a);
    strokes_table_free(b);
}

static void template_line_round_trip(void)
{
    strokes_table *t = fresh();
    /* a user's own "w": rounded, with a short middle */
    strokes_point pts[64];
    int n = 0;
    float xy[] = {0, 0, 20, 100, 40, 60, 60, 100, 80, 0};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 12; ++k) {
            float f = (float)k / 12.0f;
            pts[n].x = 200 + xy[2 * i] + f * (xy[2 * i + 2] - xy[2 * i]);
            pts[n].y = 500 + xy[2 * i + 1] + f * (xy[2 * i + 3] - xy[2 * i + 1]);
            pts[n].t_us = 1000000 + (uint64_t)n * 10000;
            n++;
        }
    pts[n].x = 280;
    pts[n].y = 500;
    pts[n].t_us = 1000000 + (uint64_t)n * 10000;
    n++;
    char line[2048];
    int len = strokes_template_line(t, NULL, "w", STROKES_SET_LETTERS, pts, n, line, sizeof line);
    CHECK(len > 0 && strncmp(line, "w letters w ", 12) == 0 && line[len - 1] == '\n');
    strokes_error err;
    CHECKF(strokes_table_add_text(t, line, "trained", &err) == 0, "%s", err.msg);
    int v = 0;
    CHECK(has_glyph(t, "w", STROKES_SET_LETTERS, &v) && v == 2);
    strokes_result r;
    CHECK(strokes_classify(t, NULL, STROKES_SET_LETTERS, pts, n, &r) == 1);
    CHECK(strcmp(r.glyph, "w") == 0 && r.confidence > 0.97f);
    CHECK(strokes_template_line(t, NULL, "nosuch", STROKES_SET_LETTERS, pts, n, line, sizeof line) == -1);
    CHECK(strokes_template_line(t, NULL, "tap", STROKES_SET_ANY, pts, n, line, sizeof line) == -1);
    CHECK(strokes_template_line(t, NULL, "w", STROKES_SET_LETTERS, pts, n, line, 20) == -1);
    CHECK(strokes_template_line(t, NULL, "w", STROKES_SET_LETTERS, pts, 1, line, sizeof line) == -1);
    strokes_table_free(t);
}

static void example_overlay(void)
{
    strokes_table *t = fresh();
    strokes_error err;
    int g0 = strokes_table_glyph_count(t), v = 0;
    CHECKF(strokes_table_add_file(t, "share/overlay-example.txt", &err) == 0, "%s:%d %s", err.source, err.line, err.msg);
    CHECK(strokes_table_glyph_count(t) == g0 - 1 && !has_glyph(t, "pound", STROKES_SET_EXT, NULL));
    CHECK(has_glyph(t, "v", STROKES_SET_LETTERS, &v) && v == 4 + 1); /* the shipped 4 and the example's */
    CHECK(has_glyph(t, "k", STROKES_SET_LETTERS, &v) && v == 1);
    strokes_table_free(t);
}

static void locale_independent(void)
{
    /* The launcher may set a locale whose decimal separator is a comma: the table must parse and print the same. */
    static const char *names[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8"};
    const char *active = NULL;
    for (size_t i = 0; i < sizeof names / sizeof names[0] && !active; ++i)
        if (setlocale(LC_NUMERIC, names[i]))
            active = names[i];
    strokes_table *t = strokes_table_new();
    strokes_error err;
    CHECK(strokes_table_add_text(t, "w letters w 0.5,0.25 25.5,100 50,30.75 75,1e2 99.75,0.5\n", "loc", &err) == 0);
    CHECK(t->nv == 5 && t->v[0].x == 0.5f && t->v[2].y == 30.75f && t->v[3].y == 100.0f);
    CHECK(strokes_table_add_text(t, "q letters q 1,,2 3,4\n", "loc", &err) == -1);
    CHECK(strokes_table_add_text(t, "q letters q 1.2.3,4 3,4\n", "loc", &err) == -1);
    CHECK(strokes_table_add_text(t, "q letters q 1e,4 3,4\n", "loc", &err) == -1);
    CHECK(strokes_table_add_text(t, "q letters q .,4 3,4\n", "loc", &err) == -1);
    strokes_point pts[20];
    for (int i = 0; i < 20; ++i) {
        pts[i].x = 200.0f + 5.3f * (float)i;
        pts[i].y = 500.0f + (float)((i * 7) % 40);
        pts[i].t_us = 1000000 + (uint64_t)i * 10000;
    }
    char line[2048];
    CHECK(strokes_template_line(t, NULL, "w", STROKES_SET_LETTERS, pts, 20, line, sizeof line) > 0);
    CHECK(strchr(line, '.') != NULL);
    CHECKF(strokes_table_add_text(t, line, "trained", &err) == 0, "%s (%s)", err.msg, active ? active : "C locale");
    setlocale(LC_NUMERIC, "C");
    strokes_table_free(t);
}

void suite_table(void)
{
    RUN(example_overlay);
    RUN(locale_independent);
    RUN(shipped_table);
    RUN(bad_lines);
    RUN(atomic_add);
    RUN(overlay_variants);
    RUN(outputs);
    RUN(load_is_deterministic);
    RUN(template_line_round_trip);
}
