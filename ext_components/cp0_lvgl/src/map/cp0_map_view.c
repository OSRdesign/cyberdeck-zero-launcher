/*
 * SPDX-License-Identifier: MIT
 *
 * Map widget, the pure core: view, tiles in load order, control layout and hit test, track simplification, label
 * placement, attribution text (see cp0_map_view.h).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* clock_gettime */
#endif
#include "cp0_map_view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------------------------------------------------------------------------------------------------- the view */

static double world_px(int z)
{
    return (double)CP0_MAP_TILE_PX * ldexp(1.0, z);
}

static double scale_of(const cp0_map_view_t *v)
{
    return (double)v->scale_x2 / 2.0;
}

static void normalise(cp0_map_view_t *v)
{
    const double s = world_px(v->z);
    if (!isfinite(v->cx)) v->cx = s / 2;
    if (!isfinite(v->cy)) v->cy = s / 2;
    v->cx = fmod(v->cx, s);
    if (v->cx < 0) v->cx += s;
    if (v->cy < 0) v->cy = 0;
    if (v->cy > s) v->cy = s;
}

void cp0_map_view_init(cp0_map_view_t *v, int w, int h)
{
    memset(v, 0, sizeof *v);
    v->w = w > 0 ? w : 1;
    v->h = h > 0 ? h : 1;
    v->z = 2;
    v->scale_x2 = 2;
    v->zmin = 0;
    v->zmax = 22;
    v->data_max = 22;
    v->cx = v->cy = world_px(2) / 2;
}

void cp0_map_view_set_size(cp0_map_view_t *v, int w, int h)
{
    v->w = w > 0 ? w : 1;
    v->h = h > 0 ? h : 1;
}

static int clamp_int(int x, int lo, int hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

static void rezoom(cp0_map_view_t *v, int z)
{
    if (z == v->z) return;
    const double f = ldexp(1.0, z - v->z);
    v->cx *= f;
    v->cy *= f;
    v->z = z;
    normalise(v);
}

void cp0_map_view_set_limits(cp0_map_view_t *v, int data_min, int data_max, int overzoom)
{
    data_max = clamp_int(data_max, 0, CP0_MAP_MAX_ZOOM);
    data_min = clamp_int(data_min, 0, data_max);
    v->data_max = data_max;
    v->zmin = data_min;
    v->zmax = clamp_int(data_max + (overzoom ? 1 : 0), 0, CP0_MAP_MAX_ZOOM);
    rezoom(v, clamp_int(v->z, v->zmin, v->zmax));
}

int cp0_map_view_set_zoom(cp0_map_view_t *v, int z)
{
    rezoom(v, clamp_int(z, v->zmin, v->zmax));
    return v->z;
}

int cp0_map_view_zoom_at(cp0_map_view_t *v, int dz, double sx, double sy)
{
    const int z = clamp_int(v->z + dz, v->zmin, v->zmax);
    if (z == v->z) return z;
    const double s = scale_of(v), ex = (sx - v->w / 2.0) / s, ey = (sy - v->h / 2.0) / s;
    const double px = v->cx + ex, py = v->cy + ey, f = ldexp(1.0, z - v->z);
    v->z = z;
    v->cx = px * f - ex;
    v->cy = py * f - ey;
    normalise(v);
    return z;
}

void cp0_map_view_set_center(cp0_map_view_t *v, double lat, double lon)
{
    cp0_map_latlon_to_world_px(lat, lon, v->z, &v->cx, &v->cy);
    normalise(v);
}

void cp0_map_view_center(const cp0_map_view_t *v, double *lat, double *lon)
{
    cp0_map_world_px_to_latlon(v->cx, v->cy, v->z, lat, lon);
}

void cp0_map_view_set_scale(cp0_map_view_t *v, int scale_x2)
{
    v->scale_x2 = clamp_int(scale_x2, 2, 4);
}

void cp0_map_view_pan(cp0_map_view_t *v, double dx, double dy)
{
    const double s = scale_of(v);
    v->cx -= dx / s;
    v->cy -= dy / s;
    normalise(v);
}

int cp0_map_view_tile_px(const cp0_map_view_t *v)
{
    return 128 * v->scale_x2;
}

int cp0_map_view_overzoom(const cp0_map_view_t *v)
{
    return v->z > v->data_max;
}

void cp0_map_view_origin(const cp0_map_view_t *v, int64_t *ox, int64_t *oy)
{
    const double s = scale_of(v);
    if (ox) *ox = (int64_t)floor(v->cx * s - v->w / 2.0);
    if (oy) *oy = (int64_t)floor(v->cy * s - v->h / 2.0);
}

static void world_to_screen(const cp0_map_view_t *v, double wx, double wy, double *sx, double *sy)
{
    const double size = world_px(v->z), s = scale_of(v);
    double dx = fmod(wx - v->cx, size);
    if (dx < -size / 2) dx += size;
    if (dx >= size / 2) dx -= size;
    int64_t ox, oy;
    cp0_map_view_origin(v, &ox, &oy);
    if (sx) *sx = (v->cx + dx) * s - (double)ox;
    if (sy) *sy = wy * s - (double)oy;
}

void cp0_map_view_world0_to_screen(const cp0_map_view_t *v, double wx0, double wy0, double *sx, double *sy)
{
    const double f = ldexp(1.0, v->z);
    world_to_screen(v, wx0 * f, wy0 * f, sx, sy);
}

void cp0_map_view_latlon_to_screen(const cp0_map_view_t *v, double lat, double lon, double *sx, double *sy)
{
    double wx, wy;
    cp0_map_latlon_to_world_px(lat, lon, v->z, &wx, &wy);
    world_to_screen(v, wx, wy, sx, sy);
}

void cp0_map_view_screen_to_latlon(const cp0_map_view_t *v, double sx, double sy, double *lat, double *lon)
{
    const double s = scale_of(v);
    int64_t ox, oy;
    cp0_map_view_origin(v, &ox, &oy);
    cp0_map_world_px_to_latlon((sx + (double)ox) / s, (sy + (double)oy) / s, v->z, lat, lon);
}

double cp0_map_view_metres_per_px(const cp0_map_view_t *v)
{
    double lat, lon;
    cp0_map_view_center(v, &lat, &lon);
    return cp0_map_metres_per_px(lat, v->z, (float)scale_of(v));
}

int cp0_map_view_visible(const cp0_map_view_t *v, double wx0, double wy0, int margin, double *sx, double *sy)
{
    double x, y;
    cp0_map_view_world0_to_screen(v, wx0, wy0, &x, &y);
    if (sx) *sx = x;
    if (sy) *sy = y;
    return x >= -margin && y >= -margin && x <= v->w + margin && y <= v->h + margin;
}

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

static int cmp_slot(const void *a, const void *b)
{
    const cp0_map_slot_t *x = (const cp0_map_slot_t *)a, *y = (const cp0_map_slot_t *)b;
    if (x->dist2 != y->dist2) return x->dist2 < y->dist2 ? -1 : 1;
    if (x->sy != y->sy) return x->sy < y->sy ? -1 : 1;
    return x->sx < y->sx ? -1 : x->sx > y->sx;
}

int cp0_map_view_tiles(const cp0_map_view_t *v, int ring, cp0_map_slot_t *out, int max)
{
    if (!out || max <= 0) return 0;
    if (ring < 0) ring = 0;
    const int64_t T = cp0_map_view_tile_px(v), n = (int64_t)1 << v->z;
    int64_t ox, oy;
    cp0_map_view_origin(v, &ox, &oy);
    const int64_t vx0 = floor_div(ox, T), vx1 = floor_div(ox + v->w - 1, T);
    const int64_t vy0 = floor_div(oy, T), vy1 = floor_div(oy + v->h - 1, T);
    int count = 0;
    for (int pass = 0; pass < 2; pass++) {
        const int start = count;
        for (int64_t ty = vy0 - ring; ty <= vy1 + ring; ty++) {
            if (ty < 0 || ty >= n) continue;
            for (int64_t tx = vx0 - ring; tx <= vx1 + ring; tx++) {
                const int visible = tx >= vx0 && tx <= vx1 && ty >= vy0 && ty <= vy1;
                if (visible != (pass == 0)) continue;
                if (count == max) break;
                cp0_map_slot_t *s = &out[count++];
                s->z = v->z;
                s->x = (uint32_t)(((tx % n) + n) % n);
                s->y = (uint32_t)ty;
                s->sx = (int)(tx * T - ox);
                s->sy = (int)(ty * T - oy);
                s->visible = (uint8_t)visible;
                s->over = (uint8_t)(v->z > v->data_max);
                const int64_t ddx = s->sx + T / 2 - v->w / 2, ddy = s->sy + T / 2 - v->h / 2;
                const int64_t d2 = ddx * ddx + ddy * ddy;
                s->dist2 = d2 > UINT32_MAX ? UINT32_MAX : (uint32_t)d2;
            }
        }
        if (count - start > 1) qsort(out + start, (size_t)(count - start), sizeof *out, cmp_slot);
    }
    return count;
}

/* -------------------------------------------------------------------------------------------------- controls */

int cp0_map_rect_overlap(const cp0_map_rect_t *a, const cp0_map_rect_t *b)
{
    if (!a->w || !b->w || !a->h || !b->h) return 0;
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static int rect_xrange_overlap(const cp0_map_rect_t *r, int x0, int x1)
{
    return r->w && r->x < x1 && x0 < r->x + r->w;
}

void cp0_map_layout(const cp0_map_layout_in_t *in, cp0_map_layout_t *out)
{
    memset(out, 0, sizeof *out);
    const int w = in->w > 0 ? in->w : 1, h = in->h > 0 ? in->h : 1;
    const int shorter = w < h ? w : h;
    /* the touch square is TARGET (never more than a third of the shorter side); the drawn plate inside it at most 15 %
     * of the shorter side */
    int target = in->target > 0 ? in->target : 44;
    if (target > shorter / 3) target = shorter / 3;
    if (target < 8) target = 8;
    int plate = target * 3 / 4;
    if (plate > shorter * 15 / 100) plate = shorter * 15 / 100;
    if (plate < 8) plate = 8;
    if (plate > target) plate = target;
    const int text_h = in->text_h > 0 ? in->text_h : 12;
    int margin = in->space > 0 ? in->space : 8;
    if (margin > plate / 2) margin = plate / 2;
    if (margin < 2) margin = 2;
    const int pad = margin / 2 > 2 ? margin / 2 : 2;
    out->margin = margin;
    out->pad = pad;

    /* the bottom row: the scale bar at the left */
    int sb_max = w / 4;
    if (sb_max > 200) sb_max = 200;
    if (sb_max < 48) sb_max = 48;
    if (sb_max > w - 2 * margin - 2 * pad) sb_max = w - 2 * margin - 2 * pad;
    if (sb_max < 8) sb_max = 8;
    const int bar_h = text_h / 4 > 3 ? text_h / 4 : 3;
    out->scalebar_max_px = sb_max;
    out->scalebar.w = sb_max + 2 * pad;
    out->scalebar.h = pad + text_h + pad / 2 + bar_h + pad;
    out->scalebar.x = margin;
    out->scalebar.y = h - margin - out->scalebar.h;

    /* the attribution at the right, or a row higher, left of the button column (cut there when still too long) */
    if (in->attribution_w > 0) {
        const int ah = text_h + 2 * pad, full = in->attribution_w + 2 * pad;
        const int beside = w - margin - (out->scalebar.x + out->scalebar.w) - margin;
        if (full <= beside) {
            out->attribution = (cp0_map_rect_t){w - margin - full, h - margin - ah, full, ah};
            out->attribution_text_w = in->attribution_w;
        } else {
            const int right = w - margin - plate - margin; /* the button column starts here */
            int aw = full;
            if (aw > right - margin) aw = right - margin;
            if (aw < 2 * pad + 8) aw = 2 * pad + 8;
            out->attribution = (cp0_map_rect_t){right - aw, out->scalebar.y - pad - ah, aw, ah};
            out->attribution_text_w = aw - 2 * pad;
        }
    }
    int bottom_top = out->scalebar.y;
    if (out->attribution.w && out->attribution.y < bottom_top) bottom_top = out->attribution.y;

    /* the top row: the message strip across, the badge at the left */
    out->strip = (cp0_map_rect_t){margin, margin, w - 2 * margin, text_h + 2 * pad};
    int bw = (in->badge_w > 0 ? in->badge_w : 8 * text_h / 2) + 2 * pad;
    if (bw > w - 2 * margin) bw = w - 2 * margin;
    out->badge = (cp0_map_rect_t){margin, margin, bw, text_h + 2 * pad};

    /* the buttons at the right edge, one TARGET apart: the zoom pair centred vertically, the locate button above it
     * with a quarter TARGET more; the plates must stay between the top row and the bottom row */
    const int n = in->locate ? 3 : 2;
    int px = w - margin - plate;
    const int zone_top = out->strip.y + out->strip.h + margin;
    int zone_bot = h - margin;
    if (rect_xrange_overlap(&out->attribution, px, px + plate) || rect_xrange_overlap(&out->scalebar, px, px + plate))
        zone_bot = bottom_top - margin;
    int pitch = target;
    int extra = in->locate ? pitch / 4 : 0;
    int span = (n - 1) * pitch + extra + plate;
    const int zone = zone_bot - zone_top;
    if (span > zone) { /* not enough height: closer together, never closer than the plates plus 4 px */
        pitch = (zone - plate) * 4 / (4 * (n - 1) + (in->locate ? 1 : 0));
        if (pitch < plate + 4) pitch = plate + 4;
        extra = in->locate ? (pitch / 4 < 2 ? 2 : pitch / 4) : 0;
        span = (n - 1) * pitch + extra + plate;
    }
    if (span > zone) { /* still too low a widget: smaller plates */
        plate = (zone - 4 * (n - 1) - (in->locate ? 2 : 0)) / n;
        if (plate < 8) plate = 8;
        pitch = plate + 4;
        extra = in->locate ? 2 : 0;
        span = (n - 1) * pitch + extra + plate;
    }
    px = w - margin - plate;
    int top = h / 2 - pitch / 2 - plate / 2 - (in->locate ? pitch + extra : 0);
    if (top + span > zone_bot) top = zone_bot - span;
    if (top < zone_top) top = zone_top + (zone - span > 0 ? (zone - span) / 2 : 0);
    int y[CP0_MAP_CTL__COUNT] = {0};
    int k = top;
    if (in->locate) {
        y[CP0_MAP_CTL_LOCATE] = k;
        k += pitch + extra;
    }
    y[CP0_MAP_CTL_ZOOM_IN] = k;
    y[CP0_MAP_CTL_ZOOM_OUT] = k + pitch;
    int touch = target < pitch ? target : pitch; /* neighbours' touch squares meet, never overlap */
    if (touch < plate) touch = plate;
    for (int c = 1; c < CP0_MAP_CTL__COUNT; c++) {
        if (c == CP0_MAP_CTL_LOCATE && !in->locate) continue;
        out->plates[c] = (cp0_map_rect_t){px, y[c], plate, plate};
        /* the touch square centred on the plate, kept inside the widget */
        int tx = px + plate / 2 - touch / 2, ty = y[c] + plate / 2 - touch / 2, tw = touch, th = touch;
        if (tx < 0) { tw += tx; tx = 0; }
        if (ty < 0) { th += ty; ty = 0; }
        if (tx + tw > w) tw = w - tx;
        if (ty + th > h) th = h - ty;
        out->ctl[c] = (cp0_map_rect_t){tx, ty, tw, th};
    }
    out->btn = touch;
    out->plate = plate;
}

cp0_map_ctl_t cp0_map_layout_hit(const cp0_map_layout_t *l, int x, int y)
{
    for (int c = 1; c < CP0_MAP_CTL__COUNT; c++) {
        const cp0_map_rect_t *r = &l->ctl[c];
        if (r->w && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) return (cp0_map_ctl_t)c;
    }
    return CP0_MAP_CTL_NONE;
}

/* ------------------------------------------------------------------------------------------- tracks, markers */

static double seg_dist2(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    const double ex = ax + t * dx - px, ey = ay + t * dy - py;
    return ex * ex + ey * ey;
}

int cp0_map_simplify(const double *x, const double *y, int n, double eps, int *keep, unsigned char *mark, int *stack)
{
    if (!x || !y || !keep || n <= 0) return 0;
    if (n <= 2) {
        for (int i = 0; i < n; i++) keep[i] = i;
        return n;
    }
    memset(mark, 0, (size_t)n);
    mark[0] = mark[n - 1] = 1;
    const double eps2 = eps * eps;
    int sp = 0;
    stack[sp++] = 0;
    stack[sp++] = n - 1;
    while (sp > 0) {
        const int last = stack[--sp], first = stack[--sp];
        double dmax = -1;
        int idx = -1;
        for (int i = first + 1; i < last; i++) {
            const double d = seg_dist2(x[i], y[i], x[first], y[first], x[last], y[last]);
            if (d > dmax) {
                dmax = d;
                idx = i;
            }
        }
        if (idx > 0 && dmax > eps2) {
            mark[idx] = 1;
            if (idx - first > 1) {
                stack[sp++] = first;
                stack[sp++] = idx;
            }
            if (last - idx > 1) {
                stack[sp++] = idx;
                stack[sp++] = last;
            }
        }
    }
    int k = 0;
    for (int i = 0; i < n; i++)
        if (mark[i]) keep[k++] = i;
    return k;
}

#define LABELS_MAX 128

int cp0_map_labels_place(const int *sx, const int *sy, const int *lw, const int *lh, int n, int r, int view_w,
                         int view_h, const cp0_map_rect_t *avoid, int navoid, int *lx, int *ly)
{
    cp0_map_rect_t placed[LABELS_MAX];
    int np = 0, placed_labels = 0;
    for (int k = 0; k < navoid && np < LABELS_MAX / 2; k++)
        if (avoid[k].w && avoid[k].h) placed[np++] = avoid[k];
    for (int i = 0; i < n; i++) {
        lx[i] = ly[i] = -1;
        if (np == LABELS_MAX || lw[i] <= 0 || lh[i] <= 0) continue;
        const int gap = 2;
        const int cand[4][2] = {{sx[i] + r + gap, sy[i] - lh[i] / 2},
                                {sx[i] - lw[i] / 2, sy[i] + r + gap},
                                {sx[i] - lw[i] / 2, sy[i] - r - gap - lh[i]},
                                {sx[i] - r - gap - lw[i], sy[i] - lh[i] / 2}};
        for (int c = 0; c < 4; c++) {
            const cp0_map_rect_t box = {cand[c][0], cand[c][1], lw[i], lh[i]};
            if (box.x < 0 || box.y < 0 || box.x + box.w > view_w || box.y + box.h > view_h) continue;
            int free_spot = 1;
            for (int k = 0; k < np && free_spot; k++)
                if (cp0_map_rect_overlap(&box, &placed[k])) free_spot = 0;
            if (!free_spot) continue;
            placed[np++] = box;
            placed_labels++;
            lx[i] = box.x;
            ly[i] = box.y;
            break;
        }
    }
    return placed_labels;
}

static void put_utf8(char *out, size_t size, size_t *o, unsigned cp)
{
    char t[4];
    int n;
    if (cp < 0x80) { t[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { t[0] = (char)(0xc0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
    else { t[0] = (char)(0xe0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); t[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
    if (*o + (size_t)n < size) {
        memcpy(out + *o, t, (size_t)n);
        *o += (size_t)n;
    }
}

void cp0_map_attribution_text(const char *in, char *out, size_t size)
{
    if (!out || !size) return;
    size_t o = 0;
    int space = 0;
    out[0] = '\0';
    if (!in) return;
    static const struct {
        const char *name;
        unsigned cp;
    } ents[] = {{"copy", 0xa9}, {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '}};
    for (const char *p = in; *p;) {
        unsigned cp = 0;
        if (*p == '<') {
            const char *e = strchr(p, '>');
            if (!e) break;
            p = e + 1;
            space = 1;
            continue;
        }
        if (*p == '&') {
            const char *e = strchr(p, ';');
            if (e && e - p <= 8) {
                const size_t len = (size_t)(e - p - 1);
                if (p[1] == '#') {
                    const int hex = p[2] == 'x' || p[2] == 'X';
                    cp = (unsigned)strtoul(p + (hex ? 3 : 2), NULL, hex ? 16 : 10);
                } else {
                    for (size_t k = 0; k < sizeof ents / sizeof ents[0]; k++)
                        if (strlen(ents[k].name) == len && !strncmp(p + 1, ents[k].name, len)) cp = ents[k].cp;
                }
                if (cp && cp < 0x10000) {
                    p = e + 1;
                    if (cp == ' ') {
                        space = 1;
                        continue;
                    }
                    if (space && o) put_utf8(out, size, &o, ' ');
                    space = 0;
                    put_utf8(out, size, &o, cp);
                    continue;
                }
            }
        }
        const unsigned char c = (unsigned char)*p++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = 1;
            continue;
        }
        if (c < 0x20) continue;
        if (space && o && o + 1 < size) out[o++] = ' ';
        space = 0;
        if (o + 1 < size) out[o++] = (char)c;
    }
    out[o] = '\0';
}

/* ----------------------------------------------------------------------------------------------------- fixes */

int64_t cp0_map_monotonic_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int cp0_map_fix_fresh(const cp0_map_fix_t *fix, int64_t now_ms)
{
    if (!fix || !isfinite(fix->lat) || !isfinite(fix->lon)) return 0;
    if (fix->mono_ms == 0) return 1;
    return now_ms - fix->mono_ms <= CP0_MAP_FIX_STALE_MS;
}

int cp0_map_view_fetch_tiles(const cp0_map_slot_t *slots, int n, int (*need)(void *user, const cp0_map_slot_t *slot),
                             void *user, cp0_map_tile_id_t *out, int max)
{
    int k = 0;
    for (int i = 0; slots && out && i < n && k < max; i++) {
        const cp0_map_slot_t *s = &slots[i];
        if (!s->visible) continue; /* the ring is read from the store ahead, never fetched */
        if (need && !need(user, s)) continue;
        cp0_map_tile_id_t t;
        t.z = s->over ? s->z - 1 : s->z;
        t.x = s->over ? s->x >> 1 : s->x;
        t.y = s->over ? s->y >> 1 : s->y;
        int dup = 0;
        for (int j = 0; j < k && !dup; j++) dup = out[j].z == t.z && out[j].x == t.x && out[j].y == t.y;
        if (!dup) out[k++] = t;
    }
    return k;
}
