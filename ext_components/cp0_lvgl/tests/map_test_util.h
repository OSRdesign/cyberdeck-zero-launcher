/*
 * SPDX-License-Identifier: MIT
 *
 * Helpers of the map engine's tests (tests/test_map_*.c): checks with a counter, a scratch folder, and writers for
 * the fixtures the tests generate themselves: PNG (stored deflate), gzip (stored or fixed-Huffman deflate), PMTiles v3
 * (none or gzip directories, leaf directories, run lengths) and MBTiles (through the engine's own SQLite loader).
 * Header-only (static functions); each test includes it once.
 */
#ifndef MAP_TEST_UTIL_H
#define MAP_TEST_UTIL_H

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* mkdtemp, setenv, strdup */
#endif
#include "cp0_map.h"
#include "cp0_map_internal.h"

#pragma GCC diagnostic ignored "-Wunused-function" /* each test uses part of the helpers */
#pragma GCC diagnostic ignored "-Wformat-truncation" /* scratch paths are short; a cut one fails its CHECK */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__SANITIZE_ADDRESS__)
#define MAP_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MAP_TEST_ASAN 1
#endif
#endif
#ifndef MAP_TEST_ASAN
#define MAP_TEST_ASAN 0
#endif

static long g_checks;

#define CHECK(cond)                                                                                                   \
    do {                                                                                                              \
        g_checks++;                                                                                                   \
        if (!(cond)) {                                                                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                                  \
            exit(1);                                                                                                  \
        }                                                                                                             \
    } while (0)

#define CHECK_RC(expr, want)                                                                                          \
    do {                                                                                                              \
        const int rc_ = (expr);                                                                                       \
        g_checks++;                                                                                                   \
        if (rc_ != (want)) {                                                                                          \
            fprintf(stderr, "%s:%d: %s = %d (%s), want %d (%s)\n", __FILE__, __LINE__, #expr, rc_,                    \
                    cp0_map_strerror(rc_), (int)(want), cp0_map_strerror(want));                                      \
            exit(1);                                                                                                  \
        }                                                                                                             \
    } while (0)

static char g_tmp[512];

static void tmp_init(const char *name)
{
    const char *t = getenv("TMPDIR");
    snprintf(g_tmp, sizeof g_tmp, "%s/%s-XXXXXX", t && t[0] ? t : "/tmp", name);
    if (!mkdtemp(g_tmp)) {
        perror("mkdtemp");
        exit(1);
    }
}

static void tmp_done(void)
{
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_tmp);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", g_tmp);
}

static void tpath(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void tpath(char *buf, size_t size, const char *fmt, ...)
{
    char rel[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(rel, sizeof rel, fmt, ap);
    va_end(ap);
    snprintf(buf, size, "%s/%s", g_tmp, rel);
}

static void mkdirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    mkdir(tmp, 0755);
}

static void write_file(const char *path, const void *data, size_t len)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdirs(dir);
    }
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(data, 1, len, f) == len);
    fclose(f);
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    CHECK(b != NULL);
    CHECK(fread(b, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *len = (size_t)n;
    return b;
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void copy_file_to(const char *from, const char *to)
{
    size_t n;
    uint8_t *b = read_file(from, &n);
    CHECK(b != NULL);
    write_file(to, b, n);
    free(b);
}

static uint8_t *fixture(const char *name, size_t *len)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", MAP_FIXTURES, name);
    uint8_t *b = read_file(p, len);
    if (!b) {
        fprintf(stderr, "missing fixture %s\n", p);
        exit(1);
    }
    return b;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* xorshift, fixed seeds: the same "random" data on every run */
static uint64_t g_rng = 0x9e3779b97f4a7c15ull;
static void rng_seed(uint64_t s) { g_rng = s ? s : 1; }
static uint32_t rng(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 16);
}

/* ------------------------------------------------------------------------------------------------ byte builder */

typedef struct {
    uint8_t *d;
    size_t n, cap;
} bb_t;

static void bb_put(bb_t *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap : 256;
        while (c < b->n + n) c *= 2;
        b->d = (uint8_t *)realloc(b->d, c);
        CHECK(b->d != NULL);
        b->cap = c;
    }
    if (n) memcpy(b->d + b->n, p, n);
    b->n += n;
}
static void bb_u8(bb_t *b, unsigned v) { uint8_t c = (uint8_t)v; bb_put(b, &c, 1); }
static void bb_be32(bb_t *b, uint32_t v)
{
    uint8_t c[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    bb_put(b, c, 4);
}
static void bb_le32(bb_t *b, uint32_t v)
{
    uint8_t c[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    bb_put(b, c, 4);
}
static void bb_le64(bb_t *b, uint64_t v)
{
    for (int i = 0; i < 8; i++) bb_u8(b, (unsigned)(v >> (8 * i)) & 0xff);
}
static void bb_varint(bb_t *b, uint64_t v)
{
    while (v >= 0x80) {
        bb_u8(b, (unsigned)(v & 0x7f) | 0x80);
        v >>= 7;
    }
    bb_u8(b, (unsigned)v);
}

/* ------------------------------------------------------------------------------------------------ crc, adler */

static uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t crc)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static uint32_t adler32_of(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

/* ----------------------------------------------------------------------------------------------------- deflate */

/* stored blocks */
static void deflate_stored(bb_t *o, const uint8_t *p, size_t n)
{
    size_t i = 0;
    do {
        const size_t k = n - i > 65535 ? 65535 : n - i;
        bb_u8(o, i + k == n ? 1 : 0);
        bb_u8(o, k & 0xff);
        bb_u8(o, k >> 8);
        bb_u8(o, (~k) & 0xff);
        bb_u8(o, ((~k) >> 8) & 0xff);
        bb_put(o, p + i, k);
        i += k;
    } while (i < n);
}

typedef struct {
    bb_t *o;
    uint32_t acc;
    int bits;
} bitw_t;

static void bw_put(bitw_t *w, uint32_t v, int n) /* LSB first */
{
    for (int i = 0; i < n; i++) {
        w->acc |= ((v >> i) & 1u) << w->bits;
        if (++w->bits == 8) {
            bb_u8(w->o, w->acc);
            w->acc = 0;
            w->bits = 0;
        }
    }
}

static void bw_code(bitw_t *w, uint32_t code, int n) /* Huffman codes go MSB first */
{
    for (int i = n - 1; i >= 0; i--) bw_put(w, (code >> i) & 1u, 1);
}

static void fixed_literal(bitw_t *w, unsigned lit)
{
    if (lit < 144)
        bw_code(w, 0x30 + lit, 8);
    else
        bw_code(w, 0x190 + (lit - 144), 9);
}

/* one fixed-Huffman block, literals only; `repeat_zero_matches` > 0 then appends that many (length 258,
 * distance 1) matches: a deflate bomb of 258 bytes per 13 bits */
static void deflate_fixed(bb_t *o, const uint8_t *p, size_t n, size_t repeat_zero_matches)
{
    bitw_t w = {o, 0, 0};
    bw_put(&w, 1, 1); /* BFINAL */
    bw_put(&w, 1, 2); /* BTYPE 01 */
    for (size_t i = 0; i < n; i++) fixed_literal(&w, p[i]);
    for (size_t i = 0; i < repeat_zero_matches; i++) {
        bw_code(&w, 0xc5, 8); /* length code 285 = 258 */
        bw_code(&w, 0, 5);    /* distance code 0 = 1 */
    }
    bw_code(&w, 0, 7); /* end of block */
    if (w.bits) bb_u8(o, w.acc);
}

/* gzip member: mode 0 stored, 1 fixed Huffman */
static void gzip_of(bb_t *o, const uint8_t *p, size_t n, int mode)
{
    static const uint8_t head[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3};
    bb_put(o, head, sizeof head);
    if (mode)
        deflate_fixed(o, p, n, 0);
    else
        deflate_stored(o, p, n);
    bb_le32(o, crc32_of(p, n, 0));
    bb_le32(o, (uint32_t)n);
}

/* --------------------------------------------------------------------------------------------------------- PNG */

static void png_chunk(bb_t *o, const char *type, const uint8_t *d, size_t n)
{
    bb_be32(o, (uint32_t)n);
    bb_t c = {0};
    bb_put(&c, type, 4);
    bb_put(&c, d, n);
    bb_put(o, c.d, c.n);
    bb_be32(o, crc32_of(c.d, c.n, 0));
    free(c.d);
}

/* a PNG of w x h pixels: ctype 2 (RGB, px 3 bytes each), 6 (RGBA), 0 (grey), 3 (palette: px are indices, pal npal
 * RGB triples). Filter 0, stored deflate (exact bytes known to the test). */
static void png_make(bb_t *o, const uint8_t *px, int w, int h, int ctype, const uint8_t *pal, int npal)
{
    const int bpp = ctype == 2 ? 3 : ctype == 6 ? 4 : 1;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    bb_put(o, sig, 8);
    uint8_t ihdr[13] = {0};
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16); ihdr[2] = (uint8_t)(w >> 8); ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16); ihdr[6] = (uint8_t)(h >> 8); ihdr[7] = (uint8_t)h;
    ihdr[8] = 8;
    ihdr[9] = (uint8_t)ctype;
    png_chunk(o, "IHDR", ihdr, 13);
    if (ctype == 3) png_chunk(o, "PLTE", pal, (size_t)npal * 3);
    bb_t raw = {0};
    for (int y = 0; y < h; y++) {
        bb_u8(&raw, 0);
        bb_put(&raw, px + (size_t)y * (size_t)w * (size_t)bpp, (size_t)w * (size_t)bpp);
    }
    bb_t z = {0};
    bb_u8(&z, 0x78);
    bb_u8(&z, 0x01);
    deflate_stored(&z, raw.d, raw.n);
    bb_be32(&z, adler32_of(raw.d, raw.n));
    png_chunk(o, "IDAT", z.d, z.n);
    png_chunk(o, "IEND", NULL, 0);
    free(raw.d);
    free(z.d);
}

/* a small RGB PNG whose pixels encode (z, x, y): (x*7 + y*3 + z) etc. */
static void png_tile_small(bb_t *o, int px, int z, uint32_t x, uint32_t y)
{
    uint8_t *rgb = (uint8_t *)malloc((size_t)px * px * 3);
    for (int j = 0; j < px; j++)
        for (int i = 0; i < px; i++) {
            uint8_t *q = rgb + ((size_t)j * px + i) * 3;
            q[0] = (uint8_t)(z * 16 + i);
            q[1] = (uint8_t)(x + j);
            q[2] = (uint8_t)(y * 3 + i + j);
        }
    png_make(o, rgb, px, px, 2, NULL, 0);
    free(rgb);
}

/* `base` (a PNG) with a tEXt chunk after IHDR naming z/x/y and padded to about `size` bytes: unique tile bytes of a
 * realistic size that decode like `base` */
static void png_with_text(bb_t *o, const uint8_t *base, size_t n, int z, uint32_t x, uint32_t y, size_t size)
{
    bb_put(o, base, 33); /* signature + IHDR */
    char text[64];
    const int tl = snprintf(text, sizeof text, "zxy%c%d/%u/%u ", 0, z, x, y);
    bb_t t = {0};
    bb_put(&t, text, (size_t)tl);
    while (o->n + 12 + t.n + (n - 33) < size) bb_u8(&t, 'a' + (unsigned)(t.n % 26));
    png_chunk(o, "tEXt", t.d, t.n);
    bb_put(o, base + 33, n - 33);
    free(t.d);
}

/* `base` (a JPEG) with a COM segment after SOI naming z/x/y */
static void jpeg_with_comment(bb_t *o, const uint8_t *base, size_t n, int z, uint32_t x, uint32_t y)
{
    bb_put(o, base, 2);
    char text[64];
    const int tl = snprintf(text, sizeof text, "zxy %d/%u/%u", z, x, y);
    bb_u8(o, 0xff);
    bb_u8(o, 0xfe);
    bb_u8(o, (unsigned)(tl + 2) >> 8);
    bb_u8(o, (unsigned)(tl + 2) & 0xff);
    bb_put(o, text, (size_t)tl);
    bb_put(o, base + 2, n - 2);
}

/* ------------------------------------------------------------------------------------------------------ PMTiles */

typedef struct {
    int z;
    uint32_t x, y;
    const uint8_t *data;
    size_t len;
    uint64_t id;
} pm_tile_t;

typedef struct {
    uint64_t id, off;
    uint32_t len, run;
} pm_ent_t;

static int cmp_pm_tile(const void *a, const void *b)
{
    const uint64_t x = ((const pm_tile_t *)a)->id, y = ((const pm_tile_t *)b)->id;
    return x < y ? -1 : x > y;
}

/* comp: 1 none, 2 gzip stored, 3 gzip fixed Huffman */
static void pm_dir(bb_t *o, const pm_ent_t *e, size_t n, int comp)
{
    bb_t raw = {0};
    bb_varint(&raw, n);
    uint64_t last = 0;
    for (size_t i = 0; i < n; i++) {
        bb_varint(&raw, e[i].id - last);
        last = e[i].id;
    }
    for (size_t i = 0; i < n; i++) bb_varint(&raw, e[i].run);
    for (size_t i = 0; i < n; i++) bb_varint(&raw, e[i].len);
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && e[i].off == e[i - 1].off + e[i - 1].len)
            bb_varint(&raw, 0);
        else
            bb_varint(&raw, e[i].off + 1);
    }
    if (comp == 1)
        bb_put(o, raw.d, raw.n);
    else
        gzip_of(o, raw.d, raw.n, comp == 3);
    free(raw.d);
}

/* writes a PMTiles v3 file. leaf_size 0: everything in the root. Identical consecutive tiles become run lengths. */
static void pm_write(const char *path, pm_tile_t *tiles, int n, int comp, int leaf_size, int tile_type,
                     const char *meta_json)
{
    for (int i = 0; i < n; i++) tiles[i].id = cp0_map__pmtiles_tile_id(tiles[i].z, tiles[i].x, tiles[i].y);
    qsort(tiles, (size_t)n, sizeof *tiles, cmp_pm_tile);
    bb_t data = {0};
    pm_ent_t *ent = (pm_ent_t *)calloc((size_t)n + 1, sizeof *ent);
    size_t ne = 0;
    int minz = 99, maxz = -1;
    for (int i = 0; i < n; i++) {
        if (tiles[i].z < minz) minz = tiles[i].z;
        if (tiles[i].z > maxz) maxz = tiles[i].z;
        if (ne > 0 && ent[ne - 1].id + ent[ne - 1].run == tiles[i].id && i > 0 && tiles[i - 1].len == tiles[i].len &&
            !memcmp(tiles[i - 1].data, tiles[i].data, tiles[i].len)) {
            ent[ne - 1].run++;
            continue;
        }
        ent[ne].id = tiles[i].id;
        ent[ne].off = data.n;
        ent[ne].len = (uint32_t)tiles[i].len;
        ent[ne].run = 1;
        bb_put(&data, tiles[i].data, tiles[i].len);
        ne++;
    }
    bb_t root = {0}, leaves = {0}, meta = {0};
    if (leaf_size > 0) {
        pm_ent_t *roots = (pm_ent_t *)calloc(ne / (size_t)leaf_size + 2, sizeof *roots);
        size_t nr = 0;
        for (size_t i = 0; i < ne; i += (size_t)leaf_size) {
            const size_t k = ne - i < (size_t)leaf_size ? ne - i : (size_t)leaf_size;
            const size_t before = leaves.n;
            pm_dir(&leaves, ent + i, k, comp);
            roots[nr].id = ent[i].id;
            roots[nr].off = before;
            roots[nr].len = (uint32_t)(leaves.n - before);
            roots[nr].run = 0;
            nr++;
        }
        pm_dir(&root, roots, nr, comp);
        free(roots);
    } else {
        pm_dir(&root, ent, ne, comp);
    }
    const char *mj = meta_json ? meta_json : "{}";
    if (comp == 1)
        bb_put(&meta, mj, strlen(mj));
    else
        gzip_of(&meta, (const uint8_t *)mj, strlen(mj), comp == 3);
    bb_t f = {0};
    bb_put(&f, "PMTiles", 7);
    bb_u8(&f, 3);
    const uint64_t root_off = 127, meta_off = root_off + root.n, leaf_off = meta_off + meta.n,
                   data_off = leaf_off + leaves.n;
    bb_le64(&f, root_off);
    bb_le64(&f, root.n);
    bb_le64(&f, meta_off);
    bb_le64(&f, meta.n);
    bb_le64(&f, leaf_off);
    bb_le64(&f, leaves.n);
    bb_le64(&f, data_off);
    bb_le64(&f, data.n);
    bb_le64(&f, (uint64_t)n);  /* addressed tiles */
    bb_le64(&f, ne);           /* tile entries */
    bb_le64(&f, ne);           /* tile contents */
    bb_u8(&f, 1);              /* clustered */
    bb_u8(&f, comp == 1 ? 1 : 2);
    bb_u8(&f, 1);              /* tile compression none */
    bb_u8(&f, (unsigned)tile_type);
    bb_u8(&f, (unsigned)(maxz < 0 ? 0 : minz));
    bb_u8(&f, (unsigned)(maxz < 0 ? 0 : maxz));
    bb_le32(&f, (uint32_t)(int32_t)-1800000000);
    bb_le32(&f, (uint32_t)(int32_t)-850511287);
    bb_le32(&f, (uint32_t)(int32_t)1800000000);
    bb_le32(&f, (uint32_t)(int32_t)850511287);
    bb_u8(&f, 0);
    bb_le32(&f, 0);
    bb_le32(&f, 0);
    CHECK(f.n == 127);
    bb_put(&f, root.d, root.n);
    bb_put(&f, meta.d, meta.n);
    bb_put(&f, leaves.d, leaves.n);
    bb_put(&f, data.d, data.n);
    write_file(path, f.d, f.n);
    free(f.d);
    free(root.d);
    free(leaves.d);
    free(meta.d);
    free(data.d);
    free(ent);
}

/* ------------------------------------------------------------------------------------------------------ MBTiles */

typedef struct {
    const cp0_map_sqlite_api_t *sql;
    sqlite3 *db;
} mbw_t;

static void mbw_exec(mbw_t *m, const char *q)
{
    char *err = NULL;
    if (m->sql->exec(m->db, q, NULL, NULL, &err) != CP0_MAP_SQLITE_OK) {
        fprintf(stderr, "sqlite: %s: %s\n", q, m->sql->errmsg(m->db));
        exit(1);
    }
}

static void mbw_open(mbw_t *m, const char *path)
{
    m->sql = cp0_map__sqlite();
    CHECK(m->sql != NULL);
    unlink(path);
    CHECK(m->sql->open_v2(path, &m->db, CP0_MAP_SQLITE_OPEN_READWRITE | CP0_MAP_SQLITE_OPEN_CREATE, NULL) == 0);
    mbw_exec(m, "CREATE TABLE metadata (name text, value text);"
                "CREATE TABLE tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob);"
                "CREATE UNIQUE INDEX tile_index ON tiles (zoom_level, tile_column, tile_row); BEGIN;");
}

static void mbw_meta(mbw_t *m, const char *k, const char *v)
{
    sqlite3_stmt *st;
    CHECK(m->sql->prepare_v2(m->db, "INSERT INTO metadata VALUES (?1, ?2)", -1, &st, NULL) == 0);
    m->sql->bind_text(st, 1, k, -1, CP0_MAP_SQLITE_TRANSIENT);
    m->sql->bind_text(st, 2, v, -1, CP0_MAP_SQLITE_TRANSIENT);
    CHECK(m->sql->step(st) == CP0_MAP_SQLITE_DONE);
    m->sql->finalize(st);
}

/* z/x/y in XYZ; stored with the TMS row */
static void mbw_tile(mbw_t *m, int z, uint32_t x, uint32_t y, const uint8_t *d, size_t n)
{
    sqlite3_stmt *st;
    CHECK(m->sql->prepare_v2(m->db, "INSERT INTO tiles VALUES (?1, ?2, ?3, ?4)", -1, &st, NULL) == 0);
    m->sql->bind_int(st, 1, z);
    m->sql->bind_int64(st, 2, x);
    m->sql->bind_int64(st, 3, cp0_map_tms_row(z, y));
    m->sql->bind_blob(st, 4, d, (int)n, CP0_MAP_SQLITE_TRANSIENT);
    CHECK(m->sql->step(st) == CP0_MAP_SQLITE_DONE);
    m->sql->finalize(st);
}

static void mbw_close(mbw_t *m)
{
    mbw_exec(m, "COMMIT;");
    CHECK(m->sql->close_v2(m->db) == 0);
    m->db = NULL;
}

/* ---------------------------------------------------------------------------------------------------- tile sets */

/* a set of distinct small PNG tiles z0..zmax (full pyramid); free with tiles_free */
typedef struct {
    pm_tile_t *t;
    bb_t *bytes;
    int n;
} tileset_t;

static void tiles_make(tileset_t *s, int zmax, int px)
{
    int n = 0;
    for (int z = 0; z <= zmax; z++) n += 1 << (2 * z);
    s->t = (pm_tile_t *)calloc((size_t)n, sizeof *s->t);
    s->bytes = (bb_t *)calloc((size_t)n, sizeof *s->bytes);
    s->n = 0;
    for (int z = 0; z <= zmax; z++)
        for (uint32_t x = 0; x < (1u << z); x++)
            for (uint32_t y = 0; y < (1u << z); y++) {
                bb_t *b = &s->bytes[s->n];
                png_tile_small(b, px, z, x, y);
                s->t[s->n].z = z;
                s->t[s->n].x = x;
                s->t[s->n].y = y;
                s->t[s->n].data = b->d;
                s->t[s->n].len = b->n;
                s->n++;
            }
}

static void tiles_free(tileset_t *s)
{
    for (int i = 0; i < s->n; i++) free(s->bytes[i].d);
    free(s->bytes);
    free(s->t);
    memset(s, 0, sizeof *s);
}

/* the tile of the set at z/x/y (or NULL) */
static const pm_tile_t *tiles_find(const tileset_t *s, int z, uint32_t x, uint32_t y)
{
    for (int i = 0; i < s->n; i++)
        if (s->t[i].z == z && s->t[i].x == x && s->t[i].y == y) return &s->t[i];
    return NULL;
}

static uint16_t rgb565_of(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3));
}

#endif /* MAP_TEST_UTIL_H */
