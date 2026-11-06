/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: images, PNG / JPEG decode and the 1x / 1.5x / 2x RGB565 scalers (see cp0_map_tile.h).
 */
#include "cp0_map_image_priv.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------------ images */

cp0_map_image_t *cp0_map_image_new(int w, int h, cp0_map_pixfmt_t fmt)
{
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || (fmt != CP0_MAP_PIX_RGB565 && fmt != CP0_MAP_PIX_ARGB8888))
        return NULL;
    const size_t bpp = fmt == CP0_MAP_PIX_RGB565 ? 2 : 4;
    const size_t head = (sizeof(cp0_map_image_block_t) + 15) & ~(size_t)15;
    cp0_map_image_block_t *b = (cp0_map_image_block_t *)malloc(head + (size_t)w * (size_t)h * bpp);
    if (!b) return NULL;
    b->magic = CP0_MAP_IMAGE_MAGIC;
    b->owner = NULL;
    b->img.w = w;
    b->img.h = h;
    b->img.stride = (int)((size_t)w * bpp);
    b->img.fmt = fmt;
    b->img.px = (uint8_t *)b + head;
    return &b->img;
}

void cp0_map_image_free(cp0_map_image_t *img)
{
    cp0_map_image_block_t *b = cp0_map__image_block(img);
    if (!b) return;
    b->magic = 0;
    free(b);
}

size_t cp0_map_image_bytes(const cp0_map_image_t *img)
{
    if (!img) return 0;
    return (size_t)img->w * (size_t)img->h * (img->fmt == CP0_MAP_PIX_RGB565 ? 2u : 4u);
}

cp0_map_image_block_t *cp0_map__image_block(const cp0_map_image_t *img)
{
    if (!img) return NULL;
    cp0_map_image_block_t *b =
        (cp0_map_image_block_t *)((uint8_t *)(uintptr_t)img - offsetof(cp0_map_image_block_t, img));
    return b->magic == CP0_MAP_IMAGE_MAGIC ? b : NULL;
}

/* ------------------------------------------------------------------------------------------------------ decode */

int cp0_map_tile_probe(const uint8_t *data, size_t len, int *w, int *h, cp0_map_format_t *fmt)
{
    if (!data || !len) return CP0_MAP_E_INVALID;
    const cp0_map_format_t f = cp0_map_format_sniff(data, len);
    if (fmt) *fmt = f;
    if (f == CP0_MAP_FMT_WEBP || f == CP0_MAP_FMT_MVT) return CP0_MAP_E_UNSUPPORTED;
    if (f != CP0_MAP_FMT_PNG && f != CP0_MAP_FMT_JPEG) return CP0_MAP_E_FORMAT;
    if (len > CP0_MAP_TILE_BYTES_MAX) return CP0_MAP_E_TOO_LARGE;
    int iw = 0, ih = 0, comp = 0;
    if (!cp0_map__stbi_info(data, (int)len, &iw, &ih, &comp) || iw <= 0 || ih <= 0) return CP0_MAP_E_DECODE;
    if (w) *w = iw;
    if (h) *h = ih;
    return CP0_MAP_OK;
}

static uint16_t rgb_565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3));
}

/* straight alpha over white, rounded */
static unsigned over_white(unsigned c, unsigned a)
{
    return (c * a + 255u * (255u - a) + 127u) / 255u;
}

int cp0_map_decode_tile(const uint8_t *data, size_t len, cp0_map_pixfmt_t pixfmt, int scale_x2, cp0_map_image_t **out)
{
    if (!out) return CP0_MAP_E_INVALID;
    *out = NULL;
    if (scale_x2 == 0) scale_x2 = 2;
    if (!data || !len || (pixfmt != CP0_MAP_PIX_RGB565 && pixfmt != CP0_MAP_PIX_ARGB8888) || scale_x2 < 2 ||
        scale_x2 > 4)
        return CP0_MAP_E_INVALID;
    if (pixfmt == CP0_MAP_PIX_ARGB8888 && scale_x2 != 2) return CP0_MAP_E_UNSUPPORTED;
    int w, h;
    int rc = cp0_map_tile_probe(data, len, &w, &h, NULL);
    if (rc) return rc;
    if (w > CP0_MAP_DECODE_MAX_PX || h > CP0_MAP_DECODE_MAX_PX) return CP0_MAP_E_TOO_LARGE;
    if (scale_x2 == 3 && ((w & 1) || (h & 1))) return CP0_MAP_E_UNSUPPORTED;
    int comp = 0;
    int iw2 = 0, ih2 = 0, comp_info = 0;
    cp0_map__stbi_info(data, (int)len, &iw2, &ih2, &comp_info);
    const int alpha = comp_info == 2 || comp_info == 4;
    const int req = (alpha || pixfmt == CP0_MAP_PIX_ARGB8888) ? 4 : 3;
    int dw = 0, dh = 0;
    uint8_t *px = cp0_map__stbi_load(data, (int)len, &dw, &dh, &comp, req);
    if (!px) return CP0_MAP_E_DECODE;
    if (dw != w || dh != h) {
        cp0_map__stbi_free(px);
        return CP0_MAP_E_DECODE;
    }
    cp0_map_image_t *base = cp0_map_image_new(w, h, pixfmt);
    if (!base) {
        cp0_map__stbi_free(px);
        return CP0_MAP_E_NOMEM;
    }
    const size_t n = (size_t)w * (size_t)h;
    if (pixfmt == CP0_MAP_PIX_ARGB8888) {
        uint32_t *d = (uint32_t *)base->px;
        for (size_t i = 0; i < n; i++) {
            const uint8_t *s = px + 4 * i;
            d[i] = ((uint32_t)s[3] << 24) | ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
        }
    } else {
        uint16_t *d = (uint16_t *)base->px;
        if (req == 3) {
            for (size_t i = 0; i < n; i++) {
                const uint8_t *s = px + 3 * i;
                d[i] = rgb_565(s[0], s[1], s[2]);
            }
        } else {
            for (size_t i = 0; i < n; i++) {
                const uint8_t *s = px + 4 * i;
                const unsigned a = s[3];
                d[i] = a == 255 ? rgb_565(s[0], s[1], s[2])
                                : rgb_565(over_white(s[0], a), over_white(s[1], a), over_white(s[2], a));
            }
        }
    }
    cp0_map__stbi_free(px);
    if (scale_x2 == 2) {
        *out = base;
        return CP0_MAP_OK;
    }
    const int mw = w * scale_x2 / 2, mh = h * scale_x2 / 2;
    cp0_map_image_t *big = cp0_map_image_new(mw, mh, CP0_MAP_PIX_RGB565);
    if (!big) {
        cp0_map_image_free(base);
        return CP0_MAP_E_NOMEM;
    }
    rc = cp0_map_scale_rgb565((const uint16_t *)base->px, w, h, base->stride, scale_x2, 0, 0, mw, mh,
                              (uint16_t *)big->px, big->stride);
    cp0_map_image_free(base);
    if (rc) {
        cp0_map_image_free(big);
        return rc;
    }
    *out = big;
    return CP0_MAP_OK;
}

/* ----------------------------------------------------------------------------------------------------- scalers */

/* mix(a, b, w) per 5/6/5 channel: (a * (256 - w) + b * w + 128) >> 8 (cp0_map_tile.h) */
static inline uint16_t mix565(uint16_t a, uint16_t b, unsigned w)
{
    const unsigned iw = 256u - w;
    const unsigned r = (((unsigned)a >> 11) * iw + ((unsigned)b >> 11) * w + 128u) >> 8;
    const unsigned g = ((((unsigned)a >> 5) & 63u) * iw + (((unsigned)b >> 5) & 63u) * w + 128u) >> 8;
    const unsigned bl = (((unsigned)a & 31u) * iw + ((unsigned)b & 31u) * w + 128u) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

#define W13 85u  /* 1/3 of 256 */
#define W23 171u /* 2/3 of 256 */

/* one source row interpolated horizontally for magnified columns x0 .. x0+n-1 (1.5x) */
static void hrow15(const uint16_t *p, int sw, int x0, int n, uint16_t *o)
{
    int j = x0 / 3, r = x0 % 3;
    for (int k = 0; k < n; k++) {
        const int c = 2 * j;
        if (r == 0)
            o[k] = p[c];
        else if (r == 1)
            o[k] = mix565(p[c], p[c + 1], W23);
        else
            o[k] = mix565(p[c + 1], p[c + 2 < sw ? c + 2 : sw - 1], W13);
        if (++r == 3) {
            r = 0;
            j++;
        }
    }
}

#define CHUNK 512 /* magnified columns per pass of the 1.5x path (stack rows of CHUNK pixels) */

static void scale15(const uint16_t *src, int sw, int sh, int src_stride, int dx, int dy, int dw, int dh, uint16_t *dst,
                    int dst_stride)
{
    uint16_t rows[3][CHUNK];
    for (int cx = 0; cx < dw; cx += CHUNK) {
        const int n = dw - cx < CHUNK ? dw - cx : CHUNK;
        int tag[3] = {-1, -1, -1};
        for (int yy = 0; yy < dh; yy++) {
            const int Y = dy + yy, k = Y / 3, r = Y % 3;
            int need[2], nneed;
            if (r == 0) {
                need[0] = 2 * k;
                nneed = 1;
            } else if (r == 1) {
                need[0] = 2 * k;
                need[1] = 2 * k + 1;
                nneed = 2;
            } else {
                need[0] = 2 * k + 1;
                need[1] = 2 * k + 2 < sh ? 2 * k + 2 : sh - 1;
                nneed = 2;
            }
            const uint16_t *h[2] = {NULL, NULL};
            for (int q = 0; q < nneed; q++) {
                int slot = -1;
                for (int s = 0; s < 3; s++)
                    if (tag[s] == need[q]) slot = s;
                if (slot < 0) {
                    /* replace the slot holding the lowest row that is not needed now (rows only go down) */
                    for (int s = 0; s < 3; s++) {
                        if (q == 1 && tag[s] == need[0]) continue;
                        if (slot < 0 || tag[s] < tag[slot]) slot = s;
                    }
                    hrow15((const uint16_t *)((const uint8_t *)src + (size_t)need[q] * (size_t)src_stride), sw,
                           dx + cx, n, rows[slot]);
                    tag[slot] = need[q];
                }
                h[q] = rows[slot];
            }
            uint16_t *o = (uint16_t *)((uint8_t *)dst + (size_t)yy * (size_t)dst_stride) + cx;
            if (r == 0)
                memcpy(o, h[0], (size_t)n * 2);
            else {
                const unsigned w = r == 1 ? W23 : W13;
                for (int x = 0; x < n; x++) o[x] = mix565(h[0][x], h[1][x], w);
            }
        }
    }
}

/* the two taps of a downscaled column / row: 0.5x X -> (2X, 2X+1, 1/2); 0.75x X = 3j+r -> (4j+r, 4j+r+1, w[r]) with the
 * weight of the second tap w = 1/4, 1/2, 3/4 (64, 128, 192 of 256): the area each output pixel covers (cp0_map_tile.h) */
static inline void down_taps(int x4, int X, int *a, unsigned *w)
{
    if (x4 == 2) {
        *a = 2 * X;
        *w = 128u;
    } else {
        static const unsigned wt[3] = {64u, 128u, 192u};
        const int j = X / 3, r = X % 3;
        *a = 4 * j + r;
        *w = wt[r];
    }
}

static void hrow_down(const uint16_t *p, int x4, int x0, int n, uint16_t *o)
{
    for (int k = 0; k < n; k++) {
        int a;
        unsigned w;
        down_taps(x4, x0 + k, &a, &w);
        o[k] = mix565(p[a], p[a + 1], w);
    }
}

static void scale_down(const uint16_t *src, int src_stride, int x4, int dx, int dy, int dw, int dh, uint16_t *dst,
                       int dst_stride)
{
    uint16_t rows[3][CHUNK];
    for (int cx = 0; cx < dw; cx += CHUNK) {
        const int n = dw - cx < CHUNK ? dw - cx : CHUNK;
        int tag[3] = {-1, -1, -1};
        for (int yy = 0; yy < dh; yy++) {
            int a;
            unsigned wv;
            down_taps(x4, dy + yy, &a, &wv);
            const int need[2] = {a, a + 1};
            const uint16_t *h[2] = {NULL, NULL};
            for (int q = 0; q < 2; q++) {
                int slot = -1;
                for (int s = 0; s < 3; s++)
                    if (tag[s] == need[q]) slot = s;
                if (slot < 0) {
                    for (int s = 0; s < 3; s++) {
                        if (q == 1 && tag[s] == need[0]) continue;
                        if (slot < 0 || tag[s] < tag[slot]) slot = s;
                    }
                    hrow_down((const uint16_t *)((const uint8_t *)src + (size_t)need[q] * (size_t)src_stride), x4,
                              dx + cx, n, rows[slot]);
                    tag[slot] = need[q];
                }
                h[q] = rows[slot];
            }
            uint16_t *o = (uint16_t *)((uint8_t *)dst + (size_t)yy * (size_t)dst_stride) + cx;
            for (int x = 0; x < n; x++) o[x] = mix565(h[0][x], h[1][x], wv);
        }
    }
}

int cp0_map_scale_rgb565(const uint16_t *src, int sw, int sh, int src_stride, int scale_x2, int dx, int dy, int dw,
                         int dh, uint16_t *dst, int dst_stride)
{
    if (scale_x2 < 2 || scale_x2 > 4) return CP0_MAP_E_INVALID;
    return cp0_map_resample_rgb565(src, sw, sh, src_stride, 2 * scale_x2, dx, dy, dw, dh, dst, dst_stride);
}

int cp0_map_resample_rgb565(const uint16_t *src, int sw, int sh, int src_stride, int scale_x4, int dx, int dy, int dw,
                            int dh, uint16_t *dst, int dst_stride)
{
    if (!src || !dst || sw <= 0 || sh <= 0 || sw > CP0_MAP_DECODE_MAX_PX || sh > CP0_MAP_DECODE_MAX_PX ||
        src_stride < sw * 2 || dw <= 0 || dh <= 0 || dst_stride < dw * 2 || dx < 0 || dy < 0)
        return CP0_MAP_E_INVALID;
    if (scale_x4 != 2 && scale_x4 != 3 && scale_x4 != 4 && scale_x4 != 6 && scale_x4 != 8) return CP0_MAP_E_INVALID;
    if ((scale_x4 == 2 || scale_x4 == 6) && ((sw & 1) || (sh & 1))) return CP0_MAP_E_INVALID;
    if (scale_x4 == 3 && ((sw & 3) || (sh & 3))) return CP0_MAP_E_INVALID;
    const int mw = sw * scale_x4 / 4, mh = sh * scale_x4 / 4;
    if (dx > mw - dw || dy > mh - dh) return CP0_MAP_E_INVALID;
    if (scale_x4 < 4) {
        scale_down(src, src_stride, scale_x4, dx, dy, dw, dh, dst, dst_stride);
        return CP0_MAP_OK;
    }
    const int scale_x2 = scale_x4 / 2;
    if (scale_x2 == 2) {
        for (int yy = 0; yy < dh; yy++)
            memcpy((uint8_t *)dst + (size_t)yy * (size_t)dst_stride,
                   (const uint8_t *)src + (size_t)(dy + yy) * (size_t)src_stride + (size_t)dx * 2, (size_t)dw * 2);
        return CP0_MAP_OK;
    }
    if (scale_x2 == 4) {
        for (int yy = 0; yy < dh; yy++) {
            const uint16_t *s = (const uint16_t *)((const uint8_t *)src + (size_t)((dy + yy) >> 1) * (size_t)src_stride);
            uint16_t *o = (uint16_t *)((uint8_t *)dst + (size_t)yy * (size_t)dst_stride);
            /* a row that repeats the one just written: copy it */
            if (yy > 0 && ((dy + yy) & 1)) {
                memcpy(o, (const uint8_t *)o - dst_stride, (size_t)dw * 2);
                continue;
            }
            int x = 0, X = dx;
            if (X & 1) {
                o[x++] = s[X >> 1];
                X++;
            }
            for (; x + 1 < dw; x += 2, X += 2) o[x] = o[x + 1] = s[X >> 1];
            if (x < dw) o[x] = s[X >> 1];
        }
        return CP0_MAP_OK;
    }
    scale15(src, sw, sh, src_stride, dx, dy, dw, dh, dst, dst_stride);
    return CP0_MAP_OK;
}
