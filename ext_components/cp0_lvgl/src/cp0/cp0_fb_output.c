/*
 * Output stage of the dpi-scaled display manager (see cp0_fb_output.h).
 *
 * The rotated copies run over 32x32 tiles: inside a tile the source is read down a column while
 * the destination is written along a buffer row, so both stay in cache. No function is called per
 * pixel.
 */

#include "cp0_fb_output.h"

#include <stdlib.h>
#include <string.h>

#define CP0_FBO_TILE 32

static const cp0_fb_layout_t k_rgb565 = {{11, 5}, {5, 6}, {0, 5}};
static const cp0_fb_layout_t k_xrgb8888 = {{16, 8}, {8, 8}, {0, 8}};

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int layout_valid16(const cp0_fb_layout_t *l)
{
    const cp0_fb_channel_t *c[3] = {&l->r, &l->g, &l->b};
    uint32_t used = 0;
    for (int i = 0; i < 3; i++) {
        if (c[i]->len < 1 || c[i]->len > 8 || c[i]->off + c[i]->len > 16) return 0;
        const uint32_t mask = ((1u << c[i]->len) - 1u) << c[i]->off;
        if (used & mask) return 0;
        used |= mask;
    }
    return 1;
}

static int layout_equal(const cp0_fb_layout_t *a, const cp0_fb_layout_t *b)
{
    return a->r.off == b->r.off && a->r.len == b->r.len && a->g.off == b->g.off && a->g.len == b->g.len &&
           a->b.off == b->b.off && a->b.len == b->b.len;
}

int cp0_fbo_setup(cp0_fb_out_t *o, uint8_t *base, size_t size, int stride, int bpp, int pw, int ph,
                  int rot, int lw, int lh, const cp0_fb_layout_t *layout)
{
    if (!o) return -1;
    memset(o, 0, sizeof(*o));
    if (!base || (bpp != 2 && bpp != 4) || stride <= 0 || pw <= 0 || ph <= 0) return -1;

    int fix = 0;
    if (rot != 0 && rot != 90 && rot != 180 && rot != 270) {
        rot = 0;
        fix |= CP0_FBO_FIX_ROTATE;
    }
    if ((size_t)pw * (size_t)bpp > (size_t)stride) {
        pw = stride / bpp;
        fix |= CP0_FBO_FIX_WIDTH;
    }
    if ((size_t)ph * (size_t)stride > size) {
        ph = (int)(size / (size_t)stride);
        fix |= CP0_FBO_FIX_HEIGHT;
    }
    if (pw <= 0 || ph <= 0) return -1;

    const int swap = rot == 90 || rot == 270;
    const int want_w = swap ? ph : pw;
    const int want_h = swap ? pw : ph;
    if ((lw > 0 || lh > 0) && (lw != want_w || lh != want_h)) fix |= CP0_FBO_FIX_LOGICAL;

    o->base = base;
    o->size = size;
    o->stride = stride;
    o->bpp = bpp;
    o->pw = pw;
    o->ph = ph;
    o->rot = rot;
    o->lw = want_w;
    o->lh = want_h;
    if (bpp == 4) {
        o->layout = k_xrgb8888;
    } else if (layout && layout_valid16(layout)) {
        o->layout = *layout;
    } else {
        o->layout = k_rgb565;
        if (layout) fix |= CP0_FBO_FIX_LAYOUT;
    }
    o->rgb565_plain = bpp == 2 && layout_equal(&o->layout, &k_rgb565);
    return fix;
}

int cp0_fbo_parse_size(const char *s, int *w, int *h)
{
    if (!s || !w || !h) return -1;
    char *end = NULL;
    const long a = strtol(s, &end, 10);
    if (end == s || (*end != 'x' && *end != 'X')) return -1;
    const char *p = end + 1;
    const long b = strtol(p, &end, 10);
    if (end == p || *end != '\0' || a <= 0 || b <= 0 || a > 16384 || b > 16384) return -1;
    *w = (int)a;
    *h = (int)b;
    return 0;
}

int cp0_fbo_parse_rotation(const char *s, int *rot)
{
    if (!s || !rot || !s[0]) return -1;
    char *end = NULL;
    const long v = strtol(s, &end, 10);
    if (*end != '\0' || (v != 0 && v != 90 && v != 180 && v != 270)) return -1;
    *rot = (int)v;
    return 0;
}

void cp0_fbo_compat_origin(int lw, int lh, int ww, int wh, int *x, int *y)
{
    int ox = (lw - ww) / 2;
    int oy = lh >= CP0_FBO_SMALL_PANEL_H ? 0 : (lh - 100 - wh) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (x) *x = ox;
    if (y) *y = oy;
}

void cp0_fbo_logical_to_buffer(const cp0_fb_out_t *o, int X, int Y, int *px, int *py)
{
    switch (o->rot) {
    case 90:  *px = o->pw - 1 - Y; *py = X; break;
    case 180: *px = o->pw - 1 - X; *py = o->ph - 1 - Y; break;
    case 270: *px = Y; *py = o->ph - 1 - X; break;
    default:  *px = X; *py = Y; break;
    }
}

void cp0_fbo_buffer_to_logical(const cp0_fb_out_t *o, int px, int py, int *X, int *Y)
{
    switch (o->rot) {
    case 90:  *X = py; *Y = o->pw - 1 - px; break;
    case 180: *X = o->pw - 1 - px; *Y = o->ph - 1 - py; break;
    case 270: *X = o->ph - 1 - py; *Y = px; break;
    default:  *X = px; *Y = py; break;
    }
}

/* raw in [mn..mx] -> pixel in [0..n): the centre of each device step, clamped. */
static int spread(int raw, int mn, int mx, int n)
{
    const long long span = (long long)mx - mn + 1;
    if (span <= 0 || n <= 0) return 0;
    const long long v = ((2LL * ((long long)raw - mn) + 1) * n) / (2 * span);
    return v < 0 ? 0 : (v >= n ? n - 1 : (int)v);
}

void cp0_fbo_touch_to_logical(const cp0_fb_out_t *o, int raw_x, int raw_y, int min_x, int max_x,
                              int min_y, int max_y, int *X, int *Y)
{
    const int px = spread(raw_x, min_x, max_x, o->pw);
    const int py = spread(raw_y, min_y, max_y, o->ph);
    cp0_fbo_buffer_to_logical(o, px, py, X, Y);
}

/* ------------------------------------------------------------------ rotated copies */

/* (X, Y, w, h) is already clipped to the logical canvas; src points at its top-left. */
#define CP0_FBO_DEFINE_ROTATORS(SUF, T)                                                             \
    static void rot90_##SUF(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *src,  \
                            int ss)                                                                 \
    {                                                                                               \
        const int left = o->pw - Y - h; /* leftmost buffer column of the block */                  \
        for (int ty = 0; ty < h; ty += CP0_FBO_TILE) {                                              \
            const int th = h - ty < CP0_FBO_TILE ? h - ty : CP0_FBO_TILE;                           \
            for (int tx = 0; tx < w; tx += CP0_FBO_TILE) {                                          \
                const int tw = w - tx < CP0_FBO_TILE ? w - tx : CP0_FBO_TILE;                       \
                for (int x = tx; x < tx + tw; x++) {                                                \
                    /* source column x -> buffer row X + x, written right to left */               \
                    T *d = (T *)(o->base + (size_t)(X + x) * o->stride) + left + (h - 1 - ty);      \
                    const uint8_t *s = src + (size_t)ty * ss + (size_t)x * sizeof(T);               \
                    for (int y = 0; y < th; y++, s += ss) d[-y] = *(const T *)s;                    \
                }                                                                                   \
            }                                                                                       \
        }                                                                                           \
    }                                                                                               \
    static void rot270_##SUF(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *src, \
                             int ss)                                                                \
    {                                                                                               \
        for (int ty = 0; ty < h; ty += CP0_FBO_TILE) {                                              \
            const int th = h - ty < CP0_FBO_TILE ? h - ty : CP0_FBO_TILE;                           \
            for (int tx = 0; tx < w; tx += CP0_FBO_TILE) {                                          \
                const int tw = w - tx < CP0_FBO_TILE ? w - tx : CP0_FBO_TILE;                       \
                for (int x = tx; x < tx + tw; x++) {                                                \
                    /* source column x -> buffer row ph-1-X-x, written left to right */            \
                    T *d = (T *)(o->base + (size_t)(o->ph - 1 - X - x) * o->stride) + Y + ty;       \
                    const uint8_t *s = src + (size_t)ty * ss + (size_t)x * sizeof(T);               \
                    for (int y = 0; y < th; y++, s += ss) d[y] = *(const T *)s;                     \
                }                                                                                   \
            }                                                                                       \
        }                                                                                           \
    }                                                                                               \
    static void rot180_##SUF(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *src, \
                             int ss)                                                                \
    {                                                                                               \
        for (int y = 0; y < h; y++) {                                                               \
            T *d = (T *)(o->base + (size_t)(o->ph - 1 - Y - y) * o->stride) + (o->pw - 1 - X);      \
            const T *s = (const T *)(src + (size_t)y * ss);                                         \
            for (int x = 0; x < w; x++) d[-x] = s[x];                                               \
        }                                                                                           \
    }

CP0_FBO_DEFINE_ROTATORS(16, uint16_t)
CP0_FBO_DEFINE_ROTATORS(32, uint32_t)

void cp0_fbo_blit(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *src, int src_stride)
{
    if (!o || !o->base || !src || w <= 0 || h <= 0) return;
    if (X < 0) {
        src += (size_t)(-X) * (size_t)o->bpp;
        w += X;
        X = 0;
    }
    if (Y < 0) {
        src += (ptrdiff_t)(-Y) * src_stride;
        h += Y;
        Y = 0;
    }
    if (X + w > o->lw) w = o->lw - X;
    if (Y + h > o->lh) h = o->lh - Y;
    if (w <= 0 || h <= 0) return;

    switch (o->rot) {
    case 90:
        if (o->bpp == 4) rot90_32(o, X, Y, w, h, src, src_stride);
        else rot90_16(o, X, Y, w, h, src, src_stride);
        break;
    case 180:
        if (o->bpp == 4) rot180_32(o, X, Y, w, h, src, src_stride);
        else rot180_16(o, X, Y, w, h, src, src_stride);
        break;
    case 270:
        if (o->bpp == 4) rot270_32(o, X, Y, w, h, src, src_stride);
        else rot270_16(o, X, Y, w, h, src, src_stride);
        break;
    default:
        for (int y = 0; y < h; y++)
            memcpy(o->base + (size_t)(Y + y) * o->stride + (size_t)X * o->bpp,
                   src + (ptrdiff_t)y * src_stride, (size_t)w * o->bpp);
        break;
    }
}

void cp0_fbo_blit_except(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *src,
                         int src_stride, int wx, int wy, int ww, int wh)
{
    if (!o || !src || w <= 0 || h <= 0) return;
    if (ww <= 0 || wh <= 0) {
        cp0_fbo_blit(o, X, Y, w, h, src, src_stride);
        return;
    }
    const int y_top = clampi(wy - Y, 0, h);      /* rows [0, y_top) lie above the hole */
    const int y_bot = clampi(wy + wh - Y, 0, h); /* rows [y_bot, h) lie below it */
    if (y_top > 0) cp0_fbo_blit(o, X, Y, w, y_top, src, src_stride);
    if (y_bot > y_top) {
        const int x_l = clampi(wx - X, 0, w);      /* columns [0, x_l) left of the hole */
        const int x_r = clampi(wx + ww - X, 0, w); /* columns [x_r, w) right of it */
        const uint8_t *rows = src + (ptrdiff_t)y_top * src_stride;
        if (x_l > 0) cp0_fbo_blit(o, X, Y + y_top, x_l, y_bot - y_top, rows, src_stride);
        if (x_r < w)
            cp0_fbo_blit(o, X + x_r, Y + y_top, w - x_r, y_bot - y_top, rows + (size_t)x_r * o->bpp,
                         src_stride);
    }
    if (y_bot < h)
        cp0_fbo_blit(o, X, Y + y_bot, w, h - y_bot, src + (ptrdiff_t)y_bot * src_stride, src_stride);
}

/* ------------------------------------------------------------------ scaled copies */

/* Upscale (and, for xrgb != 0, convert from XRGB8888) the source block [sx, sx+sw) x [sy, sy+sh)
 * through scratch, a band at a time, then hand each band to cp0_fbo_blit. (X, Y) is the logical
 * position of source pixel (0, 0). */
static void scaled_part(const cp0_fb_out_t *o, int X, int Y, const uint8_t *src, int ss, int sx, int sy,
                        int sw, int sh, int scale, int xrgb, uint8_t *scratch, size_t scratch_size)
{
    if (sw <= 0 || sh <= 0) return;
    const int bpp = o->bpp;
    const size_t per_px = (size_t)scale * scale * bpp;
    if (!scratch || scratch_size < per_px) return;
    int cw = (int)(scratch_size / per_px);
    if (cw > sw) cw = sw;

    const cp0_fb_layout_t *l = &o->layout;
    const int rs = 8 - l->r.len, gs = 8 - l->g.len, bs = 8 - l->b.len;
    const int ro = l->r.off, go = l->g.off, bo = l->b.off;
    const size_t src_px = xrgb ? 4 : (size_t)bpp;

    for (int x0 = 0; x0 < sw; x0 += cw) {
        const int nw = sw - x0 < cw ? sw - x0 : cw;
        const size_t drow = (size_t)nw * scale * bpp; /* bytes of one upscaled row */
        int band = (int)(scratch_size / (drow * scale));
        if (band < 1) band = 1;
        for (int y0 = 0; y0 < sh; y0 += band) {
            const int nb = sh - y0 < band ? sh - y0 : band;
            for (int r = 0; r < nb; r++) {
                uint8_t *d = scratch + (size_t)r * scale * drow;
                const uint8_t *s = src + (size_t)(sy + y0 + r) * ss + (size_t)(sx + x0) * src_px;
                if (bpp == 4) {
                    const uint32_t *s4 = (const uint32_t *)s; /* XRGB8888 either way */
                    uint32_t *d4 = (uint32_t *)d;
                    for (int x = 0; x < nw; x++) {
                        const uint32_t v = s4[x];
                        for (int k = 0; k < scale; k++) *d4++ = v;
                    }
                } else if (xrgb) {
                    const uint32_t *s4 = (const uint32_t *)s;
                    uint16_t *d2 = (uint16_t *)d;
                    for (int x = 0; x < nw; x++) {
                        const uint32_t p = s4[x];
                        const uint16_t v = (uint16_t)(((((p >> 16) & 0xFFu) >> rs) << ro) |
                                                      ((((p >> 8) & 0xFFu) >> gs) << go) |
                                                      (((p & 0xFFu) >> bs) << bo));
                        for (int k = 0; k < scale; k++) *d2++ = v;
                    }
                } else {
                    const uint16_t *s2 = (const uint16_t *)s;
                    uint16_t *d2 = (uint16_t *)d;
                    for (int x = 0; x < nw; x++) {
                        const uint16_t v = s2[x];
                        for (int k = 0; k < scale; k++) *d2++ = v;
                    }
                }
                for (int k = 1; k < scale; k++) memcpy(d + (size_t)k * drow, d, drow);
            }
            cp0_fbo_blit(o, X + (sx + x0) * scale, Y + (sy + y0) * scale, nw * scale, nb * scale, scratch,
                         (int)drow);
        }
    }
}

void cp0_fbo_blit_scaled(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *src,
                         int src_stride, int scale, uint8_t *scratch, size_t scratch_size)
{
    if (!o || !o->base || !src || w <= 0 || h <= 0) return;
    if (scale <= 1) {
        cp0_fbo_blit(o, X, Y, w, h, src, src_stride);
        return;
    }
    scaled_part(o, X, Y, src, src_stride, 0, 0, w, h, scale, 0, scratch, scratch_size);
}

void cp0_fbo_blit_xrgb_scaled(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *xrgb,
                              int src_stride, int scale, int ex, int ey, int ew, int eh,
                              uint8_t *scratch, size_t scratch_size)
{
    if (!o || !o->base || !xrgb || w <= 0 || h <= 0) return;
    if (scale < 1) scale = 1;
    if (ew <= 0 || eh <= 0) {
        scaled_part(o, X, Y, xrgb, src_stride, 0, 0, w, h, scale, 1, scratch, scratch_size);
        return;
    }
    const int y_top = clampi(ey, 0, h), y_bot = clampi(ey + eh, 0, h);
    const int x_l = clampi(ex, 0, w), x_r = clampi(ex + ew, 0, w);
    scaled_part(o, X, Y, xrgb, src_stride, 0, 0, w, y_top, scale, 1, scratch, scratch_size);
    scaled_part(o, X, Y, xrgb, src_stride, 0, y_top, x_l, y_bot - y_top, scale, 1, scratch, scratch_size);
    scaled_part(o, X, Y, xrgb, src_stride, x_r, y_top, w - x_r, y_bot - y_top, scale, 1, scratch,
                scratch_size);
    scaled_part(o, X, Y, xrgb, src_stride, 0, y_bot, w, h - y_bot, scale, 1, scratch, scratch_size);
}

/* ------------------------------------------------------------------ fills and formats */

void cp0_fbo_fill_black(const cp0_fb_out_t *o, int X, int Y, int w, int h)
{
    if (!o || !o->base) return;
    if (X < 0) w += X, X = 0;
    if (Y < 0) h += Y, Y = 0;
    if (X + w > o->lw) w = o->lw - X;
    if (Y + h > o->lh) h = o->lh - Y;
    if (w <= 0 || h <= 0) return;
    int bx, by, bw, bh; /* the same rectangle in buffer coordinates */
    switch (o->rot) {
    case 90:  bx = o->pw - Y - h; by = X; bw = h; bh = w; break;
    case 180: bx = o->pw - X - w; by = o->ph - Y - h; bw = w; bh = h; break;
    case 270: bx = Y; by = o->ph - X - w; bw = h; bh = w; break;
    default:  bx = X; by = Y; bw = w; bh = h; break;
    }
    for (int r = 0; r < bh; r++)
        memset(o->base + (size_t)(by + r) * o->stride + (size_t)bx * o->bpp, 0, (size_t)bw * o->bpp);
}

void cp0_fbo_to_buffer_format(const cp0_fb_out_t *o, uint8_t *px, int w, int h, int stride)
{
    if (!o || !px || o->bpp != 2 || o->rgb565_plain) return;
    const cp0_fb_layout_t *l = &o->layout;
    for (int y = 0; y < h; y++) {
        uint16_t *p = (uint16_t *)(px + (size_t)y * stride);
        for (int x = 0; x < w; x++) {
            const uint32_t v = p[x];
            const uint32_t r5 = v >> 11, g6 = (v >> 5) & 0x3Fu, b5 = v & 0x1Fu;
            const uint32_t r8 = (r5 << 3) | (r5 >> 2), g8 = (g6 << 2) | (g6 >> 4), b8 = (b5 << 3) | (b5 >> 2);
            p[x] = (uint16_t)(((r8 >> (8 - l->r.len)) << l->r.off) | ((g8 >> (8 - l->g.len)) << l->g.off) |
                              ((b8 >> (8 - l->b.len)) << l->b.off));
        }
    }
}
