/*
 * SPDX-License-Identifier: MIT
 *
 * Synthetic stroke generator (report 038 section 5, "Corpus without a board"): takes a template of the table and
 * writes it as a simulated stylus would, with per-sample shape variation (smooth wobble, corner rounding), affine
 * noise (per-axis scale, rotation, shear), a size in millimetres, a report rate, a non-uniform pen speed, Gaussian
 * jitter, landing and lift hooks, truncated ends, one capacitive drop-out and whole-pixel quantisation.
 * Deterministic for a given seed (splitmix64). It is a regression gate, not a measure of real accuracy: the samples
 * come from the same templates the recogniser matches against.
 */
#include "sample.h"
#include "strokes_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define DENSE 160
#define TWO_PI 6.28318530717959

void synth_default(synth_cfg *c)
{
    c->ppmm = 8.54f;
    c->size_min_mm = 8.0f;
    c->size_max_mm = 16.0f;
    c->scale_min = 0.8f;
    c->scale_max = 1.2f;
    c->rot_deg = 12.0f;
    c->shear = 0.15f;
    c->wobble = 0.04f;
    c->round_p = 0.5f;
    c->round_max = 0.12f;
    c->noise = 0.01f;
    c->rate_min_hz = 60.0f;
    c->rate_max_hz = 120.0f;
    c->speed_min = 60.0f;
    c->speed_max = 150.0f;
    c->hook_p = 0.3f;
    c->hook_min = 0.02f;
    c->hook_max = 0.06f;
    c->trunc_p = 0.2f;
    c->trunc_max = 0.04f;
    c->drop_p = 0.1f;
    c->drop_min_ms = 10.0f;
    c->drop_max_ms = 40.0f;
    c->tap_min_ms = 40.0f;
    c->tap_max_ms = 180.0f;
    c->tap_jitter_mm = 0.3f;
    c->quantise = 1;
}

void synth_hard(synth_cfg *c)
{
    synth_default(c);
    c->size_min_mm = 6.0f;
    c->size_max_mm = 20.0f;
    c->scale_min = 0.7f;
    c->scale_max = 1.3f;
    c->rot_deg = 20.0f;
    c->shear = 0.25f;
    c->wobble = 0.07f;
    c->round_p = 0.8f;
    c->round_max = 0.2f;
    c->noise = 0.02f;
    c->speed_max = 250.0f;
    c->hook_p = 0.5f;
    c->hook_max = 0.10f;
    c->trunc_p = 0.4f;
    c->trunc_max = 0.08f;
    c->drop_p = 0.3f;
    c->drop_max_ms = 48.0f;
}

static const struct {
    const char *name;
    size_t off;
} fields[] = {
    {"ppmm", offsetof(synth_cfg, ppmm)},
    {"size_min", offsetof(synth_cfg, size_min_mm)},
    {"size_max", offsetof(synth_cfg, size_max_mm)},
    {"scale_min", offsetof(synth_cfg, scale_min)},
    {"scale_max", offsetof(synth_cfg, scale_max)},
    {"rot", offsetof(synth_cfg, rot_deg)},
    {"shear", offsetof(synth_cfg, shear)},
    {"wobble", offsetof(synth_cfg, wobble)},
    {"round_p", offsetof(synth_cfg, round_p)},
    {"round_max", offsetof(synth_cfg, round_max)},
    {"noise", offsetof(synth_cfg, noise)},
    {"rate_min", offsetof(synth_cfg, rate_min_hz)},
    {"rate_max", offsetof(synth_cfg, rate_max_hz)},
    {"speed_min", offsetof(synth_cfg, speed_min)},
    {"speed_max", offsetof(synth_cfg, speed_max)},
    {"hook_p", offsetof(synth_cfg, hook_p)},
    {"hook_min", offsetof(synth_cfg, hook_min)},
    {"hook_max", offsetof(synth_cfg, hook_max)},
    {"trunc_p", offsetof(synth_cfg, trunc_p)},
    {"trunc_max", offsetof(synth_cfg, trunc_max)},
    {"drop_p", offsetof(synth_cfg, drop_p)},
    {"drop_min", offsetof(synth_cfg, drop_min_ms)},
    {"drop_max", offsetof(synth_cfg, drop_max_ms)},
    {"tap_min", offsetof(synth_cfg, tap_min_ms)},
    {"tap_max", offsetof(synth_cfg, tap_max_ms)},
    {"tap_jitter", offsetof(synth_cfg, tap_jitter_mm)},
};

int synth_set(synth_cfg *c, const char *kv)
{
    const char *eq = strchr(kv, '=');
    if (!eq)
        return -1;
    size_t kl = (size_t)(eq - kv);
    char *end = NULL;
    double v = strtod(eq + 1, &end);
    if (!end || *end || !isfinite(v))
        return -1;
    if (kl == 8 && strncmp(kv, "quantise", 8) == 0) {
        c->quantise = v != 0.0;
        return 0;
    }
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; ++i)
        if (strlen(fields[i].name) == kl && strncmp(kv, fields[i].name, kl) == 0) {
            *(float *)((char *)c + fields[i].off) = (float)v;
            return 0;
        }
    return -1;
}

void synth_describe(const synth_cfg *c, char *buf, size_t size)
{
    snprintf(buf, size,
             "ppmm %.2f, size %.0f-%.0f mm, axis scale %.2f-%.2f, rotation +-%.0f deg, shear +-%.2f, wobble %.0f %%, "
             "corner rounding p %.2f up to %.0f %%, jitter sigma %.1f %% of size, rate %.0f-%.0f Hz, speed %.0f-%.0f "
             "mm/s, hooks p %.2f per end %.0f-%.0f %%, truncation p %.2f per end up to %.0f %%, drop-out p %.2f "
             "%.0f-%.0f ms, taps %.0f-%.0f ms within %.1f mm, %s",
             (double)c->ppmm, (double)c->size_min_mm, (double)c->size_max_mm, (double)c->scale_min,
             (double)c->scale_max, (double)c->rot_deg, (double)c->shear, (double)(c->wobble * 100.0f),
             (double)c->round_p, (double)(c->round_max * 100.0f), (double)(c->noise * 100.0f),
             (double)c->rate_min_hz, (double)c->rate_max_hz, (double)c->speed_min, (double)c->speed_max,
             (double)c->hook_p, (double)(c->hook_min * 100.0f), (double)(c->hook_max * 100.0f), (double)c->trunc_p,
             (double)(c->trunc_max * 100.0f), (double)c->drop_p, (double)c->drop_min_ms, (double)c->drop_max_ms,
             (double)c->tap_min_ms, (double)c->tap_max_ms, (double)c->tap_jitter_mm,
             c->quantise ? "whole pixels" : "sub-pixel");
}

/* ---- rng: splitmix64 ---- */

void synth_seed(synth_rng *r, uint64_t seed)
{
    r->s = seed * 0x2545F4914F6CDD1Dull + 0x9E3779B97F4A7C15ull;
}

static uint64_t next_u64(synth_rng *r)
{
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double synth_uniform(synth_rng *r)
{
    return (double)(next_u64(r) >> 11) * (1.0 / 9007199254740992.0);
}

static float uni(synth_rng *r, float a, float b)
{
    return a + (b - a) * (float)synth_uniform(r);
}

static float gauss(synth_rng *r)
{
    double u1 = synth_uniform(r), u2 = synth_uniform(r);
    if (u1 < 1e-12)
        u1 = 1e-12;
    return (float)(sqrt(-2.0 * log(u1)) * cos(TWO_PI * u2));
}

/* ---- glyph access ---- */

int synth_glyph_count(const strokes_table *t)
{
    return t->ng;
}

const char *synth_glyph_id(const strokes_table *t, int gi)
{
    return gi >= 0 && gi < t->ng ? t->g[gi].id : "";
}

int synth_glyph_set(const strokes_table *t, int gi)
{
    return gi >= 0 && gi < t->ng ? t->g[gi].set : -1;
}

int synth_glyph_live(const strokes_table *t, int gi)
{
    return gi >= 0 && gi < t->ng && t->g[gi].n_variants > 0;
}

/* ---- rendering ---- */

typedef struct {
    stk_vec p[DENSE];
    float cum[DENSE];
} dense_path;

static stk_vec path_at(const dense_path *d, float s)
{
    if (s <= 0.0f)
        return d->p[0];
    if (s >= d->cum[DENSE - 1])
        return d->p[DENSE - 1];
    int lo = 0, hi = DENSE - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (d->cum[mid] <= s)
            lo = mid;
        else
            hi = mid;
    }
    float seg = d->cum[hi] - d->cum[lo];
    float f = seg > 0.0f ? (s - d->cum[lo]) / seg : 0.0f;
    stk_vec q = {d->p[lo].x + f * (d->p[hi].x - d->p[lo].x), d->p[lo].y + f * (d->p[hi].y - d->p[lo].y)};
    return q;
}

typedef struct {
    float x, y, t; /* t in seconds */
} tpoint;

#define MAXT SAMPLE_MAX_PTS

static void emit(tpoint *tp, int *n, stk_vec p, float t)
{
    if (*n < MAXT) {
        tp[*n].x = p.x;
        tp[*n].y = p.y;
        tp[*n].t = t;
        (*n)++;
    }
}

static void render(const stk_vec *v, int nv, const synth_cfg *c, synth_rng *r, stk_sample *s, int variation)
{
    stk_vec work[STK_MAX_VERTS * 2 + 2];
    int nw = 0;
    /* corner rounding: cut every interior vertex (Chaikin, one pass), except full reversals (a retraced stem): the
     * pen stops there, it does not cut the corner */
    if (variation && nv > 2 && synth_uniform(r) < c->round_p) {
        float rr = uni(r, 0.02f, c->round_max);
        work[nw++] = v[0];
        for (int i = 1; i < nv - 1 && nw < STK_MAX_VERTS * 2; ++i) {
            stk_vec a = v[i - 1], b = v[i], cc = v[i + 1];
            float ix = b.x - a.x, iy = b.y - a.y, ox = cc.x - b.x, oy = cc.y - b.y;
            float li = hypotf(ix, iy), lo = hypotf(ox, oy);
            if (li > 0.0f && lo > 0.0f && (ix * ox + iy * oy) / (li * lo) < -0.866f) { /* turn over 150 degrees */
                work[nw++] = b;
                continue;
            }
            stk_vec p1 = {b.x - rr * ix, b.y - rr * iy};
            stk_vec p2 = {b.x + rr * ox, b.y + rr * oy};
            work[nw++] = p1;
            work[nw++] = p2;
        }
        work[nw++] = v[nv - 1];
    } else {
        for (int i = 0; i < nv && nw < STK_MAX_VERTS * 2; ++i)
            work[nw++] = v[i];
    }
    /* unit box, centred */
    float minx = work[0].x, maxx = work[0].x, miny = work[0].y, maxy = work[0].y;
    for (int i = 1; i < nw; ++i) {
        minx = fminf(minx, work[i].x);
        maxx = fmaxf(maxx, work[i].x);
        miny = fminf(miny, work[i].y);
        maxy = fmaxf(maxy, work[i].y);
    }
    float side = fmaxf(fmaxf(maxx - minx, maxy - miny), 1e-6f);
    float cx = 0.5f * (minx + maxx), cy = 0.5f * (miny + maxy);
    for (int i = 0; i < nw; ++i) {
        work[i].x = (work[i].x - cx) / side;
        work[i].y = (work[i].y - cy) / side;
    }
    dense_path d;
    stk_resample(work, nw, d.p, DENSE);
    /* smooth wobble, then the affine noise */
    float ax = variation ? c->wobble * uni(r, -1.0f, 1.0f) : 0.0f;
    float ay = variation ? c->wobble * uni(r, -1.0f, 1.0f) : 0.0f;
    float fx = uni(r, 1.0f, 2.5f) * 3.14159265f, fy = uni(r, 1.0f, 2.5f) * 3.14159265f;
    float phx = uni(r, 0.0f, 6.2831853f), phy = uni(r, 0.0f, 6.2831853f);
    float sx = uni(r, c->scale_min, c->scale_max), sy = uni(r, c->scale_min, c->scale_max);
    float sh = uni(r, -c->shear, c->shear);
    float rot = uni(r, -c->rot_deg, c->rot_deg) * 3.14159265f / 180.0f;
    float cr = cosf(rot), sr = sinf(rot);
    for (int i = 0; i < DENSE; ++i) {
        float x = d.p[i].x + ax * sinf(fx * d.p[i].y + phx);
        float y = d.p[i].y + ay * sinf(fy * d.p[i].x + phy);
        x *= sx;
        y *= sy;
        x += sh * y;
        d.p[i].x = cr * x - sr * y;
        d.p[i].y = sr * x + cr * y;
    }
    /* size in mm, position */
    minx = maxx = d.p[0].x;
    miny = maxy = d.p[0].y;
    for (int i = 1; i < DENSE; ++i) {
        minx = fminf(minx, d.p[i].x);
        maxx = fmaxf(maxx, d.p[i].x);
        miny = fminf(miny, d.p[i].y);
        maxy = fmaxf(maxy, d.p[i].y);
    }
    side = fmaxf(fmaxf(maxx - minx, maxy - miny), 1e-6f);
    float size_px = uni(r, c->size_min_mm, c->size_max_mm) * c->ppmm;
    float k = size_px / side;
    float ox = uni(r, 150.0f, 250.0f), oy = uni(r, 550.0f, 650.0f); /* inside a 480x800 portrait canvas */
    d.cum[0] = 0.0f;
    for (int i = 0; i < DENSE; ++i) {
        d.p[i].x = ox + (d.p[i].x - minx) * k;
        d.p[i].y = oy + (d.p[i].y - miny) * k;
        if (i)
            d.cum[i] = d.cum[i - 1] + hypotf(d.p[i].x - d.p[i - 1].x, d.p[i].y - d.p[i - 1].y);
    }
    float L = d.cum[DENSE - 1];

    /* time sampling */
    float s0 = 0.0f, s1 = L;
    if (variation && synth_uniform(r) < c->trunc_p)
        s0 = uni(r, 0.0f, c->trunc_max) * L;
    if (variation && synth_uniform(r) < c->trunc_p)
        s1 = L - uni(r, 0.0f, c->trunc_max) * L;
    float vmean = uni(r, c->speed_min, c->speed_max) * c->ppmm; /* px/s */
    float rate = uni(r, c->rate_min_hz, c->rate_max_hz);
    float dt = 1.0f / rate;
    tpoint tp[MAXT];
    int n = 0;
    float t = 0.0f;
    if (variation && synth_uniform(r) < c->hook_p) {
        float hl = uni(r, c->hook_min, c->hook_max) * size_px;
        float a = uni(r, 0.0f, 6.2831853f);
        stk_vec st = path_at(&d, s0);
        stk_vec h0 = {st.x + hl * cosf(a), st.y + hl * sinf(a)};
        stk_vec h1 = {st.x + 0.5f * hl * cosf(a), st.y + 0.5f * hl * sinf(a)};
        emit(tp, &n, h0, t);
        t += dt;
        emit(tp, &n, h1, t);
        t += dt;
    }
    float pos = s0;
    float span = fmaxf(s1 - s0, 1e-3f);
    while (pos < s1 && n < MAXT - 4) {
        emit(tp, &n, path_at(&d, pos), t);
        float ph = (pos - s0) / span;
        float speed = vmean * (0.35f + 0.65f * sinf(3.14159265f * ph)) / 0.764f;
        pos += speed * dt;
        t += dt;
    }
    emit(tp, &n, path_at(&d, s1), t);
    if (variation && synth_uniform(r) < c->hook_p) {
        float hl = uni(r, c->hook_min, c->hook_max) * size_px;
        float a = uni(r, 0.0f, 6.2831853f);
        stk_vec en = path_at(&d, s1);
        t += dt;
        emit(tp, &n, (stk_vec){en.x + 0.5f * hl * cosf(a), en.y + 0.5f * hl * sinf(a)}, t);
        t += dt;
        emit(tp, &n, (stk_vec){en.x + hl * cosf(a), en.y + hl * sinf(a)}, t);
    }
    /* jitter */
    float sigma = c->noise * size_px;
    for (int i = 0; i < n; ++i) {
        tp[i].x += sigma * gauss(r);
        tp[i].y += sigma * gauss(r);
        if (c->quantise) {
            tp[i].x = floorf(tp[i].x + 0.5f);
            tp[i].y = floorf(tp[i].y + 0.5f);
        }
    }
    /* drop-out: m frames are lost; the panel reports the lift at the first lost frame and a new press at the next
     * frame it sees, so the lift-to-press gap is m frames (the configured gap rounded to whole frames) */
    int i0 = -1, i1 = -1;
    if (variation && n > 6 && synth_uniform(r) < c->drop_p) {
        i0 = 1 + (int)(uni(r, 0.2f, 0.8f) * (float)(n - 3));
        float g = uni(r, c->drop_min_ms, c->drop_max_ms) / 1000.0f;
        int m = (int)floorf(g / dt + 0.5f);
        if (m < 1)
            m = 1;
        i1 = i0 + 1 + m;
        if (i1 >= n)
            i0 = i1 = -1;
    }
    for (int i = 0; i < n; ++i) {
        if (i0 >= 0 && i > i0 && i < i1)
            continue;
        sample_add(s, tp[i].x, tp[i].y, (uint64_t)llroundf(tp[i].t * 1e6f));
        if (i == i0)
            sample_lift(s, (uint64_t)llroundf(tp[i0 + 1].t * 1e6f));
    }
    if (s->n > 0)
        sample_lift(s, s->p[s->n - 1].t_us + (uint64_t)llroundf(dt * 0.5e6f));
}

static void render_tap(const synth_cfg *c, synth_rng *r, stk_sample *s)
{
    float dur = uni(r, c->tap_min_ms, c->tap_max_ms) / 1000.0f;
    float rate = uni(r, c->rate_min_hz, c->rate_max_hz);
    int n = (int)(dur * rate) + 1;
    if (n < 1)
        n = 1;
    float cx = uni(r, 150.0f, 250.0f), cy = uni(r, 550.0f, 650.0f);
    float j = 0.5f * c->tap_jitter_mm * c->ppmm;
    for (int i = 0; i < n && i < SAMPLE_MAX_PTS; ++i) {
        float x = cx + uni(r, -j, j), y = cy + uni(r, -j, j);
        if (c->quantise) {
            x = floorf(x + 0.5f);
            y = floorf(y + 0.5f);
        }
        sample_add(s, x, y, (uint64_t)llroundf((float)i / rate * 1e6f));
    }
    sample_lift(s, (uint64_t)llroundf(dur * 1e6f));
}

int synth_glyph(const strokes_table *t, int gi, const synth_cfg *c, synth_rng *r, stk_sample *s)
{
    if (!synth_glyph_live(t, gi))
        return -1;
    const stk_glyph *g = &t->g[gi];
    sample_clear(s);
    snprintf(s->label, sizeof s->label, "%s", g->id);
    s->ppmm = c->ppmm;
    if (g->set == STROKES_SET_DIGITS)
        s->box = STROKES_BOX_DIGITS;
    else if (g->set == STROKES_SET_ANY)
        s->box = synth_uniform(r) < 0.5 ? STROKES_BOX_LETTERS : STROKES_BOX_DIGITS;
    else
        s->box = STROKES_BOX_LETTERS;
    if (g->is_tap) {
        render_tap(c, r, s);
        return 0;
    }
    int live = 0;
    for (int i = 0; i < t->nt; ++i)
        if (!t->tp[i].dead && t->tp[i].glyph == gi)
            live++;
    if (live == 0)
        return -1;
    int pick = (int)(synth_uniform(r) * live);
    if (pick >= live)
        pick = live - 1;
    for (int i = 0; i < t->nt; ++i) {
        const stk_tpl *tp = &t->tp[i];
        if (tp->dead || tp->glyph != gi)
            continue;
        if (pick-- == 0) {
            render(&t->v[tp->v0], tp->nv, c, r, s, 1);
            return 0;
        }
    }
    return -1;
}

void synth_scribble(const synth_cfg *c, synth_rng *r, stk_sample *s)
{
    sample_clear(s);
    snprintf(s->label, sizeof s->label, "?");
    s->ppmm = c->ppmm;
    s->box = synth_uniform(r) < 0.5 ? STROKES_BOX_LETTERS : STROKES_BOX_DIGITS;
    stk_vec v[8];
    int nv = 4 + (int)(synth_uniform(r) * 5.0);
    if (nv > 8)
        nv = 8;
    for (int i = 0; i < nv; ++i) {
        v[i].x = uni(r, 0.0f, 100.0f);
        v[i].y = uni(r, 0.0f, 100.0f);
    }
    render(v, nv, c, r, s, 0);
}
