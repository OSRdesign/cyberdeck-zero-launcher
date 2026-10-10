/*
 * Unit tests of the dpi-scaled output stage (src/cp0/cp0_fb_output.c): rotated blits against a
 * naive reference (all four angles, odd sizes, clipping, stride padding, guard bytes), scaled and
 * XRGB8888 blits with an excluded rectangle, RGB565 packing against the framebuffer offsets,
 * setup/fallbacks, touch mapping for the Pi 3A+ and Pi 5 cases, and equivalence with the deck's
 * previous code paths (rotation 0, 32 bpp, 640x480, compat 2x).
 */

#include "cp0_fb_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);   \
            if (++failures > 20) exit(1);                                              \
        }                                                                              \
    } while (0)

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static int rnd_range(int lo, int hi) /* [lo, hi] */
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

/* ------------------------------------------------------------ test framebuffer */

#define GUARD 64

typedef struct {
    uint8_t *mem;  /* GUARD + size + GUARD */
    uint8_t *base;
    size_t size;
} fbmem_t;

static void fb_alloc(fbmem_t *f, size_t size)
{
    f->size = size;
    f->mem = malloc(size + 2 * GUARD);
    f->base = f->mem + GUARD;
    for (size_t i = 0; i < size + 2 * GUARD; i++) f->mem[i] = (uint8_t)(0xA5 ^ (i * 7));
}

static void fb_copy(fbmem_t *dst, const fbmem_t *src)
{
    memcpy(dst->mem, src->mem, src->size + 2 * GUARD);
}

static int guards_intact(const fbmem_t *f)
{
    for (size_t i = 0; i < GUARD; i++) {
        if (f->mem[i] != (uint8_t)(0xA5 ^ (i * 7))) return 0;
        const size_t j = GUARD + f->size + i;
        if (f->mem[j] != (uint8_t)(0xA5 ^ (j * 7))) return 0;
    }
    return 1;
}

static void fb_free(fbmem_t *f)
{
    free(f->mem);
}

/* ------------------------------------------------------------ reference */

/* Independent formulation: rot/90 successive clockwise quarter turns, each W x H -> H x W with
 * (x, y) -> (H-1-y, x). */
static void ref_map(int rot, int lw, int lh, int X, int Y, int *px, int *py)
{
    int x = X, y = Y, w = lw, h = lh;
    for (int k = 0; k < rot / 90; k++) {
        const int nx = h - 1 - y, ny = x;
        x = nx;
        y = ny;
        const int t = w;
        w = h;
        h = t;
    }
    *px = x;
    *py = y;
}

static void ref_put(const cp0_fb_out_t *o, uint8_t *base, int X, int Y, const uint8_t *px)
{
    if (X < 0 || Y < 0 || X >= o->lw || Y >= o->lh) return;
    int bx, by;
    ref_map(o->rot, o->lw, o->lh, X, Y, &bx, &by);
    memcpy(base + (size_t)by * o->stride + (size_t)bx * o->bpp, px, (size_t)o->bpp);
}

static void fill_random(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)rnd();
}

/* A geometry: logical lw x lh, rotation, depth, stride padding. Builds the buffer to match. */
static void make_out(cp0_fb_out_t *o, fbmem_t *f, int lw, int lh, int rot, int bpp, int pad_px, int extra_rows)
{
    const int swap = rot == 90 || rot == 270;
    const int pw = swap ? lh : lw, ph = swap ? lw : lh;
    const int stride = (pw + pad_px) * bpp;
    fb_alloc(f, (size_t)stride * (ph + extra_rows));
    const int fix = cp0_fbo_setup(o, f->base, f->size, stride, bpp, pw, ph, rot, lw, lh, NULL);
    CHECK(fix == 0);
    CHECK(o->lw == lw && o->lh == lh && o->pw == pw && o->ph == ph);
}

/* ------------------------------------------------------------ tests */

static void test_blit_random(void)
{
    static const int sizes[][2] = {{37, 23}, {40, 30}, {1, 1}, {33, 65}};
    for (int bi = 0; bi < 2; bi++) {
        const int bpp = bi ? 4 : 2;
        for (int rot = 0; rot < 360; rot += 90) {
            for (unsigned si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
                cp0_fb_out_t o;
                fbmem_t a, b;
                make_out(&o, &a, sizes[si][0], sizes[si][1], rot, bpp, (int)(si % 3) * 3, (int)si % 2);
                fb_alloc(&b, a.size);
                fb_copy(&b, &a);
                cp0_fb_out_t ob = o;
                ob.base = b.base;
                for (int it = 0; it < 200; it++) {
                    const int w = rnd_range(1, o.lw + 8), h = rnd_range(1, o.lh + 8);
                    const int X = rnd_range(-6, o.lw + 2), Y = rnd_range(-6, o.lh + 2);
                    const int replicate = it % 17 == 0;
                    const int ss = replicate ? 0 : (w + rnd_range(0, 5)) * bpp;
                    const size_t src_size = (size_t)(replicate ? w * bpp : ss * h);
                    uint8_t *src = malloc(src_size);
                    fill_random(src, src_size);
                    cp0_fbo_blit(&o, X, Y, w, h, src, ss);
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++)
                            ref_put(&ob, b.base, X + x, Y + y, src + (size_t)y * ss + (size_t)x * bpp);
                    free(src);
                }
                CHECK(memcmp(a.mem, b.mem, a.size + 2 * GUARD) == 0);
                CHECK(guards_intact(&a));
                fb_free(&a);
                fb_free(&b);
            }
        }
    }
}

static void test_blit_except_random(void)
{
    for (int bi = 0; bi < 2; bi++) {
        const int bpp = bi ? 4 : 2;
        for (int rot = 0; rot < 360; rot += 90) {
            cp0_fb_out_t o;
            fbmem_t a, b;
            make_out(&o, &a, 48, 32, rot, bpp, 2, 0);
            fb_alloc(&b, a.size);
            fb_copy(&b, &a);
            cp0_fb_out_t ob = o;
            ob.base = b.base;
            for (int it = 0; it < 300; it++) {
                const int w = rnd_range(1, 52), h = rnd_range(1, 36);
                const int X = rnd_range(-3, 47), Y = rnd_range(-3, 31);
                const int wx = rnd_range(-5, 50), wy = rnd_range(-5, 34);
                const int ww = rnd_range(-1, 50), wh = rnd_range(-1, 34);
                const int ss = w * bpp;
                uint8_t *src = malloc((size_t)ss * h);
                fill_random(src, (size_t)ss * h);
                cp0_fbo_blit_except(&o, X, Y, w, h, src, ss, wx, wy, ww, wh);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        const int LX = X + x, LY = Y + y;
                        const int in_hole = ww > 0 && wh > 0 && LX >= wx && LX < wx + ww && LY >= wy && LY < wy + wh;
                        if (!in_hole) ref_put(&ob, b.base, LX, LY, src + (size_t)y * ss + (size_t)x * bpp);
                    }
                free(src);
            }
            CHECK(memcmp(a.mem, b.mem, a.size + 2 * GUARD) == 0);
            CHECK(guards_intact(&a));
            fb_free(&a);
            fb_free(&b);
        }
    }
}

static void test_blit_scaled_random(void)
{
    for (int bi = 0; bi < 2; bi++) {
        const int bpp = bi ? 4 : 2;
        for (int rot = 0; rot < 360; rot += 90) {
            for (int scale = 1; scale <= 3; scale++) {
                cp0_fb_out_t o;
                fbmem_t a, b;
                make_out(&o, &a, 61, 43, rot, bpp, 1, 1);
                fb_alloc(&b, a.size);
                fb_copy(&b, &a);
                cp0_fb_out_t ob = o;
                ob.base = b.base;
                const size_t scratch_sizes[] = {(size_t)scale * scale * bpp, (size_t)scale * scale * bpp * 7 + 3,
                                                4096, 1 << 16};
                for (int it = 0; it < 120; it++) {
                    const size_t ssz = scratch_sizes[it % 4];
                    uint8_t *scratch = malloc(ssz);
                    const int w = rnd_range(1, 25), h = rnd_range(1, 19);
                    const int X = rnd_range(-4, 60), Y = rnd_range(-4, 42);
                    const int ss = (w + rnd_range(0, 3)) * bpp;
                    uint8_t *src = malloc((size_t)ss * h);
                    fill_random(src, (size_t)ss * h);
                    cp0_fbo_blit_scaled(&o, X, Y, w, h, src, ss, scale, scratch, ssz);
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++)
                            for (int dy = 0; dy < scale; dy++)
                                for (int dx = 0; dx < scale; dx++)
                                    ref_put(&ob, b.base, X + x * scale + dx, Y + y * scale + dy,
                                            src + (size_t)y * ss + (size_t)x * bpp);
                    free(src);
                    free(scratch);
                }
                CHECK(memcmp(a.mem, b.mem, a.size + 2 * GUARD) == 0);
                CHECK(guards_intact(&a));
                fb_free(&a);
                fb_free(&b);
            }
        }
    }
}

static uint16_t ref_rgb565(uint32_t p)
{
    const uint8_t r = (uint8_t)(p >> 16), g = (uint8_t)(p >> 8), b = (uint8_t)p;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); /* the previous hard-coded packing */
}

static uint16_t ref_bgr565(uint32_t p)
{
    const uint8_t r = (uint8_t)(p >> 16), g = (uint8_t)(p >> 8), b = (uint8_t)p;
    return (uint16_t)(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3));
}

static void test_xrgb_scaled_random(void)
{
    static const cp0_fb_layout_t bgr565 = {{0, 5}, {5, 6}, {11, 5}};
    for (int mode = 0; mode < 3; mode++) { /* 0: 16 bpp RGB565, 1: 16 bpp BGR565, 2: 32 bpp */
        const int bpp = mode == 2 ? 4 : 2;
        for (int rot = 0; rot < 360; rot += 90) {
            for (int scale = 1; scale <= 2; scale++) {
                const int lw = 70, lh = 50;
                const int swap = rot == 90 || rot == 270;
                const int pw = swap ? lh : lw, ph = swap ? lw : lh;
                const int stride = (pw + 3) * bpp;
                fbmem_t a, b;
                fb_alloc(&a, (size_t)stride * ph);
                cp0_fb_out_t o;
                CHECK(cp0_fbo_setup(&o, a.base, a.size, stride, bpp, pw, ph, rot, 0, 0, mode == 1 ? &bgr565 : NULL) == 0);
                CHECK(o.rgb565_plain == (mode == 0));
                fb_alloc(&b, a.size);
                fb_copy(&b, &a);
                cp0_fb_out_t ob = o;
                ob.base = b.base;
                for (int it = 0; it < 80; it++) {
                    const size_t ssz = it % 2 ? 4096 : (size_t)scale * scale * bpp * 5;
                    uint8_t *scratch = malloc(ssz);
                    const int w = rnd_range(1, 30), h = rnd_range(1, 22);
                    const int X = rnd_range(-2, 20), Y = rnd_range(-2, 10);
                    const int ss = (w + rnd_range(0, 2)) * 4;
                    const int ex = rnd_range(-3, 30), ey = rnd_range(-3, 22);
                    const int ew = it % 5 == 0 ? 0 : rnd_range(1, 20), eh = rnd_range(1, 12);
                    uint8_t *src = malloc((size_t)ss * h);
                    fill_random(src, (size_t)ss * h);
                    cp0_fbo_blit_xrgb_scaled(&o, X, Y, w, h, src, ss, scale, ex, ey, ew, eh, scratch, ssz);
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++) {
                            if (ew > 0 && eh > 0 && x >= ex && x < ex + ew && y >= ey && y < ey + eh) continue;
                            uint32_t p;
                            memcpy(&p, src + (size_t)y * ss + (size_t)x * 4, 4);
                            uint8_t px[4];
                            if (mode == 2) {
                                memcpy(px, &p, 4);
                            } else {
                                const uint16_t v = mode == 0 ? ref_rgb565(p) : ref_bgr565(p);
                                memcpy(px, &v, 2);
                            }
                            for (int dy = 0; dy < scale; dy++)
                                for (int dx = 0; dx < scale; dx++)
                                    ref_put(&ob, b.base, X + x * scale + dx, Y + y * scale + dy, px);
                        }
                    free(src);
                    free(scratch);
                }
                CHECK(memcmp(a.mem, b.mem, a.size + 2 * GUARD) == 0);
                CHECK(guards_intact(&a));
                fb_free(&a);
                fb_free(&b);
            }
        }
    }
}

static void test_fill_black(void)
{
    for (int bi = 0; bi < 2; bi++) {
        const int bpp = bi ? 4 : 2;
        for (int rot = 0; rot < 360; rot += 90) {
            cp0_fb_out_t o;
            fbmem_t a, b;
            make_out(&o, &a, 29, 17, rot, bpp, 3, 0);
            fb_alloc(&b, a.size);
            fb_copy(&b, &a);
            cp0_fb_out_t ob = o;
            ob.base = b.base;
            static const uint8_t black[4] = {0, 0, 0, 0};
            for (int it = 0; it < 60; it++) {
                const int X = rnd_range(-5, 30), Y = rnd_range(-5, 18), w = rnd_range(0, 35), h = rnd_range(0, 20);
                cp0_fbo_fill_black(&o, X, Y, w, h);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) ref_put(&ob, b.base, X + x, Y + y, black);
            }
            CHECK(memcmp(a.mem, b.mem, a.size + 2 * GUARD) == 0);
            CHECK(guards_intact(&a));
            fb_free(&a);
            fb_free(&b);
        }
    }
}

static void test_pack_and_formats(void)
{
    /* Pi 3A+ panel: red off 11 len 5, green off 5 len 6, blue off 0 len 5 */
    const cp0_fb_layout_t rgb = {{11, 5}, {5, 6}, {0, 5}};
    CHECK(cp0_fbo_pack(&rgb, 0xFF, 0, 0) == 0xF800);
    CHECK(cp0_fbo_pack(&rgb, 0, 0xFF, 0) == 0x07E0);
    CHECK(cp0_fbo_pack(&rgb, 0, 0, 0xFF) == 0x001F);
    CHECK(cp0_fbo_pack(&rgb, 0xFF, 0xFF, 0xFF) == 0xFFFF);
    CHECK(cp0_fbo_pack(&rgb, 0x08, 0x04, 0x08) == 0x0821);
    for (int i = 0; i < 1000; i++) {
        const uint32_t p = rnd() & 0xFFFFFFu;
        CHECK(cp0_fbo_pack(&rgb, (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF) == ref_rgb565(p));
    }
    const cp0_fb_layout_t bgr = {{0, 5}, {5, 6}, {11, 5}};
    CHECK(cp0_fbo_pack(&bgr, 0xFF, 0, 0) == 0x001F);
    CHECK(cp0_fbo_pack(&bgr, 0, 0, 0xFF) == 0xF800);

    /* LVGL RGB565 -> the buffer layout, in place */
    uint8_t dummy[8];
    cp0_fb_out_t o;
    CHECK(cp0_fbo_setup(&o, dummy, sizeof(dummy), 4, 2, 2, 2, 0, 0, 0, &bgr) == 0);
    CHECK(!o.rgb565_plain);
    uint16_t px[6] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0x1234};
    cp0_fbo_to_buffer_format(&o, (uint8_t *)px, 3, 2, 3 * 2);
    CHECK(px[0] == 0x001F && px[1] == 0x07E0 && px[2] == 0xF800 && px[3] == 0xFFFF && px[4] == 0);
    CHECK(px[5] == (uint16_t)(((0x1234 & 0x1F) << 11) | (0x1234 & 0x07E0) | (0x1234 >> 11)));
    CHECK(cp0_fbo_setup(&o, dummy, sizeof(dummy), 4, 2, 2, 2, 0, 0, 0, &rgb) == 0);
    CHECK(o.rgb565_plain);
    uint16_t same[2] = {0x1234, 0xABCD};
    cp0_fbo_to_buffer_format(&o, (uint8_t *)same, 2, 1, 4);
    CHECK(same[0] == 0x1234 && same[1] == 0xABCD);
}

static void test_setup_and_parse(void)
{
    uint8_t *mem = malloc(640 * 480 * 4);
    cp0_fb_out_t o;
    /* Pi 3A+: fb1 320x480 16 bpp line 640, rotate 90, logical 480x320 */
    const cp0_fb_layout_t rgb = {{11, 5}, {5, 6}, {0, 5}};
    CHECK(cp0_fbo_setup(&o, mem, 640 * 480, 640, 2, 320, 480, 90, 480, 320, &rgb) == 0);
    CHECK(o.lw == 480 && o.lh == 320 && o.rgb565_plain);
    /* rotate 0 with the landscape size requested: falls back to the buffer size */
    CHECK(cp0_fbo_setup(&o, mem, 640 * 480, 640, 2, 320, 480, 0, 480, 320, &rgb) == CP0_FBO_FIX_LOGICAL);
    CHECK(o.lw == 320 && o.lh == 480);
    /* deck: 640x480 32 bpp, nothing requested */
    CHECK(cp0_fbo_setup(&o, mem, 2560 * 480, 2560, 4, 640, 480, 0, 0, 0, NULL) == 0);
    CHECK(o.lw == 640 && o.lh == 480 && o.rot == 0);
    /* Pi 5 HyperPixel: 480x800 32 bpp, rotate 90 -> 800x480 (setup does not touch the memory) */
    CHECK(cp0_fbo_setup(&o, mem, 1920 * 800, 1920, 4, 480, 800, 90, 800, 480, NULL) == 0);
    CHECK(o.lw == 800 && o.lh == 480);
    /* geometry that does not fit the mapping is clamped */
    CHECK(cp0_fbo_setup(&o, mem, 360 * 10, 360, 4, 100, 20, 0, 0, 0, NULL) ==
          (CP0_FBO_FIX_WIDTH | CP0_FBO_FIX_HEIGHT));
    CHECK(o.pw == 90 && o.ph == 10 && o.lw == 90 && o.lh == 10);
    CHECK(cp0_fbo_setup(&o, mem, 128, 64, 2, 32, 2, 45, 0, 0, NULL) == CP0_FBO_FIX_ROTATE);
    CHECK(o.rot == 0);
    const cp0_fb_layout_t bad = {{11, 0}, {5, 6}, {0, 5}};
    CHECK(cp0_fbo_setup(&o, mem, 128, 64, 2, 32, 2, 0, 0, 0, &bad) == CP0_FBO_FIX_LAYOUT);
    CHECK(o.rgb565_plain);
    const cp0_fb_layout_t overlap = {{4, 5}, {5, 6}, {0, 5}};
    CHECK(cp0_fbo_setup(&o, mem, 128, 64, 2, 32, 2, 0, 0, 0, &overlap) == CP0_FBO_FIX_LAYOUT);
    CHECK(cp0_fbo_setup(&o, mem, 128, 64, 3, 21, 2, 0, 0, 0, NULL) == -1);
    CHECK(cp0_fbo_setup(&o, NULL, 128, 64, 2, 32, 2, 0, 0, 0, NULL) == -1);
    free(mem);

    int w = 0, h = 0, r = -1;
    CHECK(cp0_fbo_parse_size("480x320", &w, &h) == 0 && w == 480 && h == 320);
    CHECK(cp0_fbo_parse_size("640X480", &w, &h) == 0 && w == 640 && h == 480);
    CHECK(cp0_fbo_parse_size("480x", &w, &h) == -1);
    CHECK(cp0_fbo_parse_size("x320", &w, &h) == -1);
    CHECK(cp0_fbo_parse_size("0x320", &w, &h) == -1);
    CHECK(cp0_fbo_parse_size("480x320 ", &w, &h) == -1);
    CHECK(cp0_fbo_parse_size("480*320", &w, &h) == -1);
    CHECK(cp0_fbo_parse_rotation("90", &r) == 0 && r == 90);
    CHECK(cp0_fbo_parse_rotation("270", &r) == 0 && r == 270);
    CHECK(cp0_fbo_parse_rotation("0", &r) == 0 && r == 0);
    CHECK(cp0_fbo_parse_rotation("45", &r) == -1);
    CHECK(cp0_fbo_parse_rotation("", &r) == -1);
    CHECK(cp0_fbo_parse_rotation("90deg", &r) == -1);
}

static void touch_case(const cp0_fb_out_t *o, int rx, int ry, int min_x, int max_x, int min_y, int max_y, int ex,
                       int ey)
{
    int X = -1, Y = -1;
    cp0_fbo_touch_to_logical(o, rx, ry, min_x, max_x, min_y, max_y, &X, &Y);
    if (X != ex || Y != ey) fprintf(stderr, "touch raw (%d,%d) -> (%d,%d), expected (%d,%d)\n", rx, ry, X, Y, ex, ey);
    CHECK(X == ex && Y == ey);
}

static void test_touch(void)
{
    static uint8_t dummy[4];
    cp0_fb_out_t o;

    /* Pi 3A+: buffer 320x480, ABS 0..319 x 0..479 in buffer orientation, rotate 90 clockwise,
     * logical 480x320. The logical top-left is drawn at the buffer's top-right corner (319, 0), so a
     * finger there must report logical (0, 0). */
    CHECK(cp0_fbo_setup(&o, dummy, 0x7FFFFFFF, 640, 2, 320, 480, 90, 480, 320, NULL) == 0);
    touch_case(&o, 319, 0, 0, 319, 0, 479, 0, 0);       /* logical top-left */
    touch_case(&o, 319, 479, 0, 319, 0, 479, 479, 0);   /* logical top-right */
    touch_case(&o, 0, 0, 0, 319, 0, 479, 0, 319);       /* logical bottom-left */
    touch_case(&o, 0, 479, 0, 319, 0, 479, 479, 319);   /* logical bottom-right */
    touch_case(&o, 160, 240, 0, 319, 0, 479, 240, 159); /* centre */
    touch_case(&o, -20, 900, 0, 319, 0, 479, 479, 319); /* out of range: clamped */
    /* 1:1 device: every raw point lands on the buffer pixel under the finger */
    for (int ry = 0; ry < 480; ry += 7)
        for (int rx = 0; rx < 320; rx += 5) {
            int X, Y, bx, by;
            cp0_fbo_touch_to_logical(&o, rx, ry, 0, 319, 0, 479, &X, &Y);
            CHECK(X >= 0 && X < 480 && Y >= 0 && Y < 320);
            ref_map(90, 480, 320, X, Y, &bx, &by);
            CHECK(bx == rx && by == ry);
        }

    /* Pi 5 HyperPixel style: buffer 480x800, ABS 0..799 x 0..479 (normalised by the device's own
     * range, axes still follow the buffer), rotate 90, logical 800x480. */
    CHECK(cp0_fbo_setup(&o, dummy, 0x7FFFFFFF, 1920, 4, 480, 800, 90, 800, 480, NULL) == 0);
    touch_case(&o, 799, 0, 0, 799, 0, 479, 0, 0);
    touch_case(&o, 799, 479, 0, 799, 0, 479, 799, 0);
    touch_case(&o, 0, 0, 0, 799, 0, 479, 0, 479);
    touch_case(&o, 0, 479, 0, 799, 0, 479, 799, 479);
    touch_case(&o, 400, 240, 0, 799, 0, 479, 400, 239);
    /* the finger and the drawn pixel agree to within one device step */
    for (int ry = 0; ry < 480; ry += 11)
        for (int rx = 0; rx < 800; rx += 13) {
            int X, Y, bx, by;
            cp0_fbo_touch_to_logical(&o, rx, ry, 0, 799, 0, 479, &X, &Y);
            ref_map(90, 800, 480, X, Y, &bx, &by);
            const double fx = (rx + 0.5) * 480.0 / 800.0, fy = (ry + 0.5) * 800.0 / 480.0;
            CHECK(bx >= (int)fx - 1 && bx <= (int)fx + 1 && by >= (int)fy - 1 && by <= (int)fy + 1);
        }

    /* a non-zero ABS minimum */
    CHECK(cp0_fbo_setup(&o, dummy, 0x7FFFFFFF, 640, 2, 320, 480, 90, 480, 320, NULL) == 0);
    touch_case(&o, 100 + 319, 50, 100, 419, 50, 529, 0, 0);

    /* every rotation: buffer -> logical -> buffer is the identity, and agrees with the reference */
    for (int rot = 0; rot < 360; rot += 90) {
        const int swap = rot == 90 || rot == 270;
        const int lw = 13, lh = 7, pw = swap ? lh : lw, ph = swap ? lw : lh;
        CHECK(cp0_fbo_setup(&o, dummy, 0x7FFFFFFF, 64, 4, pw, ph, rot, lw, lh, NULL) == 0);
        for (int py = 0; py < ph; py++)
            for (int px = 0; px < pw; px++) {
                int X, Y, bx, by, rx, ry;
                cp0_fbo_buffer_to_logical(&o, px, py, &X, &Y);
                CHECK(X >= 0 && X < lw && Y >= 0 && Y < lh);
                cp0_fbo_logical_to_buffer(&o, X, Y, &bx, &by);
                ref_map(rot, lw, lh, X, Y, &rx, &ry);
                CHECK(bx == px && by == py && rx == px && ry == py);
                cp0_fbo_touch_to_logical(&o, px, py, 0, pw - 1, 0, ph - 1, &bx, &by);
                CHECK(bx == X && by == Y);
            }
    }
}

/* ------------------------------------------------------------ deck equivalence */

/* The previous dpi-scaled code at rotation 0 / 32 bpp, verbatim apart from the globals. */
typedef struct {
    uint8_t *fb;
    int stride, bpp, ox, oy, ww, wh, scale;
} old_ctx_t;

static void old_put_pixel(const old_ctx_t *g, int X, int Y, const uint8_t *src)
{
    memcpy(g->fb + (size_t)Y * g->stride + (size_t)X * g->bpp, src, g->bpp);
}

static void old_flush_native(const old_ctx_t *g, int ax1, int ay1, int w, int h, const uint8_t *px_map, int compat)
{
    for (int y = 0; y < h; y++) {
        const int Y = ay1 + y;
        const uint8_t *src = px_map + (size_t)y * w * g->bpp;
        int x0 = 0, x1 = w;
        if (compat && Y >= g->oy && Y < g->oy + g->wh) {
            const int wx0 = g->ox - ax1, wx1 = g->ox + g->ww - ax1;
            if (wx0 <= 0 && wx1 >= w) continue;
            if (wx0 <= 0) x0 = wx1 > w ? w : wx1;
            else if (wx1 >= w) x1 = wx0 < 0 ? 0 : wx0;
        }
        if (x1 > x0)
            memcpy(g->fb + (size_t)Y * g->stride + (size_t)(ax1 + x0) * g->bpp, src + (size_t)x0 * g->bpp,
                   (size_t)(x1 - x0) * g->bpp);
    }
}

static void old_flush_compat(const old_ctx_t *g, int ax1, int ay1, int w, int h, const uint8_t *px_map)
{
    for (int y = 0; y < h; y++) {
        const uint8_t *src = px_map + (size_t)y * w * g->bpp;
        for (int x = 0; x < w; x++, src += g->bpp) {
            const int bx = g->ox + (ax1 + x) * g->scale;
            const int by = g->oy + (ay1 + y) * g->scale;
            for (int dy = 0; dy < g->scale; dy++)
                for (int dx = 0; dx < g->scale; dx++) old_put_pixel(g, bx + dx, by + dy, src);
        }
    }
}

static void old_flush_overlay(const old_ctx_t *g, int ax1, int ay1, int ax2, int ay2, const uint8_t *px_map, int ovx,
                              int ovy, int ovw, int ovh)
{
    const int aw = ax2 - ax1 + 1;
    const int x1 = ax1 > ovx ? ax1 : ovx;
    const int y1 = ay1 > ovy ? ay1 : ovy;
    const int x2 = ax2 < ovx + ovw - 1 ? ax2 : ovx + ovw - 1;
    const int y2 = ay2 < ovy + ovh - 1 ? ay2 : ovy + ovh - 1;
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++) {
            const uint8_t *src = px_map + ((size_t)(y - ay1) * aw + (size_t)(x - ax1)) * g->bpp;
            const int bx = g->ox + x * g->scale, by = g->oy + y * g->scale;
            for (int dy = 0; dy < g->scale; dy++)
                for (int dx = 0; dx < g->scale; dx++) old_put_pixel(g, bx + dx, by + dy, src);
        }
}

static void old_external_fast(const old_ctx_t *g, const void *xrgb8888, int width, int height, int stride_bytes,
                              int ov_active, int ovx, int ovy, int ovw, int ovh)
{
    const int s = g->scale;
    uint32_t *row = malloc((size_t)width * s * 4);
    for (int y = 0; y < height; y++) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)xrgb8888 + (size_t)y * stride_bytes);
        for (int x = 0; x < width; x++)
            for (int dx = 0; dx < s; dx++) row[x * s + dx] = src[x];
        const int in_overlay_rows = ov_active && y >= ovy && y < ovy + ovh;
        for (int dy = 0; dy < s; dy++) {
            uint8_t *dst = g->fb + (size_t)(g->oy + y * s + dy) * g->stride + (size_t)g->ox * 4;
            if (!in_overlay_rows) {
                memcpy(dst, row, (size_t)width * s * 4);
                continue;
            }
            const int left = ovx * s;
            const int right = (ovx + ovw) * s;
            const int total = width * s;
            if (left > 0) memcpy(dst, row, (size_t)(left < total ? left : total) * 4);
            if (right < total) memcpy(dst + (size_t)right * 4, row + right, (size_t)(total - right) * 4);
        }
    }
    free(row);
}

static void test_deck_equivalence(void)
{
    const int lw = 640, lh = 480, bpp = 4, stride = 640 * 4, scale = 2;
    fbmem_t a, b;
    fb_alloc(&a, (size_t)stride * lh * 2); /* yres_virtual = 2 x yres */
    fb_alloc(&b, a.size);
    fb_copy(&b, &a);
    cp0_fb_out_t o;
    CHECK(cp0_fbo_setup(&o, a.base, a.size, stride, bpp, lw, lh, 0, 0, 0, NULL) == 0);
    old_ctx_t old = {b.base, stride, bpp, (lw - 320 * scale) / 2, 0, 320 * scale, 170 * scale, scale};
    const size_t scratch_size = (size_t)320 * scale * scale * 16 * bpp; /* as allocated by dpi-scaled */
    uint8_t *scratch = malloc(scratch_size);

    for (int it = 0; it < 150; it++) {
        /* native flush of a partial area (96-line draw buffer), compat window shown or not */
        const int w = rnd_range(1, lw), h = rnd_range(1, 96);
        const int x1 = rnd_range(0, lw - w), y1 = rnd_range(0, lh - h);
        const int compat = it % 2;
        uint8_t *px = malloc((size_t)w * h * bpp);
        fill_random(px, (size_t)w * h * bpp);
        if (compat) cp0_fbo_blit_except(&o, x1, y1, w, h, px, w * bpp, old.ox, old.oy, old.ww, old.wh);
        else cp0_fbo_blit(&o, x1, y1, w, h, px, w * bpp);
        old_flush_native(&old, x1, y1, w, h, px, compat);
        free(px);

        /* compat flush of a partial area of the 320x170 display */
        const int cw = rnd_range(1, 320), ch = rnd_range(1, 170);
        const int cx = rnd_range(0, 320 - cw), cy = rnd_range(0, 170 - ch);
        px = malloc((size_t)cw * ch * bpp);
        fill_random(px, (size_t)cw * ch * bpp);
        if (it % 3 == 0) {
            /* external app running: only the overlay (Esc ribbon) rectangle is drawn */
            const int ox = 20, oy = 4, ow = 280, oh = 22;
            const int ix1 = cx > ox ? cx : ox, iy1 = cy > oy ? cy : oy;
            const int ix2 = cx + cw - 1 < ox + ow - 1 ? cx + cw - 1 : ox + ow - 1;
            const int iy2 = cy + ch - 1 < oy + oh - 1 ? cy + ch - 1 : oy + oh - 1;
            if (ix2 >= ix1 && iy2 >= iy1)
                cp0_fbo_blit_scaled(&o, old.ox + ix1 * scale, old.oy + iy1 * scale, ix2 - ix1 + 1, iy2 - iy1 + 1,
                                    px + (size_t)(iy1 - cy) * cw * bpp + (size_t)(ix1 - cx) * bpp, cw * bpp, scale,
                                    scratch, scratch_size);
            old_flush_overlay(&old, cx, cy, cx + cw - 1, cy + ch - 1, px, ox, oy, ow, oh);
        } else {
            cp0_fbo_blit_scaled(&o, old.ox + cx * scale, old.oy + cy * scale, cw, ch, px, cw * bpp, scale, scratch,
                                scratch_size);
            old_flush_compat(&old, cx, cy, cw, ch, px);
        }
        free(px);

        /* the generic external path gives the same picture as the kept fast path */
        if (it % 10 == 0) {
            const int ew = 320, eh = 170, es = ew * 4;
            uint8_t *app = malloc((size_t)es * eh);
            fill_random(app, (size_t)es * eh);
            const int ov = it % 20 == 0;
            fbmem_t c;
            fb_alloc(&c, a.size);
            fb_copy(&c, &b);
            old_ctx_t oc = old;
            oc.fb = c.base;
            old_external_fast(&oc, app, ew, eh, es, ov, 20, 4, 280, 22);
            fbmem_t d;
            fb_alloc(&d, a.size);
            fb_copy(&d, &b);
            cp0_fb_out_t od = o;
            od.base = d.base;
            cp0_fbo_blit_xrgb_scaled(&od, old.ox, old.oy, ew, eh, app, es, scale, ov ? 20 : 0, ov ? 4 : 0,
                                     ov ? 280 : 0, ov ? 22 : 0, scratch, scratch_size);
            CHECK(memcmp(c.mem, d.mem, c.size + 2 * GUARD) == 0);
            fb_free(&c);
            fb_free(&d);
            free(app);
        }
    }
    CHECK(memcmp(a.mem, b.mem, a.size + 2 * GUARD) == 0);
    CHECK(guards_intact(&a));

    /* fill of the window = the previous per-pixel black fill */
    cp0_fbo_fill_black(&o, old.ox, old.oy, old.ww, old.wh);
    static const uint8_t black[4] = {0, 0, 0, 0};
    for (int Y = old.oy; Y < old.oy + old.wh; Y++)
        for (int X = old.ox; X < old.ox + old.ww; X++) old_put_pixel(&old, X, Y, black);
    CHECK(memcmp(a.mem, b.mem, a.size + 2 * GUARD) == 0);

    free(scratch);
    fb_free(&a);
    fb_free(&b);
}

/* Compat window placement: the deck keeps (0, 0) / (160, 0)-style top alignment, the 480x320 panel
 * gets 25 px above and 25 px between window and the 100 px toolbar. */
static void test_compat_origin(void)
{
    int x, y;
    cp0_fbo_compat_origin(640, 480, 640, 340, &x, &y); /* deck, 2x */
    CHECK(x == 0 && y == 0);
    cp0_fbo_compat_origin(640, 480, 320, 170, &x, &y); /* deck with compat scale 1 */
    CHECK(x == 160 && y == 0);
    cp0_fbo_compat_origin(480, 320, 320, 170, &x, &y); /* Pi 3A+, 1x */
    CHECK(x == 80 && y == 25);
    CHECK(y + 170 + 25 == 320 - 100);                   /* toolbar starts at y 220 */
    cp0_fbo_compat_origin(320, 170, 320, 170, &x, &y); /* degenerate: never negative */
    CHECK(x == 0 && y == 0);
    cp0_fbo_compat_origin(200, 100, 320, 170, &x, &y);
    CHECK(x == 0 && y == 0);
}

/* The Pi 3A+ geometry end to end: 480x320 logical, 1x compat window at (80, 25), 320x480 RGB565
 * buffer with line length 640: a red logical top-left pixel lands at buffer (319, 0). */
static void test_pi3a_geometry(void)
{
    fbmem_t f;
    fb_alloc(&f, 640 * 480);
    memset(f.base, 0, f.size);
    cp0_fb_out_t o;
    const cp0_fb_layout_t rgb = {{11, 5}, {5, 6}, {0, 5}};
    CHECK(cp0_fbo_setup(&o, f.base, f.size, 640, 2, 320, 480, 90, 480, 320, &rgb) == 0);
    const uint16_t red = 0xF800, green = 0x07E0;
    cp0_fbo_blit(&o, 0, 0, 1, 1, (const uint8_t *)&red, 2);
    cp0_fbo_blit(&o, 479, 319, 1, 1, (const uint8_t *)&green, 2);
    uint16_t v;
    memcpy(&v, f.base + 0 * 640 + 319 * 2, 2);
    CHECK(v == red);
    memcpy(&v, f.base + (size_t)479 * 640 + 0 * 2, 2);
    CHECK(v == green);
    /* external picture (XRGB) at 1x in the window at (80, 0): its top-left pixel goes to logical
     * (80, 25) = buffer (319-25, 80) */
    uint32_t app[2] = {0x00FF0000u, 0x000000FFu};
    static uint16_t scratch[320 * 16]; /* as allocated by dpi-scaled at 1x / 16 bpp */
    cp0_fbo_blit_xrgb_scaled(&o, 80, 25, 2, 1, (const uint8_t *)app, 8, 1, 0, 0, 0, 0, (uint8_t *)scratch,
                             sizeof(scratch));
    memcpy(&v, f.base + (size_t)80 * 640 + (319 - 25) * 2, 2);
    CHECK(v == 0xF800);
    memcpy(&v, f.base + (size_t)81 * 640 + (319 - 25) * 2, 2);
    CHECK(v == 0x001F);

    /* black fill and blit_except honour the y offset: paint everything green, blit a full-canvas
     * red block except the window (80, 25, 320, 170), then fill the window black */
    {
        enum { LW = 480, LH = 320 };
        static uint16_t all[LW * LH];
        for (int i = 0; i < LW * LH; i++) all[i] = 0xF800;
        for (int r = 0; r < 480; r++)
            for (int c = 0; c < 320; c++) memcpy(f.base + (size_t)r * 640 + c * 2, &green, 2);
        cp0_fbo_blit_except(&o, 0, 0, LW, LH, (const uint8_t *)all, LW * 2, 80, 25, 320, 170);
        int bx, by;
        for (int Y = 0; Y < LH; Y += 7)
            for (int X = 0; X < LW; X += 5) {
                const int inside = X >= 80 && X < 400 && Y >= 25 && Y < 195;
                cp0_fbo_logical_to_buffer(&o, X, Y, &bx, &by);
                memcpy(&v, f.base + (size_t)by * 640 + bx * 2, 2);
                CHECK(v == (inside ? green : red));
            }
        cp0_fbo_fill_black(&o, 80, 25, 320, 170);
        for (int Y = 0; Y < LH; Y += 7)
            for (int X = 0; X < LW; X += 5) {
                const int inside = X >= 80 && X < 400 && Y >= 25 && Y < 195;
                cp0_fbo_logical_to_buffer(&o, X, Y, &bx, &by);
                memcpy(&v, f.base + (size_t)by * 640 + bx * 2, 2);
                CHECK(v == (inside ? 0 : red));
            }
    }

    /* touch: a finger on the window's top-left pixel (logical (80, 25)) is window pixel (0, 0) at
     * scale 1; raw device values follow the buffer (x 0..319, y 0..479) */
    {
        int px, py, X, Y;
        cp0_fbo_logical_to_buffer(&o, 80, 25, &px, &py);
        cp0_fbo_touch_to_logical(&o, px, py, 0, 319, 0, 479, &X, &Y);
        CHECK(X == 80 && Y == 25);
        CHECK((X - 80) / 1 == 0 && (Y - 25) / 1 == 0);
        cp0_fbo_logical_to_buffer(&o, 399, 194, &px, &py);
        cp0_fbo_touch_to_logical(&o, px, py, 0, 319, 0, 479, &X, &Y);
        CHECK((X - 80) == 319 && (Y - 25) == 169);
    }
    CHECK(guards_intact(&f));
    fb_free(&f);
}

int main(void)
{
    test_blit_random();
    test_blit_except_random();
    test_blit_scaled_random();
    test_xrgb_scaled_random();
    test_fill_black();
    test_pack_and_formats();
    test_setup_and_parse();
    test_touch();
    test_deck_equivalence();
    test_compat_origin();
    test_pi3a_geometry();
    if (failures) {
        fprintf(stderr, "test_fb_output: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_fb_output: ok\n");
    return 0;
}
