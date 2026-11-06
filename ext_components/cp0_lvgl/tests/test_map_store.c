/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine, the store (cp0_map_store.h):
 *   - the default folder ($XDG_DATA_HOME, else ~/.local/share), opening creates archives/ and sources/;
 *   - import of a PMTiles file copied (id from the name, progress in bytes, the record's keys, bytes equal) and of an
 *     MBTiles file registered in place on a "USB stick": archive missing while the stick is out, back after;
 *   - z/x/y folders converted into one MBTiles: XYZ, TMS (tilemapresource.xml), 512 px JPEG, a mixed folder (PNG,
 *     JPEG, .png.tile, extensionless, junk, WebP) whose result reads back byte for byte; metadata.json; a cancelled
 *     conversion resumed (rows kept, the rest added); refusals (WebP only, empty, vector MBTiles, not a map, no path);
 *   - lookup across archives (id NULL: the first covering archive in id order), source records (store and app
 *     folders, the store's wins) with their viewing cache file, removal (the store's files go, an in-place file
 *     stays, a leftover partial file), a read-only second store, refresh, the RAM cache budget;
 *   - the conversion rate of a 2,000-tile folder on this machine (printed; report 055 measured 5,570 tiles/s here
 *     and 784 tiles/s on the deck).
 */
#include "map_test_util.h"

#include <pthread.h>

typedef struct {
    int calls;
    cp0_map_phase_t last_phase;
    uint64_t last_done, last_total;
    uint64_t conv_done, conv_total, skipped, resumed; /* the last CONVERT report */
    int cancel_after;   /* convert callbacks before setting the cancel flag (0 = never) */
    int convert_calls;
    volatile int cancel;
    int monotonic;
} prog_t;

static void on_progress(void *user, const cp0_map_progress_t *p)
{
    prog_t *g = (prog_t *)user;
    if (g->calls && p->phase == g->last_phase && p->done < g->last_done) g->monotonic = 0;
    g->calls++;
    g->last_phase = p->phase;
    g->last_done = p->done;
    g->last_total = p->total;
    if (p->phase == CP0_MAP_PHASE_CONVERT) {
        g->conv_done = p->done;
        g->conv_total = p->total;
        g->skipped = p->skipped;
        g->resumed = p->resumed;
    }
    if (p->phase == CP0_MAP_PHASE_CONVERT && g->cancel_after && ++g->convert_calls >= g->cancel_after) g->cancel = 1;
}

static cp0_map_store_t *open_store(const char *dir, int read_only)
{
    cp0_map_store_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.store_dir = dir;
    cfg.read_only = read_only;
    cfg.ram_cache_bytes = 1u << 20;
    cfg.user_agent = "test/1.0 (+https://github.com/OSRdesign/cyberdeck-zero-apps)";
    cp0_map_store_t *s;
    CHECK_RC(cp0_map_store_open(&cfg, &s), CP0_MAP_OK);
    return s;
}

static int record_has(const char *store, const char *id, const char *line)
{
    char p[700];
    snprintf(p, sizeof p, "%s/archives/%s.conf", store, id);
    size_t n;
    uint8_t *t = read_file(p, &n);
    if (!t) return 0;
    char *text = (char *)realloc(t, n + 1);
    text[n] = '\0';
    const int found = strstr(text, line) != NULL;
    free(text);
    return found;
}

static void check_tiles_in(cp0_map_store_t *s, const char *id, const tileset_t *ts)
{
    cp0_map_buf_t b = {0};
    for (int i = 0; i < ts->n; i++) {
        cp0_map_tile_meta_t meta;
        CHECK_RC(cp0_map_store_get_tile(s, id, ts->t[i].z, ts->t[i].x, ts->t[i].y, &b, &meta), CP0_MAP_OK);
        CHECK(b.len == ts->t[i].len && !memcmp(b.data, ts->t[i].data, b.len));
        if (id) CHECK(!strcmp(meta.archive_id, id));
    }
    cp0_map_buf_free(&b);
}

/* writes the tiles of `ts` as a z/x/y folder; y flipped for tms; `ext` per tile */
static void write_folder(const char *dir, const tileset_t *ts, int tms, const char *ext)
{
    for (int i = 0; i < ts->n; i++) {
        const pm_tile_t *t = &ts->t[i];
        char p[800];
        snprintf(p, sizeof p, "%s/%d/%u/%u%s", dir, t->z, t->x, tms ? cp0_map_tms_row(t->z, t->y) : t->y, ext);
        write_file(p, t->data, t->len);
    }
}

static void test_default_dir(void)
{
    char buf[600];
    setenv("XDG_DATA_HOME", "/srv/data", 1);
    CHECK_RC(cp0_map_store_default_dir(buf, sizeof buf), CP0_MAP_OK);
    CHECK(!strcmp(buf, "/srv/data/cyberdeckzero/maps"));
    setenv("XDG_DATA_HOME", "relative/ignored", 1); /* the XDG rule: relative paths are ignored */
    setenv("HOME", "/home/deck", 1);
    CHECK_RC(cp0_map_store_default_dir(buf, sizeof buf), CP0_MAP_OK);
    CHECK(!strcmp(buf, "/home/deck/.local/share/cyberdeckzero/maps"));
    unsetenv("XDG_DATA_HOME");
    CHECK_RC(cp0_map_store_default_dir(buf, sizeof buf), CP0_MAP_OK);
    CHECK(!strcmp(buf, "/home/deck/.local/share/cyberdeckzero/maps"));
    CHECK_RC(cp0_map_store_default_dir(buf, 8), CP0_MAP_E_INVALID);
    /* NULL cfg: the default folder, created */
    char home[600];
    tpath(home, sizeof home, "home");
    setenv("HOME", home, 1);
    cp0_map_store_t *s;
    CHECK_RC(cp0_map_store_open(NULL, &s), CP0_MAP_OK);
    char want[700];
    snprintf(want, sizeof want, "%s/.local/share/cyberdeckzero/maps", home);
    CHECK(!strcmp(cp0_map_store_dir(s), want));
    snprintf(want, sizeof want, "%s/.local/share/cyberdeckzero/maps/archives", home);
    CHECK(file_exists(want));
    CHECK_RC(cp0_map_store_sources(s, NULL, 0), 0);
    cp0_map_store_close(s);
    printf("map store: default folder ($XDG_DATA_HOME, ~/.local/share), created on open\n");
}

static void test_import_archives(const char *store)
{
    cp0_map_store_t *s = open_store(store, 0);
    tileset_t ts;
    tiles_make(&ts, 3, 64);
    char src[600], id[CP0_MAP_ID_MAX];
    tpath(src, sizeof src, "downloads/City Z14.pmtiles");
    pm_write(src, ts.t, ts.n, 2, 3, 2, "{\"name\": \"City at zoom 14\", \"attribution\": \"\\u00a9 OSM\"}");
    prog_t pg;
    memset(&pg, 0, sizeof pg);
    pg.monotonic = 1;
    cp0_map_import_opts_t o;
    memset(&o, 0, sizeof o);
    o.copy = 1;
    o.progress = on_progress;
    o.user = &pg;
    o.out_id = id;
    o.out_id_size = sizeof id;
    CHECK_RC(cp0_map_store_import_ex(s, src, &o), CP0_MAP_OK);
    CHECK(!strcmp(id, "city-z14") && pg.calls >= 2 && pg.monotonic && pg.last_phase == CP0_MAP_PHASE_CHECK);
    cp0_map_source_info_t info;
    CHECK_RC(cp0_map_store_find(s, "city-z14", &info), CP0_MAP_OK);
    CHECK(info.kind == CP0_MAP_ENTRY_ARCHIVE && info.mode == CP0_MAP_MODE_COPY && info.state == CP0_MAP_OK);
    CHECK(info.archive == CP0_MAP_ARCHIVE_PMTILES && info.format == CP0_MAP_FMT_PNG && info.tile_px == 64);
    CHECK(info.minzoom == 0 && info.maxzoom == 3 && !strcmp(info.title, "City at zoom 14"));
    CHECK(!strcmp(info.attribution, "\xc2\xa9 OSM") && info.bytes > 0);
    char want[700];
    snprintf(want, sizeof want, "%s/archives/city-z14.pmtiles", store);
    CHECK(!strcmp(info.path, want) && file_exists(want));
    size_t n1, n2;
    uint8_t *a1 = read_file(src, &n1), *a2 = read_file(want, &n2);
    CHECK(n1 == n2 && !memcmp(a1, a2, n1));
    free(a1);
    free(a2);
    CHECK(record_has(store, "city-z14", "\nkind=pmtiles\n") && record_has(store, "city-z14", "\nmode=copy\n"));
    CHECK(record_has(store, "city-z14", "\nlicence=user supplied: terms are the importer's responsibility\n"));
    CHECK(record_has(store, "city-z14", "\ntile_px=64\n") && record_has(store, "city-z14", "\nimported=20"));
    CHECK(record_has(store, "city-z14", "v=1\n") && record_has(store, "city-z14", "\nsource_path=/"));
    check_tiles_in(s, "city-z14", &ts);
    /* the same file again: a second copy under the next id */
    CHECK_RC(cp0_map_store_import_ex(s, src, &o), CP0_MAP_OK);
    CHECK(!strcmp(id, "city-z14-2"));
    /* an explicit id that is taken */
    o.id = "city-z14";
    CHECK_RC(cp0_map_store_import_ex(s, src, &o), CP0_MAP_E_EXISTS);
    o.id = "Bad/Id";
    CHECK_RC(cp0_map_store_import_ex(s, src, &o), CP0_MAP_E_INVALID);
    o.id = NULL;

    /* an MBTiles on a USB stick, read in place */
    char usb[600], mb[600];
    tpath(usb, sizeof usb, "media/stick");
    snprintf(mb, sizeof mb, "%s/region.mbtiles", usb);
    {
        mkdirs(usb);
        mbw_t m;
        mbw_open(&m, mb);
        mbw_meta(&m, "name", "Region");
        mbw_meta(&m, "format", "png");
        mbw_meta(&m, "bounds", "0,0,90,66.5");
        mbw_meta(&m, "minzoom", "1");
        mbw_meta(&m, "maxzoom", "3");
        for (int i = 0; i < ts.n; i++)
            if (ts.t[i].z >= 1) mbw_tile(&m, ts.t[i].z, ts.t[i].x, ts.t[i].y, ts.t[i].data, ts.t[i].len);
        mbw_close(&m);
    }
    o.copy = 0;
    CHECK_RC(cp0_map_store_import_ex(s, mb, &o), CP0_MAP_OK);
    CHECK(!strcmp(id, "region"));
    CHECK_RC(cp0_map_store_find(s, "region", &info), CP0_MAP_OK);
    CHECK(info.mode == CP0_MAP_MODE_INPLACE && !strcmp(info.path, mb) && info.archive == CP0_MAP_ARCHIVE_MBTILES);
    CHECK(info.has_bounds && info.bounds.east == 90 && info.minzoom == 1);
    CHECK_RC(cp0_map_store_import_ex(s, mb, &o), CP0_MAP_E_EXISTS); /* the same file in place twice */
    cp0_map_buf_t b = {0};
    cp0_map_tile_meta_t meta;
    CHECK_RC(cp0_map_store_get_tile(s, "region", 2, 2, 1, &b, &meta), CP0_MAP_OK);
    CHECK(meta.format == CP0_MAP_FMT_PNG && !strcmp(meta.archive_id, "region"));
    /* pull the stick */
    char gone[600];
    tpath(gone, sizeof gone, "media/stick-out");
    CHECK(rename(usb, gone) == 0);
    CHECK_RC(cp0_map_store_find(s, "region", &info), CP0_MAP_OK);
    CHECK(info.state == CP0_MAP_E_ARCHIVE_MISSING && info.bytes == 0);
    CHECK_RC(cp0_map_store_get_tile(s, "region", 2, 2, 1, &b, NULL), CP0_MAP_E_ARCHIVE_MISSING);
    CHECK_RC(cp0_map_store_get_tile(s, "region", 2, 2, 1, &b, NULL), CP0_MAP_E_ARCHIVE_MISSING);
    /* any-archive lookup: copies answer; a tile only the stick has says "missing" */
    CHECK_RC(cp0_map_store_get_tile(s, NULL, 1, 1, 0, &b, &meta), CP0_MAP_OK);
    CHECK(!strcmp(meta.archive_id, "city-z14"));
    cp0_map_store_remove(s, "city-z14");
    cp0_map_store_remove(s, "city-z14-2");
    CHECK_RC(cp0_map_store_get_tile(s, NULL, 2, 2, 1, &b, NULL), CP0_MAP_E_ARCHIVE_MISSING);
    CHECK_RC(cp0_map_store_get_tile(s, NULL, 2, 0, 3, &b, NULL), CP0_MAP_E_NO_TILE); /* outside its bounds */
    CHECK(rename(gone, usb) == 0);
    CHECK_RC(cp0_map_store_get_tile(s, "region", 2, 2, 1, &b, NULL), CP0_MAP_OK);
    const pm_tile_t *t = tiles_find(&ts, 2, 2, 1);
    CHECK(b.len == t->len && !memcmp(b.data, t->data, b.len));
    CHECK_RC(cp0_map_store_get_tile(s, "nope", 0, 0, 0, &b, NULL), CP0_MAP_E_NOT_FOUND);
    CHECK_RC(cp0_map_store_get_tile(s, "region", 2, 9, 1, &b, NULL), CP0_MAP_E_INVALID);
    /* remove the in-place archive: the user's file stays */
    CHECK_RC(cp0_map_store_remove(s, "region"), CP0_MAP_OK);
    CHECK(file_exists(mb));
    CHECK_RC(cp0_map_store_find(s, "region", &info), CP0_MAP_E_NOT_FOUND);
    CHECK_RC(cp0_map_store_remove(s, "region"), CP0_MAP_E_NOT_FOUND);
    snprintf(want, sizeof want, "%s/archives/city-z14.pmtiles", store);
    CHECK(!file_exists(want));
    /* refusals */
    char p[600];
    CHECK_RC(cp0_map_store_import(s, "/no/such/file.mbtiles", 1, NULL, NULL), CP0_MAP_E_NOT_FOUND);
    tpath(p, sizeof p, "downloads/notes.txt");
    write_file(p, "just text", 9);
    CHECK_RC(cp0_map_store_import(s, p, 1, NULL, NULL), CP0_MAP_E_FORMAT);
    tpath(p, sizeof p, "downloads/vector.mbtiles");
    {
        mbw_t m;
        mbw_open(&m, p);
        mbw_meta(&m, "format", "pbf");
        static const uint8_t gz[4] = {0x1f, 0x8b, 8, 0};
        mbw_tile(&m, 0, 0, 0, gz, 4);
        mbw_close(&m);
    }
    CHECK_RC(cp0_map_store_import(s, p, 1, NULL, NULL), CP0_MAP_E_UNSUPPORTED);
    tpath(p, sizeof p, "downloads/empty.mbtiles");
    {
        mbw_t m;
        mbw_open(&m, p);
        mbw_close(&m);
    }
    CHECK_RC(cp0_map_store_import(s, p, 0, NULL, NULL), CP0_MAP_E_NO_TILE);
    CHECK_RC(cp0_map_store_sources(s, NULL, 0), 0);
    cp0_map_buf_free(&b);
    tiles_free(&ts);
    cp0_map_store_close(s);
    printf("map store: import PMTiles (copy, record, progress), MBTiles in place (archive missing, back), lookup, "
           "remove, refusals\n");
}

static void test_import_folders(const char *store)
{
    cp0_map_store_t *s = open_store(store, 0);
    char id[CP0_MAP_ID_MAX], dir[600];
    cp0_map_import_opts_t o;
    memset(&o, 0, sizeof o);
    o.out_id = id;
    o.out_id_size = sizeof id;
    cp0_map_source_info_t info;

    /* XYZ folder, 341 tiles z0-4 */
    tileset_t ts;
    tiles_make(&ts, 4, 32);
    tpath(dir, sizeof dir, "tiles/MapTileDl output");
    write_folder(dir, &ts, 0, ".png");
    prog_t pg;
    memset(&pg, 0, sizeof pg);
    pg.monotonic = 1;
    o.progress = on_progress;
    o.user = &pg;
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_OK);
    CHECK(!strcmp(id, "maptiledl-output") && pg.monotonic && pg.skipped == 0 && pg.resumed == 0);
    CHECK_RC(cp0_map_store_find(s, id, &info), CP0_MAP_OK);
    CHECK(info.mode == CP0_MAP_MODE_CONVERTED && info.archive == CP0_MAP_ARCHIVE_MBTILES && info.tile_px == 32);
    CHECK(info.minzoom == 0 && info.maxzoom == 4 && info.format == CP0_MAP_FMT_PNG);
    CHECK(!strcmp(info.title, "MapTileDl output") && !strcmp(info.attribution, "\xc2\xa9 OpenStreetMap contributors"));
    CHECK(info.has_bounds && info.bounds.west == -180 && info.bounds.east == 180 && info.bounds.north > 85);
    CHECK(record_has(store, id, "\nmode=converted\n") && record_has(store, id, "\ntiles=341\n"));
    check_tiles_in(s, id, &ts);
    /* the result is a plain MBTiles: read it with the archive reader too */
    cp0_map_archive_t *a;
    CHECK_RC(cp0_map_archive_open(info.path, &a), CP0_MAP_OK);
    cp0_map_archive_info_t ai;
    cp0_map_archive_info(a, &ai);
    CHECK(!strcmp(ai.name, "MapTileDl output") && ai.minzoom == 0 && ai.maxzoom == 4 && ai.has_bounds);
    cp0_map_archive_close(a);
    char part[700];
    snprintf(part, sizeof part, "%s/archives/%s.mbtiles.part", store, id);
    CHECK(!file_exists(part));

    /* TMS folder (gdal2tiles): rows flipped back */
    tpath(dir, sizeof dir, "tiles/gdal");
    write_folder(dir, &ts, 1, ".png");
    char xml[700];
    snprintf(xml, sizeof xml, "%s/tilemapresource.xml", dir);
    write_file(xml, "<TileMap/>", 10);
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_OK);
    check_tiles_in(s, id, &ts);
    /* the same folder read as XYZ on purpose gives other rows */
    o.scheme = CP0_MAP_SCHEME_XYZ;
    o.id = "gdal-as-xyz";
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_OK);
    cp0_map_buf_t b = {0};
    CHECK_RC(cp0_map_store_get_tile(s, "gdal-as-xyz", 1, 0, 0, &b, NULL), CP0_MAP_OK);
    const pm_tile_t *t10 = tiles_find(&ts, 1, 0, 1);
    CHECK(b.len == t10->len && !memcmp(b.data, t10->data, b.len));
    o.scheme = CP0_MAP_SCHEME_AUTO;
    o.id = NULL;
    tiles_free(&ts);

    /* 512 px JPEG folder with metadata.json */
    size_t jn;
    uint8_t *jpg = fixture("q75_512.jpg", &jn);
    tpath(dir, sizeof dir, "tiles/hd");
    tileset_t hd = {0};
    hd.t = (pm_tile_t *)calloc(5, sizeof *hd.t);
    hd.bytes = (bb_t *)calloc(5, sizeof *hd.bytes);
    for (int k = 0; k < 5; k++) {
        const int z = k ? 1 : 0;
        const uint32_t x = k ? (uint32_t)((k - 1) & 1) : 0, y = k ? (uint32_t)((k - 1) >> 1) : 0;
        jpeg_with_comment(&hd.bytes[k], jpg, jn, z, x, y);
        hd.t[k] = (pm_tile_t){z, x, y, hd.bytes[k].d, hd.bytes[k].n, 0};
        hd.n++;
    }
    write_folder(dir, &hd, 0, ".jpg");
    snprintf(xml, sizeof xml, "%s/metadata.json", dir);
    const char *mj = "{\"name\": \"HD test\", \"attribution\": \"\\u00a9 HD\", \"format\": \"jpg\"}";
    write_file(xml, mj, strlen(mj));
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_find(s, id, &info), CP0_MAP_OK);
    CHECK(info.tile_px == 512 && info.format == CP0_MAP_FMT_JPEG && !strcmp(info.title, "HD test"));
    CHECK(!strcmp(info.attribution, "\xc2\xa9 HD"));
    check_tiles_in(s, id, &hd);
    tiles_free(&hd);
    free(jpg);

    /* a mixed folder: PNG, JPEG, .png.tile, extensionless, junk and WebP files, a stray folder */
    tileset_t mix;
    tiles_make(&mix, 2, 16);
    jpg = fixture("q75_256.jpg", &jn);
    tpath(dir, sizeof dir, "tiles/mixed");
    for (int i = 0; i < mix.n; i++) {
        pm_tile_t *t = &mix.t[i];
        const char *ext = i % 4 == 0 ? ".png" : i % 4 == 1 ? ".png.tile" : i % 4 == 2 ? "" : ".jpg";
        if (i % 4 == 3) {
            free(mix.bytes[i].d);
            memset(&mix.bytes[i], 0, sizeof mix.bytes[i]);
            jpeg_with_comment(&mix.bytes[i], jpg, jn, t->z, t->x, t->y);
            t->data = mix.bytes[i].d;
            t->len = mix.bytes[i].n;
        }
        char p[800];
        snprintf(p, sizeof p, "%s/%d/%u/%u%s", dir, t->z, t->x, t->y, ext);
        write_file(p, t->data, t->len);
    }
    char p[800];
    snprintf(p, sizeof p, "%s/2/3/7.png", dir); /* junk named like a tile (y 7 is outside z2: not even counted) */
    write_file(p, "junk", 4);
    snprintf(p, sizeof p, "%s/2/3/2.webp", dir);
    write_file(p, "RIFF\0\0\0\0WEBP", 12);
    snprintf(p, sizeof p, "%s/1/0/1.txt", dir);
    write_file(p, "note", 4);
    snprintf(p, sizeof p, "%s/9/readme", dir);
    write_file(p, "x", 1);
    /* a file named like a tile that is not a picture: counted, then skipped */
    tpath(dir, sizeof dir, "tiles/mixed/0/0");
    snprintf(p, sizeof p, "%s/0.jpeg", dir);
    write_file(p, "not a jpeg", 10);
    tpath(dir, sizeof dir, "tiles/mixed");
    memset(&pg, 0, sizeof pg);
    pg.monotonic = 1;
    o.batch = 4;
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_OK);
    /* 0/0/0.jpeg (junk) and 0/0/0.png share y 0: the junk is skipped, the good one written */
    CHECK(pg.skipped == 1 && pg.conv_done == (uint64_t)mix.n && pg.conv_total == (uint64_t)mix.n);
    CHECK_RC(cp0_map_store_find(s, id, &info), CP0_MAP_OK);
    CHECK(info.minzoom == 0 && info.maxzoom == 2 && info.format == CP0_MAP_FMT_PNG); /* PNG is the majority */
    check_tiles_in(s, id, &mix);
    o.batch = 0;
    tiles_free(&mix);
    free(jpg);

    /* refusals: WebP only, empty, no such folder */
    tpath(dir, sizeof dir, "tiles/webp/0/0");
    mkdirs(dir);
    snprintf(p, sizeof p, "%s/0.webp", dir);
    write_file(p, "RIFF\0\0\0\0WEBP", 12);
    tpath(dir, sizeof dir, "tiles/webp");
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_E_UNSUPPORTED);
    tpath(dir, sizeof dir, "tiles/empty");
    mkdirs(dir);
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_E_NO_TILE);
    cp0_map_buf_free(&b);
    cp0_map_store_close(s);
    printf("map store: folders converted into one MBTiles (XYZ, TMS, 512 px JPEG + metadata.json, mixed names and "
           "formats), refusals\n");
}

static void test_resume(const char *store)
{
    cp0_map_store_t *s = open_store(store, 0);
    tileset_t ts;
    tiles_make(&ts, 4, 16);
    char dir[600], id[CP0_MAP_ID_MAX], part[700];
    tpath(dir, sizeof dir, "tiles/resume");
    write_folder(dir, &ts, 0, ".png");
    prog_t pg;
    memset(&pg, 0, sizeof pg);
    pg.monotonic = 1;
    pg.cancel_after = 3;
    cp0_map_import_opts_t o;
    memset(&o, 0, sizeof o);
    o.batch = 32;
    o.progress = on_progress;
    o.user = &pg;
    o.cancel = &pg.cancel;
    o.out_id = id;
    o.out_id_size = sizeof id;
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_E_CANCELLED);
    snprintf(part, sizeof part, "%s/archives/resume.mbtiles.part", store);
    CHECK(file_exists(part) && !id[0]);
    CHECK_RC(cp0_map_store_find(s, "resume", NULL), CP0_MAP_E_INVALID);
    cp0_map_source_info_t info;
    CHECK_RC(cp0_map_store_find(s, "resume", &info), CP0_MAP_E_NOT_FOUND);
    const uint64_t first = pg.conv_done;
    CHECK(first >= 96 && first < (uint64_t)ts.n);
    /* again, to the end: the rows of the first run are kept, the rest added */
    memset(&pg, 0, sizeof pg);
    pg.monotonic = 1;
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_OK);
    CHECK(!strcmp(id, "resume") && pg.resumed == first && !file_exists(part));
    CHECK(record_has(store, "resume", "\ntiles=341\n"));
    check_tiles_in(s, "resume", &ts);
    /* a cancelled import never resumed can be removed (only its partial file exists) */
    char dir2[600];
    tpath(dir2, sizeof dir2, "tiles/abandoned");
    write_folder(dir2, &ts, 0, ".png");
    memset(&pg, 0, sizeof pg);
    pg.cancel_after = 1;
    CHECK_RC(cp0_map_store_import_ex(s, dir2, &o), CP0_MAP_E_CANCELLED);
    snprintf(part, sizeof part, "%s/archives/abandoned.mbtiles.part", store);
    CHECK(file_exists(part));
    /* another folder with the same name does not resume it: it gets the next id */
    char dir3[600];
    tpath(dir3, sizeof dir3, "elsewhere/abandoned");
    tileset_t small;
    tiles_make(&small, 1, 8);
    write_folder(dir3, &small, 0, ".png");
    o.cancel = NULL;
    o.progress = NULL;
    CHECK_RC(cp0_map_store_import_ex(s, dir3, &o), CP0_MAP_OK);
    CHECK(!strcmp(id, "abandoned-2"));
    check_tiles_in(s, "abandoned-2", &small);
    CHECK_RC(cp0_map_store_remove(s, "abandoned"), CP0_MAP_OK);
    CHECK(!file_exists(part));
    /* a copy left behind by a killed app */
    snprintf(part, sizeof part, "%s/archives/ghost.pmtiles.copy", store);
    write_file(part, "PMTiles", 7);
    CHECK_RC(cp0_map_store_remove(s, "ghost"), CP0_MAP_OK);
    CHECK(!file_exists(part));
    tiles_free(&small);
    tiles_free(&ts);
    cp0_map_store_close(s);
    printf("map store: cancelled conversion resumed (%llu rows kept), abandoned partial removed, same name elsewhere\n",
           (unsigned long long)first);
}

static void test_sources_readonly(const char *store)
{
    /* source records: the store's folder and an app's own folder (the store's wins on the same id) */
    char p[700], appsrc[600];
    const char *rec = "v=1\ntitle=OpenTopoMap\nattribution=Kartendaten: OSM\nminzoom=0\nmaxzoom=17\nfuture_key=1\n";
    snprintf(p, sizeof p, "%s/sources/opentopomap.conf", store);
    write_file(p, rec, strlen(rec));
    rec = "v=2\ntitle=Not for us\n"; /* another version: ignored */
    snprintf(p, sizeof p, "%s/sources/broken.conf", store);
    write_file(p, rec, strlen(rec));
    tpath(appsrc, sizeof appsrc, "app-sources");
    rec = "v=1\ntitle=Shadowed\n";
    snprintf(p, sizeof p, "%s/opentopomap.conf", appsrc);
    write_file(p, rec, strlen(rec));
    rec = "v=1\nname=Thunderforest\n";
    snprintf(p, sizeof p, "%s/thunderforest.conf", appsrc);
    write_file(p, rec, strlen(rec));
    cp0_map_store_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.store_dir = store;
    cfg.sources_dir = appsrc;
    cp0_map_store_t *s;
    CHECK_RC(cp0_map_store_open(&cfg, &s), CP0_MAP_OK);
    cp0_map_source_info_t list[16];
    const int n = cp0_map_store_sources(s, list, 16);
    CHECK(n >= 2);
    int na = 0;
    while (na < n && list[na].kind == CP0_MAP_ENTRY_ARCHIVE) na++;
    for (int i = 1; i < na; i++) CHECK(strcmp(list[i - 1].id, list[i].id) < 0); /* archives sorted */
    CHECK(n - na == 2 && !strcmp(list[na].id, "opentopomap") && !strcmp(list[na].title, "OpenTopoMap"));
    CHECK(!strcmp(list[na + 1].id, "thunderforest") && !strcmp(list[na + 1].title, "Thunderforest"));
    CHECK(list[na].state == CP0_MAP_OK && list[na].maxzoom == 17 && list[na].bytes == 0);
    cp0_map_buf_t b = {0};
    CHECK_RC(cp0_map_store_get_tile(s, "opentopomap", 1, 0, 0, &b, NULL), CP0_MAP_E_NO_TILE); /* no cache yet */
    /* a viewing cache appears (task 3 will write it): its tiles are read */
    tileset_t ts;
    tiles_make(&ts, 1, 8);
    snprintf(p, sizeof p, "%s/tiles", store);
    mkdirs(p);
    snprintf(p, sizeof p, "%s/tiles/opentopomap.mbtiles", store);
    {
        mbw_t m;
        mbw_open(&m, p);
        for (int i = 0; i < ts.n; i++) mbw_tile(&m, ts.t[i].z, ts.t[i].x, ts.t[i].y, ts.t[i].data, ts.t[i].len);
        mbw_close(&m);
    }
    check_tiles_in(s, "opentopomap", &ts);
    tiles_free(&ts);
    cp0_map_cache_stats_t st;
    CHECK_RC(cp0_map_store_cache_stats(s, &st), CP0_MAP_OK);
    CHECK(cp0_map_store_ram_cache(s) != NULL && st.budget > 0);

    /* a second app, read-only: lists and reads, never writes */
    cp0_map_store_t *ro = open_store(store, 1);
    CHECK_RC(cp0_map_store_sources(ro, NULL, 0), n - 1); /* without the app's own folder */
    CHECK_RC(cp0_map_store_cache_stats(ro, &st), CP0_MAP_OK);
    CHECK(st.budget == 1u << 20);
    CHECK_RC(cp0_map_store_import(ro, p, 1, NULL, NULL), CP0_MAP_E_READ_ONLY);
    CHECK_RC(cp0_map_store_remove(ro, "resume"), CP0_MAP_E_READ_ONLY);
    CHECK_RC(cp0_map_store_get_tile(ro, "resume", 2, 1, 1, &b, NULL), CP0_MAP_OK);
    /* the writer imports; the reader sees it after refresh */
    tileset_t t2;
    tiles_make(&t2, 0, 8);
    char src[600];
    tpath(src, sizeof src, "downloads/late.pmtiles");
    pm_write(src, t2.t, t2.n, 1, 0, 2, NULL);
    CHECK_RC(cp0_map_store_import(s, src, 1, NULL, NULL), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_get_tile(ro, "late", 0, 0, 0, &b, NULL), CP0_MAP_E_NOT_FOUND);
    CHECK_RC(cp0_map_store_refresh(ro), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_get_tile(ro, "late", 0, 0, 0, &b, NULL), CP0_MAP_OK);
    tiles_free(&t2);
    cp0_map_store_close(ro);
    /* read-only on a folder that does not exist: empty, nothing created */
    char none[600];
    tpath(none, sizeof none, "no-store-here");
    ro = open_store(none, 1);
    CHECK_RC(cp0_map_store_sources(ro, NULL, 0), 0);
    CHECK(!file_exists(none));
    cp0_map_store_close(ro);
    cp0_map_buf_free(&b);
    cp0_map_store_close(s);
    printf("map store: source records (store + app folders), viewing cache read, read-only second store, refresh\n");
}

typedef struct {
    cp0_map_store_t *s;
    int reads, errors;
    volatile int stop;
} reader_t;

static void *reader_thread(void *arg)
{
    reader_t *r = (reader_t *)arg;
    cp0_map_buf_t b = {0};
    while (!__atomic_load_n(&r->stop, __ATOMIC_RELAXED)) {
        const int rc = cp0_map_store_get_tile(r->s, "resume", 3, (uint32_t)(r->reads % 8), 2, &b, NULL);
        if (rc != CP0_MAP_OK) r->errors++;
        r->reads++;
    }
    cp0_map_buf_free(&b);
    return NULL;
}

static void test_rate(const char *store)
{
    /* 2,000 tiles of about 6 KB (report 055's set averaged 6.1 KB), z0..11 over a small area */
    size_t pn;
    uint8_t *png = fixture("pal256.png", &pn);
    char dir[600];
    tpath(dir, sizeof dir, "tiles/rate");
    int n = 0;
    for (int z = 0; z <= 11 && n < 2000; z++) {
        const uint32_t side = z < 6 ? (1u << z) : 24;
        for (uint32_t x = 0; x < side && n < 2000; x++)
            for (uint32_t y = 0; y < side && n < 2000; y++) {
                bb_t t = {0};
                png_with_text(&t, png, pn, z, x + (z >= 6 ? 1000u >> (11 - z) : 0), y, 6000);
                char p[800];
                snprintf(p, sizeof p, "%s/%d/%u/%u.png", dir, z, x + (z >= 6 ? 1000u >> (11 - z) : 0), y);
                write_file(p, t.d, t.n);
                free(t.d);
                n++;
            }
    }
    free(png);
    cp0_map_store_t *s = open_store(store, 0);
    /* readers keep reading another archive while the import runs */
    reader_t rd = {s, 0, 0, 0};
    pthread_t th;
    CHECK(pthread_create(&th, NULL, reader_thread, &rd) == 0);
    char id[CP0_MAP_ID_MAX];
    cp0_map_import_opts_t o;
    memset(&o, 0, sizeof o);
    o.out_id = id;
    o.out_id_size = sizeof id;
    const double t0 = now_ms();
    CHECK_RC(cp0_map_store_import_ex(s, dir, &o), CP0_MAP_OK);
    const double ms = now_ms() - t0;
    __atomic_store_n(&rd.stop, 1, __ATOMIC_RELAXED);
    pthread_join(th, NULL);
    CHECK(rd.errors == 0 && rd.reads > 0);
    CHECK(record_has(store, id, "\ntiles=2000\n"));
    cp0_map_source_info_t info;
    CHECK_RC(cp0_map_store_find(s, id, &info), CP0_MAP_OK);
    printf("map store: folder of %d tiles (6 KB each, %.1f MB) converted in %.0f ms: %.0f tiles/s on this machine%s; "
           "file %.1f MB; %d tile reads from another thread meanwhile\n",
           n, n * 6000 / 1e6, ms, n / (ms / 1000.0), MAP_TEST_ASAN ? " (with sanitizers)" : "",
           (double)info.bytes / 1e6, rd.reads);
    cp0_map_store_close(s);
}

int main(void)
{
    tmp_init("cp0-map-store");
    char store[600];
    tpath(store, sizeof store, "store");
    test_default_dir();
    test_import_archives(store);
    test_import_folders(store);
    test_resume(store);
    test_sources_readonly(store);
    test_rate(store);
    tmp_done();
    printf("map store OK (%ld checks)\n", g_checks);
    return 0;
}
