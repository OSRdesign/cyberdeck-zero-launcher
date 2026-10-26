/*
 * SPDX-License-Identifier: MIT
 *
 * The glyph table: parser of the plain text format (README "Glyph table") for the shipped table and the user
 * overlays. Every add is atomic: the table is copied, the source applied to the copy, and the copy kept only when
 * the whole source parsed.
 */
#include "strokes_internal.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXT (1024 * 1024)

strokes_table *strokes_table_new(void)
{
    return calloc(1, sizeof(strokes_table));
}

void strokes_table_free(strokes_table *t)
{
    if (!t)
        return;
    free(t->g);
    free(t->tp);
    free(t->v);
    free(t);
}

static int clone_into(strokes_table *dst, const strokes_table *src)
{
    memset(dst, 0, sizeof *dst);
    dst->nsrc = src->nsrc;
    if (src->ng) {
        dst->g = malloc((size_t)src->ng * sizeof *dst->g);
        if (!dst->g)
            return -1;
        memcpy(dst->g, src->g, (size_t)src->ng * sizeof *dst->g);
        dst->ng = dst->capg = src->ng;
    }
    if (src->nt) {
        dst->tp = malloc((size_t)src->nt * sizeof *dst->tp);
        if (!dst->tp)
            return -1;
        memcpy(dst->tp, src->tp, (size_t)src->nt * sizeof *dst->tp);
        dst->nt = dst->capt = src->nt;
    }
    if (src->nv) {
        dst->v = malloc((size_t)src->nv * sizeof *dst->v);
        if (!dst->v)
            return -1;
        memcpy(dst->v, src->v, (size_t)src->nv * sizeof *dst->v);
        dst->nv = dst->capv = src->nv;
    }
    return 0;
}

static void free_parts(strokes_table *t)
{
    free(t->g);
    free(t->tp);
    free(t->v);
    memset(t, 0, sizeof *t);
}

static int grow(void **p, int *cap, int need, size_t elem)
{
    if (need <= *cap)
        return 0;
    int nc = *cap ? *cap * 2 : 32;
    while (nc < need)
        nc *= 2;
    void *q = realloc(*p, (size_t)nc * elem);
    if (!q)
        return -1;
    *p = q;
    *cap = nc;
    return 0;
}

#ifdef __GNUC__
__attribute__((format(printf, 4, 5)))
#endif
static void set_err(strokes_error *err, const char *name, int line, const char *fmt, ...)
{
    if (!err)
        return;
    snprintf(err->source, sizeof err->source, "%s", name ? name : "");
    err->line = line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->msg, sizeof err->msg, fmt, ap);
    va_end(ap);
}

int stk_set_matches(const stk_glyph *g, int active_set)
{
    if (g->set == active_set)
        return 1;
    if (g->set != STROKES_SET_ANY)
        return 0;
    if (active_set == STROKES_SET_LETTERS || active_set == STROKES_SET_DIGITS)
        return 1;
    /* after a punctuation or extended prefix the gestures' strokes are glyphs of that set (a line right is the
     * hyphen, a line up the exclamation mark, ...): only backspace stays, to leave the prefix */
    return (active_set == STROKES_SET_PUNCT || active_set == STROKES_SET_EXT) && g->out == STK_OUT_KEY &&
           g->key == STROKES_KEY_BACKSPACE;
}

int stk_find_glyph(const strokes_table *t, const char *id, int set)
{
    for (int i = 0; i < t->ng; ++i)
        if (t->g[i].set == set && strcmp(t->g[i].id, id) == 0)
            return i;
    return -1;
}

static const char *set_names[STK_NSETS] = {"letters", "digits", "punct", "any", "ext"};

const char *strokes_set_name(int set)
{
    return set >= 0 && set < STK_NSETS ? set_names[set] : "?";
}

int strokes_set_from_name(const char *name)
{
    for (int i = 0; i < STK_NSETS; ++i)
        if (strcmp(name, set_names[i]) == 0)
            return i;
    return -1;
}

static const char *key_names[] = {"", "KEY_SPACE", "KEY_BACKSPACE", "KEY_ENTER", "KEY_TAB"};

const char *strokes_key_name(int key)
{
    return key >= 1 && key <= 4 ? key_names[key] : "";
}

static const struct {
    const char *token;
    unsigned op;
} mode_tokens[] = {{"MODE_SHIFT", STROKES_MODE_SHIFT},
                   {"MODE_CAPS", STROKES_MODE_CAPS},
                   {"MODE_PUNCT", STROKES_MODE_PUNCT},
                   {"MODE_EXT", STROKES_MODE_EXT}};

static const char *accent_tokens[] = {"",           "ACCENT_GRAVE", "ACCENT_ACUTE",    "ACCENT_CIRCUMFLEX",
                                      "ACCENT_TILDE", "ACCENT_DIAERESIS", "ACCENT_RING"};
static const char *accent_names[] = {"", "grave", "acute", "circumflex", "tilde", "diaeresis", "ring"};
#define N_ACCENTS ((int)(sizeof accent_names / sizeof accent_names[0]))

const char *strokes_accent_name(int accent)
{
    return accent >= 1 && accent < N_ACCENTS ? accent_names[accent] : "";
}

int strokes_key_linux_code(int key)
{
    switch (key) {
    case STROKES_KEY_SPACE: return 57;
    case STROKES_KEY_BACKSPACE: return 14;
    case STROKES_KEY_ENTER: return 28;
    case STROKES_KEY_TAB: return 15;
    default: return 0;
    }
}

/* Encodes code point cp as UTF-8 into out (8 bytes). Returns 0 on success. */
int stk_utf8_encode(unsigned long cp, char out[8])
{
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return -1;
    if (cp < 0x80) {
        out[0] = (char)cp;
        out[1] = 0;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = 0;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = 0;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        out[4] = 0;
    }
    return 0;
}

unsigned long stk_utf8_single(const char *s)
{
    const unsigned char *u = (const unsigned char *)s;
    unsigned long cp;
    int len;
    if (u[0] == 0)
        return 0;
    if (u[0] < 0x80) {
        cp = u[0];
        len = 1;
    } else if (u[0] >= 0xC2 && u[0] < 0xE0) {
        cp = u[0] & 0x1Fu;
        len = 2;
    } else if (u[0] >= 0xE0 && u[0] < 0xF0) {
        cp = u[0] & 0x0Fu;
        len = 3;
    } else if (u[0] >= 0xF0 && u[0] < 0xF5) {
        cp = u[0] & 0x07u;
        len = 4;
    } else {
        return 0;
    }
    for (int i = 1; i < len; ++i) {
        if ((u[i] & 0xC0) != 0x80)
            return 0;
        cp = (cp << 6) | (u[i] & 0x3Fu);
    }
    return u[len] == 0 ? cp : 0;
}

/* The out column: a literal character (1..7 bytes), U+XXXX, KEY_*, MODE_*, ACCENT_*. */
static int parse_out(const char *s, stk_glyph *g)
{
    g->out = STK_OUT_CHAR;
    g->key = 0;
    g->mode_op = 0;
    g->accent = 0;
    g->utf8[0] = 0;
    for (int k = 1; k <= 4; ++k)
        if (strcmp(s, key_names[k]) == 0) {
            g->out = STK_OUT_KEY;
            g->key = k;
            return 0;
        }
    for (size_t k = 0; k < sizeof mode_tokens / sizeof mode_tokens[0]; ++k)
        if (strcmp(s, mode_tokens[k].token) == 0) {
            g->out = STK_OUT_MODE;
            g->mode_op = mode_tokens[k].op;
            return 0;
        }
    for (int k = 1; k < N_ACCENTS; ++k)
        if (strcmp(s, accent_tokens[k]) == 0) {
            g->out = STK_OUT_ACCENT;
            g->accent = k;
            return 0;
        }
    if ((s[0] == 'U' || s[0] == 'u') && s[1] == '+' && s[2]) {
        char *end = NULL;
        errno = 0;
        unsigned long cp = strtoul(s + 2, &end, 16);
        if (errno || *end || end - (s + 2) > 6)
            return -1;
        return stk_utf8_encode(cp, g->utf8);
    }
    size_t n = strlen(s);
    if (n == 0 || n >= sizeof g->utf8)
        return -1;
    if (strncmp(s, "KEY_", 4) == 0 || strncmp(s, "MODE_", 5) == 0 || strncmp(s, "ACCENT_", 7) == 0)
        return -1; /* a misspelt keyword, not text */
    memcpy(g->utf8, s, n + 1);
    return 0;
}

const char *stk_out_token(const stk_glyph *g, char buf[16])
{
    if (g->out == STK_OUT_KEY)
        return key_names[g->key];
    if (g->out == STK_OUT_MODE) {
        for (size_t k = 0; k < sizeof mode_tokens / sizeof mode_tokens[0]; ++k)
            if (g->mode_op == mode_tokens[k].op)
                return mode_tokens[k].token;
        return "MODE_PUNCT";
    }
    if (g->out == STK_OUT_ACCENT)
        return g->accent >= 1 && g->accent < N_ACCENTS ? accent_tokens[g->accent] : "ACCENT_GRAVE";
    const unsigned char *u = (const unsigned char *)g->utf8;
    int plain = u[0] != 0 && u[0] != '#';
    for (const unsigned char *c = u; *c; ++c)
        if (*c <= ' ')
            plain = 0;
    if (plain)
        return g->utf8;
    /* Decode the first code point for U+XXXX (enough for one-character outputs). */
    unsigned long cp = u[0];
    if (u[0] >= 0xF0)
        cp = ((unsigned long)(u[0] & 7) << 18) | ((unsigned long)(u[1] & 63) << 12) | ((unsigned long)(u[2] & 63) << 6) |
             (u[3] & 63);
    else if (u[0] >= 0xE0)
        cp = ((unsigned long)(u[0] & 15) << 12) | ((unsigned long)(u[1] & 63) << 6) | (u[2] & 63);
    else if (u[0] >= 0xC0)
        cp = ((unsigned long)(u[0] & 31) << 6) | (u[1] & 63);
    snprintf(buf, 16, "U+%04lX", cp);
    return buf;
}

/* ---- gates ---- */

static const char *cell_names[9] = {"tl", "tc", "tr", "ml", "mc", "mr", "bl", "bc", "br"};

static int parse_cells(const char *v, uint16_t *mask)
{
    *mask = 0;
    while (*v) {
        const char *comma = strchr(v, ',');
        size_t n = comma ? (size_t)(comma - v) : strlen(v);
        char tok[8];
        if (n == 0 || n >= sizeof tok)
            return -1;
        memcpy(tok, v, n);
        tok[n] = 0;
        v += n + (comma ? 1 : 0);
        if (comma && !*v)
            return -1; /* trailing comma */
        int found = 0;
        for (int i = 0; i < 9; ++i)
            if (strcmp(tok, cell_names[i]) == 0) {
                *mask |= (uint16_t)(1u << i);
                found = 1;
            }
        if (!found && strlen(tok) == 1) {
            found = 1;
            switch (tok[0]) {
            case 't': *mask |= 0x007; break;
            case 'm': *mask |= 0x038; break;
            case 'b': *mask |= 0x1C0; break;
            case 'l': *mask |= 0x049; break;
            case 'c': *mask |= 0x092; break;
            case 'r': *mask |= 0x124; break;
            case '*': *mask |= 0x1FF; break;
            default: found = 0;
            }
        }
        if (!found)
            return -1;
    }
    return *mask ? 0 : -1;
}

static int parse_dir(const char *v, float *angle)
{
    static const struct {
        const char *name;
        int digit;
    } dirs[] = {{"right", 6}, {"downright", 3}, {"down", 2}, {"downleft", 1}, {"left", 4},
                {"upleft", 7}, {"up", 8},        {"upright", 9}};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; ++i)
        if (strcmp(v, dirs[i].name) == 0) {
            *angle = stk_numpad_angle(dirs[i].digit);
            return 0;
        }
    if (v[0] >= '1' && v[0] <= '9' && v[0] != '5' && !v[1]) {
        *angle = stk_numpad_angle(v[0] - '0');
        return 0;
    }
    return -1;
}

/* One key=value gate token. Returns 0, or -1 with a message. */
static int parse_gate(const char *tok, stk_gates *g, char *msg, size_t msz)
{
    const char *eq = strchr(tok, '=');
    char key[16];
    size_t kl = (size_t)(eq - tok);
    if (kl == 0 || kl >= sizeof key) {
        snprintf(msg, msz, "bad gate '%s'", tok);
        return -1;
    }
    memcpy(key, tok, kl);
    key[kl] = 0;
    const char *v = eq + 1;
    int bad = 0;
    if (strcmp(key, "start") == 0)
        bad = parse_cells(v, &g->start_mask);
    else if (strcmp(key, "end") == 0)
        bad = parse_cells(v, &g->end_mask);
    else if (strcmp(key, "line") == 0) {
        if (strcmp(v, "no") == 0)
            g->line = STK_G_NO;
        else if (strcmp(v, "any") == 0)
            g->line = STK_G_ANY;
        else {
            g->line = STK_G_YES;
            bad = parse_dir(v, &g->line_dir);
        }
    } else if (strcmp(key, "closed") == 0) {
        if (strcmp(v, "yes") == 0)
            g->closed = STK_G_YES;
        else if (strcmp(v, "no") == 0)
            g->closed = STK_G_NO;
        else if (strcmp(v, "any") == 0)
            g->closed = STK_G_ANY;
        else
            bad = 1;
    } else if (strcmp(key, "turn") == 0) {
        /* turn=cw | ccw [:DEG]: net turning in that sense of at least DEG degrees (default 90) */
        int deg = 90;
        char extra;
        const char *colon = strchr(v, ':');
        size_t vl = colon ? (size_t)(colon - v) : strlen(v);
        if (colon && (sscanf(colon + 1, "%d%c", &deg, &extra) != 1 || deg < 0 || deg > 1080))
            bad = 1;
        else if (vl == 2 && strncmp(v, "cw", 2) == 0)
            g->turn = 1;
        else if (vl == 3 && strncmp(v, "ccw", 3) == 0)
            g->turn = -1;
        else if (!colon && strcmp(v, "any") == 0)
            g->turn = 0;
        else
            bad = 1;
        g->turn_min = (float)deg * STK_PI / 180.0f;
    } else if (strcmp(key, "corners") == 0) {
        int a, b;
        char extra;
        if (sscanf(v, "%d-%d%c", &a, &b, &extra) == 2 && a >= 0 && b >= a && b <= 20) {
            g->cmin = (int8_t)a;
            g->cmax = (int8_t)b;
        } else if (sscanf(v, "%d%c", &a, &extra) == 1 && a >= 0 && a <= 20) {
            g->cmin = g->cmax = (int8_t)a;
        } else
            bad = 1;
    } else if (strcmp(key, "sharp") == 0) {
        /* sharp=MIN | MIN-MAX: the sharpest turn over three consecutive vertices, degrees (at most 3 x 180) */
        int a, b;
        char extra;
        if (sscanf(v, "%d-%d%c", &a, &b, &extra) == 2 && a >= 0 && b >= a && b >= 1 && b <= 540) {
            g->sharp_min = (float)a * STK_PI / 180.0f;
            g->sharp_max = (float)b * STK_PI / 180.0f;
        } else if (sscanf(v, "%d%c", &a, &extra) == 1 && a >= 0 && a <= 540) {
            g->sharp_min = (float)a * STK_PI / 180.0f;
            g->sharp_max = 3.0f * STK_PI;
        } else
            bad = 1;
    } else if (strcmp(key, "dirs") == 0) {
        size_t n = strlen(v);
        bad = n == 0 || n >= sizeof g->dirs;
        for (size_t i = 0; i < n && !bad; ++i)
            bad = v[i] < '1' || v[i] > '9' || v[i] == '5';
        if (!bad)
            memcpy(g->dirs, v, n + 1);
    } else {
        snprintf(msg, msz, "unknown gate '%s'", key);
        return -1;
    }
    if (bad) {
        snprintf(msg, msz, "bad value in gate '%s'", tok);
        return -1;
    }
    return 0;
}

/* A decimal number [+-]digits[.digits][e[+-]digits], always with a '.' whatever the C locale (the launcher may set
 * one; strtod would then want a ','). Returns -1 when malformed or larger than 1e6 in magnitude. */
static int parse_num(const char *s, const char *end, float *out)
{
    const char *p = s;
    double sign = 1.0, v = 0.0;
    int digits = 0;
    if (p < end && (*p == '+' || *p == '-')) {
        sign = *p == '-' ? -1.0 : 1.0;
        p++;
    }
    while (p < end && *p >= '0' && *p <= '9') {
        if (v < 1e12)
            v = v * 10.0 + (*p - '0');
        p++;
        digits++;
    }
    if (p < end && *p == '.') {
        p++;
        double scale = 0.1;
        while (p < end && *p >= '0' && *p <= '9') {
            v += (*p - '0') * scale;
            scale *= 0.1;
            p++;
            digits++;
        }
    }
    if (digits == 0)
        return -1;
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        int esign = 1, ex = 0, edigits = 0;
        if (p < end && (*p == '+' || *p == '-')) {
            esign = *p == '-' ? -1 : 1;
            p++;
        }
        while (p < end && *p >= '0' && *p <= '9') {
            if (ex < 100)
                ex = ex * 10 + (*p - '0');
            p++;
            edigits++;
        }
        if (edigits == 0)
            return -1;
        while (ex-- > 0)
            v = esign > 0 ? v * 10.0 : v / 10.0;
    }
    if (p != end || !(v <= 1e6))
        return -1;
    *out = (float)(sign * v);
    return 0;
}

static int parse_point(const char *tok, stk_vec *p)
{
    const char *comma = strchr(tok, ',');
    if (!comma || strchr(comma + 1, ','))
        return -1;
    if (parse_num(tok, comma, &p->x) || parse_num(comma + 1, comma + strlen(comma), &p->y))
        return -1;
    return 0;
}

/* ---- source parsing ---- */

#define MAX_TOKENS (STK_MAX_VERTS + 64) /* id, set, out, gates, then the points */

typedef struct {
    strokes_table *t;
    const char *name;
    int src;
    strokes_error *err;
} parse_ctx;

static void kill_glyph_variants(strokes_table *t, int gi, int before_src)
{
    for (int i = 0; i < t->nt; ++i) {
        stk_tpl *tp = &t->tp[i];
        if (tp->dead || tp->glyph != gi)
            continue;
        if (before_src < 0 || tp->source < before_src) {
            tp->dead = 1;
            t->g[gi].n_variants--;
        }
    }
    if (t->g[gi].is_tap) {
        /* a tap glyph has no template: replace or remove drops it; a later line may define it again */
        t->g[gi].n_variants = 0;
        t->g[gi].is_tap = 0;
    }
}

static int directive(parse_ctx *c, int line, char **tok, int nt)
{
    if (nt != 3) {
        set_err(c->err, c->name, line, "'%s' needs: %s <id> <set>", tok[0], tok[0]);
        return -1;
    }
    int set = strokes_set_from_name(tok[2]);
    if (set < 0) {
        set_err(c->err, c->name, line, "unknown set '%s'", tok[2]);
        return -1;
    }
    int gi = stk_find_glyph(c->t, tok[1], set);
    if (gi < 0)
        return 0; /* nothing to replace or remove: fine */
    if (strcmp(tok[0], "replace") == 0)
        kill_glyph_variants(c->t, gi, c->src);
    else
        kill_glyph_variants(c->t, gi, -1);
    return 0;
}

static int glyph_line(parse_ctx *c, int line, char **tok, int nt)
{
    strokes_table *t = c->t;
    if (nt < 4) {
        set_err(c->err, c->name, line, "a glyph line needs: <id> <set> <out> [gates] <template>");
        return -1;
    }
    if (strlen(tok[0]) >= STROKES_ID_MAX) {
        set_err(c->err, c->name, line, "glyph id '%s' longer than %d bytes", tok[0], STROKES_ID_MAX - 1);
        return -1;
    }
    int set = strokes_set_from_name(tok[1]);
    if (set < 0) {
        set_err(c->err, c->name, line, "unknown set '%s' (letters, digits, punct, ext, any)", tok[1]);
        return -1;
    }
    stk_glyph ng;
    memset(&ng, 0, sizeof ng);
    snprintf(ng.id, sizeof ng.id, "%s", tok[0]);
    ng.set = set;
    if (parse_out(tok[2], &ng)) {
        set_err(c->err, c->name, line, "bad output '%s'", tok[2]);
        return -1;
    }
    stk_gates g;
    memset(&g, 0, sizeof g);
    g.line = STK_G_AUTO;
    g.closed = STK_G_AUTO;
    g.cmin = g.cmax = -1;
    int is_tap = 0;
    stk_vec verts[STK_MAX_VERTS];
    int nv = 0;
    for (int i = 3; i < nt; ++i) {
        if (strcmp(tok[i], "tap") == 0 && nv == 0 && !is_tap) {
            is_tap = 1;
            continue;
        }
        if (strchr(tok[i], '=') && nv == 0 && !is_tap) {
            char msg[96];
            if (parse_gate(tok[i], &g, msg, sizeof msg)) {
                set_err(c->err, c->name, line, "%s", msg);
                return -1;
            }
            continue;
        }
        if (is_tap) {
            set_err(c->err, c->name, line, "'tap' takes no points");
            return -1;
        }
        if (nv >= STK_MAX_VERTS) {
            set_err(c->err, c->name, line, "more than %d points", STK_MAX_VERTS);
            return -1;
        }
        if (parse_point(tok[i], &verts[nv])) {
            set_err(c->err, c->name, line, "bad point '%s' (x,y)", tok[i]);
            return -1;
        }
        nv++;
    }
    if (!is_tap && nv < 2) {
        set_err(c->err, c->name, line, "a template needs at least 2 points (or 'tap')");
        return -1;
    }
    if (!is_tap && stk_path_len(verts, nv) <= 1e-3f) {
        set_err(c->err, c->name, line, "template has no length");
        return -1;
    }

    int gi = stk_find_glyph(t, ng.id, set);
    if (gi >= 0) {
        stk_glyph *old = &t->g[gi];
        int live = old->n_variants > 0;
        if (live && (old->out != ng.out || old->key != ng.key || old->mode_op != ng.mode_op ||
                     old->accent != ng.accent || strcmp(old->utf8, ng.utf8) != 0)) {
            set_err(c->err, c->name, line, "'%s' (%s): output differs from the earlier definition (use replace)",
                    ng.id, tok[1]);
            return -1;
        }
        if (live && old->is_tap != is_tap) {
            set_err(c->err, c->name, line, "'%s' (%s): mixes 'tap' and a drawn template", ng.id, tok[1]);
            return -1;
        }
        if (live && is_tap) {
            set_err(c->err, c->name, line, "'%s' (%s): a tap glyph has one definition", ng.id, tok[1]);
            return -1;
        }
        old->out = ng.out;
        old->key = ng.key;
        old->mode_op = ng.mode_op;
        old->accent = ng.accent;
        memcpy(old->utf8, ng.utf8, sizeof old->utf8);
    } else {
        if (t->ng >= STK_MAX_GLYPHS) {
            set_err(c->err, c->name, line, "more than %d glyphs", STK_MAX_GLYPHS);
            return -1;
        }
        if (grow((void **)&t->g, &t->capg, t->ng + 1, sizeof *t->g)) {
            set_err(c->err, c->name, line, "out of memory");
            return -1;
        }
        gi = t->ng++;
        t->g[gi] = ng;
    }
    if (is_tap) {
        t->g[gi].is_tap = 1;
        t->g[gi].n_variants = 1;
        return 0;
    }
    if (t->nt >= STK_MAX_TEMPLATES) {
        set_err(c->err, c->name, line, "more than %d templates", STK_MAX_TEMPLATES);
        return -1;
    }
    if (grow((void **)&t->tp, &t->capt, t->nt + 1, sizeof *t->tp) ||
        grow((void **)&t->v, &t->capv, t->nv + nv, sizeof *t->v)) {
        set_err(c->err, c->name, line, "out of memory");
        return -1;
    }
    stk_tpl *tp = &t->tp[t->nt];
    memset(tp, 0, sizeof *tp);
    tp->glyph = gi;
    tp->source = c->src;
    tp->line = line;
    tp->v0 = t->nv;
    tp->nv = nv;
    memcpy(&t->v[t->nv], verts, (size_t)nv * sizeof *verts);

    stk_vec rs[STK_N];
    stk_resample(verts, nv, rs, STK_N);
    stk_features(rs, STK_N, &tp->f);
    stk_normalise(rs, STK_N, tp->p);

    /* Resolve the implicit gates from the template itself. */
    if (g.line == STK_G_AUTO) {
        if (tp->f.straight >= STK_LINE_TPL) {
            g.line = STK_G_YES;
            g.line_dir = tp->f.dir;
        } else {
            g.line = STK_G_NO;
        }
    }
    if (g.closed == STK_G_AUTO)
        g.closed = tp->f.gap <= STK_CLOSED_TPL ? STK_G_YES : tp->f.gap >= STK_OPEN_TPL ? STK_G_NO : STK_G_ANY;
    tp->g = g;
    if (!stk_gates_pass(&tp->g, &tp->f, &tp->f)) {
        set_err(c->err, c->name, line, "'%s' (%s): the template fails its own gates", ng.id, tok[1]);
        return -1;
    }
    t->nv += nv;
    t->nt++;
    t->g[gi].n_variants++;
    return 0;
}

static int apply_text(parse_ctx *c, const char *text)
{
    size_t len = strlen(text);
    if (len > MAX_TEXT) {
        set_err(c->err, c->name, 0, "file larger than %d bytes", MAX_TEXT);
        return -1;
    }
    char *buf = malloc(len + 1);
    if (!buf) {
        set_err(c->err, c->name, 0, "out of memory");
        return -1;
    }
    memcpy(buf, text, len + 1);
    char **tok = malloc(MAX_TOKENS * sizeof *tok);
    if (!tok) {
        free(buf);
        set_err(c->err, c->name, 0, "out of memory");
        return -1;
    }
    int rc = 0, line = 0;
    char *p = buf;
    while (*p && rc == 0) {
        line++;
        char *eol = strchr(p, '\n');
        char *next = eol ? eol + 1 : p + strlen(p);
        if (eol)
            *eol = 0;
        int nt = 0;
        char *s = p;
        while (*s && isspace((unsigned char)*s))
            s++;
        if (*s && *s != '#') {
            while (*s) {
                while (*s && isspace((unsigned char)*s))
                    *s++ = 0;
                if (!*s)
                    break;
                if (nt >= MAX_TOKENS) {
                    set_err(c->err, c->name, line, "too many fields");
                    rc = -1;
                    break;
                }
                tok[nt++] = s;
                while (*s && !isspace((unsigned char)*s))
                    s++;
            }
            if (rc == 0 && nt > 0) {
                if (strcmp(tok[0], "replace") == 0 || strcmp(tok[0], "remove") == 0)
                    rc = directive(c, line, tok, nt);
                else
                    rc = glyph_line(c, line, tok, nt);
            }
        }
        p = next;
    }
    free(tok);
    free(buf);
    return rc;
}

int strokes_table_add_text(strokes_table *t, const char *text, const char *name, strokes_error *err)
{
    if (err)
        memset(err, 0, sizeof *err);
    if (!t || !text) {
        set_err(err, name, 0, "no table or no text");
        return -1;
    }
    strokes_table work;
    if (clone_into(&work, t)) {
        free_parts(&work);
        set_err(err, name, 0, "out of memory");
        return -1;
    }
    parse_ctx c = {&work, name ? name : "text", t->nsrc, err};
    if (apply_text(&c, text)) {
        free_parts(&work);
        return -1;
    }
    work.nsrc++;
    free_parts(t);
    *t = work;
    return 0;
}

int strokes_table_add_file(strokes_table *t, const char *path, strokes_error *err)
{
    if (err)
        memset(err, 0, sizeof *err);
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_err(err, path, 0, "cannot open: %s", strerror(errno));
        return -1;
    }
    char *buf = malloc(MAX_TEXT + 1);
    if (!buf) {
        fclose(f);
        set_err(err, path, 0, "out of memory");
        return -1;
    }
    size_t n = fread(buf, 1, MAX_TEXT + 1, f);
    int rd_err = ferror(f);
    fclose(f);
    if (rd_err || n > MAX_TEXT) {
        free(buf);
        if (rd_err)
            set_err(err, path, 0, "read error");
        else
            set_err(err, path, 0, "file larger than %d bytes", MAX_TEXT);
        return -1;
    }
    buf[n] = 0;
    if (memchr(buf, 0, n)) {
        free(buf);
        set_err(err, path, 0, "binary file (NUL byte)");
        return -1;
    }
    int rc = strokes_table_add_text(t, buf, path, err);
    free(buf);
    return rc;
}

int strokes_table_glyph_count(const strokes_table *t)
{
    int n = 0;
    for (int i = 0; t && i < t->ng; ++i)
        if (t->g[i].n_variants > 0)
            n++;
    return n;
}

int strokes_table_template_count(const strokes_table *t)
{
    int n = 0;
    for (int i = 0; t && i < t->ng; ++i)
        if (t->g[i].n_variants > 0)
            n += t->g[i].n_variants;
    return n;
}

int strokes_table_template_points(const strokes_table *t, const char *id, int set, int v, float *xy, int max)
{
    if (!t || !id || v < 0)
        return -1;
    const int gi = stk_find_glyph(t, id, set);
    if (gi < 0 || t->g[gi].n_variants <= 0)
        return -1;
    if (t->g[gi].is_tap)
        return v == 0 ? 0 : -1;
    int k = 0;
    for (int i = 0; i < t->nt; ++i) {
        const stk_tpl *tp = &t->tp[i];
        if (tp->dead || tp->glyph != gi)
            continue;
        if (k++ != v)
            continue;
        for (int j = 0; xy && j < tp->nv && j < max; ++j) {
            xy[2 * j] = t->v[tp->v0 + j].x;
            xy[2 * j + 1] = t->v[tp->v0 + j].y;
        }
        return tp->nv;
    }
    return -1;
}

int strokes_table_glyph_info(const strokes_table *t, int i, char id[STROKES_ID_MAX], int *set, int *variants)
{
    if (!t || i < 0)
        return -1;
    int k = 0;
    for (int j = 0; j < t->ng; ++j) {
        if (t->g[j].n_variants <= 0)
            continue;
        if (k == i) {
            if (id)
                snprintf(id, STROKES_ID_MAX, "%s", t->g[j].id);
            if (set)
                *set = t->g[j].set;
            if (variants)
                *variants = t->g[j].n_variants;
            return 0;
        }
        k++;
    }
    return -1;
}
