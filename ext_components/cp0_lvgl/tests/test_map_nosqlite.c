/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine without SQLite: $CP0_MAP_SQLITE_LIB points the run-time loader at a library that does not exist (a board
 * without libsqlite3-0). MBTiles calls answer CP0_MAP_E_SQLITE_MISSING with the "SQLite library missing" text, a
 * folder import refuses to start, and PMTiles keep working: opened, imported (copied) and read through the store.
 * Its own process: the loader decides once per process.
 */
#include "map_test_util.h"

int main(void)
{
    setenv(CP0_MAP_SQLITE_LIB_ENV, "libsqlite3-not-installed.so.0", 1);
    tmp_init("cp0-map-nosqlite");
    CHECK(cp0_map__sqlite() == NULL);
    char p[600];
    snprintf(p, sizeof p, "%s/ref_view.mbtiles", MAP_FIXTURES);
    cp0_map_archive_t *a;
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_E_SQLITE_MISSING);
    CHECK(strstr(cp0_map_strerror(CP0_MAP_E_SQLITE_MISSING), "SQLite library missing") != NULL);
    char store[600];
    tpath(store, sizeof store, "store");
    cp0_map_store_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.store_dir = store;
    cp0_map_store_t *s;
    CHECK_RC(cp0_map_store_open(&cfg, &s), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_import(s, p, 1, NULL, NULL), CP0_MAP_E_SQLITE_MISSING);
    CHECK_RC(cp0_map_store_import(s, p, 0, NULL, NULL), CP0_MAP_E_SQLITE_MISSING);
    /* a folder conversion needs SQLite to write: refused before anything is written */
    char dir[600];
    tpath(dir, sizeof dir, "folder/0/0");
    mkdirs(dir);
    size_t n;
    uint8_t *png = fixture("pal256.png", &n);
    snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "/0.png");
    write_file(dir, png, n);
    free(png);
    tpath(dir, sizeof dir, "folder");
    CHECK_RC(cp0_map_store_import(s, dir, 1, NULL, NULL), CP0_MAP_E_SQLITE_MISSING);
    CHECK_RC(cp0_map_store_sources(s, NULL, 0), 3); /* no archive: the shipped source records only */
    /* an online source's viewing cache needs SQLite to be read: its tiles answer "missing library" or "no tile" */
    {
        cp0_map_buf_t vb = {0};
        const int rc = cp0_map_store_get_tile(s, "opentopomap", 1, 0, 0, &vb, NULL);
        CHECK(rc == CP0_MAP_E_NO_TILE || rc == CP0_MAP_E_SQLITE_MISSING);
        cp0_map_buf_free(&vb);
    }
    /* PMTiles need no SQLite */
    snprintf(p, sizeof p, "%s/ref_gzip.pmtiles", MAP_FIXTURES);
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_archive_close(a);
    char id[CP0_MAP_ID_MAX];
    cp0_map_import_opts_t o;
    memset(&o, 0, sizeof o);
    o.copy = 1;
    o.out_id = id;
    o.out_id_size = sizeof id;
    CHECK_RC(cp0_map_store_import_ex(s, p, &o), CP0_MAP_OK);
    CHECK(!strcmp(id, "ref_gzip"));
    cp0_map_buf_t b = {0};
    CHECK_RC(cp0_map_store_get_tile(s, "ref_gzip", 1, 1, 0, &b, NULL), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_get_tile(s, NULL, 2, 3, 3, &b, NULL), CP0_MAP_OK);
    cp0_map_buf_free(&b);
    cp0_map_store_close(s);
    tmp_done();
    printf("map without SQLite: MBTiles and folder imports refused with \"%s\", PMTiles work (%ld checks)\n",
           cp0_map_strerror(CP0_MAP_E_SQLITE_MISSING), g_checks);
    return 0;
}
