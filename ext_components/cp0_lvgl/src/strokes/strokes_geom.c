/*
 * SPDX-License-Identifier: MIT
 *
 * Geometry of the Strokes recogniser: resampling, smoothing, hook trimming, features, normalisation and the
 * template score. Our own code; the resample-then-compare idea is the one published with the $1 recogniser and
 * Protractor (cosine of centred, length-normalised point vectors), without their rotation step.
 */
#include "strokes_internal.h"

#include <math.h>
#include <string.h>

float stk_path_len(const stk_vec *p, int n)
{
    float len = 0.0f;
    for (int i = 1; i < n; ++i)
        len += hypotf(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    return len;
}

void stk_resample(const stk_vec *in, int n, stk_vec *out, int m)
{
    if (n <= 0) {
        for (int i = 0; i < m; ++i)
            out[i].x = out[i].y = 0.0f;
        return;
    }
    float total = stk_path_len(in, n);
    if (n == 1 || total <= 1e-6f) {
        for (int i = 0; i < m; ++i)
            out[i] = in[0];
        return;
    }
    float step = total / (float)(m - 1);
    int k = 0;
    out[k++] = in[0];
    float acc = 0.0f; /* distance walked since the last emitted point */
    stk_vec prev = in[0];
    for (int i = 1; i < n && k < m - 1; ++i) {
        stk_vec cur = in[i];
        float seg = hypotf(cur.x - prev.x, cur.y - prev.y);
        while (seg > 0.0f && acc + seg >= step && k < m - 1) {
            float f = (step - acc) / seg;
            stk_vec q = {prev.x + f * (cur.x - prev.x), prev.y + f * (cur.y - prev.y)};
            out[k++] = q;
            prev = q;
            seg = hypotf(cur.x - prev.x, cur.y - prev.y);
            acc = 0.0f;
        }
        acc += seg;
        prev = cur;
    }
    while (k < m)
        out[k++] = in[n - 1];
}

void stk_smooth(stk_vec *p, int n)
{
    if (n < 3)
        return;
    stk_vec prev = p[0];
    for (int i = 1; i < n - 1; ++i) {
        stk_vec cur = p[i];
        p[i].x = 0.25f * prev.x + 0.5f * cur.x + 0.25f * p[i + 1].x;
        p[i].y = 0.25f * prev.y + 0.5f * cur.y + 0.25f * p[i + 1].y;
        prev = cur;
    }
}

/* Angle between two vectors, 0..pi; 0 when either is null. */
static float vec_angle(stk_vec a, stk_vec b)
{
    float la = hypotf(a.x, a.y), lb = hypotf(b.x, b.y);
    if (la <= 1e-9f || lb <= 1e-9f)
        return 0.0f;
    float c = (a.x * b.x + a.y * b.y) / (la * lb);
    if (c > 1.0f)
        c = 1.0f;
    if (c < -1.0f)
        c = -1.0f;
    return acosf(c);
}

/* Index of the first point at least `dist` along the path from point `from` (forward), or n - 1. */
static int walk(const stk_vec *p, int n, int from, float dist)
{
    float acc = 0.0f;
    for (int i = from + 1; i < n; ++i) {
        acc += hypotf(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
        if (acc >= dist)
            return i;
    }
    return n - 1;
}

/* Hook at the start: a vertex h within max_len of the start where the path turns back: h is the farthest point
 * from the start so far (the next vertex comes closer again) and the body that follows leaves at more than 100
 * degrees from the hook. The turn must happen within max_len: a short first stroke of the glyph itself that turns
 * further on (the tail of q, the bar of g, the end of v, read from the other end) is not a hook. Returns h, or 0. */
static int start_hook(const stk_vec *p, int n, float max_len, float body)
{
    int best = 0;
    float best_a = 100.0f * STK_PI / 180.0f;
    float acc = 0.0f;
    for (int h = 1; h < n - 2; ++h) {
        acc += hypotf(p[h].x - p[h - 1].x, p[h].y - p[h - 1].y);
        if (acc > max_len)
            break;
        float d = hypotf(p[h].x - p[0].x, p[h].y - p[0].y);
        if (hypotf(p[h + 1].x - p[0].x, p[h + 1].y - p[0].y) >= d)
            continue; /* still moving away from the start: not the turn */
        int k = walk(p, n, h, body);
        stk_vec in = {p[h].x - p[0].x, p[h].y - p[0].y};
        stk_vec out = {p[k].x - p[h].x, p[k].y - p[h].y};
        float a = vec_angle(in, out);
        if (a > best_a) {
            best_a = a;
            best = h;
        }
    }
    return best;
}

int stk_trim_hooks(stk_vec *p, int n, float max_len)
{
    if (n < 6 || max_len <= 0.0f)
        return n;
    float total = stk_path_len(p, n);
    float body = 0.15f * total;
    int h = start_hook(p, n, max_len, body);
    if (h > 0) {
        memmove(p, p + h, (size_t)(n - h) * sizeof *p);
        n -= h;
    }
    if (n < 6)
        return n;
    /* End hook: the same test on the reversed path. */
    stk_vec rev[STK_FINE * 2];
    if (n > (int)(sizeof rev / sizeof rev[0]))
        return n;
    for (int i = 0; i < n; ++i)
        rev[i] = p[n - 1 - i];
    h = start_hook(rev, n, max_len, body);
    if (h > 0)
        n -= h;
    return n;
}

int stk_numpad_dir(float a)
{
    static const int digit[8] = {6, 3, 2, 1, 4, 7, 8, 9}; /* sector k * 45 degrees, y down */
    float s = a / (STK_PI / 4.0f);
    int k = (int)floorf(s + 0.5f);
    k %= 8;
    if (k < 0)
        k += 8;
    return digit[k];
}

float stk_numpad_angle(int d)
{
    switch (d) {
    case 6: return 0.0f;
    case 3: return STK_PI / 4.0f;
    case 2: return STK_PI / 2.0f;
    case 1: return 3.0f * STK_PI / 4.0f;
    case 4: return STK_PI;
    case 7: return -3.0f * STK_PI / 4.0f;
    case 8: return -STK_PI / 2.0f;
    case 9: return -STK_PI / 4.0f;
    default: return 0.0f;
    }
}

float stk_angle_diff(float a, float b)
{
    float d = fmodf(fabsf(a - b), 2.0f * STK_PI);
    return d > STK_PI ? 2.0f * STK_PI - d : d;
}

static void chain_code(const stk_vec *p, int n, char *out, size_t size)
{
    int code[STK_N];
    int m = 0;
    for (int i = 1; i < n && m < STK_N; ++i) {
        float dx = p[i].x - p[i - 1].x, dy = p[i].y - p[i - 1].y;
        if (fabsf(dx) + fabsf(dy) <= 1e-9f)
            continue;
        code[m++] = stk_numpad_dir(atan2f(dy, dx));
    }
    /* Runs, then drop runs of one segment between two others (noise), then merge equal neighbours. */
    int rd[STK_N], rl[STK_N], nr = 0;
    for (int i = 0; i < m; ++i) {
        if (nr > 0 && rd[nr - 1] == code[i])
            rl[nr - 1]++;
        else {
            rd[nr] = code[i];
            rl[nr] = 1;
            nr++;
        }
    }
    int fd[STK_N], nf = 0;
    for (int i = 0; i < nr; ++i) {
        if (rl[i] < 2 && nr > 1)
            continue;
        if (nf > 0 && fd[nf - 1] == rd[i])
            continue;
        fd[nf++] = rd[i];
    }
    if (nf == 0 && nr > 0)
        fd[nf++] = rd[0];
    size_t k = 0;
    for (int i = 0; i < nf && k + 1 < size; ++i)
        out[k++] = (char)('0' + fd[i]);
    out[k] = '\0';
}

void stk_features(const stk_vec *p, int n, stk_feat *f)
{
    memset(f, 0, sizeof *f);
    if (n < 2)
        return;
    float minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
    for (int i = 1; i < n; ++i) {
        minx = fminf(minx, p[i].x);
        maxx = fmaxf(maxx, p[i].x);
        miny = fminf(miny, p[i].y);
        maxy = fmaxf(maxy, p[i].y);
    }
    float side = fmaxf(maxx - minx, maxy - miny);
    if (side <= 1e-9f)
        side = 1e-9f;
    float cx = 0.5f * (minx + maxx), cy = 0.5f * (miny + maxy);
    f->su = (p[0].x - cx) / side + 0.5f;
    f->sv = (p[0].y - cy) / side + 0.5f;
    f->eu = (p[n - 1].x - cx) / side + 0.5f;
    f->ev = (p[n - 1].y - cy) / side + 0.5f;

    float path = stk_path_len(p, n);
    float dx = p[n - 1].x - p[0].x, dy = p[n - 1].y - p[0].y;
    float chord = hypotf(dx, dy);
    f->straight = path > 1e-9f ? chord / path : 0.0f;
    if (f->straight > 1.0f)
        f->straight = 1.0f;
    f->dir = atan2f(dy, dx);
    f->gap = chord / side;

    /* Turning: signed angle between consecutive segments (cross > 0 = clockwise on a y-down screen). */
    float t[STK_N];
    int nt = 0;
    for (int i = 1; i < n - 1 && nt < STK_N; ++i) {
        float ax = p[i].x - p[i - 1].x, ay = p[i].y - p[i - 1].y;
        float bx = p[i + 1].x - p[i].x, by = p[i + 1].y - p[i].y;
        float a = 0.0f;
        if ((fabsf(ax) + fabsf(ay)) > 1e-9f && (fabsf(bx) + fabsf(by)) > 1e-9f)
            a = atan2f(ax * by - ay * bx, ax * bx + ay * by);
        t[nt++] = a;
        f->turn += a;
    }
    /* Corners: windows of three vertices turning more than STK_CORNER_DEG; a cluster counts once. The sharpest
     * window is the stroke's sharpest turn (gate sharp=). */
    float lim = STK_CORNER_DEG * STK_PI / 180.0f;
    int in_corner = 0;
    for (int i = 0; i < nt; ++i) {
        float w = t[i];
        if (i > 0)
            w += t[i - 1];
        if (i + 1 < nt)
            w += t[i + 1];
        if (fabsf(w) > f->sharp)
            f->sharp = fabsf(w);
        if (fabsf(w) > lim) {
            if (!in_corner)
                f->corners++;
            in_corner = 1;
        } else {
            in_corner = 0;
        }
    }
    chain_code(p, n, f->dirs, sizeof f->dirs);
}

int stk_normalise(const stk_vec *in, int n, stk_vec *out)
{
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < n; ++i) {
        sx += in[i].x;
        sy += in[i].y;
    }
    float cx = (float)(sx / n), cy = (float)(sy / n);
    double ss = 0.0;
    for (int i = 0; i < n; ++i) {
        out[i].x = in[i].x - cx;
        out[i].y = in[i].y - cy;
        ss += (double)out[i].x * out[i].x + (double)out[i].y * out[i].y;
    }
    if (!(ss > 1e-18)) {
        for (int i = 0; i < n; ++i)
            out[i].x = out[i].y = 0.0f;
        return 0;
    }
    float k = (float)(1.0 / sqrt(ss));
    for (int i = 0; i < n; ++i) {
        out[i].x *= k;
        out[i].y *= k;
    }
    return 1;
}

int stk_prepare(const stk_vec *fine, int n, float xstretch, stk_vec out[STK_N])
{
    stk_vec tmp[STK_FINE];
    if (n > STK_FINE)
        n = STK_FINE;
    for (int i = 0; i < n; ++i) {
        tmp[i].x = fine[i].x * xstretch;
        tmp[i].y = fine[i].y;
    }
    stk_vec rs[STK_N];
    stk_resample(tmp, n, rs, STK_N);
    return stk_normalise(rs, STK_N, out);
}

void stk_rot_init(stk_rot *r, float max_deg)
{
    if (!(max_deg > 0.0f)) {
        r->c = 1.0f;
        r->s = 0.0f;
        r->t = 0.0f;
        return;
    }
    if (max_deg > 45.0f)
        max_deg = 45.0f;
    float a = max_deg * STK_PI / 180.0f;
    r->c = cosf(a);
    r->s = sinf(a);
    r->t = tanf(a);
}

/* Cosine similarity of two centred unit vectors, maximised over a rotation of a within +-bound (closed form, as in
 * Protractor, but bounded: the matcher stays orientation-sensitive, it only forgives a slant). */
float stk_score(const stk_vec *a, const stk_vec *b, int n, const stk_rot *rot)
{
    float A = 0.0f, B = 0.0f;
    for (int i = 0; i < n; ++i) {
        A += a[i].x * b[i].x + a[i].y * b[i].y;
        B += a[i].x * b[i].y - a[i].y * b[i].x;
    }
    float dot;
    float absb = fabsf(B);
    if (rot->t <= 0.0f)
        dot = A;
    else if (A > 0.0f && absb <= A * rot->t)
        dot = sqrtf(A * A + B * B); /* the best rotation is inside the bound */
    else
        dot = A * rot->c + absb * rot->s; /* clamped to the bound */
    if (dot > 1.0f)
        dot = 1.0f;
    if (dot < -1.0f)
        dot = -1.0f;
    float s = 1.0f - acosf(dot) / (STK_PI / 2.0f);
    return s < 0.0f ? 0.0f : s;
}
