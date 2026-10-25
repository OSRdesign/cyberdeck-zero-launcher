/*
 * SPDX-License-Identifier: MIT
 *
 * Samples, the corpus file format, the replay harness and the evtest-style writer (PC tools of tools/strokes).
 */
#include "sample.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void sample_clear(stk_sample *s)
{
    s->label[0] = 0;
    s->box = STROKES_BOX_LETTERS;
    s->ppmm = 8.54f;
    s->n = 0;
    s->nl = 0;
}

int sample_add(stk_sample *s, float x, float y, uint64_t t_us)
{
    if (s->n >= SAMPLE_MAX_PTS)
        return -1;
    s->p[s->n].x = x;
    s->p[s->n].y = y;
    s->p[s->n].t_us = t_us;
    s->n++;
    return 0;
}

int sample_lift(stk_sample *s, uint64_t t_us)
{
    if (s->n == 0)
        return -1;
    if (s->nl > 0 && s->lift_after[s->nl - 1] == s->n - 1)
        return 0; /* already lifted after this point */
    if (s->nl >= SAMPLE_MAX_LIFTS)
        return -1;
    s->lift_after[s->nl] = s->n - 1;
    s->lift_t[s->nl] = t_us;
    s->nl++;
    return 0;
}

/* ---- corpus ---- */

static int parse_float(const char *s, size_t n, double *out)
{
    char buf[40];
    if (n == 0 || n >= sizeof buf)
        return -1;
    memcpy(buf, s, n);
    buf[n] = 0;
    char *e = NULL;
    errno = 0;
    double d = strtod(buf, &e);
    if (errno || *e || !isfinite(d))
        return -1;
    *out = d;
    return 0;
}

static uint64_t ms_to_us(double ms)
{
    if (ms <= 0.0)
        return 0;
    if (ms > 1e12)
        ms = 1e12;
    return (uint64_t)llround(ms * 1000.0);
}

int corpus_parse_line(const char *line, stk_sample *s, char *msg, size_t msz)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t')
        p++;
    if (!*p || *p == '\n' || *p == '\r' || *p == '#')
        return 0;
    sample_clear(s);
    /* three tab-separated fields, then space-separated tokens */
    const char *f[3];
    size_t fl[3];
    const char *q = line;
    for (int i = 0; i < 3; ++i) {
        const char *tab = strchr(q, '\t');
        if (!tab) {
            snprintf(msg, msz, "expected label<TAB>box<TAB>ppmm<TAB>points");
            return -1;
        }
        f[i] = q;
        fl[i] = (size_t)(tab - q);
        q = tab + 1;
    }
    if (fl[0] == 0 || fl[0] >= sizeof s->label) {
        snprintf(msg, msz, "bad label");
        return -1;
    }
    memcpy(s->label, f[0], fl[0]);
    s->label[fl[0]] = 0;
    if (fl[1] == 7 && strncmp(f[1], "letters", 7) == 0)
        s->box = STROKES_BOX_LETTERS;
    else if (fl[1] == 6 && strncmp(f[1], "digits", 6) == 0)
        s->box = STROKES_BOX_DIGITS;
    else {
        snprintf(msg, msz, "box must be letters or digits");
        return -1;
    }
    double ppmm;
    if (parse_float(f[2], fl[2], &ppmm) || ppmm <= 0.1 || ppmm > 1000.0) {
        snprintf(msg, msz, "bad ppmm");
        return -1;
    }
    s->ppmm = (float)ppmm;
    int lifted = 0;
    while (*q) {
        while (*q == ' ' || *q == '\t')
            q++;
        if (!*q || *q == '\n' || *q == '\r')
            break;
        const char *e = q;
        while (*e && !isspace((unsigned char)*e))
            e++;
        size_t n = (size_t)(e - q);
        if (*q == '^') {
            double t;
            if (parse_float(q + 1, n - 1, &t) || s->n == 0) {
                snprintf(msg, msz, "bad lift token");
                return -1;
            }
            uint64_t tu = ms_to_us(t);
            if (tu < s->p[s->n - 1].t_us)
                tu = s->p[s->n - 1].t_us;
            if (sample_lift(s, tu)) {
                snprintf(msg, msz, "too many lifts");
                return -1;
            }
            lifted = 1;
        } else {
            const char *c1 = memchr(q, ',', n);
            const char *c2 = c1 ? memchr(c1 + 1, ',', (size_t)(e - c1 - 1)) : NULL;
            double t, x, y;
            if (!c1 || !c2 || parse_float(q, (size_t)(c1 - q), &t) ||
                parse_float(c1 + 1, (size_t)(c2 - c1 - 1), &x) || parse_float(c2 + 1, (size_t)(e - c2 - 1), &y) ||
                fabs(x) > 1e6 || fabs(y) > 1e6) {
                snprintf(msg, msz, "bad point token");
                return -1;
            }
            uint64_t tu = ms_to_us(t);
            if (s->n > 0 && tu < s->p[s->n - 1].t_us)
                tu = s->p[s->n - 1].t_us;
            if (sample_add(s, (float)x, (float)y, tu)) {
                snprintf(msg, msz, "more than %d points", SAMPLE_MAX_PTS);
                return -1;
            }
            lifted = 0;
        }
        q = e;
    }
    if (s->n == 0) {
        snprintf(msg, msz, "no points");
        return -1;
    }
    if (!lifted)
        sample_lift(s, s->p[s->n - 1].t_us);
    return 1;
}

void corpus_write_header(FILE *f, const char *tool, const char *note)
{
    fprintf(f, "# strokes-corpus 1\n");
    fprintf(f, "# written by %s; format: tools/strokes/README.md \"Corpus format\"\n", tool);
    fprintf(f, "# label<TAB>box<TAB>ppmm<TAB>t_ms,x_px,y_px ... ^t_ms (lift)\n");
    if (note && *note)
        fprintf(f, "# %s\n", note);
}

static void put_num(FILE *f, double v, int decimals)
{
    char buf[48];
    snprintf(buf, sizeof buf, "%.*f", decimals, v);
    char *dot = strchr(buf, '.');
    if (dot) {
        char *end = buf + strlen(buf) - 1;
        while (end > dot && *end == '0')
            *end-- = 0;
        if (end == dot)
            *end = 0;
    }
    if (strcmp(buf, "-0") == 0)
        strcpy(buf, "0");
    fputs(buf, f);
}

void corpus_write(FILE *f, const stk_sample *s)
{
    fprintf(f, "%s\t%s\t", s->label[0] ? s->label : "?", s->box == STROKES_BOX_DIGITS ? "digits" : "letters");
    put_num(f, s->ppmm, 3);
    fputc('\t', f);
    uint64_t t0 = s->n ? s->p[0].t_us : 0;
    int li = 0;
    for (int i = 0; i < s->n; ++i) {
        if (i)
            fputc(' ', f);
        put_num(f, (double)(s->p[i].t_us - t0) / 1000.0, 3);
        fputc(',', f);
        put_num(f, s->p[i].x, 1);
        fputc(',', f);
        put_num(f, s->p[i].y, 1);
        while (li < s->nl && s->lift_after[li] == i) {
            fputs(" ^", f);
            put_num(f, (double)(s->lift_t[li] - t0) / 1000.0, 3);
            li++;
        }
    }
    fputc('\n', f);
}

int corpus_read_file(const char *path, int (*fn)(void *ctx, const stk_sample *s), void *ctx, char *msg, size_t msz)
{
    if (msz)
        msg[0] = 0;
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "r");
    if (!f) {
        snprintf(msg, msz, "%s: %s", path, strerror(errno));
        return -1;
    }
    size_t cap = 1 << 16;
    char *line = malloc(cap);
    stk_sample *s = malloc(sizeof *s);
    if (!line || !s) {
        free(line);
        free(s);
        if (f != stdin)
            fclose(f);
        snprintf(msg, msz, "out of memory");
        return -1;
    }
    int count = 0, lineno = 0, rc = 0;
    for (;;) {
        size_t len = 0;
        int c;
        while ((c = fgetc(f)) != EOF && c != '\n') {
            if (len + 2 >= cap) {
                size_t nc = cap * 2;
                if (nc > (64u << 20)) {
                    rc = -1;
                    break;
                }
                char *nl = realloc(line, nc);
                if (!nl) {
                    rc = -1;
                    break;
                }
                line = nl;
                cap = nc;
            }
            line[len++] = (char)c;
        }
        if (rc)
            break;
        if (c == EOF && len == 0)
            break;
        line[len] = 0;
        lineno++;
        char m[96];
        int r = corpus_parse_line(line, s, m, sizeof m);
        if (r < 0) {
            snprintf(msg, msz, "%s:%d: %s", path, lineno, m);
            rc = -1;
            break;
        }
        if (r > 0) {
            count++;
            if (fn && fn(ctx, s))
                break;
        }
        if (c == EOF)
            break;
    }
    if (rc && !msg[0])
        snprintf(msg, msz, "%s:%d: line too long", path, lineno + 1);
    free(line);
    free(s);
    if (f != stdin)
        fclose(f);
    return rc ? -1 : count;
}

/* ---- replay ---- */

unsigned replay_mode_for_set(int set)
{
    return set == STROKES_SET_PUNCT ? STROKES_MODE_PUNCT : set == STROKES_SET_EXT ? STROKES_MODE_EXT : 0u;
}

int replay_sample(strokes_engine *e, const stk_sample *s, strokes_result *res, int max)
{
    int nres = 0, li = 0, down = 0;
    strokes_result tmp;
    for (int i = 0; i < s->n; ++i) {
        const strokes_point *p = &s->p[i];
        if (!down) {
            strokes_down(e, s->box, p->x, p->y, p->t_us);
            down = 1;
        } else {
            strokes_move(e, p->x, p->y, p->t_us);
        }
        while (strokes_poll(e, p->t_us, &tmp))
            if (nres < max)
                res[nres++] = tmp;
        while (li < s->nl && s->lift_after[li] == i) {
            if (down)
                strokes_up(e, s->lift_t[li]);
            down = 0;
            li++;
        }
    }
    if (down)
        strokes_up(e, s->n ? s->p[s->n - 1].t_us : 0);
    while (strokes_flush(e, &tmp))
        if (nres < max)
            res[nres++] = tmp;
    return nres;
}

/* ---- evtest-style text ---- */

void evtest_write_header(FILE *f, int w, int h)
{
    fprintf(f, "Input driver version is 1.0.1\n");
    fprintf(f, "Input device ID: bus 0x18 vendor 0x0 product 0x0 version 0x0\n");
    fprintf(f, "Input device name: \"strokes synthetic touch\"\n");
    fprintf(f, "Supported events:\n  Event type 0 (EV_SYN)\n  Event type 1 (EV_KEY)\n    Event code 330 (BTN_TOUCH)\n");
    fprintf(f, "  Event type 3 (EV_ABS)\n");
    static const struct {
        int code;
        const char *name;
        int axis;
    } ax[] = {{0, "ABS_X", 0}, {1, "ABS_Y", 1}, {47, "ABS_MT_SLOT", 2}, {53, "ABS_MT_POSITION_X", 0},
              {54, "ABS_MT_POSITION_Y", 1}, {57, "ABS_MT_TRACKING_ID", 3}};
    for (size_t i = 0; i < sizeof ax / sizeof ax[0]; ++i) {
        int mx = ax[i].axis == 0 ? w - 1 : ax[i].axis == 1 ? h - 1 : ax[i].axis == 2 ? 9 : 65535;
        fprintf(f, "    Event code %d (%s)\n      Value      0\n      Min        0\n      Max   %6d\n", ax[i].code,
                ax[i].name, mx);
    }
    fprintf(f, "Properties:\n  Property type 1 (INPUT_PROP_DIRECT)\nTesting ... (interrupt to exit)\n");
}

static void ev(FILE *f, uint64_t t, int type, const char *tname, int code, const char *cname, int value)
{
    fprintf(f, "Event: time %llu.%06llu, type %d (%s), code %d (%s), value %d\n", (unsigned long long)(t / 1000000u),
            (unsigned long long)(t % 1000000u), type, tname, code, cname, value);
}

static void syn(FILE *f, uint64_t t)
{
    fprintf(f, "Event: time %llu.%06llu, -------------- SYN_REPORT ------------\n", (unsigned long long)(t / 1000000u),
            (unsigned long long)(t % 1000000u));
}

void evtest_write_sample(FILE *f, const stk_sample *s, uint64_t base_us, int *tracking_id)
{
    int li = 0, down = 0;
    for (int i = 0; i < s->n; ++i) {
        uint64_t t = base_us + s->p[i].t_us;
        int x = (int)lroundf(s->p[i].x), y = (int)lroundf(s->p[i].y);
        if (!down) {
            ev(f, t, 3, "EV_ABS", 47, "ABS_MT_SLOT", 0);
            ev(f, t, 3, "EV_ABS", 57, "ABS_MT_TRACKING_ID", (*tracking_id)++);
            ev(f, t, 3, "EV_ABS", 53, "ABS_MT_POSITION_X", x);
            ev(f, t, 3, "EV_ABS", 54, "ABS_MT_POSITION_Y", y);
            ev(f, t, 1, "EV_KEY", 330, "BTN_TOUCH", 1);
            ev(f, t, 3, "EV_ABS", 0, "ABS_X", x);
            ev(f, t, 3, "EV_ABS", 1, "ABS_Y", y);
            down = 1;
        } else {
            ev(f, t, 3, "EV_ABS", 53, "ABS_MT_POSITION_X", x);
            ev(f, t, 3, "EV_ABS", 54, "ABS_MT_POSITION_Y", y);
            ev(f, t, 3, "EV_ABS", 0, "ABS_X", x);
            ev(f, t, 3, "EV_ABS", 1, "ABS_Y", y);
        }
        syn(f, t);
        while (li < s->nl && s->lift_after[li] == i) {
            uint64_t tl = base_us + s->lift_t[li];
            ev(f, tl, 3, "EV_ABS", 57, "ABS_MT_TRACKING_ID", -1);
            ev(f, tl, 1, "EV_KEY", 330, "BTN_TOUCH", 0);
            syn(f, tl);
            down = 0;
            li++;
        }
    }
}
