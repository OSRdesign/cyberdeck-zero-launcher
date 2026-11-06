/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine, pure parts (cp0_map_geo.h, cp0_map_common.h): Web Mercator on known points (the reference values were
 * computed with an independent Python implementation of the same formulas), the latitude clamp, tile indices of
 * known cities, tile boxes, the TMS row flip, box parsing and tile counts, the scale bar's 1-2-5 steps in metric and
 * imperial, the display scale table of cp0_map_geo.h with its override spellings, result texts, formats, ids, JSON.
 */
#ifndef MAP_FIXTURES
#define MAP_FIXTURES "" /* this test reads no fixture */
#endif
#include "map_test_util.h"

#include <math.h>

static int near(double a, double b, double eps)
{
    return fabs(a - b) <= eps;
}

static void test_projection(void)
{
    double x, y, lat, lon;
    cp0_map_latlon_to_world_px(0, 0, 0, &x, &y);
    CHECK(near(x, 128, 1e-9) && near(y, 128, 1e-9));
    cp0_map_latlon_to_world_px(0, 0, 1, &x, &y);
    CHECK(near(x, 256, 1e-9) && near(y, 256, 1e-9));
    /* the corners of the square world, and the clamp beyond them */
    cp0_map_latlon_to_world_px(CP0_MAP_MAX_LAT, -180, 3, &x, &y);
    CHECK(near(x, 0, 1e-9) && near(y, 0, 1e-6));
    cp0_map_latlon_to_world_px(89.9, -180, 3, &x, &y);
    CHECK(near(y, 0, 1e-9));
    cp0_map_latlon_to_world_px(-90, 0, 3, &x, &y);
    CHECK(near(y, 2048, 1e-9));
    CHECK(near(cp0_map_clamp_lat(86.0), 85.0511287798066, 1e-12));
    CHECK(near(cp0_map_clamp_lat(-86.0), -85.0511287798066, 1e-12));
    CHECK(cp0_map_clamp_lat(NAN) == 0.0);
    CHECK(near(CP0_MAP_MAX_LAT, 85.0511287798066, 1e-12));
    /* reference values */
    cp0_map_latlon_to_world_px(48.8566, 2.3522, 12, &x, &y);
    CHECK(near(x, 531139.2790755556, 1e-6) && near(y, 360739.9267749889, 1e-6));
    cp0_map_latlon_to_world_px(52.5200, 13.4050, 17, &x, &y);
    CHECK(near(x, 18026652.558222223, 1e-4) && near(y, 11004349.142384987, 1e-4));
    /* round trip */
    for (int z = 0; z <= 20; z += 4) {
        cp0_map_latlon_to_world_px(-33.8688, 151.2093, z, &x, &y);
        cp0_map_world_px_to_latlon(x, y, z, &lat, &lon);
        CHECK(near(lat, -33.8688, 1e-9) && near(lon, 151.2093, 1e-9));
    }
    /* longitude wrap */
    CHECK(near(cp0_map_wrap_lon(190), -170, 1e-12));
    CHECK(near(cp0_map_wrap_lon(-190), 170, 1e-12));
    CHECK(near(cp0_map_wrap_lon(180), -180, 1e-12));
    CHECK(near(cp0_map_wrap_lon(540), -180, 1e-12));
    CHECK(cp0_map_wrap_lon(INFINITY) == 0.0);
    cp0_map_latlon_to_world_px(10, 370, 2, &x, &y);
    double x2;
    cp0_map_latlon_to_world_px(10, 10, 2, &x2, &y);
    CHECK(near(x, x2, 1e-9));
    /* metres per pixel */
    CHECK(near(cp0_map_metres_per_px(0, 0, 1.0f), 156543.03392804097, 1e-6));
    CHECK(near(cp0_map_metres_per_px(48, 12, 1.0f), 25.573177542364594, 1e-9));
    CHECK(near(cp0_map_metres_per_px(48, 16, 2.0f), 0.7991617981988935, 1e-12));
    CHECK(near(cp0_map_metres_per_px(48, 12, 0.0f), cp0_map_metres_per_px(48, 12, 1.0f), 1e-12));
    printf("map geo: projection, clamp, wrap, metres per px\n");
}

static void test_tiles(void)
{
    uint32_t x, y;
    CHECK_RC(cp0_map_latlon_to_tile(0, 0, 0, &x, &y), CP0_MAP_OK);
    CHECK(x == 0 && y == 0);
    CHECK_RC(cp0_map_latlon_to_tile(0, 0, 1, &x, &y), CP0_MAP_OK);
    CHECK(x == 1 && y == 1);
    CHECK_RC(cp0_map_latlon_to_tile(48.8566, 2.3522, 12, &x, &y), CP0_MAP_OK);
    CHECK(x == 2074 && y == 1409); /* Paris */
    CHECK_RC(cp0_map_latlon_to_tile(51.5074, -0.1278, 10, &x, &y), CP0_MAP_OK);
    CHECK(x == 511 && y == 340); /* London */
    CHECK_RC(cp0_map_latlon_to_tile(-33.8688, 151.2093, 15, &x, &y), CP0_MAP_OK);
    CHECK(x == 30147 && y == 19663); /* Sydney */
    CHECK_RC(cp0_map_latlon_to_tile(40.7128, -74.0060, 16, &x, &y), CP0_MAP_OK);
    CHECK(x == 19295 && y == 24640); /* New York */
    /* the edges stay inside the world */
    CHECK_RC(cp0_map_latlon_to_tile(90, 179.9999999, 5, &x, &y), CP0_MAP_OK);
    CHECK(x == 31 && y == 0);
    CHECK_RC(cp0_map_latlon_to_tile(-90, -180, 5, &x, &y), CP0_MAP_OK);
    CHECK(x == 0 && y == 31);
    CHECK_RC(cp0_map_latlon_to_tile(0, 0, -1, &x, &y), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_latlon_to_tile(0, 0, CP0_MAP_MAX_ZOOM + 1, &x, &y), CP0_MAP_E_INVALID);
    /* tile box */
    cp0_map_bbox_t b;
    CHECK_RC(cp0_map_tile_bbox(12, 2074, 1409, &b), CP0_MAP_OK);
    CHECK(near(b.north, 48.86471476180278, 1e-9) && near(b.west, 2.28515625, 1e-9));
    CHECK(near(b.south, 48.80686346108518, 1e-9) && near(b.east, 2.373046875, 1e-9));
    CHECK(cp0_map_bbox_contains(&b, 48.8566, 2.3522));
    CHECK_RC(cp0_map_tile_bbox(0, 0, 0, &b), CP0_MAP_OK);
    CHECK(near(b.west, -180, 1e-12) && near(b.east, 180, 1e-12) && near(b.north, CP0_MAP_MAX_LAT, 1e-9));
    CHECK_RC(cp0_map_tile_bbox(2, 4, 0, &b), CP0_MAP_E_INVALID);
    CHECK(cp0_map_tile_valid(0, 0, 0) && !cp0_map_tile_valid(0, 1, 0) && cp0_map_tile_valid(30, (1u << 30) - 1, 5));
    CHECK(!cp0_map_tile_valid(31, 0, 0) && !cp0_map_tile_valid(-1, 0, 0));
    double lat, lon;
    cp0_map_tile_to_latlon(1, 1, 1, &lat, &lon);
    CHECK(near(lat, 0, 1e-12) && near(lon, 0, 1e-12));
    /* TMS rows: 2^z - 1 - y, its own inverse */
    CHECK(cp0_map_tms_row(0, 0) == 0);
    CHECK(cp0_map_tms_row(1, 0) == 1 && cp0_map_tms_row(1, 1) == 0);
    CHECK(cp0_map_tms_row(12, 1409) == 2686);
    CHECK(cp0_map_tms_row(12, cp0_map_tms_row(12, 1409)) == 1409);
    CHECK(cp0_map_tms_row(30, 0) == (1u << 30) - 1);
    printf("map geo: tile indices (Paris, London, Sydney, New York), boxes, TMS rows\n");
}

static void test_boxes(void)
{
    cp0_map_bbox_t b;
    CHECK_RC(cp0_map_bbox_parse("-180,-85.0511,180,85.0511", &b), CP0_MAP_OK);
    CHECK(b.west == -180 && b.north == 85.0511);
    CHECK_RC(cp0_map_bbox_parse(" 2.2 , 48.8,2.5 ,48.9 ", &b), CP0_MAP_OK);
    CHECK(near(b.west, 2.2, 1e-12) && near(b.south, 48.8, 1e-12) && near(b.east, 2.5, 1e-12));
    CHECK_RC(cp0_map_bbox_parse("1,2,3", &b), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_bbox_parse("1,2,3,4,5", &b), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_bbox_parse("3,2,1,4", &b), CP0_MAP_E_INVALID); /* west > east */
    CHECK_RC(cp0_map_bbox_parse("1,2,x,4", &b), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_bbox_parse("-200,0,0,1", &b), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_bbox_parse(NULL, &b), CP0_MAP_E_INVALID);
    char buf[80];
    b = (cp0_map_bbox_t){2.2, 48.8, 2.5, 48.9};
    CHECK(cp0_map_bbox_format(&b, buf, sizeof buf) > 0 && !strcmp(buf, "2.200000,48.800000,2.500000,48.900000"));
    CHECK_RC(cp0_map_bbox_format(&b, buf, 10), CP0_MAP_E_INVALID);
    cp0_map_bbox_t c = {2.4, 48.85, 3.0, 49.5}, d = {3.1, 48.0, 4.0, 48.5};
    CHECK(cp0_map_bbox_intersects(&b, &c) && !cp0_map_bbox_intersects(&b, &d));
    cp0_map_bbox_union(&c, &d);
    CHECK(c.west == 2.4 && c.east == 4.0 && c.south == 48.0 && c.north == 49.5);
    uint32_t x0, y0, x1, y1;
    CHECK_RC(cp0_map_bbox_tiles(&b, 12, &x0, &y0, &x1, &y1), CP0_MAP_OK);
    CHECK(x0 == 2073 && x1 == 2076 && y0 <= 1409 && y1 >= 1409);
    cp0_map_bbox_t world = {-180, -90, 180, 90};
    CHECK(cp0_map_bbox_tile_count(&world, 0, 2) == 1 + 4 + 16);
    CHECK(cp0_map_bbox_tile_count(&world, 3, 3) == 64);
    CHECK(cp0_map_bbox_tile_count(&world, 3, 2) == 0);
    /* a 20 x 20 km city at zoom 0-16 is about 3,500 tiles (report 054 2.5) */
    cp0_map_bbox_t city = {2.25, 48.77, 2.52, 48.95};
    const uint64_t n = cp0_map_bbox_tile_count(&city, 0, 16);
    CHECK(n > 2500 && n < 5000);
    cp0_map_bbox_t bad = {3, 0, 2, 1};
    CHECK_RC(cp0_map_bbox_tiles(&bad, 3, &x0, &y0, &x1, &y1), CP0_MAP_E_INVALID);
    printf("map geo: boxes (parse, format, intersect, union, tile ranges, a city at z0-16 = %llu tiles)\n",
           (unsigned long long)n);
}

static void test_scalebar(void)
{
    CHECK(cp0_map_nice_step(1) == 1 && cp0_map_nice_step(1.99) == 1 && cp0_map_nice_step(2) == 2);
    CHECK(cp0_map_nice_step(4.99) == 2 && cp0_map_nice_step(5) == 5 && cp0_map_nice_step(9.99) == 5);
    CHECK(cp0_map_nice_step(10) == 10 && cp0_map_nice_step(999) == 500 && cp0_map_nice_step(1000) == 1000);
    CHECK(near(cp0_map_nice_step(0.37), 0.2, 1e-15) && near(cp0_map_nice_step(0.05), 0.05, 1e-15));
    CHECK(cp0_map_nice_step(0) == 0 && cp0_map_nice_step(-3) == 0 && cp0_map_nice_step(INFINITY) == 0);
    /* every power of ten from 1e-3 to 1e7, and just under it */
    for (int e = -3; e <= 7; e++) {
        const double p = pow(10, e);
        CHECK(near(cp0_map_nice_step(p), p, p * 1e-12));
        CHECK(near(cp0_map_nice_step(p * 0.999999), p / 2, p * 1e-12));
    }
    cp0_map_scalebar_t s;
    CHECK_RC(cp0_map_scalebar(1.0, 120, CP0_MAP_UNITS_METRIC, &s), CP0_MAP_OK);
    CHECK(s.value == 100 && s.length_m == 100 && s.px == 100 && !strcmp(s.label, "100 m"));
    CHECK_RC(cp0_map_scalebar(25.6, 150, CP0_MAP_UNITS_METRIC, &s), CP0_MAP_OK);
    CHECK(s.value == 2 && s.length_m == 2000 && s.px == 78 && !strcmp(s.label, "2 km"));
    CHECK_RC(cp0_map_scalebar(0.004, 100, CP0_MAP_UNITS_METRIC, &s), CP0_MAP_OK);
    CHECK(near(s.value, 0.2, 1e-12) && s.px == 50 && !strcmp(s.label, "0.2 m"));
    CHECK_RC(cp0_map_scalebar(4000.0, 140, CP0_MAP_UNITS_METRIC, &s), CP0_MAP_OK);
    CHECK(s.value == 500 && !strcmp(s.label, "500 km") && s.px == 125);
    CHECK_RC(cp0_map_scalebar(1.0, 120, CP0_MAP_UNITS_IMPERIAL, &s), CP0_MAP_OK);
    CHECK(s.value == 200 && s.px == 61 && !strcmp(s.label, "200 ft"));
    CHECK_RC(cp0_map_scalebar(30.0, 120, CP0_MAP_UNITS_IMPERIAL, &s), CP0_MAP_OK);
    CHECK(s.value == 2 && s.px == 107 && !strcmp(s.label, "2 mi"));
    /* the bar never passes max_px, at any zoom of a map at 48 N and 2x */
    for (int z = 0; z <= 20; z++) {
        CHECK_RC(cp0_map_scalebar(cp0_map_metres_per_px(48, z, 2.0f), 100, CP0_MAP_UNITS_METRIC, &s), CP0_MAP_OK);
        CHECK(s.px > 0 && s.px <= 100 && s.px >= 40);
    }
    CHECK_RC(cp0_map_scalebar(0, 100, CP0_MAP_UNITS_METRIC, &s), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_scalebar(1, 0, CP0_MAP_UNITS_METRIC, &s), CP0_MAP_E_INVALID);
    printf("map geo: scale bar 1-2-5 steps (m / km, ft / mi)\n");
}

static void test_display_scale(void)
{
    /* the table of cp0_map_geo.h */
    static const struct {
        int ppmm;
        float scale;
        const char *board;
    } boards[] = {{630, 1.5f, "zero8 3.5\""}, {740, 2.0f, "Pi 4B 5\""},    {854, 2.0f, "Pi 3A+ 4.3\""},
                  {857, 2.0f, "FZ-M1"},       {1130, 2.0f, "deck"},         {0, 2.0f, "unknown"},
                  {-5, 2.0f, "invalid"},      {378, 1.0f, "a 96 dpi panel"}, {474, 1.0f, "4.74 px/mm"},
                  {475, 1.5f, "4.75 px/mm"},  {664, 1.5f, "6.64 px/mm"},    {665, 2.0f, "6.65 px/mm"}};
    for (size_t i = 0; i < sizeof boards / sizeof boards[0]; i++) {
        if (cp0_map_choose_scale(boards[i].ppmm) != boards[i].scale) {
            fprintf(stderr, "scale for %s (%d): %g\n", boards[i].board, boards[i].ppmm,
                    (double)cp0_map_choose_scale(boards[i].ppmm));
            CHECK(0);
        }
        g_checks++;
    }
    CHECK(cp0_map_resolve_scale(0.0f, 630) == 1.5f && cp0_map_resolve_scale(0.0f, 1130) == 2.0f);
    CHECK(cp0_map_resolve_scale(1.0f, 1130) == 1.0f && cp0_map_resolve_scale(1.5f, 1130) == 1.5f);
    CHECK(cp0_map_resolve_scale(2.0f, 630) == 2.0f && cp0_map_resolve_scale(1.6f, 0) == 1.5f);
    CHECK(cp0_map_resolve_scale(3.0f, 0) == 2.0f && cp0_map_resolve_scale(-1.0f, 630) == 1.5f);
    CHECK(cp0_map_scale_x2(1.0f) == 2 && cp0_map_scale_x2(1.5f) == 3 && cp0_map_scale_x2(2.0f) == 4);
    CHECK(cp0_map_scale_x2(0.0f) == 2 && cp0_map_scale_x2(1.24f) == 2 && cp0_map_scale_x2(1.75f) == 4);
    CHECK(!strcmp(cp0_map_scale_name(0), "auto") && !strcmp(cp0_map_scale_name(1), "1x"));
    CHECK(!strcmp(cp0_map_scale_name(1.5f), "1.5x") && !strcmp(cp0_map_scale_name(2), "2x"));
    float v;
    CHECK_RC(cp0_map_scale_parse("Auto", &v), CP0_MAP_OK);
    CHECK(v == 0.0f);
    CHECK_RC(cp0_map_scale_parse("1.5x", &v), CP0_MAP_OK);
    CHECK(v == 1.5f);
    CHECK_RC(cp0_map_scale_parse("2", &v), CP0_MAP_OK);
    CHECK(v == 2.0f);
    CHECK_RC(cp0_map_scale_parse("3x", &v), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_scale_parse(NULL, &v), CP0_MAP_E_INVALID);
    printf("map geo: display scale table (zero8 1.5x, deck 2x, ...), override spellings\n");
}

static void test_common(void)
{
    for (int c = 0; c >= -17; c--) CHECK(strcmp(cp0_map_strerror(c), "unknown error") != 0);
    CHECK(!strcmp(cp0_map_strerror(-99), "unknown error"));
    CHECK(strstr(cp0_map_strerror(CP0_MAP_E_SQLITE_MISSING), "SQLite library missing") != NULL);
    CHECK(!strcmp(cp0_map_strerror(CP0_MAP_E_ARCHIVE_MISSING), "archive missing"));
    CHECK(cp0_map_format_parse("PNG") == CP0_MAP_FMT_PNG && cp0_map_format_parse("jpeg") == CP0_MAP_FMT_JPEG);
    CHECK(cp0_map_format_parse("pbf") == CP0_MAP_FMT_MVT && cp0_map_format_parse("tiff") == CP0_MAP_FMT_UNKNOWN);
    CHECK(!strcmp(cp0_map_format_name(CP0_MAP_FMT_JPEG), "jpg") && !strcmp(cp0_map_format_name(CP0_MAP_FMT_UNKNOWN), ""));
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}, jpg[3] = {0xff, 0xd8, 0xff},
                         webp[12] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'}, gz[2] = {0x1f, 0x8b};
    CHECK(cp0_map_format_sniff(png, 8) == CP0_MAP_FMT_PNG && cp0_map_format_sniff(png, 7) == CP0_MAP_FMT_UNKNOWN);
    CHECK(cp0_map_format_sniff(jpg, 3) == CP0_MAP_FMT_JPEG && cp0_map_format_sniff(webp, 12) == CP0_MAP_FMT_WEBP);
    CHECK(cp0_map_format_sniff(gz, 2) == CP0_MAP_FMT_MVT && cp0_map_format_sniff(NULL, 9) == CP0_MAP_FMT_UNKNOWN);
    /* ids */
    char id[CP0_MAP_ID_MAX];
    cp0_map__make_id("City Z14", id, sizeof id);
    CHECK(!strcmp(id, "city-z14"));
    cp0_map__make_id("  Île-de-France (2026).v2 ", id, sizeof id);
    CHECK(!strcmp(id, "le-de-france-2026-.v2") || cp0_map__id_valid(id));
    cp0_map__make_id("...", id, sizeof id);
    CHECK(!strcmp(id, "map"));
    cp0_map__make_id("a_b.c", id, sizeof id);
    CHECK(!strcmp(id, "a_b.c"));
    char longname[300];
    memset(longname, 'x', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    cp0_map__make_id(longname, id, sizeof id);
    CHECK(strlen(id) == CP0_MAP_ID_MAX - 1 && cp0_map__id_valid(id));
    CHECK(cp0_map__id_valid("osm-2026") && !cp0_map__id_valid("Osm") && !cp0_map__id_valid(".x"));
    CHECK(!cp0_map__id_valid("a/b") && !cp0_map__id_valid("") && !cp0_map__id_valid(NULL));
    CHECK(cp0_map_source_hash("a") != cp0_map_source_hash("b") && cp0_map_source_hash(NULL) == 0);
    /* JSON strings */
    char out[64];
    const char *j = "{\"a\": 1, \"b\": [1, {\"name\": \"no\"}], \"name\": \"A \\\"q\\\" \\u00e9\\ud83d\\ude00\","
                    " \"x\": true}";
    CHECK(cp0_map__json_string(j, strlen(j), "name", out, sizeof out) == 0);
    CHECK(!strcmp(out, "A \"q\" \xc3\xa9\xf0\x9f\x98\x80"));
    CHECK(cp0_map__json_string(j, strlen(j), "a", out, sizeof out) == -1);       /* not a string */
    CHECK(cp0_map__json_string(j, strlen(j), "missing", out, sizeof out) == -1);
    CHECK(cp0_map__json_string("{\"name\": \"x", 11, "name", out, sizeof out) == -1);
    CHECK(cp0_map__json_string("[\"name\"]", 8, "name", out, sizeof out) == -1);
    CHECK(cp0_map__json_string("{\"name\":\"abcdefghij\"}", 21, "name", out, 5) == 0 && !strcmp(out, "abcd"));
    /* JSON fuzz: random bytes and mutations never crash, never write past the buffer */
    rng_seed(77);
    for (int k = 0; k < 4000; k++) {
        char buf[96];
        const size_t n = strlen(j);
        memcpy(buf, j, n < sizeof buf ? n : sizeof buf);
        const size_t len = n < sizeof buf ? n : sizeof buf;
        for (int m = 0; m < 1 + (int)(rng() % 6); m++) buf[rng() % len] = (char)rng();
        char small[8];
        cp0_map__json_string(buf, len, "name", small, sizeof small);
        g_checks++;
    }
    /* gzip: stored and fixed-Huffman members, a lying ISIZE, garbage */
    const char *text = "hello hello hello map engine";
    for (int mode = 0; mode < 2; mode++) {
        bb_t g = {0};
        gzip_of(&g, (const uint8_t *)text, strlen(text), mode);
        uint8_t *o = NULL;
        size_t on = 0;
        CHECK_RC(cp0_map__gunzip(g.d, g.n, 1 << 20, &o, &on), CP0_MAP_OK);
        CHECK(on == strlen(text) && !memcmp(o, text, on));
        free(o);
        CHECK_RC(cp0_map__gunzip(g.d, g.n, 4, &o, &on), CP0_MAP_E_TOO_LARGE);
        g.d[g.n - 1] = 0x7f; /* ISIZE about 2 GB */
        CHECK_RC(cp0_map__gunzip(g.d, g.n, 1 << 20, &o, &on), CP0_MAP_E_CORRUPT);
        free(g.d);
    }
    uint8_t *o = NULL;
    size_t on;
    CHECK_RC(cp0_map__gunzip((const uint8_t *)"not gzip at all, really", 23, 100, &o, &on), CP0_MAP_E_CORRUPT);
    printf("map common: result texts, formats, ids, JSON strings (+4000 mutations), gzip\n");
}

int main(void)
{
    test_projection();
    test_tiles();
    test_boxes();
    test_scalebar();
    test_display_scale();
    test_common();
    printf("map geo OK (%ld checks)\n", g_checks);
    return 0;
}
