/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: imports into the store (see cp0_map_store.h): an MBTiles / PMTiles file copied or registered in place,
 * a z/x/y tile folder converted into one MBTiles. The folder conversion follows report 055 (M8): a new file with a
 * rollback journal and synchronous=NORMAL, 256 tiles per transaction, rows in (z, x, y) order; it resumes an
 * interrupted conversion of the same folder by skipping the rows already written.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* realpath, O_CLOEXEC, fsync */
#endif
#include "cp0_map_store_priv.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define COPY_CHUNK (256 * 1024)
#define DEFAULT_BATCH 256
#define SOURCE_PATH_KEY "cp0_import_source"

static int cancelled(const cp0_map_import_opts_t *o)
{
    return o->cancel && __atomic_load_n((const int *)o->cancel, __ATOMIC_RELAXED) != 0;
}

static void report(const cp0_map_import_opts_t *o, cp0_map_phase_t phase, uint64_t done, uint64_t total,
                   uint64_t skipped, uint64_t resumed)
{
    if (!o->progress) return;
    cp0_map_progress_t p = {phase, done, total, skipped, resumed};
    o->progress(o->user, &p);
}

/* the last path element without its extension (".mbtiles", ".pmtiles"; a folder keeps its name) */
static void base_name(const char *path, int strip_ext, char *out, size_t size)
{
    char tmp[CP0_MAP_PATH_MAX];
    cp0_map__copy_str(tmp, sizeof tmp, path, 0);
    size_t n = strlen(tmp);
    while (n > 1 && tmp[n - 1] == '/') tmp[--n] = '\0';
    const char *b = strrchr(tmp, '/');
    b = b ? b + 1 : tmp;
    cp0_map__copy_str(out, size, b, 0);
    if (strip_ext) {
        char *dot = strrchr(out, '.');
        if (dot && dot != out) *dot = '\0';
    }
}

/* the id to use: opts->id (must be free), else from the name with -2, -3... */
static int choose_id(cp0_map_store_t *s, const cp0_map_import_opts_t *o, const char *name, char *id, size_t size)
{
    if (o->id) {
        if (!cp0_map__id_valid(o->id)) return CP0_MAP_E_INVALID;
        cp0_map__copy_str(id, size, o->id, 0);
        return cp0_map__record_exists(s, id) ? CP0_MAP_E_EXISTS : CP0_MAP_OK;
    }
    char base[CP0_MAP_ID_MAX];
    cp0_map__make_id(name, base, sizeof base);
    cp0_map__copy_str(id, size, base, 0);
    for (int k = 2; cp0_map__record_exists(s, id); k++) {
        char suffix[16];
        snprintf(suffix, sizeof suffix, "-%d", k);
        base[CP0_MAP_ID_MAX - 1 - strlen(suffix)] = '\0';
        if (cp0_map__fmt(id, size, "%s%s", base, suffix)) return CP0_MAP_E_INVALID;
        if (k > 9999) return CP0_MAP_E_EXISTS;
    }
    return CP0_MAP_OK;
}

static void set_out_id(const cp0_map_import_opts_t *o, const char *id)
{
    if (o->out_id && o->out_id_size) cp0_map__copy_str(o->out_id, o->out_id_size, id, 0);
}

/* registers a finished archive: record file, then the store's list */
static int register_archive(cp0_map_store_t *s, const cp0_map_rec_t *r)
{
    int rc = cp0_map__record_write(s, r);
    if (rc) return rc;
    pthread_mutex_lock(&s->mu);
    cp0_map_cache_drop(s->ram, cp0_map_source_hash(r->info.id));
    rc = cp0_map__store_reload_locked(s);
    pthread_mutex_unlock(&s->mu);
    return rc;
}

static void fill_from_info(cp0_map_rec_t *r, const cp0_map_archive_info_t *ai, const cp0_map_import_opts_t *o,
                           const char *fallback_title)
{
    cp0_map_source_info_t *i = &r->info;
    i->kind = CP0_MAP_ENTRY_ARCHIVE;
    i->archive = ai->kind;
    i->format = ai->format;
    i->tile_px = ai->tile_px;
    i->minzoom = ai->minzoom;
    i->maxzoom = ai->maxzoom;
    i->has_bounds = ai->has_bounds;
    i->bounds = ai->bounds;
    i->bytes = ai->bytes;
    cp0_map__copy_str(i->title, sizeof i->title, o->title ? o->title : ai->name[0] ? ai->name : fallback_title, 1);
    cp0_map__copy_str(i->attribution, sizeof i->attribution, o->attribution ? o->attribution : ai->attribution, 1);
}

/* ------------------------------------------------------------------------------------------------ archive files */

static int copy_file(const char *from, const char *to, const cp0_map_import_opts_t *o, uint64_t total)
{
    const int in = open(from, O_RDONLY | O_CLOEXEC);
    if (in < 0) return errno == ENOENT ? CP0_MAP_E_NOT_FOUND : CP0_MAP_E_IO;
    const int out = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) {
        close(in);
        return CP0_MAP_E_IO;
    }
    uint8_t *buf = (uint8_t *)malloc(COPY_CHUNK);
    int rc = buf ? CP0_MAP_OK : CP0_MAP_E_NOMEM;
    uint64_t done = 0;
    report(o, CP0_MAP_PHASE_COPY, 0, total, 0, 0);
    while (!rc) {
        if (cancelled(o)) {
            rc = CP0_MAP_E_CANCELLED;
            break;
        }
        const ssize_t r = read(in, buf, COPY_CHUNK);
        if (r < 0) {
            if (errno == EINTR) continue;
            rc = CP0_MAP_E_IO;
            break;
        }
        if (r == 0) break;
        for (ssize_t w = 0; w < r;) {
            const ssize_t k = write(out, buf + w, (size_t)(r - w));
            if (k < 0) {
                if (errno == EINTR) continue;
                rc = CP0_MAP_E_IO;
                break;
            }
            w += k;
        }
        done += (uint64_t)r;
        report(o, CP0_MAP_PHASE_COPY, done, total, 0, 0);
    }
    free(buf);
    close(in);
    if (!rc && fsync(out) != 0) rc = CP0_MAP_E_IO;
    if (close(out) != 0 && !rc) rc = CP0_MAP_E_IO;
    if (rc) unlink(to);
    return rc;
}

static int raster_ok(const cp0_map_archive_info_t *ai)
{
    if (ai->format == CP0_MAP_FMT_MVT || ai->format == CP0_MAP_FMT_WEBP) return CP0_MAP_E_UNSUPPORTED;
    if (ai->maxzoom < 0) return CP0_MAP_E_NO_TILE;
    return CP0_MAP_OK;
}

static int import_archive(cp0_map_store_t *s, const char *path, const cp0_map_import_opts_t *o)
{
    char abs[PATH_MAX];
    if (!realpath(path, abs) || strlen(abs) >= CP0_MAP_PATH_MAX) return CP0_MAP_E_INVALID;
    cp0_map_archive_t *a;
    int rc = cp0_map_archive_open(abs, &a);
    if (rc) return rc == CP0_MAP_E_ARCHIVE_MISSING ? CP0_MAP_E_NOT_FOUND : rc;
    cp0_map_archive_info_t ai;
    cp0_map_archive_info(a, &ai);
    cp0_map_archive_close(a);
    rc = raster_ok(&ai);
    if (rc) return rc;
    char name[CP0_MAP_PATH_MAX];
    base_name(abs, 1, name, sizeof name);
    cp0_map_rec_t r;
    memset(&r, 0, sizeof r);
    r.tiles = -1;
    cp0_map__copy_str(r.source_path, sizeof r.source_path, abs, 0);
    if (!o->copy) {
        /* the same file registered twice in place is refused */
        pthread_mutex_lock(&s->mu);
        for (int k = 0; k < s->narchives && !rc; k++)
            if (s->recs[k].info.mode == CP0_MAP_MODE_INPLACE && !strcmp(s->recs[k].info.path, abs)) rc = CP0_MAP_E_EXISTS;
        pthread_mutex_unlock(&s->mu);
        if (rc) return rc;
    }
    rc = choose_id(s, o, name, r.info.id, sizeof r.info.id);
    if (rc) return rc;
    if (o->copy) {
        const char *ext = ai.kind == CP0_MAP_ARCHIVE_PMTILES ? ".pmtiles" : ".mbtiles";
        char dest[CP0_MAP_PATH_MAX], tmp[CP0_MAP_PATH_MAX];
        char ext_part[24];
        snprintf(ext_part, sizeof ext_part, "%s.copy", ext);
        if (cp0_map__archive_file(s, r.info.id, ext, dest, sizeof dest) ||
            cp0_map__archive_file(s, r.info.id, ext_part, tmp, sizeof tmp))
            return CP0_MAP_E_INVALID;
        rc = copy_file(abs, tmp, o, ai.bytes);
        if (rc) return rc;
        if (rename(tmp, dest) != 0) {
            unlink(tmp);
            return CP0_MAP_E_IO;
        }
        report(o, CP0_MAP_PHASE_CHECK, 0, 1, 0, 0);
        rc = cp0_map_archive_open(dest, &a);
        if (rc) {
            unlink(dest);
            return rc;
        }
        cp0_map_archive_info(a, &ai);
        cp0_map_archive_close(a);
        cp0_map__copy_str(r.info.path, sizeof r.info.path, dest, 0);
        r.info.mode = CP0_MAP_MODE_COPY;
    } else {
        cp0_map__copy_str(r.info.path, sizeof r.info.path, abs, 0);
        r.info.mode = CP0_MAP_MODE_INPLACE;
    }
    fill_from_info(&r, &ai, o, name);
    rc = register_archive(s, &r);
    if (rc) {
        if (r.info.mode == CP0_MAP_MODE_COPY) unlink(r.info.path);
        return rc;
    }
    report(o, CP0_MAP_PHASE_CHECK, 1, 1, 0, 0);
    set_out_id(o, r.info.id);
    return CP0_MAP_OK;
}

/* -------------------------------------------------------------------------------------------- z/x/y folders */

/* "<digits>" -> value (no sign, no leading '+'); -1 otherwise or over max */
static long parse_num(const char *s, size_t n, long max)
{
    if (!n || n > 10) return -1;
    long long v = 0;
    for (size_t i = 0; i < n; i++) {
        if (!isdigit((unsigned char)s[i])) return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v > (long long)max ? -1 : (long)v;
}

/* a tile file name: <y>[.png|.jpg|.jpeg|.webp][.tile] (any case); its y, or -1. *webp set for a .webp name. */
static long tile_name_y(const char *name, long max, int *webp)
{
    size_t n = 0;
    while (isdigit((unsigned char)name[n])) n++;
    const long y = parse_num(name, n, max);
    if (y < 0) return -1;
    const char *ext = name + n;
    static const char *const ok[] = {"", ".png", ".jpg", ".jpeg", ".webp", ".tile", ".png.tile", ".jpg.tile",
                                     ".jpeg.tile", ".webp.tile"};
    for (size_t k = 0; k < sizeof ok / sizeof ok[0]; k++)
        if (!strcasecmp(ext, ok[k])) {
            if (webp) *webp = strncasecmp(ext, ".webp", 5) == 0;
            return y;
        }
    return -1;
}

typedef struct {
    long v;
    char *name;
} num_entry_t;

static int cmp_num(const void *a, const void *b)
{
    const long x = ((const num_entry_t *)a)->v, y = ((const num_entry_t *)b)->v;
    if (x != y) return x < y ? -1 : 1;
    return strcmp(((const num_entry_t *)a)->name, ((const num_entry_t *)b)->name);
}

static void free_entries(num_entry_t *e, int n)
{
    for (int i = 0; i < n; i++) free(e[i].name);
    free(e);
}

/* the numbered entries of a folder, sorted: directories (want_dir) or tile files; NULL with *n 0 when empty */
static int list_numbered(const char *dir, int want_dir, long max, num_entry_t **out, int *n, int *webp_seen)
{
    *out = NULL;
    *n = 0;
    DIR *d = opendir(dir);
    if (!d) return errno == ENOMEM ? CP0_MAP_E_NOMEM : CP0_MAP_OK;
    int cap = 0, rc = CP0_MAP_OK;
    num_entry_t *v = NULL;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        long num;
        int webp = 0;
        if (want_dir) {
            num = parse_num(e->d_name, strlen(e->d_name), max);
            if (num < 0) continue;
            if (e->d_type != DT_DIR && e->d_type != DT_UNKNOWN && e->d_type != DT_LNK) continue;
        } else {
            num = tile_name_y(e->d_name, max, &webp);
            if (num < 0) continue;
            if (e->d_type == DT_DIR) continue;
            if (webp) {
                if (webp_seen) (*webp_seen)++;
                continue;
            }
        }
        if (*n == cap) {
            const int nc = cap ? cap * 2 : 64;
            num_entry_t *nv = (num_entry_t *)realloc(v, (size_t)nc * sizeof *v);
            if (!nv) {
                rc = CP0_MAP_E_NOMEM;
                break;
            }
            v = nv;
            cap = nc;
        }
        v[*n].v = num;
        v[*n].name = strdup(e->d_name);
        if (!v[*n].name) {
            rc = CP0_MAP_E_NOMEM;
            break;
        }
        (*n)++;
    }
    closedir(d);
    if (rc) {
        free_entries(v, *n);
        *n = 0;
        return rc;
    }
    if (*n > 1) qsort(v, (size_t)*n, sizeof *v, cmp_num);
    *out = v;
    return CP0_MAP_OK;
}

typedef struct {
    int present;
    uint32_t x0, x1, y0, y1;     /* XYZ extents of the tiles at this zoom */
} zoom_extent_t;

/* pass 1: count the tile files and the extent of each zoom */
static int scan_folder(const char *dir, int tms, const cp0_map_import_opts_t *o, uint64_t *total, zoom_extent_t *ext,
                       int *webp)
{
    num_entry_t *zs;
    int nz;
    *total = 0;
    int rc = list_numbered(dir, 1, 30, &zs, &nz, NULL);
    if (rc) return rc;
    char path[CP0_MAP_PATH_MAX];
    for (int iz = 0; iz < nz && !rc; iz++) {
        const int z = (int)zs[iz].v;
        if (iz > 0 && zs[iz - 1].v == z) continue; /* "1" and "01" */
        const long max = (1L << z) - 1;
        num_entry_t *xs;
        int nx;
        if (cp0_map__fmt(path, sizeof path, "%s/%s", dir, zs[iz].name)) continue;
        rc = list_numbered(path, 1, max, &xs, &nx, NULL);
        for (int ix = 0; ix < nx && !rc; ix++) {
            if (cancelled(o)) {
                rc = CP0_MAP_E_CANCELLED;
                break;
            }
            num_entry_t *ys;
            int ny;
            char xpath[CP0_MAP_PATH_MAX];
            if (cp0_map__fmt(xpath, sizeof xpath, "%s/%s", path, xs[ix].name)) continue;
            rc = list_numbered(xpath, 0, max, &ys, &ny, webp);
            for (int iy = 0; iy < ny && !rc; iy++) {
                if (iy > 0 && ys[iy - 1].v == ys[iy].v) continue;
                const uint32_t x = (uint32_t)xs[ix].v;
                const uint32_t y = tms ? cp0_map_tms_row(z, (uint32_t)ys[iy].v) : (uint32_t)ys[iy].v;
                zoom_extent_t *e = &ext[z];
                if (!e->present) {
                    e->present = 1;
                    e->x0 = e->x1 = x;
                    e->y0 = e->y1 = y;
                } else {
                    if (x < e->x0) e->x0 = x;
                    if (x > e->x1) e->x1 = x;
                    if (y < e->y0) e->y0 = y;
                    if (y > e->y1) e->y1 = y;
                }
                (*total)++;
            }
            free_entries(ys, ny);
            report(o, CP0_MAP_PHASE_SCAN, *total, 0, 0, 0);
        }
        free_entries(xs, nx);
    }
    free_entries(zs, nz);
    return rc;
}

static int read_tile_file(const char *path, cp0_map_buf_t *buf)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return CP0_MAP_E_IO;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uint64_t)st.st_size > CP0_MAP_TILE_BYTES_MAX) {
        close(fd);
        return CP0_MAP_E_TOO_LARGE;
    }
    int rc = cp0_map_buf_reserve(buf, (size_t)st.st_size);
    if (!rc) rc = cp0_map__pread_all(fd, buf->data, (size_t)st.st_size, 0);
    close(fd);
    if (!rc) buf->len = (size_t)st.st_size;
    return rc;
}

typedef struct {
    const cp0_map_sqlite_api_t *sql;
    sqlite3 *db;
    sqlite3_stmt *ins, *has;
} conv_db_t;

static int db_exec(conv_db_t *c, const char *q)
{
    const int rc = c->sql->exec(c->db, q, NULL, NULL, NULL);
    return rc == CP0_MAP_SQLITE_OK ? CP0_MAP_OK : cp0_map__sqlite_code(c->sql->extended_errcode(c->db));
}

static void db_close(conv_db_t *c)
{
    if (c->ins) c->sql->finalize(c->ins);
    if (c->has) c->sql->finalize(c->has);
    if (c->db) c->sql->close_v2(c->db);
    c->ins = c->has = NULL;
    c->db = NULL;
}

static int meta_put(conv_db_t *c, const char *k, const char *v)
{
    sqlite3_stmt *st = NULL;
    int rc = c->sql->prepare_v2(c->db, "INSERT INTO metadata (name, value) VALUES (?1, ?2)", -1, &st, NULL);
    if (rc == CP0_MAP_SQLITE_OK) {
        c->sql->bind_text(st, 1, k, -1, CP0_MAP_SQLITE_TRANSIENT);
        c->sql->bind_text(st, 2, v, -1, CP0_MAP_SQLITE_TRANSIENT);
        rc = c->sql->step(st);
        rc = rc == CP0_MAP_SQLITE_DONE ? CP0_MAP_SQLITE_OK : rc;
    }
    c->sql->finalize(st);
    return rc == CP0_MAP_SQLITE_OK ? CP0_MAP_OK : cp0_map__sqlite_code(rc);
}

/* the source folder a partial file belongs to ("" when it has none); 0 or an error */
static int meta_get(conv_db_t *c, const char *k, char *out, size_t size)
{
    sqlite3_stmt *st = NULL;
    out[0] = '\0';
    int rc = c->sql->prepare_v2(c->db, "SELECT value FROM metadata WHERE name = ?1", -1, &st, NULL);
    if (rc == CP0_MAP_SQLITE_OK) {
        c->sql->bind_text(st, 1, k, -1, CP0_MAP_SQLITE_TRANSIENT);
        rc = c->sql->step(st);
        if (rc == CP0_MAP_SQLITE_ROW) {
            const char *v = (const char *)c->sql->column_text(st, 0);
            cp0_map__copy_str(out, size, v, 0);
            rc = CP0_MAP_SQLITE_OK;
        } else if (rc == CP0_MAP_SQLITE_DONE) {
            rc = CP0_MAP_SQLITE_OK;
        }
    }
    c->sql->finalize(st);
    return rc == CP0_MAP_SQLITE_OK ? CP0_MAP_OK : cp0_map__sqlite_code(rc);
}

static long long count_rows(conv_db_t *c)
{
    sqlite3_stmt *st = NULL;
    long long n = -1;
    if (c->sql->prepare_v2(c->db, "SELECT COUNT(*) FROM tiles", -1, &st, NULL) == CP0_MAP_SQLITE_OK &&
        c->sql->step(st) == CP0_MAP_SQLITE_ROW)
        n = c->sql->column_int64(st, 0);
    c->sql->finalize(st);
    return n;
}

/* opens or creates the partial file; EXISTS when it belongs to another folder */
static int conv_open(conv_db_t *c, const char *part, const char *src_dir, int *resuming)
{
    *resuming = 0;
    int rc = c->sql->open_v2(part, &c->db, CP0_MAP_SQLITE_OPEN_READWRITE | CP0_MAP_SQLITE_OPEN_CREATE | CP0_MAP_SQLITE_OPEN_NOMUTEX,
                             NULL);
    if (rc != CP0_MAP_SQLITE_OK) return cp0_map__sqlite_code(rc);
    c->sql->busy_timeout(c->db, 2000);
    rc = db_exec(c, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=NORMAL; PRAGMA cache_size=-1024;"
                    "CREATE TABLE IF NOT EXISTS metadata (name text, value text);"
                    "CREATE TABLE IF NOT EXISTS tiles (zoom_level integer, tile_column integer, tile_row integer,"
                    " tile_data blob);"
                    "CREATE UNIQUE INDEX IF NOT EXISTS tile_index ON tiles (zoom_level, tile_column, tile_row);");
    if (rc) return rc;
    char prev[CP0_MAP_PATH_MAX];
    rc = meta_get(c, SOURCE_PATH_KEY, prev, sizeof prev);
    if (rc) return rc;
    if (prev[0] && strcmp(prev, src_dir) != 0) return CP0_MAP_E_EXISTS;
    if (!prev[0]) {
        rc = meta_put(c, SOURCE_PATH_KEY, src_dir);
        if (rc) return rc;
    }
    *resuming = count_rows(c) > 0;
    if (c->sql->prepare_v2(c->db, "INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?1, ?2, ?3, ?4)",
                           -1, &c->ins, NULL) != CP0_MAP_SQLITE_OK ||
        c->sql->prepare_v2(c->db, "SELECT 1 FROM tiles WHERE zoom_level = ?1 AND tile_column = ?2 AND tile_row = ?3",
                           -1, &c->has, NULL) != CP0_MAP_SQLITE_OK)
        return CP0_MAP_E_SQLITE;
    return CP0_MAP_OK;
}

static int row_exists(conv_db_t *c, int z, uint32_t x, uint32_t row)
{
    c->sql->bind_int(c->has, 1, z);
    c->sql->bind_int64(c->has, 2, (long long)x);
    c->sql->bind_int64(c->has, 3, (long long)row);
    const int rc = c->sql->step(c->has);
    c->sql->reset(c->has);
    return rc == CP0_MAP_SQLITE_ROW;
}

typedef struct {
    uint64_t done, total, skipped, resumed, png, jpg;
    int tile_px;
} conv_stats_t;

/* pass 2: every tile file into the partial file, `batch` per transaction */
static int convert_tiles(conv_db_t *c, const char *dir, int tms, int resuming, const cp0_map_import_opts_t *o,
                         conv_stats_t *cs)
{
    const int batch = o->batch > 0 ? o->batch : DEFAULT_BATCH;
    num_entry_t *zs;
    int nz;
    int rc = list_numbered(dir, 1, 30, &zs, &nz, NULL);
    if (rc) return rc;
    cp0_map_buf_t buf = {0};
    int in_txn = 0, pending = 0;
    char zpath[CP0_MAP_PATH_MAX], xpath[CP0_MAP_PATH_MAX], file[CP0_MAP_PATH_MAX];
    rc = db_exec(c, "BEGIN");
    in_txn = rc == CP0_MAP_OK;
    for (int iz = 0; iz < nz && !rc; iz++) {
        const int z = (int)zs[iz].v;
        if (iz > 0 && zs[iz - 1].v == z) continue;
        const long max = (1L << z) - 1;
        num_entry_t *xs;
        int nx;
        if (cp0_map__fmt(zpath, sizeof zpath, "%s/%s", dir, zs[iz].name)) continue;
        rc = list_numbered(zpath, 1, max, &xs, &nx, NULL);
        for (int ix = 0; ix < nx && !rc; ix++) {
            num_entry_t *ys;
            int ny;
            if (cp0_map__fmt(xpath, sizeof xpath, "%s/%s", zpath, xs[ix].name)) continue;
            rc = list_numbered(xpath, 0, max, &ys, &ny, NULL);
            long have_y = -1; /* the y already written from this folder: 5.png and 5.jpg, the first good one wins */
            for (int iy = 0; iy < ny && !rc; iy++) {
                if (ys[iy].v == have_y) {
                    cs->skipped++;
                    continue;
                }
                if (cancelled(o)) {
                    rc = CP0_MAP_E_CANCELLED;
                    break;
                }
                const uint32_t x = (uint32_t)xs[ix].v;
                const uint32_t row = tms ? (uint32_t)ys[iy].v : cp0_map_tms_row(z, (uint32_t)ys[iy].v);
                if (resuming && row_exists(c, z, x, row)) {
                    cs->resumed++;
                    cs->done++;
                    have_y = ys[iy].v;
                    continue;
                }
                if (cp0_map__fmt(file, sizeof file, "%s/%s", xpath, ys[iy].name) || read_tile_file(file, &buf)) {
                    cs->skipped++;
                    continue;
                }
                const cp0_map_format_t f = cp0_map_format_sniff(buf.data, buf.len);
                if (f != CP0_MAP_FMT_PNG && f != CP0_MAP_FMT_JPEG) {
                    cs->skipped++;
                    continue;
                }
                if (!cs->tile_px) {
                    int w = 0, h = 0;
                    if (cp0_map_tile_probe(buf.data, buf.len, &w, &h, NULL) == CP0_MAP_OK) cs->tile_px = w;
                }
                if (f == CP0_MAP_FMT_PNG) cs->png++;
                else cs->jpg++;
                c->sql->bind_int(c->ins, 1, z);
                c->sql->bind_int64(c->ins, 2, (long long)x);
                c->sql->bind_int64(c->ins, 3, (long long)row);
                c->sql->bind_blob(c->ins, 4, buf.data, (int)buf.len, CP0_MAP_SQLITE_STATIC);
                const int st = c->sql->step(c->ins);
                c->sql->reset(c->ins);
                if (st != CP0_MAP_SQLITE_DONE) {
                    rc = cp0_map__sqlite_code(c->sql->extended_errcode(c->db));
                    if (!rc) rc = CP0_MAP_E_SQLITE;
                    break;
                }
                cs->done++;
                have_y = ys[iy].v;
                if (++pending >= batch) {
                    rc = db_exec(c, "COMMIT");
                    in_txn = 0;
                    if (!rc) {
                        rc = db_exec(c, "BEGIN");
                        in_txn = rc == CP0_MAP_OK;
                    }
                    pending = 0;
                    report(o, CP0_MAP_PHASE_CONVERT, cs->done, cs->total, cs->skipped, cs->resumed);
                }
            }
            free_entries(ys, ny);
        }
        free_entries(xs, nx);
    }
    free_entries(zs, nz);
    cp0_map_buf_free(&buf);
    /* what is written stays (a cancel or an error can be resumed) */
    if (in_txn) {
        const int crc = db_exec(c, "COMMIT");
        if (!rc) rc = crc;
    }
    report(o, CP0_MAP_PHASE_CONVERT, cs->done, cs->total, cs->skipped, cs->resumed);
    return rc;
}

/* metadata.json of a tile folder (mbutil's export): name and attribution, when present */
static void folder_metadata(const char *dir, char *name, size_t nsize, char *attr, size_t asize)
{
    char path[CP0_MAP_PATH_MAX];
    name[0] = attr[0] = '\0';
    if (cp0_map__fmt(path, sizeof path, "%s/metadata.json", dir)) return;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    char *buf = (char *)malloc(65536);
    if (buf) {
        size_t n = 0;
        ssize_t r;
        while (n < 65535 && (r = read(fd, buf + n, 65535 - n)) > 0) n += (size_t)r;
        buf[n] = '\0';
        char tmp[256];
        if (cp0_map__json_string(buf, n, "name", tmp, sizeof tmp) == 0) cp0_map__copy_str(name, nsize, tmp, 1);
        if (cp0_map__json_string(buf, n, "attribution", tmp, sizeof tmp) == 0) cp0_map__copy_str(attr, asize, tmp, 1);
        free(buf);
    }
    close(fd);
}

/* the folder's id: opts->id, else from its name; a partial file of the same folder is resumed, one of another
 * folder (or a finished archive) makes the id move on to -2, -3... */
static int folder_id(cp0_map_store_t *s, const cp0_map_import_opts_t *o, const char *name, const char *abs, char *id,
                     size_t size, const cp0_map_sqlite_api_t *sql)
{
    char base[CP0_MAP_ID_MAX];
    if (o->id) {
        if (!cp0_map__id_valid(o->id)) return CP0_MAP_E_INVALID;
        cp0_map__copy_str(base, sizeof base, o->id, 0);
    } else {
        cp0_map__make_id(name, base, sizeof base);
    }
    for (int k = 1; k < 10000; k++) {
        if (k == 1)
            cp0_map__copy_str(id, size, base, 0);
        else {
            char suffix[16];
            snprintf(suffix, sizeof suffix, "-%d", k);
            char b[CP0_MAP_ID_MAX];
            cp0_map__copy_str(b, sizeof b, base, 0);
            b[CP0_MAP_ID_MAX - 1 - strlen(suffix)] = '\0';
            if (cp0_map__fmt(id, size, "%s%s", b, suffix)) return CP0_MAP_E_INVALID;
        }
        if (cp0_map__record_exists(s, id)) {
            if (o->id) return CP0_MAP_E_EXISTS;
            continue;
        }
        char part[CP0_MAP_PATH_MAX];
        struct stat st;
        if (cp0_map__archive_file(s, id, ".mbtiles.part", part, sizeof part)) return CP0_MAP_E_INVALID;
        if (stat(part, &st) != 0) return CP0_MAP_OK; /* a fresh id */
        /* a partial file: ours to resume only when it was started from the same folder */
        conv_db_t c = {sql, NULL, NULL, NULL};
        char prev[CP0_MAP_PATH_MAX] = "";
        int rc = c.sql->open_v2(part, &c.db, CP0_MAP_SQLITE_OPEN_READONLY | CP0_MAP_SQLITE_OPEN_NOMUTEX, NULL);
        if (rc == CP0_MAP_SQLITE_OK) rc = meta_get(&c, SOURCE_PATH_KEY, prev, sizeof prev);
        db_close(&c);
        if (rc == CP0_MAP_OK && (!prev[0] || !strcmp(prev, abs))) return CP0_MAP_OK;
        if (o->id) return CP0_MAP_E_EXISTS;
    }
    return CP0_MAP_E_EXISTS;
}

static int import_folder(cp0_map_store_t *s, const char *path, const cp0_map_import_opts_t *o)
{
    char abs[PATH_MAX];
    if (!realpath(path, abs) || strlen(abs) >= CP0_MAP_PATH_MAX) return CP0_MAP_E_INVALID;
    const cp0_map_sqlite_api_t *sql = cp0_map__sqlite();
    if (!sql) return CP0_MAP_E_SQLITE_MISSING;
    int tms = o->scheme == CP0_MAP_SCHEME_TMS;
    if (o->scheme == CP0_MAP_SCHEME_AUTO) {
        char x[CP0_MAP_PATH_MAX];
        struct stat st;
        tms = cp0_map__fmt(x, sizeof x, "%s/tilemapresource.xml", abs) == 0 && stat(x, &st) == 0;
    }
    zoom_extent_t ext[31];
    memset(ext, 0, sizeof ext);
    uint64_t total = 0;
    int webp = 0;
    int rc = scan_folder(abs, tms, o, &total, ext, &webp);
    if (rc) return rc;
    if (!total) return webp ? CP0_MAP_E_UNSUPPORTED : CP0_MAP_E_NO_TILE;
    char name[CP0_MAP_PATH_MAX], meta_name[128], meta_attr[256];
    base_name(abs, 0, name, sizeof name);
    folder_metadata(abs, meta_name, sizeof meta_name, meta_attr, sizeof meta_attr);
    cp0_map_rec_t r;
    memset(&r, 0, sizeof r);
    rc = folder_id(s, o, name, abs, r.info.id, sizeof r.info.id, sql);
    if (rc) return rc;
    char part[CP0_MAP_PATH_MAX], dest[CP0_MAP_PATH_MAX];
    if (cp0_map__archive_file(s, r.info.id, ".mbtiles.part", part, sizeof part) ||
        cp0_map__archive_file(s, r.info.id, ".mbtiles", dest, sizeof dest))
        return CP0_MAP_E_INVALID;
    conv_db_t c = {sql, NULL, NULL, NULL};
    int resuming = 0;
    rc = conv_open(&c, part, abs, &resuming);
    if (rc) {
        db_close(&c);
        return rc;
    }
    conv_stats_t cs;
    memset(&cs, 0, sizeof cs);
    cs.total = total;
    rc = convert_tiles(&c, abs, tms, resuming, o, &cs);
    if (rc) {
        db_close(&c);
        return rc; /* the partial file stays for a resume */
    }
    /* the metadata, then the finished file */
    int minz = -1, maxz = -1;
    for (int z = 0; z <= 30; z++)
        if (ext[z].present) {
            if (minz < 0) minz = z;
            maxz = z;
        }
    cp0_map_bbox_t box = {0, 0, 0, 0};
    int has_box = 0;
    if (maxz >= 0 && maxz <= CP0_MAP_MAX_ZOOM) {
        cp0_map_bbox_t a, b;
        if (cp0_map_tile_bbox(maxz, ext[maxz].x0, ext[maxz].y0, &a) == CP0_MAP_OK &&
            cp0_map_tile_bbox(maxz, ext[maxz].x1, ext[maxz].y1, &b) == CP0_MAP_OK) {
            box = a;
            cp0_map_bbox_union(&box, &b);
            has_box = 1;
        }
    }
    const long long rows = count_rows(&c);
    cp0_map_format_t fmt = cs.jpg > cs.png ? CP0_MAP_FMT_JPEG : CP0_MAP_FMT_PNG;
    if (!cs.png && !cs.jpg) {
        /* every tile came from the earlier run: the format of one of them */
        sqlite3_stmt *st = NULL;
        if (c.sql->prepare_v2(c.db, "SELECT tile_data FROM tiles LIMIT 1", -1, &st, NULL) == CP0_MAP_SQLITE_OK &&
            c.sql->step(st) == CP0_MAP_SQLITE_ROW) {
            const uint8_t *d = (const uint8_t *)c.sql->column_blob(st, 0);
            const int n = c.sql->column_bytes(st, 0);
            if (d && n > 0 && cp0_map_format_sniff(d, (size_t)n) == CP0_MAP_FMT_JPEG) fmt = CP0_MAP_FMT_JPEG;
        }
        c.sql->finalize(st);
    }
    const char *title = o->title ? o->title : meta_name[0] ? meta_name : name;
    const char *attr = o->attribution ? o->attribution : meta_attr[0] ? meta_attr : CP0_MAP_FOLDER_ATTRIBUTION;
    char num[64], bounds[160];
    rc = db_exec(&c, "BEGIN; DELETE FROM metadata;");
    if (!rc) rc = meta_put(&c, "name", title);
    if (!rc) rc = meta_put(&c, "type", "baselayer");
    if (!rc) rc = meta_put(&c, "version", "1.3");
    if (!rc) rc = meta_put(&c, "format", cp0_map_format_name(fmt));
    if (!rc && has_box && cp0_map_bbox_format(&box, bounds, sizeof bounds) > 0) rc = meta_put(&c, "bounds", bounds);
    snprintf(num, sizeof num, "%d", minz);
    if (!rc) rc = meta_put(&c, "minzoom", num);
    snprintf(num, sizeof num, "%d", maxz);
    if (!rc) rc = meta_put(&c, "maxzoom", num);
    if (!rc) rc = meta_put(&c, "attribution", attr);
    if (!rc) rc = meta_put(&c, "description", "converted from a z/x/y tile folder by the CyberDeck Zero map engine");
    if (!rc) rc = db_exec(&c, "COMMIT");
    db_close(&c);
    if (rc) return rc;
    {
        const int fd = open(part, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            if (fsync(fd) != 0) rc = CP0_MAP_E_IO;
            close(fd);
        }
    }
    if (!rc && rename(part, dest) != 0) rc = CP0_MAP_E_IO;
    if (rc) return rc;
    report(o, CP0_MAP_PHASE_CHECK, 0, 1, 0, 0);
    cp0_map_archive_t *a;
    rc = cp0_map_archive_open(dest, &a);
    if (rc) return rc;
    cp0_map_archive_info_t ai;
    cp0_map_archive_info(a, &ai);
    cp0_map_archive_close(a);
    if (!ai.tile_px) ai.tile_px = cs.tile_px;
    r.tiles = rows;
    r.info.mode = CP0_MAP_MODE_CONVERTED;
    cp0_map__copy_str(r.info.path, sizeof r.info.path, dest, 0);
    cp0_map__copy_str(r.source_path, sizeof r.source_path, abs, 0);
    fill_from_info(&r, &ai, o, name);
    cp0_map__copy_str(r.info.title, sizeof r.info.title, title, 1);
    cp0_map__copy_str(r.info.attribution, sizeof r.info.attribution, attr, 1);
    rc = register_archive(s, &r);
    if (rc) return rc;
    report(o, CP0_MAP_PHASE_CHECK, 1, 1, 0, 0);
    set_out_id(o, r.info.id);
    return CP0_MAP_OK;
}

/* ------------------------------------------------------------------------------------------------------ entries */

int cp0_map_store_import_ex(cp0_map_store_t *s, const char *path, const cp0_map_import_opts_t *opts)
{
    if (!s || !path || !path[0]) return CP0_MAP_E_INVALID;
    cp0_map_import_opts_t defaults;
    memset(&defaults, 0, sizeof defaults);
    defaults.copy = 1;
    const cp0_map_import_opts_t *o = opts ? opts : &defaults;
    if (o->out_id && o->out_id_size) o->out_id[0] = '\0';
    if (s->read_only) return CP0_MAP_E_READ_ONLY;
    struct stat st;
    if (stat(path, &st) != 0) return CP0_MAP_E_NOT_FOUND;
    pthread_mutex_lock(&s->import_mu);
    int rc;
    if (S_ISDIR(st.st_mode))
        rc = import_folder(s, path, o);
    else if (S_ISREG(st.st_mode))
        rc = import_archive(s, path, o);
    else
        rc = CP0_MAP_E_FORMAT;
    pthread_mutex_unlock(&s->import_mu);
    return rc;
}

int cp0_map_store_import(cp0_map_store_t *s, const char *path, int copy, cp0_map_progress_cb cb, void *user)
{
    cp0_map_import_opts_t o;
    memset(&o, 0, sizeof o);
    o.copy = copy;
    o.progress = cb;
    o.user = user;
    return cp0_map_store_import_ex(s, path, &o);
}
