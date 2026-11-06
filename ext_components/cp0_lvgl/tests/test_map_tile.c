/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine, decoded tiles (cp0_map_tile.h):
 *   - decode: the formula fixtures (RGB PNG, palette PNG at 256 and 512 px, grey + alpha over white, JPEG quadrants),
 *     generated PNGs pixel for pixel, RGB565 and ARGB8888, magnified decode, the refusals (WebP, garbage, truncated,
 *     oversized headers, odd sides at 1.5x), a deflate bomb stopped by the memory ceiling, 3000 mutated tiles;
 *   - scalers: 1x, 2x nearest and the fixed-phase 1.5x against a plain per-pixel reference of the definition in
 *     cp0_map_tile.h, on random pictures of several sizes and random destination rectangles (exact, bit for bit);
 *   - RAM cache: LRU order, pins (a pinned tile survives eviction and a replace; FULL when all is pinned), the budget
 *     (never above it, also with orphans), TOO_LARGE, drop, set_budget, statistics, the default budgets (table and
 *     board.state), four threads hammering one cache;
 *   - memory: with MAP_TILE_RSS (a build without sanitizers) the process's VmRSS after filling an 8 MiB cache with 400
 *     decoded 256 px tiles stays within the budget plus a small margin.
 */
#include "map_test_util.h"

#include <malloc.h>
#include <pthread.h>

/* ------------------------------------------------------------------------------------------------------ decode */

static uint16_t px565(const cp0_map_image_t *img, int x, int y)
{
    return ((const uint16_t *)(img->px + (size_t)y * (size_t)img->stride))[x];
}

static uint32_t px8888(const cp0_map_image_t *img, int x, int y)
{
    return ((const uint32_t *)(img->px + (size_t)y * (size_t)img->stride))[x];
}

static int close565(uint16_t c, unsigned r, unsigned g, unsigned b, int tol)
{
    const int cr = (c >> 11) << 3, cg = ((c >> 5) & 63) << 2, cb = (c & 31) << 3;
    return abs(cr - (int)r) <= tol && abs(cg - (int)g) <= tol && abs(cb - (int)b) <= tol;
}

static void test_decode_fixtures(void)
{
    size_t n;
    uint8_t *d = fixture("rgb256.png", &n);
    int w, h;
    cp0_map_format_t f;
    CHECK_RC(cp0_map_tile_probe(d, n, &w, &h, &f), CP0_MAP_OK);
    CHECK(w == 256 && h == 256 && f == CP0_MAP_FMT_PNG);
    cp0_map_image_t *img;
    CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_OK);
    CHECK(img->w == 256 && img->h == 256 && img->stride == 512 && img->fmt == CP0_MAP_PIX_RGB565);
    CHECK(cp0_map_image_bytes(img) == 256 * 256 * 2);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            if (px565(img, x, y) != rgb565_of((unsigned)x, (unsigned)y, (unsigned)(x + y) & 255)) CHECK(0);
    g_checks += 65536;
    cp0_map_image_free(img);
    CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_ARGB8888, 0, &img), CP0_MAP_OK);
    CHECK(img->stride == 1024 && px8888(img, 10, 20) == (0xff000000u | (10u << 16) | (20u << 8) | 30u));
    cp0_map_image_free(img);
    /* magnified decode: 2x is nearest; 1.5x is the scaler applied to the 1x decode */
    CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_RGB565, 4, &img), CP0_MAP_OK);
    CHECK(img->w == 512 && img->h == 512);
    CHECK(px565(img, 21, 40) == rgb565_of(10, 20, 30) && px565(img, 511, 511) == rgb565_of(255, 255, 254));
    cp0_map_image_free(img);
    cp0_map_image_t *one, *big;
    CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_RGB565, 2, &one), CP0_MAP_OK);
    CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_RGB565, 3, &big), CP0_MAP_OK);
    CHECK(big->w == 384 && big->h == 384);
    uint16_t *ref = (uint16_t *)malloc(384 * 384 * 2);
    CHECK_RC(cp0_map_scale_rgb565((const uint16_t *)one->px, 256, 256, one->stride, 3, 0, 0, 384, 384, ref, 768),
             CP0_MAP_OK);
    CHECK(!memcmp(ref, big->px, 384 * 384 * 2));
    free(ref);
    cp0_map_image_free(one);
    cp0_map_image_free(big);
    CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_ARGB8888, 4, &img), CP0_MAP_E_UNSUPPORTED);
    CHECK(img == NULL);
    free(d);

    static const uint8_t pal[16][3] = {{0, 0, 0},      {255, 255, 255}, {255, 0, 0},     {0, 255, 0},
                                       {0, 0, 255},    {255, 255, 0},   {0, 255, 255},   {255, 0, 255},
                                       {128, 128, 128}, {64, 32, 16},   {16, 64, 32},    {32, 16, 64},
                                       {200, 100, 50}, {50, 200, 100},  {100, 50, 200},  {240, 240, 230}};
    for (int px = 256; px <= 512; px *= 2) {
        char name[32];
        snprintf(name, sizeof name, "pal%d.png", px);
        d = fixture(name, &n);
        CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_OK);
        CHECK(img->w == px);
        const int b = px / 4;
        for (int y = 0; y < px; y += 7)
            for (int x = 0; x < px; x += 5) {
                const uint8_t *c = pal[(x / b) + 4 * (y / b)];
                if (px565(img, x, y) != rgb565_of(c[0], c[1], c[2])) CHECK(0);
                g_checks++;
            }
        cp0_map_image_free(img);
        free(d);
    }

    d = fixture("la256.png", &n);
    CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_OK);
    CHECK(px565(img, 5, 5) == 0xffff);                    /* alpha 0 over white */
    CHECK(px565(img, 200, 5) == rgb565_of(100, 100, 100)); /* opaque grey */
    cp0_map_image_free(img);
    CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_ARGB8888, 2, &img), CP0_MAP_OK);
    CHECK((px8888(img, 5, 5) >> 24) == 0 && px8888(img, 200, 5) == 0xff646464u);
    cp0_map_image_free(img);
    free(d);

    for (int px = 256; px <= 512; px *= 2) {
        char name[32];
        snprintf(name, sizeof name, "q75_%d.jpg", px);
        d = fixture(name, &n);
        CHECK_RC(cp0_map_tile_probe(d, n, &w, &h, &f), CP0_MAP_OK);
        CHECK(w == px && f == CP0_MAP_FMT_JPEG);
        CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_OK);
        const int q = px / 4;
        CHECK(close565(px565(img, q, q), 255, 0, 0, 24));
        CHECK(close565(px565(img, 3 * q, q), 0, 255, 0, 24));
        CHECK(close565(px565(img, q, 3 * q), 0, 0, 255, 24));
        CHECK(close565(px565(img, 3 * q, 3 * q), 255, 255, 255, 24));
        cp0_map_image_free(img);
        free(d);
    }
    printf("map tile: decode fixtures (RGB / palette PNG 256 + 512, grey + alpha, JPEG q75), 2x and 1.5x decode\n");
}

static void test_decode_generated(void)
{
    /* every colour type our PNG writer makes, pixel for pixel */
    const int W = 30, H = 18;
    uint8_t rgb[30 * 18 * 3], rgba[30 * 18 * 4], grey[30 * 18], idx[30 * 18], pal[4 * 3] = {9, 8, 7, 200, 150, 100,
                                                                                         0, 255, 0, 33, 66, 99};
    for (int i = 0; i < W * H; i++) {
        rgb[3 * i] = (uint8_t)(i * 7);
        rgb[3 * i + 1] = (uint8_t)(i * 13);
        rgb[3 * i + 2] = (uint8_t)(i * 31);
        memcpy(rgba + 4 * i, rgb + 3 * i, 3);
        rgba[4 * i + 3] = 255;
        grey[i] = (uint8_t)(i * 5);
        idx[i] = (uint8_t)(i % 4);
    }
    struct {
        int ctype;
        const uint8_t *px;
    } cases[] = {{2, rgb}, {6, rgba}, {0, grey}, {3, idx}};
    for (int c = 0; c < 4; c++) {
        bb_t png = {0};
        png_make(&png, cases[c].px, W, H, cases[c].ctype, pal, 4);
        cp0_map_image_t *img;
        CHECK_RC(cp0_map_decode_tile(png.d, png.n, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_OK);
        CHECK(img->w == W && img->h == H);
        for (int i = 0; i < W * H; i++) {
            unsigned r, g, b;
            if (cases[c].ctype == 0) r = g = b = grey[i];
            else if (cases[c].ctype == 3) { r = pal[3 * idx[i]]; g = pal[3 * idx[i] + 1]; b = pal[3 * idx[i] + 2]; }
            else { r = rgb[3 * i]; g = rgb[3 * i + 1]; b = rgb[3 * i + 2]; }
            if (px565(img, i % W, i / W) != rgb565_of(r, g, b)) CHECK(0);
        }
        g_checks += W * H;
        cp0_map_image_free(img);
        /* odd sides: 2x fine, 1.5x refused */
        CHECK_RC(cp0_map_decode_tile(png.d, png.n, CP0_MAP_PIX_RGB565, 4, &img), CP0_MAP_OK);
        cp0_map_image_free(img);
        free(png.d);
    }
    bb_t odd = {0};
    png_make(&odd, rgb, 15, 15, 2, NULL, 0);
    cp0_map_image_t *img;
    CHECK_RC(cp0_map_decode_tile(odd.d, odd.n, CP0_MAP_PIX_RGB565, 3, &img), CP0_MAP_E_UNSUPPORTED);
    free(odd.d);
    printf("map tile: generated PNGs (RGB, RGBA, grey, palette) pixel for pixel\n");
}

static void test_decode_refusals(void)
{
    cp0_map_image_t *img = (cp0_map_image_t *)1;
    CHECK_RC(cp0_map_decode_tile(NULL, 10, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_E_INVALID);
    CHECK(img == NULL);
    static const uint8_t junk[] = "this is not a picture at all";
    CHECK_RC(cp0_map_decode_tile(junk, sizeof junk, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_E_FORMAT);
    static const uint8_t webp[16] = {'R', 'I', 'F', 'F', 8, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', ' '};
    CHECK_RC(cp0_map_decode_tile(webp, 16, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_E_UNSUPPORTED);
    static const uint8_t mvt[4] = {0x1f, 0x8b, 8, 0};
    CHECK_RC(cp0_map_tile_probe(mvt, 4, NULL, NULL, NULL), CP0_MAP_E_UNSUPPORTED);
    size_t n;
    uint8_t *d = fixture("rgb256.png", &n);
    CHECK_RC(cp0_map_decode_tile(d, 60, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_E_DECODE); /* truncated */
    CHECK_RC(cp0_map_decode_tile(d, n, CP0_MAP_PIX_RGB565, 5, &img), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_decode_tile(d, n, (cp0_map_pixfmt_t)7, 2, &img), CP0_MAP_E_INVALID);
    /* a header claiming 2000 x 2000 (over CP0_MAP_DECODE_MAX_PX) */
    uint8_t *big = (uint8_t *)malloc(n);
    memcpy(big, d, n);
    big[16] = 0; big[17] = 0; big[18] = 0x07; big[19] = 0xd0;
    big[20] = 0; big[21] = 0; big[22] = 0x07; big[23] = 0xd0;
    CHECK_RC(cp0_map_decode_tile(big, n, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_E_TOO_LARGE);
    free(big);
    /* a deflate bomb: a 1024 x 1024 RGB PNG whose IDAT expands to about 33 MB (more than its own 3 MB) */
    {
        bb_t f = {0};
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        bb_put(&f, sig, 8);
        uint8_t ihdr[13] = {0, 0, 4, 0, 0, 0, 4, 0, 8, 2, 0, 0, 0};
        png_chunk(&f, "IHDR", ihdr, 13);
        bb_t z = {0};
        bb_u8(&z, 0x78);
        bb_u8(&z, 0x01);
        static const uint8_t zero = 0;
        deflate_fixed(&z, &zero, 1, 130000);
        bb_be32(&z, 0);
        png_chunk(&f, "IDAT", z.d, z.n);
        png_chunk(&f, "IEND", NULL, 0);
        const double t0 = now_ms();
        const int rc = cp0_map_decode_tile(f.d, f.n, CP0_MAP_PIX_RGB565, 2, &img);
        CHECK(rc == CP0_MAP_E_DECODE || rc == CP0_MAP_E_NOMEM);
        CHECK(img == NULL);
        printf("map tile: deflate bomb (%zu bytes) refused in %.0f ms: %s\n", f.n, now_ms() - t0, cp0_map_strerror(rc));
        free(z.d);
        free(f.d);
    }
    /* 3000 mutated tiles (PNG and JPEG): any result but a crash */
    const char *names[] = {"rgb256.png", "pal256.png", "q75_256.jpg", "la256.png"};
    int ok = 0, fail = 0;
    rng_seed(4242);
    for (int k = 0; k < 3000; k++) {
        size_t fn;
        uint8_t *src = fixture(names[k % 4], &fn);
        const int muts = 1 + (int)(rng() % 8);
        for (int m = 0; m < muts; m++) src[8 + rng() % (fn - 8)] = (uint8_t)rng();
        size_t len = fn;
        if (rng() % 5 == 0) len = 8 + rng() % (fn - 8);
        const int rc = cp0_map_decode_tile(src, len, k & 1 ? CP0_MAP_PIX_ARGB8888 : CP0_MAP_PIX_RGB565,
                                           k & 1 ? 2 : 2 + (int)(rng() % 3), &img);
        if (rc == CP0_MAP_OK) {
            ok++;
            CHECK(img && img->w > 0);
            cp0_map_image_free(img);
        } else {
            fail++;
            CHECK(img == NULL && rc < 0);
        }
        free(src);
    }
    g_checks += 3000;
    free(d);
    printf("map tile: refusals (garbage, WebP, MVT, truncated, 2000 px header) and 3000 mutated tiles (%d decoded, "
           "%d refused)\n",
           ok, fail);
}

/* ----------------------------------------------------------------------------------------------------- scalers */

static unsigned ref_mix(unsigned a, unsigned b, unsigned w)
{
    return (a * (256 - w) + b * w + 128) >> 8;
}

static uint16_t ref_mix565(uint16_t a, uint16_t b, unsigned w)
{
    return (uint16_t)((ref_mix(a >> 11, b >> 11, w) << 11) | (ref_mix((a >> 5) & 63, (b >> 5) & 63, w) << 5) |
                      ref_mix(a & 31, b & 31, w));
}

/* the definition of cp0_map_tile.h, one pixel at a time */
static uint16_t ref_h15(const uint16_t *src, int sw, int row, int X)
{
    const uint16_t *p = src + (size_t)row * sw;
    const int j = X / 3, r = X % 3;
    if (r == 0) return p[2 * j];
    if (r == 1) return ref_mix565(p[2 * j], p[2 * j + 1], 171);
    return ref_mix565(p[2 * j + 1], p[2 * j + 2 < sw ? 2 * j + 2 : sw - 1], 85);
}

static uint16_t ref_15(const uint16_t *src, int sw, int sh, int X, int Y)
{
    const int k = Y / 3, r = Y % 3;
    if (r == 0) return ref_h15(src, sw, 2 * k, X);
    if (r == 1) return ref_mix565(ref_h15(src, sw, 2 * k, X), ref_h15(src, sw, 2 * k + 1, X), 171);
    return ref_mix565(ref_h15(src, sw, 2 * k + 1, X), ref_h15(src, sw, 2 * k + 2 < sh ? 2 * k + 2 : sh - 1, X), 85);
}

static uint16_t ref_px(const uint16_t *src, int sw, int sh, int s, int X, int Y)
{
    if (s == 2) return src[(size_t)Y * sw + X];
    if (s == 4) return src[(size_t)(Y / 2) * sw + X / 2];
    return ref_15(src, sw, sh, X, Y);
}

static void check_rect(const uint16_t *src, int sw, int sh, int s, int dx, int dy, int dw, int dh)
{
    const int stride = dw + 3; /* a destination stride wider than the rectangle */
    uint16_t *dst = (uint16_t *)malloc((size_t)stride * (size_t)dh * 2);
    for (int i = 0; i < stride * dh; i++) dst[i] = 0xdead;
    CHECK_RC(cp0_map_scale_rgb565(src, sw, sh, sw * 2, s, dx, dy, dw, dh, dst, stride * 2), CP0_MAP_OK);
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++)
            if (dst[(size_t)y * stride + x] != ref_px(src, sw, sh, s, dx + x, dy + y)) {
                fprintf(stderr, "scale %d src %dx%d rect %d,%d %dx%d: pixel %d,%d differs\n", s, sw, sh, dx, dy, dw, dh,
                        x, y);
                CHECK(0);
            }
        for (int x = dw; x < stride; x++) CHECK(dst[(size_t)y * stride + x] == 0xdead); /* nothing past the rectangle */
    }
    g_checks += (long)dw * dh;
    free(dst);
}

static void test_scalers(void)
{
    const int sizes[][2] = {{2, 2}, {4, 6}, {8, 8}, {16, 10}, {256, 256}, {512, 512}};
    rng_seed(1234);
    long rects = 0;
    for (size_t si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        const int sw = sizes[si][0], sh = sizes[si][1];
        uint16_t *src = (uint16_t *)malloc((size_t)sw * sh * 2);
        for (int i = 0; i < sw * sh; i++) src[i] = (uint16_t)rng();
        for (int s = 2; s <= 4; s++) {
            const int mw = sw * s / 2, mh = sh * s / 2;
            check_rect(src, sw, sh, s, 0, 0, mw, mh); /* the whole tile */
            rects++;
            const int tries = sw >= 256 ? 12 : 40;
            for (int t = 0; t < tries; t++) {
                const int dx = (int)(rng() % (unsigned)mw), dy = (int)(rng() % (unsigned)mh);
                const int dw = 1 + (int)(rng() % (unsigned)(mw - dx)), dh = 1 + (int)(rng() % (unsigned)(mh - dy));
                check_rect(src, sw, sh, s, dx, dy, dw, dh);
                rects++;
            }
        }
        free(src);
    }
    /* a constant picture stays constant; phase-0 pixels of 1.5x are source pixels */
    uint16_t flat[64], out[144];
    for (int i = 0; i < 64; i++) flat[i] = 0x7bef;
    CHECK_RC(cp0_map_scale_rgb565(flat, 8, 8, 16, 3, 0, 0, 12, 12, out, 24), CP0_MAP_OK);
    for (int i = 0; i < 144; i++) CHECK(out[i] == 0x7bef);
    uint16_t grad[64];
    for (int i = 0; i < 64; i++) grad[i] = (uint16_t)(i * 997);
    CHECK_RC(cp0_map_scale_rgb565(grad, 8, 8, 16, 3, 0, 0, 12, 12, out, 24), CP0_MAP_OK);
    for (int y = 0; y < 12; y += 3)
        for (int x = 0; x < 12; x += 3) CHECK(out[y * 12 + x] == grad[(y / 3 * 2) * 8 + x / 3 * 2]);
    /* refusals */
    CHECK_RC(cp0_map_scale_rgb565(flat, 7, 8, 14, 3, 0, 0, 10, 12, out, 24), CP0_MAP_E_INVALID); /* odd side */
    CHECK_RC(cp0_map_scale_rgb565(flat, 8, 8, 16, 3, 1, 0, 12, 12, out, 24), CP0_MAP_E_INVALID); /* past the edge */
    CHECK_RC(cp0_map_scale_rgb565(flat, 8, 8, 16, 5, 0, 0, 4, 4, out, 24), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_scale_rgb565(flat, 8, 8, 8, 2, 0, 0, 4, 4, out, 24), CP0_MAP_E_INVALID);    /* short stride */
    CHECK_RC(cp0_map_scale_rgb565(flat, 8, 8, 16, 4, 0, 0, 4, 4, out, 4), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_scale_rgb565(NULL, 8, 8, 16, 4, 0, 0, 4, 4, out, 24), CP0_MAP_E_INVALID);
    /* speed on this machine (information) */
    uint16_t *src = (uint16_t *)malloc(256 * 256 * 2), *dst = (uint16_t *)malloc(512 * 512 * 2);
    for (int i = 0; i < 65536; i++) src[i] = (uint16_t)rng();
    double t0 = now_ms();
    for (int k = 0; k < 50; k++) cp0_map_scale_rgb565(src, 256, 256, 512, 3, 0, 0, 384, 384, dst, 768);
    const double t15 = (now_ms() - t0) / 50;
    t0 = now_ms();
    for (int k = 0; k < 50; k++) cp0_map_scale_rgb565(src, 256, 256, 512, 4, 0, 0, 512, 512, dst, 1024);
    const double t2 = (now_ms() - t0) / 50;
    free(src);
    free(dst);
    printf("map tile: scalers exact against the reference on %ld rectangles (1x, 2x, 1.5x); 256 px tile here: "
           "1.5x %.3f ms, 2x %.3f ms\n",
           rects, t15, t2);
}

/* ---------------------------------------------------------------------------------------------------- RAM cache */

static cp0_map_image_t *img16(uint16_t fill)
{
    cp0_map_image_t *i = cp0_map_image_new(16, 16, CP0_MAP_PIX_RGB565); /* 512 bytes */
    CHECK(i != NULL);
    for (int k = 0; k < 256; k++) ((uint16_t *)i->px)[k] = fill;
    return i;
}

static cp0_map_tile_key_t key(uint32_t src, int z, uint32_t x, uint32_t y)
{
    cp0_map_tile_key_t k = {src, x, y, (uint8_t)z, cp0_map_tile_variant(2, CP0_MAP_PIX_RGB565)};
    return k;
}

static void test_cache_lru(void)
{
    cp0_map_cache_t *c = cp0_map_cache_new(4 * 512);
    CHECK(c != NULL);
    cp0_map_tile_key_t k[8];
    for (int i = 0; i < 8; i++) k[i] = key(7, 3, (uint32_t)i, 1);
    for (int i = 0; i < 4; i++) CHECK_RC(cp0_map_cache_put(c, &k[i], img16((uint16_t)i), NULL), CP0_MAP_OK);
    /* A is used again: B becomes the least recently used */
    const cp0_map_image_t *a = cp0_map_cache_get(c, &k[0]);
    CHECK(a && ((const uint16_t *)a->px)[0] == 0);
    cp0_map_cache_release(c, a);
    CHECK_RC(cp0_map_cache_put(c, &k[4], img16(4), NULL), CP0_MAP_OK);
    CHECK(!cp0_map_cache_contains(c, &k[1]) && cp0_map_cache_contains(c, &k[0]));
    CHECK_RC(cp0_map_cache_put(c, &k[5], img16(5), NULL), CP0_MAP_OK);
    CHECK(!cp0_map_cache_contains(c, &k[2]));
    /* order now (old -> new): 3, 0, 4, 5 */
    CHECK(cp0_map_cache_get(c, &k[1]) == NULL);
    cp0_map_cache_stats_t st;
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.hits == 1 && st.misses == 1 && st.inserts == 6 && st.evictions == 2 && st.entries == 4);
    CHECK(st.bytes == 2048 && st.budget == 2048 && st.pinned == 0);
    /* a pinned tile is skipped by eviction: pin 3 (the oldest), then add 6: 0 goes */
    const cp0_map_image_t *p3 = cp0_map_cache_get(c, &k[3]);
    CHECK(p3 != NULL);
    CHECK_RC(cp0_map_cache_put(c, &k[6], img16(6), NULL), CP0_MAP_OK);
    CHECK(cp0_map_cache_contains(c, &k[3]) && !cp0_map_cache_contains(c, &k[0]));
    /* replace a pinned tile: the old picture lives on (orphan, counted) until released */
    const cp0_map_image_t *pinned_new = NULL;
    CHECK_RC(cp0_map_cache_put(c, &k[3], img16(33), &pinned_new), CP0_MAP_OK);
    CHECK(((const uint16_t *)p3->px)[0] == 3 && ((const uint16_t *)pinned_new->px)[0] == 33);
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.bytes <= st.budget && st.pinned == 2);
    /* everything pinned: a new tile does not fit */
    const cp0_map_image_t *pins[4];
    int np = 0;
    for (int i = 0; i < 8; i++) {
        if (i == 3) continue;
        const cp0_map_image_t *g = cp0_map_cache_get(c, &k[i]);
        if (g) pins[np++] = g;
    }
    cp0_map_image_t *extra = img16(99);
    CHECK_RC(cp0_map_cache_put(c, &k[7], extra, NULL), CP0_MAP_E_FULL);
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.rejected == 1 && st.bytes <= st.budget);
    cp0_map_cache_release(c, p3); /* the orphan goes: room for one */
    CHECK_RC(cp0_map_cache_put(c, &k[7], extra, NULL), CP0_MAP_OK);
    for (int i = 0; i < np; i++) cp0_map_cache_release(c, pins[i]);
    cp0_map_cache_release(c, pinned_new);
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.pinned == 0 && st.bytes <= st.budget && st.peak_bytes <= st.budget);
    /* too large for the whole budget; NULL; the same picture twice */
    cp0_map_image_t *huge = cp0_map_image_new(64, 64, CP0_MAP_PIX_RGB565);
    CHECK_RC(cp0_map_cache_put(c, &k[0], huge, NULL), CP0_MAP_E_TOO_LARGE);
    cp0_map_image_free(huge);
    CHECK_RC(cp0_map_cache_put(c, &k[0], NULL, NULL), CP0_MAP_E_INVALID);
    const cp0_map_image_t *g7 = cp0_map_cache_get(c, &k[7]);
    CHECK_RC(cp0_map_cache_put(c, &k[1], (cp0_map_image_t *)(uintptr_t)g7, NULL), CP0_MAP_E_INVALID);
    cp0_map_cache_release(c, g7);
    /* drop by source, a smaller budget */
    cp0_map_tile_key_t other = key(8, 3, 0, 0);
    CHECK_RC(cp0_map_cache_put(c, &other, img16(1), NULL), CP0_MAP_OK);
    CHECK(cp0_map_cache_drop(c, 7) >= 1);
    CHECK(cp0_map_cache_contains(c, &other) && !cp0_map_cache_contains(c, &k[7]));
    for (int i = 0; i < 3; i++) CHECK_RC(cp0_map_cache_put(c, &k[i], img16(1), NULL), CP0_MAP_OK);
    cp0_map_cache_set_budget(c, 1024);
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.entries == 2 && st.bytes == 1024 && st.budget == 1024);
    CHECK(cp0_map_cache_drop(c, 0) == 2);
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.entries == 0 && st.bytes == 0);
    /* freeing with a pinned picture still frees it (ASan would see a leak) */
    CHECK_RC(cp0_map_cache_put(c, &k[0], img16(1), &pinned_new), CP0_MAP_OK);
    cp0_map_cache_drop(c, 0); /* now an orphan */
    cp0_map_cache_free(c);
    /* variants are separate entries */
    c = cp0_map_cache_new(1 << 20);
    cp0_map_tile_key_t v1 = key(1, 5, 2, 2), v2 = v1;
    v2.variant = cp0_map_tile_variant(4, CP0_MAP_PIX_RGB565);
    CHECK(v1.variant != v2.variant);
    CHECK_RC(cp0_map_cache_put(c, &v1, img16(1), NULL), CP0_MAP_OK);
    CHECK(!cp0_map_cache_contains(c, &v2));
    /* many keys: the table grows */
    for (uint32_t i = 0; i < 1500; i++) {
        cp0_map_tile_key_t kk = key(2, 12, i, i * 3);
        CHECK_RC(cp0_map_cache_put(c, &kk, img16((uint16_t)i), NULL), CP0_MAP_OK);
    }
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.entries == 1501 - (int)st.evictions && st.bytes <= st.budget);
    for (uint32_t i = 1499; i > 1400; i--) {
        cp0_map_tile_key_t kk = key(2, 12, i, i * 3);
        const cp0_map_image_t *g = cp0_map_cache_get(c, &kk);
        CHECK(g && ((const uint16_t *)g->px)[0] == (uint16_t)i);
        cp0_map_cache_release(c, g);
    }
    cp0_map_cache_free(c);
    printf("map tile: RAM cache LRU order, pins, replace, FULL, TOO_LARGE, drop, budget change, growth\n");
}

static void test_cache_budgets(void)
{
    const size_t MiB = 1024 * 1024;
    CHECK(cp0_map_cache_default_budget(-1) == 8 * MiB && cp0_map_cache_default_budget(0) == 8 * MiB);
    CHECK(cp0_map_cache_default_budget(414) == 8 * MiB && cp0_map_cache_default_budget(767) == 8 * MiB);
    CHECK(cp0_map_cache_default_budget(768) == 24 * MiB && cp0_map_cache_default_budget(1800) == 24 * MiB);
    CHECK(cp0_map_cache_default_budget(3072) == 48 * MiB && cp0_map_cache_default_budget(3800) == 48 * MiB);
    CHECK(8 * MiB / (256 * 256 * 2) == 64); /* 64 tiles of 256 px RGB565 */
    /* from board.state, through the launcher's reader (a live pid) */
    char dir[600], path[700], text[200];
    tpath(dir, sizeof dir, "state");
    mkdirs(dir);
    snprintf(path, sizeof path, "%s/board.state", dir);
    snprintf(text, sizeof text, "v=1\ngen=3\npid=%d\nram_mb=1800\n", (int)getpid());
    write_file(path, text, strlen(text));
    setenv("APPLAUNCH_STATE_DIR", dir, 1);
    CHECK(cp0_map_cache_budget_from_board() == 24 * MiB);
    cp0_map_cache_t *c = cp0_map_cache_new(0);
    cp0_map_cache_stats_t st;
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.budget == 24 * MiB);
    cp0_map_cache_free(c);
    snprintf(text, sizeof text, "v=1\ngen=3\npid=%d\nram_mb=414\n", (int)getpid());
    write_file(path, text, strlen(text));
    CHECK(cp0_map_cache_budget_from_board() == 8 * MiB);
    /* a launcher that is gone: its file is not trusted (the RAM of this machine decides) */
    snprintf(text, sizeof text, "v=1\ngen=3\npid=999999999\nram_mb=414\n");
    write_file(path, text, strlen(text));
    const size_t b = cp0_map_cache_budget_from_board();
    CHECK(b == 8 * MiB || b == 24 * MiB || b == 48 * MiB);
    unsetenv("APPLAUNCH_STATE_DIR");
    printf("map tile: default budgets (8 / 24 / 48 MiB by ram_mb), board.state ram_mb through cp0_app_board_read\n");
}

typedef struct {
    cp0_map_cache_t *c;
    uint64_t seed;
    long ops;
} worker_t;

static void *cache_worker(void *arg)
{
    worker_t *w = (worker_t *)arg;
    uint64_t s = w->seed;
    for (long i = 0; i < w->ops; i++) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        const uint32_t r = (uint32_t)(s >> 20);
        cp0_map_tile_key_t k = key(1, 4, r % 64, 0);
        if (r & 0x100000) {
            cp0_map_image_t *img = cp0_map_image_new(16, 16, CP0_MAP_PIX_RGB565);
            for (int p = 0; p < 256; p++) ((uint16_t *)img->px)[p] = (uint16_t)(r % 64);
            const cp0_map_image_t *pin = NULL;
            if (cp0_map_cache_put(w->c, &k, img, (r & 3) ? NULL : &pin) != CP0_MAP_OK)
                cp0_map_image_free(img);
            else if (pin) {
                if (((const uint16_t *)pin->px)[255] != (uint16_t)(r % 64)) abort();
                cp0_map_cache_release(w->c, pin);
            }
        } else {
            const cp0_map_image_t *g = cp0_map_cache_get(w->c, &k);
            if (g) {
                if (((const uint16_t *)g->px)[0] != (uint16_t)(r % 64)) abort();
                cp0_map_cache_release(w->c, g);
            }
        }
    }
    return NULL;
}

static void test_cache_threads(void)
{
    cp0_map_cache_t *c = cp0_map_cache_new(16 * 512);
    pthread_t t[4];
    worker_t w[4];
    for (int i = 0; i < 4; i++) {
        w[i] = (worker_t){c, 0x1234567ull * (uint64_t)(i + 1), MAP_TEST_ASAN ? 20000 : 100000};
        CHECK(pthread_create(&t[i], NULL, cache_worker, &w[i]) == 0);
    }
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    cp0_map_cache_stats_t st;
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.pinned == 0 && st.bytes <= st.budget && st.peak_bytes <= st.budget && st.entries <= 16);
    CHECK(st.hits + st.misses > 0 && st.inserts > 0);
    cp0_map_cache_free(c);
    printf("map tile: four threads on one cache (%ld operations each), budget kept, no pins left\n", w[0].ops);
}

#ifdef MAP_TILE_RSS
static long vm_rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long v = -1;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "VmRSS:", 6)) v = atol(line + 6);
    fclose(f);
    return v;
}
#endif

static void test_memory_budget(void)
{
    size_t n;
    uint8_t *png = fixture("rgb256.png", &n);
    const size_t budget = 8u * 1024 * 1024;
    cp0_map_cache_t *c = cp0_map_cache_new(budget);
#ifdef MAP_TILE_RSS
    malloc_trim(0);
    const long rss0 = vm_rss_kb();
#endif
    cp0_map_cache_stats_t st;
    for (uint32_t i = 0; i < 400; i++) {
        cp0_map_image_t *img;
        CHECK_RC(cp0_map_decode_tile(png, n, CP0_MAP_PIX_RGB565, i % 7 == 0 ? 4 : 2, &img), CP0_MAP_OK);
        cp0_map_tile_key_t k = key(3, 14, i, 0);
        k.variant = cp0_map_tile_variant(i % 7 == 0 ? 4 : 2, CP0_MAP_PIX_RGB565);
        CHECK_RC(cp0_map_cache_put(c, &k, img, NULL), CP0_MAP_OK);
        cp0_map_cache_get_stats(c, &st);
        if (st.bytes > budget) CHECK(0);
    }
    cp0_map_cache_get_stats(c, &st);
    CHECK(st.peak_bytes <= budget && st.bytes >= budget - 512 * 1024 && st.entries <= 64);
#ifdef MAP_TILE_RSS
    malloc_trim(0);
    const long rss1 = vm_rss_kb();
    const long grew = rss1 - rss0;
    /* the cache's pixels plus allocator and entry overhead: within 1.5 MiB of the budget */
    CHECK(grew <= (long)(budget / 1024) + 1536);
    printf("map tile: memory: 400 decodes into an 8 MiB cache, VmRSS grew %ld KiB (budget %zu KiB), %d tiles held\n",
           grew, budget / 1024, st.entries);
#else
    printf("map tile: memory: 400 decodes into an 8 MiB cache, bytes held never above the budget (VmRSS check in "
           "the build without sanitizers)\n");
#endif
    cp0_map_cache_free(c);
    free(png);
}

int main(void)
{
    tmp_init("cp0-map-tile");
    test_decode_fixtures();
    test_decode_generated();
    test_decode_refusals();
    test_scalers();
    test_cache_lru();
    test_cache_budgets();
    test_cache_threads();
    test_memory_budget();
    tmp_done();
    printf("map tile OK (%ld checks)\n", g_checks);
    return 0;
}
