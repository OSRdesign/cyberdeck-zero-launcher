/* SPDX-License-Identifier: MIT
 * p0probe: throwaway measurements for the Deck Atlas P0 spike (launcher report 055). Not product code.
 *
 *   p0probe decode   FILE.pmtiles [--lib stb|lodepng] [--passes 3]           M1 decode time per tile
 *   p0probe view     FILE.pmtiles --w 640 --h 480 --scale 2 [--views 12] [--workers 2] [--cold 1]
 *                                                                              a full view: read + decode + 565 (+ magnify)
 *   p0probe read     --method pread|mmap|sqlite|sqlmmap FILE [--passes 2]     M2 read path per tile, cold and warm
 *   p0probe lru      FILE.pmtiles --slots 64 --bpp 2|3|4 [--scale 1|2] [--cycle 4000]   M3 RAM of a decoded-tile LRU
 *   p0probe convert  FOLDER OUT.mbtiles [--batch 256] [--max N] [--cold 1]     M8 folder -> MBTiles rate
 *   p0probe verify   FILE.pmtiles FILE.mbtiles                                 the two archives hold the same bytes
 *
 * The tile list is the generator's area (gen_tiles.py): zoom 0-14 over z14 x 8283..8321, y 5654..5692.
 * Output: lines starting with "RESULT" (key=value), times in ms. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../third_party/sqlite/sqlite-amalgamation-3500400/sqlite3.h"
#include "pmt.h"

unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req_comp);
void stbi_image_free(void *retval_from_stbi_load);
unsigned lodepng_decode24(unsigned char **out, unsigned *w, unsigned *h, const unsigned char *in, size_t insize);

#define ZMAX 14
#define AX0 8283
#define AX1 8321
#define AY0 5654
#define AY1 5692

/* ------------------------------------------------------------------ helpers */
static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static long status_kb(const char *key)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long v = -1;
    size_t kl = strlen(key);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == ':') { v = atol(line + kl + 1); break; }
    fclose(f);
    return v;
}

static long long io_field(const char *key)
{
    FILE *f = fopen("/proc/self/io", "r");
    if (!f) return -1;
    char line[256];
    long long v = -1;
    size_t kl = strlen(key);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == ':') { v = atoll(line + kl + 1); break; }
    fclose(f);
    return v;
}

static void print_mem(const char *tag)
{
    printf("RESULT mem tag=%s VmRSS_kb=%ld VmHWM_kb=%ld RssAnon_kb=%ld RssFile_kb=%ld\n", tag, status_kb("VmRSS"),
           status_kb("VmHWM"), status_kb("RssAnon"), status_kb("RssFile"));
}

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

typedef struct { double med, p95, mean, max; } stats_t;

static stats_t stats(double *v, int n)
{
    stats_t s = {0, 0, 0, 0};
    if (n <= 0) return s;
    double *c = (double *)malloc(sizeof(double) * n);
    memcpy(c, v, sizeof(double) * n);
    qsort(c, n, sizeof(double), cmpd);
    double sum = 0;
    for (int i = 0; i < n; i++) sum += c[i];
    s.med = c[n / 2];
    s.p95 = c[(int)(0.95 * (n - 1))];
    s.mean = sum / n;
    s.max = c[n - 1];
    free(c);
    return s;
}

static const char *arg(int argc, char **argv, const char *k, const char *def)
{
    for (int i = 0; i < argc - 1; i++) if (!strcmp(argv[i], k)) return argv[i + 1];
    return def;
}

static void drop_cache(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    fdatasync(fd);
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    close(fd);
}

typedef struct { int z; uint32_t x, y; } zxy_t;

static int tile_list(zxy_t **out)
{
    int cap = 4096, n = 0;
    zxy_t *v = (zxy_t *)malloc(sizeof(zxy_t) * cap);
    for (int z = 0; z <= ZMAX; z++) {
        int s = ZMAX - z;
        for (uint32_t x = AX0 >> s; x <= (uint32_t)(AX1 >> s); x++)
            for (uint32_t y = AY0 >> s; y <= (uint32_t)(AY1 >> s); y++) {
                if (n == cap) { cap *= 2; v = (zxy_t *)realloc(v, sizeof(zxy_t) * cap); }
                v[n].z = z; v[n].x = x; v[n].y = y; n++;
            }
    }
    *out = v;
    return n;
}

static uint64_t rng_state = 0x5555aaaa12345678ULL;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 11);
}

static void shuffle(zxy_t *v, int n)
{
    for (int i = n - 1; i > 0; i--) { int j = rnd() % (i + 1); zxy_t t = v[i]; v[i] = v[j]; v[j] = t; }
}

/* ------------------------------------------------------------------ pixel paths */
static void rgb_to_565(const uint8_t *s, uint16_t *d, int npx)
{
    for (int i = 0; i < npx; i++, s += 3)
        d[i] = (uint16_t)(((s[0] & 0xf8) << 8) | ((s[1] & 0xfc) << 3) | (s[2] >> 3));
}

static void rgb_to_8888(const uint8_t *s, uint32_t *d, int npx)
{
    for (int i = 0; i < npx; i++, s += 3) d[i] = 0xff000000u | ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
}

static void mag2_565(const uint16_t *s, int w, int h, uint16_t *d)
{
    for (int y = 0; y < h; y++) {
        uint16_t *r0 = d + (size_t)(2 * y) * (2 * w), *r1 = r0 + 2 * w;
        const uint16_t *sr = s + (size_t)y * w;
        for (int x = 0; x < w; x++) { uint16_t c = sr[x]; r0[2 * x] = r0[2 * x + 1] = c; }
        memcpy(r1, r0, sizeof(uint16_t) * 2 * w);
    }
}

/* bilinear magnify RGB888 by num/den into RGB565 (dw = w*num/den). Fixed point 16.16. */
static void magbil_rgb_565(const uint8_t *s, int w, int h, uint16_t *d, int dw, int dh)
{
    uint32_t fx = ((uint32_t)(w - 1) << 16) / (dw - 1), fy = ((uint32_t)(h - 1) << 16) / (dh - 1);
    for (int y = 0; y < dh; y++) {
        uint32_t sy = y * fy, y0 = sy >> 16, wy = (sy >> 8) & 0xff;
        if (y0 >= (uint32_t)h - 1) { y0 = h - 2; wy = 256; }
        const uint8_t *r0 = s + (size_t)y0 * w * 3, *r1 = r0 + (size_t)w * 3;
        for (int x = 0; x < dw; x++) {
            uint32_t sx = x * fx, x0 = sx >> 16, wx = (sx >> 8) & 0xff;
            if (x0 >= (uint32_t)w - 1) { x0 = w - 2; wx = 256; }
            const uint8_t *a = r0 + x0 * 3, *b = r1 + x0 * 3;
            uint32_t c[3];
            for (int k = 0; k < 3; k++) {
                uint32_t top = a[k] * (256 - wx) + a[k + 3] * wx, bot = b[k] * (256 - wx) + b[k + 3] * wx;
                c[k] = (top * (256 - wy) + bot * wy) >> 16;
            }
            d[(size_t)y * dw + x] = (uint16_t)(((c[0] & 0xf8) << 8) | ((c[1] & 0xfc) << 3) | (c[2] >> 3));
        }
    }
}

/* fast 1.5x: linear interpolation at the fixed phases 0, 2/3, 4/3 (every 2 source px give 3), RGB888 -> RGB565.
 * Rows are interpolated horizontally once into a 3-row ring, then mixed vertically with the same weights. */
static void hrow15(const uint8_t *s, int w, uint8_t *o)
{
    for (int j = 0; j < w / 2; j++) {
        const uint8_t *a = s + 6 * j, *b = a + 3, *c = (2 * j + 2 < w) ? a + 6 : b;
        uint8_t *d = o + 9 * j;
        for (int k = 0; k < 3; k++) {
            d[k] = a[k];
            d[3 + k] = (uint8_t)((a[k] * 85 + b[k] * 171) >> 8);
            d[6 + k] = (uint8_t)((b[k] * 171 + c[k] * 85) >> 8);
        }
    }
}

static void mag15_rgb_565(const uint8_t *s, int w, int h, uint16_t *d)
{
    int dw = w * 3 / 2;
    uint8_t *r0 = (uint8_t *)malloc((size_t)dw * 3 * 3), *r1 = r0 + dw * 3, *r2 = r1 + dw * 3;
    for (int k = 0; k < h / 2; k++) {
        hrow15(s + (size_t)(2 * k) * w * 3, w, r0);
        hrow15(s + (size_t)(2 * k + 1) * w * 3, w, r1);
        hrow15(s + (size_t)((2 * k + 2 < h) ? 2 * k + 2 : 2 * k + 1) * w * 3, w, r2);
        uint16_t *o0 = d + (size_t)(3 * k) * dw, *o1 = o0 + dw, *o2 = o1 + dw;
        for (int x = 0; x < dw; x++) {
            const uint8_t *a = r0 + 3 * x, *b = r1 + 3 * x, *c = r2 + 3 * x;
            uint32_t p1[3], p2[3];
            for (int q = 0; q < 3; q++) {
                p1[q] = (a[q] * 85 + b[q] * 171) >> 8;
                p2[q] = (b[q] * 171 + c[q] * 85) >> 8;
            }
            o0[x] = (uint16_t)(((a[0] & 0xf8) << 8) | ((a[1] & 0xfc) << 3) | (a[2] >> 3));
            o1[x] = (uint16_t)(((p1[0] & 0xf8) << 8) | ((p1[1] & 0xfc) << 3) | (p1[2] >> 3));
            o2[x] = (uint16_t)(((p2[0] & 0xf8) << 8) | ((p2[1] & 0xfc) << 3) | (p2[2] >> 3));
        }
    }
    free(r0);
}

static int g_lib_lode = 0;
static int g_mag_bil = 0;   /* view: 1 = generic bilinear for 1.5x instead of the fast 2->3 path */

static uint8_t *decode_rgb(const uint8_t *b, long n, int *w, int *h)
{
    if (g_lib_lode && n > 4 && b[0] == 0x89) {
        unsigned char *out = NULL; unsigned uw, uh;
        if (lodepng_decode24(&out, &uw, &uh, b, (size_t)n)) { free(out); return NULL; }
        *w = (int)uw; *h = (int)uh;
        return out;
    }
    int c;
    return stbi_load_from_memory(b, (int)n, w, h, &c, 3);
}

static void free_rgb(uint8_t *p) { if (g_lib_lode) free(p); else stbi_image_free(p); }

/* ------------------------------------------------------------------ decode (M1) */
static int cmd_decode(int argc, char **argv)
{
    const char *path = argv[2];
    g_lib_lode = !strcmp(arg(argc, argv, "--lib", "stb"), "lodepng");
    int passes = atoi(arg(argc, argv, "--passes", "3"));
    pmt_t p;
    if (pmt_open(&p, path, 0)) { fprintf(stderr, "open %s failed\n", path); return 1; }
    zxy_t *t; int n = tile_list(&t);
    int maxn = atoi(arg(argc, argv, "--max", "0"));
    if (maxn > 0 && maxn < n) {           /* an evenly spaced sample of the list (all zooms, mostly z14) */
        zxy_t *s = (zxy_t *)malloc(sizeof(zxy_t) * maxn);
        for (int i = 0; i < maxn; i++) s[i] = t[(long)i * n / maxn];
        t = s; n = maxn;
    }
    /* tiles are read (untimed) just before each decode: nothing big is held in RAM (the deck) */
    long *len = (long *)calloc(n, sizeof *len);
    uint8_t *tmp = (uint8_t *)malloc(4 << 20);
    long total = 0;
    for (int i = 0; i < n; i++) {
        uint64_t off;
        len[i] = pmt_find(&p, t[i].z, t[i].x, t[i].y, &off);
        if (len[i] <= 0) { fprintf(stderr, "missing tile %d/%u/%u\n", t[i].z, t[i].x, t[i].y); return 1; }
        total += len[i];
    }
    /* dense = the top quarter by encoded size (urban tiles) */
    long *sl = (long *)malloc(sizeof(long) * n);
    memcpy(sl, len, sizeof(long) * n);
    for (int i = 1; i < n; i++) { long v = sl[i]; int j = i - 1; while (j >= 0 && sl[j] > v) { sl[j + 1] = sl[j]; j--; } sl[j + 1] = v; }
    long dense_min = sl[(3 * n) / 4];
    double *first = (double *)malloc(sizeof(double) * n), *warm = (double *)malloc(sizeof(double) * n * passes);
    double *dense = (double *)malloc(sizeof(double) * n * passes), *c565 = (double *)malloc(sizeof(double) * n);
    int nw = 0, nd = 0, w0 = 0, h0 = 0;
    uint16_t *d565 = (uint16_t *)malloc(1024 * 1024 * 2);
    for (int pass = 0; pass < passes; pass++) {
        for (int i = 0; i < n; i++) {
            int w, h;
            if (pmt_get(&p, t[i].z, t[i].x, t[i].y, tmp, 4 << 20) != len[i]) return 1;
            double t0 = now_ms();
            uint8_t *rgb = decode_rgb(tmp, len[i], &w, &h);
            double t1 = now_ms();
            if (!rgb) { fprintf(stderr, "decode failed %d\n", i); return 1; }
            w0 = w; h0 = h;
            if (pass == 0) {
                first[i] = t1 - t0;
                double t2 = now_ms();
                rgb_to_565(rgb, d565, w * h);
                c565[i] = now_ms() - t2;
            } else {
                warm[nw++] = t1 - t0;
                if (len[i] >= dense_min) dense[nd++] = t1 - t0;
            }
            free_rgb(rgb);
        }
    }
    stats_t sf = stats(first, n), sw = stats(warm, nw), sd = stats(dense, nd), sc = stats(c565, n);
    printf("RESULT decode file=%s lib=%s px=%dx%d n=%d mean_kb=%.1f first_med=%.3f first_p95=%.3f warm_med=%.3f "
           "warm_p95=%.3f warm_mean=%.3f warm_max=%.3f dense_med=%.3f dense_p95=%.3f to565_med=%.3f\n",
           path, g_lib_lode ? "lodepng" : "stb", w0, h0, n, total / 1024.0 / n, sf.med, sf.p95, sw.med, sw.p95, sw.mean,
           sw.max, sd.med, sd.p95, sc.med);
    /* magnify costs on the dense tiles (warm) */
    if (w0 == 256) {
        double *m2 = (double *)malloc(sizeof(double) * n), *mb15 = (double *)malloc(sizeof(double) * n),
               *mb2 = (double *)malloc(sizeof(double) * n), *mf15 = (double *)malloc(sizeof(double) * n);
        uint16_t *src565 = (uint16_t *)malloc(256 * 256 * 2), *big = (uint16_t *)malloc(512 * 512 * 2);
        int k = 0;
        for (int i = 0; i < n && k < 200; i++) {
            if (len[i] < dense_min) continue;
            int w, h;
            if (pmt_get(&p, t[i].z, t[i].x, t[i].y, tmp, 4 << 20) != len[i]) return 1;
            uint8_t *rgb = decode_rgb(tmp, len[i], &w, &h);
            rgb_to_565(rgb, src565, w * h);
            double t0 = now_ms();
            mag2_565(src565, w, h, big);
            double t1 = now_ms();
            magbil_rgb_565(rgb, w, h, big, 384, 384);
            double t2 = now_ms();
            magbil_rgb_565(rgb, w, h, big, 512, 512);
            double t3 = now_ms();
            mag15_rgb_565(rgb, w, h, big);
            double t4 = now_ms();
            m2[k] = t1 - t0; mb15[k] = t2 - t1; mb2[k] = t3 - t2; mf15[k] = t4 - t3; k++;
            free_rgb(rgb);
        }
        stats_t a = stats(m2, k), b = stats(mb15, k), c = stats(mb2, k), f = stats(mf15, k);
        printf("RESULT magnify n=%d nearest2x_med=%.3f bilinear1.5x_med=%.3f bilinear2x_med=%.3f fast1.5x_med=%.3f\n", k,
               a.med, b.med, c.med, f.med);
    }
    print_mem("decode_end");
    pmt_close(&p);
    return 0;
}

/* ------------------------------------------------------------------ view: a full screen of tiles */
typedef struct {
    pmt_t *p;
    zxy_t *tiles;
    int n;
    int next;
    pthread_mutex_t mu;
    int scale_x2;           /* 2 = 1x, 3 = 1.5x, 4 = 2x */
    double busy_ms;
    int fail;
} view_job_t;

static void *view_worker(void *a)
{
    view_job_t *j = (view_job_t *)a;
    uint8_t *buf = (uint8_t *)malloc(4 << 20);
    uint16_t *s565 = (uint16_t *)malloc(512 * 512 * 2);
    uint16_t *out = (uint16_t *)malloc(1024 * 1024 * 2);
    for (;;) {
        pthread_mutex_lock(&j->mu);
        int i = j->next++;
        pthread_mutex_unlock(&j->mu);
        if (i >= j->n) break;
        long n = pmt_get(j->p, j->tiles[i].z, j->tiles[i].x, j->tiles[i].y, buf, 4 << 20);
        if (n <= 0) { j->fail = 1; continue; }
        int w, h, c;
        uint8_t *rgb = stbi_load_from_memory(buf, (int)n, &w, &h, &c, 3);
        if (!rgb) { j->fail = 1; continue; }
        if (j->scale_x2 == 2) rgb_to_565(rgb, out, w * h);
        else if (j->scale_x2 == 4) { rgb_to_565(rgb, s565, w * h); mag2_565(s565, w, h, out); }
        else if (j->scale_x2 == 3 && !g_mag_bil) mag15_rgb_565(rgb, w, h, out);
        else magbil_rgb_565(rgb, w, h, out, w * j->scale_x2 / 2, h * j->scale_x2 / 2);
        stbi_image_free(rgb);
    }
    free(buf); free(s565); free(out);
    return NULL;
}

static int cmd_view(int argc, char **argv)
{
    const char *path = argv[2];
    int W = atoi(arg(argc, argv, "--w", "640")), H = atoi(arg(argc, argv, "--h", "480"));
    double scale = atof(arg(argc, argv, "--scale", "1"));
    int views = atoi(arg(argc, argv, "--views", "12")), workers = atoi(arg(argc, argv, "--workers", "2"));
    int cold = atoi(arg(argc, argv, "--cold", "1"));
    int zoom = atoi(arg(argc, argv, "--zoom", "14"));
    g_mag_bil = !strcmp(arg(argc, argv, "--mag", "fast"), "bil");
    int scale_x2 = (int)(scale * 2 + 0.5);
    double *tv = (double *)malloc(sizeof(double) * views);
    int ntiles_sum = 0, tpx = 0;
    for (int v = 0; v < views; v++) {
        if (cold) drop_cache(path);
        pmt_t p;
        double t0 = now_ms();
        if (pmt_open(&p, path, 0)) { fprintf(stderr, "open failed\n"); return 1; }
        if (!tpx) {
            uint8_t *b = (uint8_t *)malloc(4 << 20);
            long n = pmt_get(&p, zoom, (AX0 + AX1) >> (ZMAX - zoom + 1), (AY0 + AY1) >> (ZMAX - zoom + 1), b, 4 << 20);
            int w, h, c;
            uint8_t *rgb = stbi_load_from_memory(b, (int)n, &w, &h, &c, 3);
            tpx = w; stbi_image_free(rgb); free(b);
            pmt_close(&p);
            if (cold) drop_cache(path);
            t0 = now_ms();
            pmt_open(&p, path, 0);
        }
        /* source pixels on screen, a pseudo-random centre inside the area at this zoom, unaligned */
        double sw = W / scale, sh = H / scale;
        int s = ZMAX - zoom;
        double ax0 = (double)(AX0 >> s) * tpx, ax1 = (double)((AX1 >> s) + 1) * tpx;
        double ay0 = (double)(AY0 >> s) * tpx, ay1 = (double)((AY1 >> s) + 1) * tpx;
        double cx = ax0 + sw / 2 + (rnd() % 10000) / 10000.0 * (ax1 - ax0 - sw);
        double cy = ay0 + sh / 2 + (rnd() % 10000) / 10000.0 * (ay1 - ay0 - sh);
        int tx0 = (int)((cx - sw / 2) / tpx), tx1 = (int)((cx + sw / 2 - 1) / tpx);
        int ty0 = (int)((cy - sh / 2) / tpx), ty1 = (int)((cy + sh / 2 - 1) / tpx);
        zxy_t list[256]; int nt = 0;
        for (int x = tx0; x <= tx1; x++) for (int y = ty0; y <= ty1; y++) { list[nt].z = zoom; list[nt].x = x; list[nt].y = y; nt++; }
        view_job_t j = {&p, list, nt, 0, PTHREAD_MUTEX_INITIALIZER, scale_x2, 0, 0};
        pthread_t th[8];
        for (int k = 0; k < workers; k++) pthread_create(&th[k], NULL, view_worker, &j);
        for (int k = 0; k < workers; k++) pthread_join(th[k], NULL);
        tv[v] = now_ms() - t0;
        ntiles_sum += nt;
        if (j.fail) fprintf(stderr, "view %d: a tile failed\n", v);
        pmt_close(&p);
    }
    stats_t st = stats(tv, views);
    printf("RESULT view file=%s screen=%dx%d scale=%.1f mag=%s zoom=%d tile=%d workers=%d cold=%d views=%d "
           "tiles_mean=%.1f med=%.1f p95=%.1f max=%.1f\n", path, W, H, scale, g_mag_bil ? "bil" : "fast", zoom, tpx,
           workers, cold, views,
           (double)ntiles_sum / views, st.med, st.p95, st.max);
    print_mem("view_end");
    return 0;
}

/* ------------------------------------------------------------------ read paths (M2) */
static int cmd_read(int argc, char **argv)
{
    const char *method = arg(argc, argv, "--method", "pread");
    const char *path = argv[argc - 1];
    int passes = atoi(arg(argc, argv, "--passes", "2"));
    zxy_t *t; int n = tile_list(&t);
    shuffle(t, n);
    drop_cache(path);
    print_mem("read_start");
    uint8_t *buf = (uint8_t *)malloc(4 << 20);
    pmt_t p; sqlite3 *db = NULL; sqlite3_stmt *st = NULL;
    int is_sql = !strncmp(method, "sql", 3);
    double t0 = now_ms();
    if (is_sql) {
        if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) { fprintf(stderr, "sqlite open\n"); return 1; }
        if (!strcmp(method, "sqlmmap")) sqlite3_exec(db, "PRAGMA mmap_size=268435456", NULL, NULL, NULL);
        if (sqlite3_prepare_v2(db, "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?", -1,
                               &st, NULL) != SQLITE_OK) { fprintf(stderr, "prepare: %s\n", sqlite3_errmsg(db)); return 1; }
    } else if (pmt_open(&p, path, !strcmp(method, "mmap"))) { fprintf(stderr, "pmt open\n"); return 1; }
    double open_ms = now_ms() - t0;
    double *tc = (double *)malloc(sizeof(double) * n), *tw = (double *)malloc(sizeof(double) * n * passes);
    int nw = 0; long long bytes = 0;
    for (int pass = 0; pass < passes; pass++) {
        for (int i = 0; i < n; i++) {
            double a = now_ms();
            long len;
            if (is_sql) {
                sqlite3_bind_int(st, 1, t[i].z);
                sqlite3_bind_int(st, 2, (int)t[i].x);
                sqlite3_bind_int(st, 3, (int)((1u << t[i].z) - 1 - t[i].y));
                len = -1;
                if (sqlite3_step(st) == SQLITE_ROW) {
                    len = sqlite3_column_bytes(st, 0);
                    memcpy(buf, sqlite3_column_blob(st, 0), (size_t)len);
                }
                sqlite3_reset(st);
            } else len = pmt_get(&p, t[i].z, t[i].x, t[i].y, buf, 4 << 20);
            double b = now_ms() - a;
            if (len <= 0) { fprintf(stderr, "missing %d/%u/%u\n", t[i].z, t[i].x, t[i].y); return 1; }
            if (pass == 0) { tc[i] = b; bytes += len; } else tw[nw++] = b;
        }
        if (pass == 0) print_mem("read_after_cold");
    }
    stats_t sc = stats(tc, n), sw = stats(tw, nw);
    printf("RESULT read method=%s file=%s n=%d open_ms=%.2f cold_med=%.3f cold_p95=%.3f cold_mean=%.3f cold_max=%.2f "
           "warm_med=%.4f warm_p95=%.4f warm_mean=%.4f mb=%.1f\n", method, path, n, open_ms, sc.med, sc.p95, sc.mean, sc.max,
           sw.med, sw.p95, sw.mean, bytes / 1e6);
    print_mem("read_end");
    if (is_sql) { sqlite3_finalize(st); sqlite3_close(db); } else pmt_close(&p);
    return 0;
}

/* ------------------------------------------------------------------ LRU RAM (M3) */
static int cmd_lru(int argc, char **argv)
{
    const char *path = argv[2];
    int slots = atoi(arg(argc, argv, "--slots", "64")), bpp = atoi(arg(argc, argv, "--bpp", "2"));
    int scale = atoi(arg(argc, argv, "--scale", "1")), cycle = atoi(arg(argc, argv, "--cycle", "4000"));
    pmt_t p;
    if (pmt_open(&p, path, 0)) return 1;
    zxy_t *t; int n = tile_list(&t);
    shuffle(t, n);
    print_mem("lru_start");
    uint8_t *buf = (uint8_t *)malloc(4 << 20);
    long first = pmt_get(&p, t[0].z, t[0].x, t[0].y, buf, 4 << 20);
    int w, h, c;
    uint8_t *rgb = stbi_load_from_memory(buf, (int)first, &w, &h, &c, 3);
    stbi_image_free(rgb);
    int dw = w * scale, dh = h * scale;
    size_t slot_bytes = (size_t)dw * dh * bpp;
    uint8_t **slot = (uint8_t **)calloc(slots, sizeof *slot);
    for (int i = 0; i < slots; i++) slot[i] = (uint8_t *)malloc(slot_bytes);
    print_mem("lru_alloc_untouched");
    uint16_t *s565 = (uint16_t *)malloc((size_t)w * h * 2);
    double tdec = 0;
    for (int k = 0; k < cycle; k++) {
        zxy_t *z = &t[k % n];
        long len = pmt_get(&p, z->z, z->x, z->y, buf, 4 << 20);
        double a = now_ms();
        rgb = stbi_load_from_memory(buf, (int)len, &w, &h, &c, 3);
        uint8_t *d = slot[k % slots];
        if (bpp == 2 && scale == 1) rgb_to_565(rgb, (uint16_t *)d, w * h);
        else if (bpp == 2) { rgb_to_565(rgb, s565, w * h); mag2_565(s565, w, h, (uint16_t *)d); }
        else if (bpp == 4) rgb_to_8888(rgb, (uint32_t *)d, w * h);   /* scale 1 only */
        else memcpy(d, rgb, (size_t)w * h * 3);
        stbi_image_free(rgb);
        tdec += now_ms() - a;
        if (k == slots - 1) print_mem("lru_filled");
    }
    print_mem("lru_cycled");
    malloc_trim(0);
    print_mem("lru_trimmed");
    printf("RESULT lru slots=%d bpp=%d scale=%d slot_kb=%zu cache_mb=%.1f cycle=%d decode_ms_mean=%.3f VmRSS_kb=%ld "
           "VmHWM_kb=%ld\n", slots, bpp, scale, slot_bytes / 1024, slots * slot_bytes / 1048576.0, cycle, tdec / cycle,
           status_kb("VmRSS"), status_kb("VmHWM"));
    pmt_close(&p);
    return 0;
}

/* ------------------------------------------------------------------ folder -> MBTiles (M8) */
typedef struct { char *path; int z, x, y; } ftile_t;

static int cmpft(const void *a, const void *b)
{
    const ftile_t *x = (const ftile_t *)a, *y = (const ftile_t *)b;
    if (x->z != y->z) return x->z - y->z;
    if (x->x != y->x) return x->x - y->x;
    return x->y - y->y;
}

static int isnum(const char *s) { if (!*s) return 0; for (; *s; s++) if (*s < '0' || *s > '9') return 0; return 1; }

static int cmd_convert(int argc, char **argv)
{
    const char *dir = argv[2], *out = argv[3];
    int batch = atoi(arg(argc, argv, "--batch", "256")), maxn = atoi(arg(argc, argv, "--max", "0"));
    int cold = atoi(arg(argc, argv, "--cold", "1"));
    const char *journal = arg(argc, argv, "--journal", "WAL");
    double t_scan0 = now_ms();
    int cap = 4096, n = 0;
    ftile_t *v = (ftile_t *)malloc(sizeof(ftile_t) * cap);
    char p1[1024], p2[2048], p3[4096];
    DIR *dz = opendir(dir);
    if (!dz) { fprintf(stderr, "opendir %s\n", dir); return 1; }
    struct dirent *ez;
    while ((ez = readdir(dz))) {
        if (!isnum(ez->d_name)) continue;
        snprintf(p1, sizeof p1, "%s/%s", dir, ez->d_name);
        DIR *dx = opendir(p1);
        if (!dx) continue;
        struct dirent *ex;
        while ((ex = readdir(dx))) {
            if (!isnum(ex->d_name)) continue;
            snprintf(p2, sizeof p2, "%s/%s", p1, ex->d_name);
            DIR *dy = opendir(p2);
            if (!dy) continue;
            struct dirent *ey;
            while ((ey = readdir(dy))) {
                if (ey->d_name[0] == '.') continue;
                int y = atoi(ey->d_name);
                snprintf(p3, sizeof p3, "%s/%s", p2, ey->d_name);
                if (n == cap) { cap *= 2; v = (ftile_t *)realloc(v, sizeof(ftile_t) * cap); }
                v[n].path = strdup(p3); v[n].z = atoi(ez->d_name); v[n].x = atoi(ex->d_name); v[n].y = y; n++;
            }
            closedir(dy);
        }
        closedir(dx);
    }
    closedir(dz);
    qsort(v, n, sizeof *v, cmpft);
    if (maxn > 0 && maxn < n) n = maxn;
    double scan_ms = now_ms() - t_scan0;
    if (cold) { sync(); for (int i = 0; i < n; i++) drop_cache(v[i].path); }
    unlink(out);
    char wal[4200]; snprintf(wal, sizeof wal, "%s-wal", out); unlink(wal);
    char shm[4200]; snprintf(shm, sizeof shm, "%s-shm", out); unlink(shm);
    long long wb0 = io_field("write_bytes"), rb0 = io_field("read_bytes");
    double t0 = now_ms();
    sqlite3 *db;
    if (sqlite3_open(out, &db) != SQLITE_OK) return 1;
    char sql[256];
    snprintf(sql, sizeof sql, "PRAGMA journal_mode=%s; PRAGMA synchronous=NORMAL;", journal);
    sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_exec(db, "CREATE TABLE metadata (name text, value text);"
                     "CREATE TABLE tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob);"
                     "CREATE UNIQUE INDEX tile_index on tiles (zoom_level, tile_column, tile_row);"
                     "INSERT INTO metadata VALUES ('name','converted'),('format','png'),('minzoom','0'),('maxzoom','14');",
                 NULL, NULL, NULL);
    sqlite3_stmt *ins;
    sqlite3_prepare_v2(db, "INSERT INTO tiles VALUES (?,?,?,?)", -1, &ins, NULL);
    uint8_t *buf = (uint8_t *)malloc(4 << 20);
    long long bytes = 0;
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    for (int i = 0; i < n; i++) {
        int fd = open(v[i].path, O_RDONLY);
        if (fd < 0) continue;
        ssize_t len = read(fd, buf, 4 << 20);
        close(fd);
        if (len <= 0) continue;
        bytes += len;
        sqlite3_bind_int(ins, 1, v[i].z);
        sqlite3_bind_int(ins, 2, v[i].x);
        sqlite3_bind_int(ins, 3, (1 << v[i].z) - 1 - v[i].y);
        sqlite3_bind_blob(ins, 4, buf, (int)len, SQLITE_STATIC);
        if (sqlite3_step(ins) != SQLITE_DONE) { fprintf(stderr, "insert: %s\n", sqlite3_errmsg(db)); return 1; }
        sqlite3_reset(ins);
        if ((i + 1) % batch == 0) { sqlite3_exec(db, "COMMIT", NULL, NULL, NULL); sqlite3_exec(db, "BEGIN", NULL, NULL, NULL); }
    }
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    sqlite3_finalize(ins);
    sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE)", NULL, NULL, NULL);
    sqlite3_close(db);
    int fd = open(out, O_RDONLY); if (fd >= 0) { fsync(fd); close(fd); }
    double dt = now_ms() - t0;
    long long wb = io_field("write_bytes") - wb0, rb = io_field("read_bytes") - rb0;
    struct stat st; stat(out, &st);
    printf("RESULT convert dir=%s n=%d batch=%d journal=%s cold=%d scan_ms=%.0f ms=%.0f tiles_s=%.1f in_mb=%.1f "
           "out_mb=%.1f write_mb=%.1f read_mb=%.1f\n", dir, n, batch, journal, cold, scan_ms, dt, n / (dt / 1000.0),
           bytes / 1e6, st.st_size / 1e6, wb / 1e6, rb / 1e6);
    print_mem("convert_end");
    return 0;
}

/* ------------------------------------------------------------------ verify */
static int cmd_verify(int argc, char **argv)
{
    (void)argc;
    pmt_t p;
    if (pmt_open(&p, argv[2], 0)) { fprintf(stderr, "pmt open\n"); return 1; }
    sqlite3 *db; sqlite3_stmt *st;
    if (sqlite3_open_v2(argv[3], &db, SQLITE_OPEN_READONLY, NULL)) return 1;
    sqlite3_prepare_v2(db, "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?", -1, &st, NULL);
    zxy_t *t; int n = tile_list(&t), bad = 0;
    uint8_t *buf = (uint8_t *)malloc(4 << 20);
    for (int i = 0; i < n; i++) {
        long len = pmt_get(&p, t[i].z, t[i].x, t[i].y, buf, 4 << 20);
        sqlite3_bind_int(st, 1, t[i].z); sqlite3_bind_int(st, 2, (int)t[i].x);
        sqlite3_bind_int(st, 3, (int)((1u << t[i].z) - 1 - t[i].y));
        int ok = sqlite3_step(st) == SQLITE_ROW && sqlite3_column_bytes(st, 0) == len &&
                 !memcmp(sqlite3_column_blob(st, 0), buf, (size_t)len);
        sqlite3_reset(st);
        if (!ok) bad++;
    }
    printf("RESULT verify n=%d bad=%d entries=%llu root_len=%llu leaf_len=%llu\n", n, bad,
           (unsigned long long)p.n_entries, (unsigned long long)p.root_len, (unsigned long long)p.leaf_len);
    return bad != 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: see the header of p0probe.c\n"); return 2; }
    const char *c = argv[1];
    if (!strcmp(c, "decode")) return cmd_decode(argc, argv);
    if (!strcmp(c, "view")) return cmd_view(argc, argv);
    if (!strcmp(c, "read")) return cmd_read(argc, argv);
    if (!strcmp(c, "lru")) return cmd_lru(argc, argv);
    if (!strcmp(c, "convert")) return cmd_convert(argc, argv);
    if (!strcmp(c, "verify")) return cmd_verify(argc, argv);
    fprintf(stderr, "unknown command %s\n", c);
    return 2;
}
