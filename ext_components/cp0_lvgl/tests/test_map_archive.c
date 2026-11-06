/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine, archive readers (cp0_map_archive.h):
 *   - MBTiles written here through the engine's SQLite loader: metadata, TMS rows read back as XYZ, every tile byte for
 *     byte, the size from a sample (256, 512), the format from a sample when not declared, zooms computed when absent;
 *     mbutil's view layout (fixture ref_view.mbtiles, Python's sqlite3);
 *   - PMTiles written here: no compression, gzip stored, gzip fixed Huffman, leaf directories, run lengths, PNG / JPEG
 *     / MVT tile types; the reference writer's files (fixtures ref_gzip.pmtiles, ref_leaves.pmtiles: zlib-compressed
 *     directories and metadata, run lengths): name, attribution with escapes, bounds, every tile decoded;
 *   - the PMTiles tile id (Hilbert) on the specification's examples;
 *   - refusals: no file, a folder, text, empty, PMTiles v2, brotli directories, truncation at every 7th length,
 *     3000 mutated PMTiles and 300 mutated MBTiles (defined errors, no crash);
 *   - "archive missing": the file removed while open (both kinds), retried and reopened when it is back; a file
 *     replaced under the same path is reopened with its new tiles.
 */
#include "map_test_util.h"

static void check_all_tiles(cp0_map_archive_t *a, const tileset_t *s)
{
    cp0_map_buf_t b = {0};
    for (int i = 0; i < s->n; i++) {
        cp0_map_format_t f;
        CHECK_RC(cp0_map_archive_get_tile(a, s->t[i].z, s->t[i].x, s->t[i].y, &b, &f), CP0_MAP_OK);
        CHECK(b.len == s->t[i].len && !memcmp(b.data, s->t[i].data, b.len) && f == CP0_MAP_FMT_PNG);
    }
    cp0_map_buf_free(&b);
}

static void write_mbtiles(const char *path, const tileset_t *s, int with_meta)
{
    mbw_t m;
    mbw_open(&m, path);
    if (with_meta) {
        mbw_meta(&m, "name", "Test MB");
        mbw_meta(&m, "format", "png");
        mbw_meta(&m, "bounds", "-10.5,40,20,55.25");
        mbw_meta(&m, "minzoom", "0");
        mbw_meta(&m, "maxzoom", "3");
        mbw_meta(&m, "attribution", "\xc2\xa9 Test contributors");
    }
    for (int i = 0; i < s->n; i++) mbw_tile(&m, s->t[i].z, s->t[i].x, s->t[i].y, s->t[i].data, s->t[i].len);
    mbw_close(&m);
}

static void test_mbtiles(void)
{
    tileset_t s;
    tiles_make(&s, 3, 256);
    char p[600];
    tpath(p, sizeof p, "a.mbtiles");
    write_mbtiles(p, &s, 1);
    cp0_map_archive_kind_t kind;
    CHECK_RC(cp0_map_archive_sniff(p, &kind), CP0_MAP_OK);
    CHECK(kind == CP0_MAP_ARCHIVE_MBTILES);
    cp0_map_archive_t *a;
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_archive_info_t i;
    CHECK_RC(cp0_map_archive_info(a, &i), CP0_MAP_OK);
    CHECK(i.kind == CP0_MAP_ARCHIVE_MBTILES && i.format == CP0_MAP_FMT_PNG && i.tile_px == 256);
    CHECK(i.minzoom == 0 && i.maxzoom == 3 && i.has_bounds && i.bounds.west == -10.5 && i.bounds.north == 55.25);
    CHECK(!strcmp(i.name, "Test MB") && !strcmp(i.attribution, "\xc2\xa9 Test contributors") && i.bytes > 0);
    CHECK(!strcmp(cp0_map_archive_path(a), p));
    check_all_tiles(a, &s);
    /* TMS: the row stored for z3 y0 is 7 */
    cp0_map_buf_t b = {0};
    const pm_tile_t *t = tiles_find(&s, 3, 2, 0);
    CHECK_RC(cp0_map_archive_get_tile(a, 3, 2, 0, &b, NULL), CP0_MAP_OK);
    CHECK(b.len == t->len && !memcmp(b.data, t->data, b.len));
    CHECK_RC(cp0_map_archive_get_tile(a, 4, 0, 0, &b, NULL), CP0_MAP_E_NO_TILE);
    CHECK_RC(cp0_map_archive_get_tile(a, 2, 4, 0, &b, NULL), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_archive_get_tile(a, 31, 0, 0, &b, NULL), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_archive_check(a), CP0_MAP_OK);
    cp0_map_archive_close(a);
    tiles_free(&s);

    /* no metadata at all: zooms from the tiles, format and size from a sample (512 px JPEG) */
    size_t jn;
    uint8_t *jpg = fixture("q75_512.jpg", &jn);
    mbw_t m;
    tpath(p, sizeof p, "b.mbtiles");
    mbw_open(&m, p);
    for (uint32_t x = 0; x < 4; x++) {
        bb_t one = {0};
        jpeg_with_comment(&one, jpg, jn, 2, x, 1);
        mbw_tile(&m, 2, x, 1, one.d, one.n);
        free(one.d);
    }
    mbw_tile(&m, 1, 0, 0, jpg, jn);
    mbw_close(&m);
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_archive_info(a, &i);
    CHECK(i.format == CP0_MAP_FMT_JPEG && i.tile_px == 512 && i.minzoom == 1 && i.maxzoom == 2 && !i.has_bounds);
    cp0_map_format_t f;
    CHECK_RC(cp0_map_archive_get_tile(a, 1, 0, 0, &b, &f), CP0_MAP_OK);
    CHECK(f == CP0_MAP_FMT_JPEG && b.len == jn);
    cp0_map_archive_close(a);
    free(jpg);

    /* mbutil's layout (a view over map + images), written by Python's sqlite3 */
    char fx[600];
    snprintf(fx, sizeof fx, "%s/ref_view.mbtiles", MAP_FIXTURES);
    CHECK_RC(cp0_map_archive_open(fx, &a), CP0_MAP_OK);
    cp0_map_archive_info(a, &i);
    CHECK(!strcmp(i.name, "Reference view") && i.minzoom == 0 && i.maxzoom == 2 && i.tile_px == 256);
    CHECK(!strcmp(i.attribution, "\xc2\xa9 Test view") && i.has_bounds && i.bounds.east == 180);
    for (int z = 0; z <= 2; z++)
        for (uint32_t x = 0; x < (1u << z); x++)
            for (uint32_t y = 0; y < (1u << z); y++) {
                CHECK_RC(cp0_map_archive_get_tile(a, z, x, y, &b, NULL), CP0_MAP_OK);
                cp0_map_image_t *img;
                CHECK_RC(cp0_map_decode_tile(b.data, b.len, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_OK);
                uint32_t cx = x, cy = y;
                if (z == 2 && (x || y)) cx = cy = 1;
                CHECK(((uint16_t *)img->px)[1000] == rgb565_of(40u * (unsigned)z + 20, (cx * 50 + 10) & 255, (cy * 50 + 30) & 255));
                cp0_map_image_free(img);
            }
    cp0_map_archive_close(a);
    cp0_map_buf_free(&b);
    printf("map archive: MBTiles (metadata, TMS rows, every tile, sampled size / format, mbutil view layout)\n");
}

static const char *META = "{\"name\": \"Gen \\\"pm\\\"\", \"attribution\": \"\\u00a9 Gen\", \"vector_layers\": []}";

static void test_pmtiles_generated(void)
{
    tileset_t s;
    tiles_make(&s, 4, 16);
    const struct {
        int comp, leaf;
        const char *what;
    } cases[] = {{1, 0, "no compression"}, {2, 0, "gzip stored"}, {3, 0, "gzip fixed Huffman"},
                 {2, 5, "gzip, leaves of 5"}, {1, 1, "leaves of 1"}, {3, 64, "fixed Huffman, leaves of 64"}};
    char p[600];
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        tpath(p, sizeof p, "g%zu.pmtiles", c);
        pm_write(p, s.t, s.n, cases[c].comp, cases[c].leaf, 2, META);
        cp0_map_archive_t *a;
        CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
        cp0_map_archive_info_t i;
        cp0_map_archive_info(a, &i);
        CHECK(i.kind == CP0_MAP_ARCHIVE_PMTILES && i.format == CP0_MAP_FMT_PNG && i.tile_px == 16);
        CHECK(i.minzoom == 0 && i.maxzoom == 4 && i.has_bounds && i.bounds.west == -180);
        CHECK(!strcmp(i.name, "Gen \"pm\"") && !strcmp(i.attribution, "\xc2\xa9 Gen"));
        check_all_tiles(a, &s);
        cp0_map_buf_t b = {0};
        CHECK_RC(cp0_map_archive_get_tile(a, 5, 0, 0, &b, NULL), CP0_MAP_E_NO_TILE);
        CHECK_RC(cp0_map_archive_get_tile(a, 4, 16, 0, &b, NULL), CP0_MAP_E_INVALID);
        cp0_map_buf_free(&b);
        cp0_map_archive_close(a);
    }
    /* run lengths: a sparse z3 where many neighbours share one picture */
    bb_t same = {0}, other = {0};
    png_tile_small(&same, 16, 9, 9, 9);
    png_tile_small(&other, 16, 1, 1, 1);
    pm_tile_t t[64];
    int n = 0;
    for (uint32_t x = 0; x < 8; x++)
        for (uint32_t y = 0; y < 8; y++) {
            if ((x + y) % 5 == 4) continue; /* holes */
            t[n].z = 3;
            t[n].x = x;
            t[n].y = y;
            t[n].data = (x == 0 && y == 0) ? other.d : same.d;
            t[n].len = (x == 0 && y == 0) ? other.n : same.n;
            n++;
        }
    tpath(p, sizeof p, "runs.pmtiles");
    pm_write(p, t, n, 2, 0, 2, NULL);
    cp0_map_archive_t *a;
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_buf_t b = {0};
    for (uint32_t x = 0; x < 8; x++)
        for (uint32_t y = 0; y < 8; y++) {
            const int rc = cp0_map_archive_get_tile(a, 3, x, y, &b, NULL);
            if ((x + y) % 5 == 4) {
                CHECK_RC(rc, CP0_MAP_E_NO_TILE);
            } else {
                CHECK_RC(rc, CP0_MAP_OK);
                const bb_t *want = (x == 0 && y == 0) ? &other : &same;
                CHECK(b.len == want->n && !memcmp(b.data, want->d, b.len));
            }
        }
    cp0_map_archive_close(a);
    /* tile types: JPEG (512) and MVT are reported */
    size_t jn;
    uint8_t *jpg = fixture("q75_512.jpg", &jn);
    pm_tile_t jt = {2, 1, 1, jpg, jn, 0};
    tpath(p, sizeof p, "jpg.pmtiles");
    pm_write(p, &jt, 1, 2, 0, 3, NULL);
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_archive_info_t i;
    cp0_map_archive_info(a, &i);
    CHECK(i.format == CP0_MAP_FMT_JPEG && i.tile_px == 512 && i.minzoom == 2 && !i.name[0]);
    cp0_map_archive_close(a);
    free(jpg);
    static const uint8_t mvt[6] = {0x1a, 4, 'r', 'o', 'a', 'd'};
    pm_tile_t mt = {0, 0, 0, mvt, sizeof mvt, 0};
    tpath(p, sizeof p, "mvt.pmtiles");
    pm_write(p, &mt, 1, 1, 0, 1, NULL);
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_archive_info(a, &i);
    CHECK(i.format == CP0_MAP_FMT_MVT && i.tile_px == 0);
    cp0_map_format_t f;
    CHECK_RC(cp0_map_archive_get_tile(a, 0, 0, 0, &b, &f), CP0_MAP_OK);
    CHECK(f == CP0_MAP_FMT_MVT && b.len == sizeof mvt);
    cp0_map_archive_close(a);
    cp0_map_buf_free(&b);
    free(same.d);
    free(other.d);
    tiles_free(&s);
    printf("map archive: PMTiles written here (none / gzip stored / gzip fixed, leaves, run lengths, holes, "
           "JPEG 512, MVT reported)\n");
}

static void test_pmtiles_reference(void)
{
    /* the specification's examples */
    CHECK(cp0_map__pmtiles_tile_id(0, 0, 0) == 0);
    CHECK(cp0_map__pmtiles_tile_id(1, 0, 0) == 1 && cp0_map__pmtiles_tile_id(1, 0, 1) == 2);
    CHECK(cp0_map__pmtiles_tile_id(1, 1, 1) == 3 && cp0_map__pmtiles_tile_id(1, 1, 0) == 4);
    CHECK(cp0_map__pmtiles_tile_id(2, 0, 0) == 5);
    CHECK(cp0_map__pmtiles_tile_id(12, 3423, 1763) == 19078479ull);       /* checked with the reference package */
    CHECK(cp0_map__pmtiles_tile_id(20, 500000, 300000) == 527794063359ull);
    const char *names[] = {"ref_gzip.pmtiles", "ref_leaves.pmtiles"};
    for (int k = 0; k < 2; k++) {
        char p[600];
        snprintf(p, sizeof p, "%s/%s", MAP_FIXTURES, names[k]);
        cp0_map_archive_t *a;
        CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
        cp0_map_archive_info_t i;
        cp0_map_archive_info(a, &i);
        CHECK(!strcmp(i.name, k ? "Reference leaves" : "Reference gzip"));
        CHECK(!strcmp(i.attribution, "\xc2\xa9 Test \"data\""));
        CHECK(i.format == CP0_MAP_FMT_PNG && i.tile_px == 256 && i.minzoom == 0 && i.maxzoom == 2);
        CHECK(i.has_bounds && i.bounds.west == -180 && i.bounds.east == 180 && i.bounds.north > 85.05);
        cp0_map_buf_t b = {0};
        for (int z = 0; z <= 2; z++)
            for (uint32_t x = 0; x < (1u << z); x++)
                for (uint32_t y = 0; y < (1u << z); y++) {
                    CHECK_RC(cp0_map_archive_get_tile(a, z, x, y, &b, NULL), CP0_MAP_OK);
                    cp0_map_image_t *img;
                    CHECK_RC(cp0_map_decode_tile(b.data, b.len, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_OK);
                    uint32_t cx = x, cy = y;
                    if (z == 2 && (x || y)) cx = cy = 1;
                    CHECK(((uint16_t *)img->px)[777] ==
                          rgb565_of(40u * (unsigned)z + 20, (cx * 50 + 10) & 255, (cy * 50 + 30) & 255));
                    cp0_map_image_free(img);
                }
        CHECK_RC(cp0_map_archive_get_tile(a, 3, 0, 0, &b, NULL), CP0_MAP_E_NO_TILE);
        cp0_map_buf_free(&b);
        cp0_map_archive_close(a);
    }
    printf("map archive: PMTiles of the reference writer (zlib directories, run lengths, leaves), Hilbert ids\n");
}

/* opens a damaged file; when it opens, reads every tile of a z0-3 pyramid. Returns the open result. */
static int poke(const char *path)
{
    cp0_map_archive_t *a;
    const int rc = cp0_map_archive_open(path, &a);
    if (rc) {
        CHECK(rc < 0 && a == NULL);
        return rc;
    }
    cp0_map_buf_t b = {0};
    cp0_map_archive_info_t i;
    cp0_map_archive_info(a, &i);
    for (int z = 0; z <= 3; z++)
        for (uint32_t x = 0; x < (1u << z); x++)
            for (uint32_t y = 0; y < (1u << z); y++) {
                const int r = cp0_map_archive_get_tile(a, z, x, y, &b, NULL);
                CHECK(r == CP0_MAP_OK || r < 0);
            }
    cp0_map_buf_free(&b);
    cp0_map_archive_close(a);
    return rc;
}

static void test_refusals_and_fuzz(void)
{
    char p[600];
    cp0_map_archive_t *a = (cp0_map_archive_t *)1;
    tpath(p, sizeof p, "nothing-here.pmtiles");
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_E_ARCHIVE_MISSING);
    CHECK(a == NULL);
    CHECK_RC(cp0_map_archive_open(g_tmp, &a), CP0_MAP_E_FORMAT); /* a folder */
    CHECK_RC(cp0_map_archive_open(NULL, &a), CP0_MAP_E_INVALID);
    tpath(p, sizeof p, "text.mbtiles");
    write_file(p, "hello, this is not an archive\n", 30);
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_E_FORMAT);
    tpath(p, sizeof p, "empty.pmtiles");
    write_file(p, "", 0);
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_E_FORMAT);
    /* PMTiles v2, brotli directories */
    size_t n;
    uint8_t *ref = fixture("ref_gzip.pmtiles", &n);
    uint8_t *m = (uint8_t *)malloc(n);
    memcpy(m, ref, n);
    m[7] = 2;
    tpath(p, sizeof p, "v2.pmtiles");
    write_file(p, m, n);
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_E_UNSUPPORTED);
    memcpy(m, ref, n);
    m[97] = 3;
    tpath(p, sizeof p, "brotli.pmtiles");
    write_file(p, m, n);
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_E_UNSUPPORTED);
    /* a root directory pointing past the end */
    memcpy(m, ref, n);
    m[16] = 0xff;
    m[17] = 0xff;
    tpath(p, sizeof p, "past.pmtiles");
    write_file(p, m, n);
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_E_CORRUPT);
    /* truncated at every 7th length */
    int opened = 0, refused = 0;
    tpath(p, sizeof p, "trunc.pmtiles");
    for (size_t len = 0; len < n; len += 7) {
        write_file(p, ref, len);
        if (poke(p) == CP0_MAP_OK) opened++;
        else refused++;
    }
    printf("map archive: PMTiles truncated at %d lengths: %d opened (tiles past the cut refused), %d refused\n",
           opened + refused, opened, refused);
    /* 3000 mutated PMTiles: 1-8 random bytes changed, sometimes in the header and directories only */
    int codes[20] = {0};
    rng_seed(20261106);
    tpath(p, sizeof p, "fuzz.pmtiles");
    for (int k = 0; k < 3000; k++) {
        memcpy(m, ref, n);
        const int muts = 1 + (int)(rng() % 8);
        const size_t span = (k % 3) ? 600 : n; /* the first 600 bytes hold the header, the directories, the metadata */
        for (int j = 0; j < muts; j++) m[rng() % (span < n ? span : n)] = (uint8_t)rng();
        write_file(p, m, n);
        const int rc = poke(p);
        codes[-rc < 20 ? -rc : 19]++;
    }
    printf("map archive: 3000 mutated PMTiles: %d opened, refused %d format, %d corrupt, %d unsupported, %d other\n",
           codes[0], codes[-CP0_MAP_E_FORMAT], codes[-CP0_MAP_E_CORRUPT], codes[-CP0_MAP_E_UNSUPPORTED],
           3000 - codes[0] - codes[-CP0_MAP_E_FORMAT] - codes[-CP0_MAP_E_CORRUPT] - codes[-CP0_MAP_E_UNSUPPORTED]);
    free(m);
    free(ref);
    /* 300 mutated MBTiles (SQLite checks its own pages; we must still answer with a code) */
    tileset_t s;
    tiles_make(&s, 3, 16);
    char good[600];
    tpath(good, sizeof good, "good.mbtiles");
    write_mbtiles(good, &s, 1);
    ref = read_file(good, &n);
    m = (uint8_t *)malloc(n);
    memset(codes, 0, sizeof codes);
    tpath(p, sizeof p, "fuzz.mbtiles");
    for (int k = 0; k < 300; k++) {
        memcpy(m, ref, n);
        const int muts = 1 + (int)(rng() % 16);
        for (int j = 0; j < muts; j++) m[16 + rng() % (n - 16)] = (uint8_t)rng();
        write_file(p, m, k % 10 == 9 ? n / 2 : n);
        const int rc = poke(p);
        codes[-rc < 20 ? -rc : 19]++;
    }
    printf("map archive: 300 mutated / truncated MBTiles: %d opened, %d refused\n", codes[0], 300 - codes[0]);
    free(m);
    free(ref);
    tiles_free(&s);
}

static void missing_case(const char *name, int kind)
{
    tileset_t s;
    tiles_make(&s, 2, 16);
    char p[600], keep[600];
    tpath(p, sizeof p, "usb/%s", name);
    tpath(keep, sizeof keep, "keep-%s", name);
    if (kind == CP0_MAP_ARCHIVE_PMTILES)
        pm_write(p, s.t, s.n, 2, 0, 2, NULL);
    else {
        char d[600];
        tpath(d, sizeof d, "usb");
        mkdirs(d);
        write_mbtiles(p, &s, 1);
    }
    copy_file_to(p, keep);
    cp0_map_archive_t *a;
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_buf_t b = {0};
    CHECK_RC(cp0_map_archive_get_tile(a, 1, 1, 0, &b, NULL), CP0_MAP_OK);
    /* the stick is pulled (the file goes away while open) */
    CHECK(unlink(p) == 0);
    CHECK_RC(cp0_map_archive_get_tile(a, 1, 1, 0, &b, NULL), CP0_MAP_E_ARCHIVE_MISSING);
    CHECK_RC(cp0_map_archive_get_tile(a, 2, 1, 1, &b, NULL), CP0_MAP_E_ARCHIVE_MISSING);
    CHECK_RC(cp0_map_archive_check(a), CP0_MAP_E_ARCHIVE_MISSING);
    /* and put back: the next call reopens it */
    copy_file_to(keep, p);
    CHECK_RC(cp0_map_archive_get_tile(a, 2, 1, 1, &b, NULL), CP0_MAP_OK);
    const pm_tile_t *t = tiles_find(&s, 2, 1, 1);
    CHECK(b.len == t->len && !memcmp(b.data, t->data, b.len));
    /* replaced by another archive under the same name: its tiles */
    tileset_t s2;
    tiles_make(&s2, 1, 8);
    char other[600];
    tpath(other, sizeof other, "other-%s", name);
    if (kind == CP0_MAP_ARCHIVE_PMTILES)
        pm_write(other, s2.t, s2.n, 1, 0, 2, NULL);
    else
        write_mbtiles(other, &s2, 0);
    CHECK(rename(other, p) == 0);
    CHECK_RC(cp0_map_archive_get_tile(a, 1, 1, 0, &b, NULL), CP0_MAP_OK);
    t = tiles_find(&s2, 1, 1, 0);
    CHECK(b.len == t->len && !memcmp(b.data, t->data, b.len));
    CHECK_RC(cp0_map_archive_get_tile(a, 2, 1, 1, &b, NULL), CP0_MAP_E_NO_TILE);
    cp0_map_archive_info_t i;
    cp0_map_archive_info(a, &i);
    CHECK(i.maxzoom == 1);
    /* the whole folder gone (the mount point removed) */
    char usb[600];
    tpath(usb, sizeof usb, "usb");
    char gone[600];
    tpath(gone, sizeof gone, "usb-gone");
    CHECK(rename(usb, gone) == 0);
    CHECK_RC(cp0_map_archive_get_tile(a, 1, 1, 0, &b, NULL), CP0_MAP_E_ARCHIVE_MISSING);
    CHECK(rename(gone, usb) == 0);
    CHECK_RC(cp0_map_archive_get_tile(a, 1, 1, 0, &b, NULL), CP0_MAP_OK);
    cp0_map_archive_close(a);
    cp0_map_buf_free(&b);
    tiles_free(&s);
    tiles_free(&s2);
}

static void test_missing(void)
{
    missing_case("m.pmtiles", CP0_MAP_ARCHIVE_PMTILES);
    missing_case("m.mbtiles", CP0_MAP_ARCHIVE_MBTILES);
    printf("map archive: archive missing while open (PMTiles, MBTiles), back again, replaced, folder gone\n");
}

int main(void)
{
    tmp_init("cp0-map-archive");
    test_mbtiles();
    test_pmtiles_generated();
    test_pmtiles_reference();
    test_refusals_and_fuzz();
    test_missing();
    tmp_done();
    printf("map archive OK (%ld checks)\n", g_checks);
    return 0;
}
