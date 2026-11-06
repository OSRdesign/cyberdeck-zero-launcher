/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: the MBTiles 1.3 reader (SQLite through cp0_map_sqlite.c). Opened read-only with a 256 KiB page cache and
 * no mmap (report 055: SQLite's default 2 MB page cache showed up as anonymous memory; mmap counts in VmRSS).
 */
#include "cp0_map_internal.h"

#include <stdlib.h>
#include <string.h>

struct cp0_map_mbtiles {
    const cp0_map_sqlite_api_t *sql;
    sqlite3 *db;
    sqlite3_stmt *get;
};

static int parse_zoom(const char *s)
{
    if (!s || !*s) return -1;
    char *end;
    const long v = strtol(s, &end, 10);
    while (*end == ' ') end++;
    return (*end || v < 0 || v > 30) ? -1 : (int)v;
}

static void read_metadata(cp0_map_mbtiles_t *m, cp0_map_archive_info_t *info, int *minz, int *maxz)
{
    sqlite3_stmt *st = NULL;
    if (m->sql->prepare_v2(m->db, "SELECT name, value FROM metadata", -1, &st, NULL) != CP0_MAP_SQLITE_OK) {
        m->sql->finalize(st);
        return; /* no metadata table: everything from the tiles */
    }
    while (m->sql->step(st) == CP0_MAP_SQLITE_ROW) {
        const char *k = (const char *)m->sql->column_text(st, 0);
        const char *v = (const char *)m->sql->column_text(st, 1);
        if (!k || !v) continue;
        if (!strcmp(k, "name"))
            cp0_map__copy_str(info->name, sizeof info->name, v, 1);
        else if (!strcmp(k, "attribution"))
            cp0_map__copy_str(info->attribution, sizeof info->attribution, v, 1);
        else if (!strcmp(k, "format"))
            info->format = cp0_map_format_parse(v);
        else if (!strcmp(k, "bounds"))
            info->has_bounds = cp0_map_bbox_parse(v, &info->bounds) == CP0_MAP_OK;
        else if (!strcmp(k, "minzoom"))
            *minz = parse_zoom(v);
        else if (!strcmp(k, "maxzoom"))
            *maxz = parse_zoom(v);
    }
    m->sql->finalize(st);
}

/* one row of a one-row query into ints; -1 for NULL */
static int query_two_ints(cp0_map_mbtiles_t *m, const char *q, int *a, int *b)
{
    sqlite3_stmt *st = NULL;
    *a = *b = -1;
    int rc = m->sql->prepare_v2(m->db, q, -1, &st, NULL);
    if (rc == CP0_MAP_SQLITE_OK) {
        rc = m->sql->step(st);
        if (rc == CP0_MAP_SQLITE_ROW) {
            if (m->sql->column_type(st, 0) != CP0_MAP_SQLITE_NULL) *a = m->sql->column_int(st, 0);
            if (m->sql->column_type(st, 1) != CP0_MAP_SQLITE_NULL) *b = m->sql->column_int(st, 1);
            rc = CP0_MAP_SQLITE_OK;
        } else if (rc == CP0_MAP_SQLITE_DONE) {
            rc = CP0_MAP_SQLITE_OK;
        }
    }
    m->sql->finalize(st);
    return rc == CP0_MAP_SQLITE_OK ? CP0_MAP_OK : cp0_map__sqlite_code(rc);
}

/* the size (and format, when not declared) from one tile of the deepest zoom */
static void sample_tile(cp0_map_mbtiles_t *m, cp0_map_archive_info_t *info)
{
    sqlite3_stmt *st = NULL;
    if (info->maxzoom < 0) return;
    if (m->sql->prepare_v2(m->db, "SELECT tile_data FROM tiles WHERE zoom_level = ?1 LIMIT 1", -1, &st, NULL) ==
        CP0_MAP_SQLITE_OK) {
        m->sql->bind_int(st, 1, info->maxzoom);
        if (m->sql->step(st) == CP0_MAP_SQLITE_ROW) {
            const uint8_t *d = (const uint8_t *)m->sql->column_blob(st, 0);
            const int n = m->sql->column_bytes(st, 0);
            if (d && n > 0) {
                int w = 0, h = 0;
                cp0_map_format_t f = CP0_MAP_FMT_UNKNOWN;
                if (cp0_map_tile_probe(d, (size_t)n, &w, &h, &f) == CP0_MAP_OK) info->tile_px = w;
                if (info->format == CP0_MAP_FMT_UNKNOWN) info->format = cp0_map_format_sniff(d, (size_t)n);
            }
        }
    }
    m->sql->finalize(st);
}

void cp0_map__mbtiles_close(cp0_map_mbtiles_t *m)
{
    if (!m) return;
    if (m->get) m->sql->finalize(m->get);
    if (m->db) m->sql->close_v2(m->db);
    free(m);
}

int cp0_map__mbtiles_open(const char *path, cp0_map_mbtiles_t **out, cp0_map_archive_info_t *info)
{
    *out = NULL;
    const cp0_map_sqlite_api_t *sql = cp0_map__sqlite();
    if (!sql) return CP0_MAP_E_SQLITE_MISSING;
    cp0_map_mbtiles_t *m = (cp0_map_mbtiles_t *)calloc(1, sizeof *m);
    if (!m) return CP0_MAP_E_NOMEM;
    m->sql = sql;
    int rc = sql->open_v2(path, &m->db, CP0_MAP_SQLITE_OPEN_READONLY | CP0_MAP_SQLITE_OPEN_NOMUTEX, NULL);
    if (rc != CP0_MAP_SQLITE_OK) {
        cp0_map__mbtiles_close(m);
        return cp0_map__sqlite_code(rc);
    }
    sql->busy_timeout(m->db, 2000);
    sql->exec(m->db, "PRAGMA cache_size=-256; PRAGMA mmap_size=0; PRAGMA query_only=1", NULL, NULL, NULL);
    rc = sql->prepare_v2(m->db, "SELECT tile_data FROM tiles WHERE zoom_level = ?1 AND tile_column = ?2 AND "
                                "tile_row = ?3",
                         -1, &m->get, NULL);
    if (rc != CP0_MAP_SQLITE_OK) {
        /* not a database, or no tiles table */
        const int code = cp0_map__sqlite_code(rc);
        cp0_map__mbtiles_close(m);
        return code == CP0_MAP_E_SQLITE ? CP0_MAP_E_FORMAT : code;
    }
    int minz = -1, maxz = -1;
    read_metadata(m, info, &minz, &maxz);
    if (minz < 0 || maxz < 0 || minz > maxz) {
        rc = query_two_ints(m, "SELECT MIN(zoom_level), MAX(zoom_level) FROM tiles", &minz, &maxz);
        if (rc) {
            cp0_map__mbtiles_close(m);
            return rc;
        }
    }
    info->minzoom = minz;
    info->maxzoom = maxz;
    sample_tile(m, info);
    *out = m;
    return CP0_MAP_OK;
}

int cp0_map__mbtiles_get(cp0_map_mbtiles_t *m, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out)
{
    const cp0_map_sqlite_api_t *sql = m->sql;
    sql->bind_int(m->get, 1, z);
    sql->bind_int64(m->get, 2, (long long)x);
    sql->bind_int64(m->get, 3, (long long)cp0_map_tms_row(z, y));
    int rc = sql->step(m->get);
    int result;
    if (rc == CP0_MAP_SQLITE_ROW) {
        const void *d = sql->column_blob(m->get, 0);
        const int n = sql->column_bytes(m->get, 0);
        if (!d || n <= 0)
            result = CP0_MAP_E_NO_TILE;
        else if ((size_t)n > CP0_MAP_TILE_BYTES_MAX)
            result = CP0_MAP_E_TOO_LARGE;
        else if ((result = cp0_map_buf_reserve(out, (size_t)n)) == CP0_MAP_OK) {
            memcpy(out->data, d, (size_t)n);
            out->len = (size_t)n;
        }
    } else if (rc == CP0_MAP_SQLITE_DONE) {
        result = CP0_MAP_E_NO_TILE;
    } else {
        result = cp0_map__sqlite_code(sql->extended_errcode(m->db));
        if (result == CP0_MAP_OK) result = cp0_map__sqlite_code(rc);
        if (result == CP0_MAP_OK) result = CP0_MAP_E_SQLITE;
    }
    sql->reset(m->get);
    return result;
}
