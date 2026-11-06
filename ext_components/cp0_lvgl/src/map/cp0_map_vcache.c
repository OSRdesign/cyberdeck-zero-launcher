/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: the persistent viewing cache of the online sources (cp0_map_store.h, VIEWING CACHE). One MBTiles file
 * per source, tiles/<id>.mbtiles, written by one process (flock on tiles/<id>.lock, taken when the store opens: the
 * first app that opens the store read-write is the writer, a second one reads), batched by a cache-writer thread per
 * store: a commit every CP0_MAP_CACHE_BATCH tiles or CP0_MAP_CACHE_COMMIT_MS, rollback journal (report 055: half the
 * SD writes of WAL), synchronous=NORMAL, auto_vacuum=INCREMENTAL so eviction gives the space back. The file stays a
 * plain MBTiles 1.3 (metadata + tiles with TMS rows and the unique index) that QGIS, mbutil or tileserver-gl open; the
 * engine's own tables sit beside:
 *   cp0_tile_state (zoom_level, tile_column, tile_row, bytes, fetched, expires, last_used, hash, etag, last_modified)
 *   cp0_missing    (zoom_level, tile_column, tile_row, until)          404 / 410 answers, CP0_MAP_MISSING_TTL_S
 * Readers check the uncommitted queue first, so a tile is readable the moment it is fetched. Tiles a source may not
 * keep (viewing_cache=0, Cache-Control: no-store) live in a small RAM ring instead.
 *
 * Eviction: when the files of all viewing caches pass the cap, the least recently used tiles across them go (oldest
 * last_used, then oldest fetch) until 90 % of the cap. Imported archives are elsewhere (archives/) and never seen here.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "cp0_map_online.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define PEND_MAX 64        /* queued writes per cache (a producer waits beyond) */
#define RAM_MAX 48         /* tiles kept in RAM only, per source */
#define TOUCH_S 86400      /* last use written at most once a day per tile */
#define CAP_RECHECK_MS 10000
#define FULL_RETRY_S 60    /* after a failed write the cache stops growing this long */
#define EVICT_CAND 256

enum { P_TILE = 1, P_REFRESH, P_MISSING, P_TOUCH };

typedef struct {
    int kind;
    int z;
    uint32_t x, y;
    uint8_t *data;
    size_t len;
    uint64_t hash;
    int64_t expires, stamp;      /* stamp: fetched (TILE), last use (TOUCH, REFRESH) */
    char etag[128], lm[64];
} pend_t;

typedef struct {
    int z;
    uint32_t x, y;
    uint8_t *data;
    size_t len;
    uint64_t hash;
    int64_t expires;
} ram_t;

struct cp0_map_vcache {
    cp0_map_vcset_t *set;
    struct cp0_map_vcache *next;
    pthread_mutex_t mu;
    pthread_cond_t room;
    char id[CP0_MAP_ID_MAX];
    char path[CP0_MAP_PATH_MAX], lock_path[CP0_MAP_PATH_MAX];
    char title[128], attribution[256];
    cp0_map_format_t format;
    int minz, maxz;
    int lock_fd, writer;
    const cp0_map_sqlite_api_t *sql;
    sqlite3 *db;
    dev_t dev;
    ino_t ino;
    int has_state;               /* the engine's tables are there */
    sqlite3_stmt *q_get, *q_state, *q_missing;
    sqlite3_stmt *w_tile, *w_state, *w_unmiss, *w_refresh, *w_missing, *w_touch, *w_bytes;
    pend_t pend[PEND_MAX];
    int npend;
    int64_t oldest_ms;
    ram_t ram[RAM_MAX];
    int nram, ram_next;
    uint64_t tiles, tile_bytes;
    int counted;
    uint64_t hits, misses;
    uint64_t tiles_seen;         /* the last count handed out (atomic): for callers that must not wait */
};

struct cp0_map_vcset {
    pthread_mutex_t mu;
    pthread_cond_t cv, done_cv;
    char dir[CP0_MAP_PATH_MAX];
    int read_only;
    cp0_map_logger_t *log;
    cp0_map_vcache_t *list;
    uint64_t cap_req;            /* 0: the default rule */
    uint64_t cap_cached;
    int64_t cap_checked_ms;
    int evict_req;
    uint64_t flush_gen, flush_done;
    pthread_t thread;
    int running, stop;
    uint64_t test_free;
    uint64_t commits;
    int low_space;
    int64_t full_until;
};

static int g_test_abort_after; /* tests: abort() inside a commit after this many writes (crash safety) */

void cp0_map__vcache_test_abort_after(int n)
{
    g_test_abort_after = n;
}

static int64_t mono_ms(void)
{
    return cp0_map_monotonic_ms();
}

/* ------------------------------------------------------------------------------------------------ database */

static void finalize_all(cp0_map_vcache_t *vc)
{
    sqlite3_stmt **all[] = {&vc->q_get, &vc->q_state, &vc->q_missing, &vc->w_tile, &vc->w_state,
                            &vc->w_unmiss, &vc->w_refresh, &vc->w_missing, &vc->w_touch, &vc->w_bytes};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        if (*all[i]) vc->sql->finalize(*all[i]);
        *all[i] = NULL;
    }
}

static void db_close(cp0_map_vcache_t *vc)
{
    if (!vc->db) return;
    finalize_all(vc);
    vc->sql->close_v2(vc->db);
    vc->db = NULL;
    vc->has_state = 0;
}

static int prep(cp0_map_vcache_t *vc, const char *q, sqlite3_stmt **st)
{
    return vc->sql->prepare_v2(vc->db, q, -1, st, NULL) == CP0_MAP_SQLITE_OK ? 0 : -1;
}

#define KEY_WHERE "zoom_level = ?1 AND tile_column = ?2 AND tile_row = ?3"

static const char k_schema[] =
    "CREATE TABLE IF NOT EXISTS metadata (name text, value text);"
    "CREATE TABLE IF NOT EXISTS tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob);"
    "CREATE UNIQUE INDEX IF NOT EXISTS tile_index ON tiles (zoom_level, tile_column, tile_row);"
    "CREATE TABLE IF NOT EXISTS cp0_tile_state (zoom_level integer NOT NULL, tile_column integer NOT NULL,"
    " tile_row integer NOT NULL, bytes integer NOT NULL, fetched integer NOT NULL, expires integer NOT NULL,"
    " last_used integer NOT NULL, hash integer, etag text, last_modified text,"
    " PRIMARY KEY (zoom_level, tile_column, tile_row));"
    "CREATE INDEX IF NOT EXISTS cp0_tile_state_lru ON cp0_tile_state (last_used, fetched);"
    "CREATE INDEX IF NOT EXISTS cp0_tile_state_hash ON cp0_tile_state (hash);"
    "CREATE TABLE IF NOT EXISTS cp0_missing (zoom_level integer NOT NULL, tile_column integer NOT NULL,"
    " tile_row integer NOT NULL, until integer NOT NULL, PRIMARY KEY (zoom_level, tile_column, tile_row));";

static int exec(cp0_map_vcache_t *vc, const char *q)
{
    const int rc = vc->sql->exec(vc->db, q, NULL, NULL, NULL);
    return rc == CP0_MAP_SQLITE_OK ? CP0_MAP_OK : cp0_map__sqlite_code(rc);
}

static void meta_put(cp0_map_vcache_t *vc, const char *k, const char *v)
{
    sqlite3_stmt *st = NULL;
    if (prep(vc, "INSERT INTO metadata (name, value) VALUES (?1, ?2)", &st) == 0) {
        vc->sql->bind_text(st, 1, k, -1, CP0_MAP_SQLITE_TRANSIENT);
        vc->sql->bind_text(st, 2, v, -1, CP0_MAP_SQLITE_TRANSIENT);
        vc->sql->step(st);
    }
    vc->sql->finalize(st);
}

static void count_rows(cp0_map_vcache_t *vc)
{
    sqlite3_stmt *st = NULL;
    const char *q = vc->has_state ? "SELECT COUNT(*), COALESCE(SUM(bytes), 0) FROM cp0_tile_state"
                                  : "SELECT COUNT(*), COALESCE(SUM(LENGTH(tile_data)), 0) FROM tiles";
    if (prep(vc, q, &st) == 0 && vc->sql->step(st) == CP0_MAP_SQLITE_ROW) {
        vc->tiles = (uint64_t)vc->sql->column_int64(st, 0);
        vc->tile_bytes = (uint64_t)vc->sql->column_int64(st, 1);
        vc->counted = 1;
    }
    vc->sql->finalize(st);
}

static int prepare_reads(cp0_map_vcache_t *vc)
{
    vc->has_state =
        prep(vc, "SELECT t.tile_data, s.expires, s.last_used, s.zoom_level FROM tiles t LEFT JOIN cp0_tile_state s"
                 " ON s.zoom_level = t.zoom_level AND s.tile_column = t.tile_column AND s.tile_row = t.tile_row"
                 " WHERE t.zoom_level = ?1 AND t.tile_column = ?2 AND t.tile_row = ?3",
             &vc->q_get) == 0;
    if (!vc->has_state) {
        /* a plain MBTiles (another tool wrote it): tiles only, no expiry known */
        if (vc->q_get) vc->sql->finalize(vc->q_get);
        vc->q_get = NULL;
        if (prep(vc, "SELECT tile_data, NULL, NULL, NULL FROM tiles WHERE " KEY_WHERE, &vc->q_get)) return CP0_MAP_E_FORMAT;
        return CP0_MAP_OK;
    }
    if (prep(vc, "SELECT s.expires, s.etag, s.last_modified, s.zoom_level FROM tiles t LEFT JOIN cp0_tile_state s"
                 " ON s.zoom_level = t.zoom_level AND s.tile_column = t.tile_column AND s.tile_row = t.tile_row"
                 " WHERE t.zoom_level = ?1 AND t.tile_column = ?2 AND t.tile_row = ?3",
             &vc->q_state) ||
        prep(vc, "SELECT until FROM cp0_missing WHERE " KEY_WHERE, &vc->q_missing))
        return CP0_MAP_E_FORMAT;
    return CP0_MAP_OK;
}

static int prepare_writes(cp0_map_vcache_t *vc)
{
    return (prep(vc, "INSERT OR REPLACE INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?1, ?2, ?3, ?4)",
                 &vc->w_tile) ||
            prep(vc, "INSERT OR REPLACE INTO cp0_tile_state (zoom_level, tile_column, tile_row, bytes, fetched, expires,"
                     " last_used, hash, etag, last_modified) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?5, ?7, ?8, ?9)",
                 &vc->w_state) ||
            prep(vc, "DELETE FROM cp0_missing WHERE " KEY_WHERE, &vc->w_unmiss) ||
            prep(vc, "UPDATE cp0_tile_state SET expires = ?4, last_used = MAX(last_used, ?5), etag = COALESCE(?6, etag),"
                     " last_modified = COALESCE(?7, last_modified) WHERE " KEY_WHERE,
                 &vc->w_refresh) ||
            prep(vc, "INSERT OR REPLACE INTO cp0_missing (zoom_level, tile_column, tile_row, until) VALUES (?1, ?2, ?3, ?4)",
                 &vc->w_missing) ||
            prep(vc, "UPDATE cp0_tile_state SET last_used = ?4 WHERE " KEY_WHERE " AND last_used < ?4", &vc->w_touch) ||
            prep(vc, "SELECT bytes FROM cp0_tile_state WHERE " KEY_WHERE, &vc->w_bytes))
               ? CP0_MAP_E_SQLITE
               : CP0_MAP_OK;
}

/* opens the file (create: make it when absent, writer only). 0, CP0_MAP_E_NO_TILE (no file), errors */
static int db_open(cp0_map_vcache_t *vc, int create)
{
    struct stat st;
    const int exists = stat(vc->path, &st) == 0;
    if (vc->db) {
        if (vc->writer) return CP0_MAP_OK;
        /* a reader: the writer may have cleared (deleted) the file meanwhile */
        if (exists && st.st_dev == vc->dev && st.st_ino == vc->ino) return CP0_MAP_OK;
        db_close(vc);
        vc->counted = 0;
    }
    if (!exists && !(create && vc->writer)) return CP0_MAP_E_NO_TILE;
    if (!vc->sql && !(vc->sql = cp0_map__sqlite())) return CP0_MAP_E_SQLITE_MISSING;
    const int flags = vc->writer ? (CP0_MAP_SQLITE_OPEN_READWRITE | CP0_MAP_SQLITE_OPEN_CREATE | CP0_MAP_SQLITE_OPEN_NOMUTEX)
                                 : (CP0_MAP_SQLITE_OPEN_READONLY | CP0_MAP_SQLITE_OPEN_NOMUTEX);
    if (vc->writer && !exists) {
        char dir[CP0_MAP_PATH_MAX];
        if (!cp0_map__fmt(dir, sizeof dir, "%s/tiles", vc->set->dir)) cp0_map__mkdirs(dir, 0755);
    }
    int rc = vc->sql->open_v2(vc->path, &vc->db, flags, NULL);
    if (rc != CP0_MAP_SQLITE_OK) {
        if (vc->db) vc->sql->close_v2(vc->db);
        vc->db = NULL;
        return cp0_map__sqlite_code(rc);
    }
    vc->sql->busy_timeout(vc->db, 2000);
    int code;
    if (vc->writer) {
        code = exec(vc, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=NORMAL; PRAGMA cache_size=-256;"
                        " PRAGMA mmap_size=0");
        int fresh = 0;
        sqlite3_stmt *q = NULL;
        if (!code && prep(vc, "SELECT COUNT(*) FROM sqlite_master", &q) == 0 && vc->sql->step(q) == CP0_MAP_SQLITE_ROW)
            fresh = vc->sql->column_int(q, 0) == 0;
        else if (!code)
            code = cp0_map__sqlite_code(vc->sql->extended_errcode(vc->db));
        vc->sql->finalize(q);
        if (!code && fresh) code = exec(vc, "PRAGMA auto_vacuum=INCREMENTAL");
        if (!code) code = exec(vc, k_schema);
        if (!code && fresh) {
            char num[32];
            exec(vc, "BEGIN");
            meta_put(vc, "name", vc->title[0] ? vc->title : vc->id);
            meta_put(vc, "type", "baselayer");
            meta_put(vc, "version", "1");
            meta_put(vc, "description", "viewing cache (cp0_lvgl map engine): tiles viewed, kept per the source terms");
            meta_put(vc, "format", cp0_map_format_name(vc->format == CP0_MAP_FMT_UNKNOWN ? CP0_MAP_FMT_PNG : vc->format));
            meta_put(vc, "bounds", "-180,-85.05113,180,85.05113");
            snprintf(num, sizeof num, "%d", vc->minz < 0 ? 0 : vc->minz);
            meta_put(vc, "minzoom", num);
            snprintf(num, sizeof num, "%d", vc->maxz < 0 ? 18 : vc->maxz);
            meta_put(vc, "maxzoom", num);
            if (vc->attribution[0]) meta_put(vc, "attribution", vc->attribution);
            meta_put(vc, "cp0_cache_version", "1");
            code = exec(vc, "COMMIT");
        }
        if (!code) code = prepare_reads(vc);
        if (!code) code = prepare_writes(vc);
    } else {
        code = exec(vc, "PRAGMA cache_size=-256; PRAGMA mmap_size=0; PRAGMA query_only=1");
        if (!code) code = prepare_reads(vc);
    }
    if (code) {
        db_close(vc);
        if (vc->writer && (code == CP0_MAP_E_CORRUPT || code == CP0_MAP_E_FORMAT)) {
            /* a damaged cache is only a cache: set it aside and start a new one */
            char aside[CP0_MAP_PATH_MAX + 16];
            if (!cp0_map__fmt(aside, sizeof aside, "%s.corrupt", vc->path) && rename(vc->path, aside) == 0)
                cp0_map__log(vc->set->log, "map cache %s: damaged file set aside as %s", vc->id, aside);
        }
        return code;
    }
    if (stat(vc->path, &st) == 0) {
        vc->dev = st.st_dev;
        vc->ino = st.st_ino;
    }
    if (!vc->counted) count_rows(vc);
    return CP0_MAP_OK;
}

static void bind_key(cp0_map_vcache_t *vc, sqlite3_stmt *st, int z, uint32_t x, uint32_t y)
{
    vc->sql->bind_int(st, 1, z);
    vc->sql->bind_int64(st, 2, (long long)x);
    vc->sql->bind_int64(st, 3, (long long)cp0_map_tms_row(z, y));
}

/* ----------------------------------------------------------------------------------------------- the queue */

static void pend_free(pend_t *p)
{
    free(p->data);
    p->data = NULL;
}

static int step_done(cp0_map_vcache_t *vc, sqlite3_stmt *st)
{
    const int rc = vc->sql->step(st);
    vc->sql->reset(st);
    return (rc == CP0_MAP_SQLITE_DONE || rc == CP0_MAP_SQLITE_ROW) ? 0 : -1;
}

static void maybe_abort(void)
{
    if (g_test_abort_after > 0 && --g_test_abort_after == 0) abort();
}

static int write_one(cp0_map_vcache_t *vc, const pend_t *p)
{
    const cp0_map_sqlite_api_t *s = vc->sql;
    switch (p->kind) {
    case P_TILE: {
        uint64_t old = 0;
        int had = 0;
        bind_key(vc, vc->w_bytes, p->z, p->x, p->y);
        if (s->step(vc->w_bytes) == CP0_MAP_SQLITE_ROW) {
            had = 1;
            old = (uint64_t)s->column_int64(vc->w_bytes, 0);
        }
        s->reset(vc->w_bytes);
        bind_key(vc, vc->w_tile, p->z, p->x, p->y);
        s->bind_blob(vc->w_tile, 4, p->data, (int)p->len, CP0_MAP_SQLITE_STATIC);
        if (step_done(vc, vc->w_tile)) return -1;
        bind_key(vc, vc->w_state, p->z, p->x, p->y);
        s->bind_int64(vc->w_state, 4, (long long)p->len);
        s->bind_int64(vc->w_state, 5, (long long)p->stamp);
        s->bind_int64(vc->w_state, 6, (long long)p->expires);
        s->bind_int64(vc->w_state, 7, (long long)p->hash);
        if (p->etag[0]) s->bind_text(vc->w_state, 8, p->etag, -1, CP0_MAP_SQLITE_TRANSIENT);
        else s->bind_text(vc->w_state, 8, NULL, -1, CP0_MAP_SQLITE_STATIC);
        if (p->lm[0]) s->bind_text(vc->w_state, 9, p->lm, -1, CP0_MAP_SQLITE_TRANSIENT);
        else s->bind_text(vc->w_state, 9, NULL, -1, CP0_MAP_SQLITE_STATIC);
        if (step_done(vc, vc->w_state)) return -1;
        bind_key(vc, vc->w_unmiss, p->z, p->x, p->y);
        if (step_done(vc, vc->w_unmiss)) return -1;
        if (had) vc->tile_bytes -= old;
        else vc->tiles++;
        vc->tile_bytes += p->len;
        return 0;
    }
    case P_REFRESH:
        bind_key(vc, vc->w_refresh, p->z, p->x, p->y);
        s->bind_int64(vc->w_refresh, 4, (long long)p->expires);
        s->bind_int64(vc->w_refresh, 5, (long long)p->stamp);
        if (p->etag[0]) s->bind_text(vc->w_refresh, 6, p->etag, -1, CP0_MAP_SQLITE_TRANSIENT);
        else s->bind_text(vc->w_refresh, 6, NULL, -1, CP0_MAP_SQLITE_STATIC);
        if (p->lm[0]) s->bind_text(vc->w_refresh, 7, p->lm, -1, CP0_MAP_SQLITE_TRANSIENT);
        else s->bind_text(vc->w_refresh, 7, NULL, -1, CP0_MAP_SQLITE_STATIC);
        return step_done(vc, vc->w_refresh);
    case P_MISSING:
        bind_key(vc, vc->w_missing, p->z, p->x, p->y);
        s->bind_int64(vc->w_missing, 4, (long long)p->expires);
        return step_done(vc, vc->w_missing);
    case P_TOUCH:
        bind_key(vc, vc->w_touch, p->z, p->x, p->y);
        s->bind_int64(vc->w_touch, 4, (long long)p->stamp);
        return step_done(vc, vc->w_touch);
    default: return 0;
    }
}

static void set_full_for(cp0_map_vcset_t *set, int64_t secs)
{
    pthread_mutex_lock(&set->mu);
    set->full_until = cp0_map__now() + secs;
    pthread_mutex_unlock(&set->mu);
}

/* commits the queue (vc->mu held) */
static int commit_locked(cp0_map_vcache_t *vc)
{
    if (!vc->npend) return CP0_MAP_OK;
    int rc = vc->writer ? db_open(vc, 1) : CP0_MAP_E_READ_ONLY;
    int n = vc->npend;
    if (!rc) rc = exec(vc, "BEGIN");
    if (!rc) {
        for (int i = 0; i < n && !rc; i++) {
            if (write_one(vc, &vc->pend[i])) rc = cp0_map__sqlite_code(vc->sql->extended_errcode(vc->db));
            if (!rc) maybe_abort();
        }
        if (!rc) rc = exec(vc, "COMMIT");
        if (rc) {
            exec(vc, "ROLLBACK");
            vc->counted = 0;
            count_rows(vc);
        }
    }
    if (rc) {
        cp0_map__log(vc->set->log, "map cache %s: %d tiles not written (%s): the cache stops growing for a minute",
                     vc->id, n, cp0_map_strerror(rc));
        if (vc->writer) set_full_for(vc->set, FULL_RETRY_S);
    } else {
        __atomic_add_fetch(&vc->set->commits, 1, __ATOMIC_RELAXED);
    }
    for (int i = 0; i < n; i++) pend_free(&vc->pend[i]);
    vc->npend = 0;
    vc->oldest_ms = 0;
    pthread_cond_broadcast(&vc->room);
    return rc;
}

/* ------------------------------------------------------------------------------------------------ the set */

static uint64_t file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0;
}

static uint64_t writers_bytes(cp0_map_vcset_t *set, cp0_map_vcache_t *head)
{
    (void)set;
    uint64_t total = 0;
    for (cp0_map_vcache_t *vc = head; vc; vc = vc->next)
        if (vc->writer) total += file_size(vc->path);
    return total;
}

static cp0_map_vcache_t *list_head(cp0_map_vcset_t *set)
{
    pthread_mutex_lock(&set->mu);
    cp0_map_vcache_t *h = set->list;
    pthread_mutex_unlock(&set->mu);
    return h;
}

static uint64_t free_space(cp0_map_vcset_t *set)
{
    const uint64_t fake = __atomic_load_n(&set->test_free, __ATOMIC_RELAXED);
    if (fake) return fake;
    struct statvfs v;
    if (statvfs(set->dir, &v) != 0) return 0;
    return (uint64_t)v.f_bavail * (uint64_t)v.f_frsize;
}

uint64_t cp0_map_store_default_cap(uint64_t free_bytes)
{
    const uint64_t tenth = free_bytes / CP0_MAP_CACHE_CAP_FREE_DIV;
    return tenth < CP0_MAP_CACHE_CAP_MAX ? tenth : CP0_MAP_CACHE_CAP_MAX;
}

/* the cap in force and the low-space flag, recomputed at most every CAP_RECHECK_MS (force: now) */
static uint64_t effective_cap(cp0_map_vcset_t *set, int force)
{
    const int64_t now = mono_ms();
    pthread_mutex_lock(&set->mu);
    const int stale = force || !set->cap_checked_ms || now - set->cap_checked_ms >= CAP_RECHECK_MS;
    uint64_t cap = set->cap_cached;
    const uint64_t req = set->cap_req;
    cp0_map_vcache_t *head = set->list;
    pthread_mutex_unlock(&set->mu);
    if (!stale) return cap;
    const uint64_t fs = free_space(set);
    const uint64_t mine = writers_bytes(set, head);
    cap = req ? req : cp0_map_store_default_cap(fs + mine);
    pthread_mutex_lock(&set->mu);
    set->cap_cached = cap;
    set->cap_checked_ms = now;
    set->low_space = fs < CP0_MAP_CACHE_MIN_FREE;
    pthread_mutex_unlock(&set->mu);
    return cap;
}

typedef struct {
    cp0_map_vcache_t *vc;
    int z;
    uint32_t x, row;
    int64_t last_used, fetched;
    uint64_t bytes;
} cand_t;

static int cmp_cand(const void *a, const void *b)
{
    const cand_t *p = (const cand_t *)a, *q = (const cand_t *)b;
    if (p->last_used != q->last_used) return p->last_used < q->last_used ? -1 : 1;
    if (p->fetched != q->fetched) return p->fetched < q->fetched ? -1 : 1;
    return 0;
}

/* least recently used tiles out until the files are under target (committer thread) */
static void evict(cp0_map_vcset_t *set, uint64_t target)
{
    cand_t *c = NULL;
    size_t ccap = 0;
    for (int round = 0; round < 256; round++) {
        cp0_map_vcache_t *head = list_head(set);
        const uint64_t total = writers_bytes(set, head);
        if (total <= target) break;
        size_t n = 0;
        for (cp0_map_vcache_t *vc = head; vc; vc = vc->next) {
            if (!vc->writer) continue;
            pthread_mutex_lock(&vc->mu);
            sqlite3_stmt *st = NULL;
            if (db_open(vc, 0) == CP0_MAP_OK && vc->has_state &&
                prep(vc, "SELECT zoom_level, tile_column, tile_row, last_used, fetched, bytes FROM cp0_tile_state"
                         " ORDER BY last_used, fetched LIMIT 256",
                     &st) == 0) {
                while (vc->sql->step(st) == CP0_MAP_SQLITE_ROW) {
                    if (n == ccap) {
                        const size_t nc = ccap ? ccap * 2 : EVICT_CAND * 4;
                        cand_t *nn = (cand_t *)realloc(c, nc * sizeof *c);
                        if (!nn) break;
                        c = nn;
                        ccap = nc;
                    }
                    cand_t *k = &c[n++];
                    k->vc = vc;
                    k->z = vc->sql->column_int(st, 0);
                    k->x = (uint32_t)vc->sql->column_int64(st, 1);
                    k->row = (uint32_t)vc->sql->column_int64(st, 2);
                    k->last_used = vc->sql->column_int64(st, 3);
                    k->fetched = vc->sql->column_int64(st, 4);
                    k->bytes = (uint64_t)vc->sql->column_int64(st, 5);
                }
            }
            if (st) vc->sql->finalize(st);
            pthread_mutex_unlock(&vc->mu);
        }
        if (!n) break;
        qsort(c, n, sizeof *c, cmp_cand);
        const uint64_t need = total - target;
        uint64_t freed = 0;
        size_t take = 0;
        while (take < n && (take == 0 || freed < need)) freed += c[take++].bytes + 96; /* + row and index overhead */
        int deleted = 0;
        for (cp0_map_vcache_t *vc = head; vc; vc = vc->next) {
            int mine = 0;
            for (size_t i = 0; i < take && !mine; i++) mine = c[i].vc == vc;
            if (!mine) continue;
            pthread_mutex_lock(&vc->mu);
            sqlite3_stmt *dt = NULL, *ds = NULL;
            if (db_open(vc, 0) == CP0_MAP_OK && exec(vc, "BEGIN") == CP0_MAP_OK &&
                prep(vc, "DELETE FROM tiles WHERE " KEY_WHERE, &dt) == 0 &&
                prep(vc, "DELETE FROM cp0_tile_state WHERE " KEY_WHERE, &ds) == 0) {
                for (size_t i = 0; i < take; i++) {
                    if (c[i].vc != vc) continue;
                    vc->sql->bind_int(dt, 1, c[i].z);
                    vc->sql->bind_int64(dt, 2, (long long)c[i].x);
                    vc->sql->bind_int64(dt, 3, (long long)c[i].row);
                    vc->sql->bind_int(ds, 1, c[i].z);
                    vc->sql->bind_int64(ds, 2, (long long)c[i].x);
                    vc->sql->bind_int64(ds, 3, (long long)c[i].row);
                    if (step_done(vc, dt) || step_done(vc, ds)) break;
                    vc->tiles = vc->tiles ? vc->tiles - 1 : 0;
                    vc->tile_bytes = vc->tile_bytes >= c[i].bytes ? vc->tile_bytes - c[i].bytes : 0;
                    deleted++;
                }
                if (exec(vc, "COMMIT") != CP0_MAP_OK) {
                    exec(vc, "ROLLBACK");
                    vc->counted = 0;
                    count_rows(vc);
                }
                exec(vc, "PRAGMA incremental_vacuum");
            }
            if (dt) vc->sql->finalize(dt);
            if (ds) vc->sql->finalize(ds);
            pthread_mutex_unlock(&vc->mu);
        }
        if (!deleted) break;
    }
    free(c);
}

static void *committer(void *arg)
{
    cp0_map_vcset_t *set = (cp0_map_vcset_t *)arg;
    pthread_mutex_lock(&set->mu);
    while (!set->stop) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 250 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        if (set->flush_gen == set->flush_done && !set->evict_req) pthread_cond_timedwait(&set->cv, &set->mu, &ts);
        const uint64_t gen = set->flush_gen;
        const int evict_req = set->evict_req;
        set->evict_req = 0;
        cp0_map_vcache_t *head = set->list;
        pthread_mutex_unlock(&set->mu);

        const int64_t now = mono_ms();
        const int flushing = gen != set->flush_done;
        for (cp0_map_vcache_t *vc = head; vc; vc = vc->next) {
            pthread_mutex_lock(&vc->mu);
            if (vc->npend && (flushing || vc->npend >= CP0_MAP_CACHE_BATCH || now - vc->oldest_ms >= CP0_MAP_CACHE_COMMIT_MS))
                commit_locked(vc);
            pthread_mutex_unlock(&vc->mu);
        }
        const uint64_t cap = effective_cap(set, evict_req);
        if (writers_bytes(set, head) > cap) evict(set, cap / 10 * 9);

        pthread_mutex_lock(&set->mu);
        set->flush_done = gen;
        pthread_cond_broadcast(&set->done_cv);
    }
    pthread_mutex_unlock(&set->mu);
    return NULL;
}

cp0_map_vcset_t *cp0_map__vcset_new(const char *dir, int read_only, uint64_t cap, cp0_map_logger_t *log)
{
    cp0_map_vcset_t *set = (cp0_map_vcset_t *)calloc(1, sizeof *set);
    if (!set) return NULL;
    cp0_map__copy_str(set->dir, sizeof set->dir, dir, 0);
    set->read_only = read_only;
    set->log = log;
    set->cap_req = cap;
    pthread_mutex_init(&set->mu, NULL);
    pthread_cond_init(&set->cv, NULL);
    pthread_cond_init(&set->done_cv, NULL);
    if (!read_only) {
        if (pthread_create(&set->thread, NULL, committer, set) == 0) set->running = 1;
    }
    return set;
}

static void vcache_free(cp0_map_vcache_t *vc)
{
    pthread_mutex_lock(&vc->mu);
    commit_locked(vc);
    db_close(vc);
    for (int i = 0; i < vc->nram; i++) free(vc->ram[i].data);
    vc->nram = 0;
    pthread_mutex_unlock(&vc->mu);
    if (vc->lock_fd >= 0) close(vc->lock_fd); /* releases the writer lock; the lock file stays */
    pthread_cond_destroy(&vc->room);
    pthread_mutex_destroy(&vc->mu);
    free(vc);
}

void cp0_map__vcset_free(cp0_map_vcset_t *set)
{
    if (!set) return;
    if (set->running) {
        pthread_mutex_lock(&set->mu);
        set->stop = 1;
        pthread_cond_broadcast(&set->cv);
        pthread_mutex_unlock(&set->mu);
        pthread_join(set->thread, NULL);
    }
    cp0_map_vcache_t *vc = set->list;
    while (vc) {
        cp0_map_vcache_t *next = vc->next;
        vcache_free(vc);
        vc = next;
    }
    pthread_cond_destroy(&set->cv);
    pthread_cond_destroy(&set->done_cv);
    pthread_mutex_destroy(&set->mu);
    free(set);
}

cp0_map_vcache_t *cp0_map__vcset_get(cp0_map_vcset_t *set, const char *id)
{
    if (!set || !cp0_map__id_valid(id)) return NULL;
    pthread_mutex_lock(&set->mu);
    for (cp0_map_vcache_t *vc = set->list; vc; vc = vc->next)
        if (!strcmp(vc->id, id)) {
            pthread_mutex_unlock(&set->mu);
            return vc;
        }
    pthread_mutex_unlock(&set->mu);
    cp0_map_vcache_t *vc = (cp0_map_vcache_t *)calloc(1, sizeof *vc);
    if (!vc) return NULL;
    vc->set = set;
    vc->lock_fd = -1;
    vc->minz = vc->maxz = -1;
    cp0_map__copy_str(vc->id, sizeof vc->id, id, 0);
    if (cp0_map__fmt(vc->path, sizeof vc->path, "%s/tiles/%s.mbtiles", set->dir, id) ||
        cp0_map__fmt(vc->lock_path, sizeof vc->lock_path, "%s/tiles/%s.lock", set->dir, id)) {
        free(vc);
        return NULL;
    }
    pthread_mutex_init(&vc->mu, NULL);
    pthread_cond_init(&vc->room, NULL);
    if (!set->read_only) {
        char dir[CP0_MAP_PATH_MAX];
        if (!cp0_map__fmt(dir, sizeof dir, "%s/tiles", set->dir)) cp0_map__mkdirs(dir, 0755);
        vc->lock_fd = open(vc->lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (vc->lock_fd >= 0 && flock(vc->lock_fd, LOCK_EX | LOCK_NB) == 0) {
            vc->writer = 1;
        } else if (vc->lock_fd >= 0) {
            close(vc->lock_fd);
            vc->lock_fd = -1;
        }
    }
    pthread_mutex_lock(&set->mu);
    /* another thread may have made it meanwhile */
    for (cp0_map_vcache_t *o = set->list; o; o = o->next)
        if (!strcmp(o->id, id)) {
            pthread_mutex_unlock(&set->mu);
            vcache_free(vc);
            return o;
        }
    vc->next = set->list;
    set->list = vc;
    pthread_mutex_unlock(&set->mu);
    return vc;
}

void cp0_map__vcset_set_cap(cp0_map_vcset_t *set, uint64_t cap)
{
    if (!set) return;
    pthread_mutex_lock(&set->mu);
    set->cap_req = cap;
    set->cap_checked_ms = 0;
    set->evict_req = 1;
    pthread_cond_broadcast(&set->cv);
    pthread_mutex_unlock(&set->mu);
}

uint64_t cp0_map__vcset_cap(cp0_map_vcset_t *set, int *is_auto)
{
    if (!set) return 0;
    const uint64_t cap = effective_cap(set, 0);
    if (is_auto) {
        pthread_mutex_lock(&set->mu);
        *is_auto = set->cap_req == 0;
        pthread_mutex_unlock(&set->mu);
    }
    return cap;
}

int cp0_map__vcset_flush(cp0_map_vcset_t *set)
{
    if (!set || !set->running) return CP0_MAP_OK;
    pthread_mutex_lock(&set->mu);
    const uint64_t gen = ++set->flush_gen;
    pthread_cond_broadcast(&set->cv);
    while (set->flush_done < gen && !set->stop) pthread_cond_wait(&set->done_cv, &set->mu);
    pthread_mutex_unlock(&set->mu);
    return CP0_MAP_OK;
}

int cp0_map__vcset_full(cp0_map_vcset_t *set)
{
    if (!set) return 0;
    effective_cap(set, 0);
    pthread_mutex_lock(&set->mu);
    const int full = set->low_space || cp0_map__now() < set->full_until;
    pthread_mutex_unlock(&set->mu);
    return full;
}

uint64_t cp0_map__vcset_disk_bytes(cp0_map_vcset_t *set)
{
    uint64_t total = 0;
    for (cp0_map_vcache_t *vc = set ? list_head(set) : NULL; vc; vc = vc->next) total += file_size(vc->path);
    return total;
}

void cp0_map__vcset_test_free(cp0_map_vcset_t *set, uint64_t free_bytes)
{
    if (!set) return;
    __atomic_store_n(&set->test_free, free_bytes, __ATOMIC_RELAXED);
    effective_cap(set, 1);
}

uint64_t cp0_map__vcset_commits(cp0_map_vcset_t *set)
{
    return set ? __atomic_load_n(&set->commits, __ATOMIC_RELAXED) : 0;
}

/* --------------------------------------------------------------------------------------------- one cache */

void cp0_map__vcache_meta(cp0_map_vcache_t *vc, const char *title, const char *attribution, cp0_map_format_t format,
                          int minzoom, int maxzoom)
{
    if (!vc) return;
    pthread_mutex_lock(&vc->mu);
    cp0_map__copy_str(vc->title, sizeof vc->title, title ? title : "", 0);
    cp0_map__copy_str(vc->attribution, sizeof vc->attribution, attribution ? attribution : "", 0);
    vc->format = format;
    vc->minz = minzoom;
    vc->maxz = maxzoom;
    pthread_mutex_unlock(&vc->mu);
}

int cp0_map__vcache_writer(cp0_map_vcache_t *vc)
{
    return vc ? vc->writer : 0;
}

static int copy_out(cp0_map_buf_t *out, const void *d, size_t n)
{
    if (n > CP0_MAP_TILE_BYTES_MAX) return CP0_MAP_E_TOO_LARGE;
    const int rc = cp0_map_buf_reserve(out, n);
    if (rc) return rc;
    memcpy(out->data, d, n);
    out->len = n;
    return CP0_MAP_OK;
}

static void queue_touch(cp0_map_vcache_t *vc, int z, uint32_t x, uint32_t y, int64_t now)
{
    if (!vc->writer || vc->npend >= PEND_MAX) return; /* best effort: a full queue skips it */
    pend_t *p = &vc->pend[vc->npend++];
    memset(p, 0, sizeof *p);
    p->kind = P_TOUCH;
    p->z = z;
    p->x = x;
    p->y = y;
    p->stamp = now;
    if (vc->npend == 1) vc->oldest_ms = mono_ms();
}

int cp0_map__vcache_get(cp0_map_vcache_t *vc, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out, int *stale,
                        int64_t *expires)
{
    if (stale) *stale = 0;
    if (expires) *expires = 0;
    if (!vc || !out) return CP0_MAP_E_INVALID;
    const int64_t now = cp0_map__now();
    pthread_mutex_lock(&vc->mu);
    int rc = CP0_MAP_E_NO_TILE;
    for (int i = vc->npend - 1; i >= 0; i--) {
        const pend_t *p = &vc->pend[i];
        if (p->kind == P_TILE && p->z == z && p->x == x && p->y == y) {
            rc = copy_out(out, p->data, p->len);
            if (stale) *stale = p->expires <= now;
            if (expires) *expires = p->expires;
            goto done;
        }
    }
    for (int i = 0; i < vc->nram; i++) {
        const ram_t *r = &vc->ram[i];
        if (r->z == z && r->x == x && r->y == y) {
            rc = copy_out(out, r->data, r->len);
            if (stale) *stale = r->expires <= now;
            if (expires) *expires = r->expires;
            goto done;
        }
    }
    rc = db_open(vc, 0);
    if (rc) goto done;
    bind_key(vc, vc->q_get, z, x, y);
    const int s = vc->sql->step(vc->q_get);
    if (s == CP0_MAP_SQLITE_ROW) {
        const void *d = vc->sql->column_blob(vc->q_get, 0);
        const int n = vc->sql->column_bytes(vc->q_get, 0);
        const int has_state = vc->sql->column_type(vc->q_get, 3) != CP0_MAP_SQLITE_NULL;
        const int64_t exp = has_state ? vc->sql->column_int64(vc->q_get, 1) : 0;
        const int64_t used = has_state ? vc->sql->column_int64(vc->q_get, 2) : now;
        rc = (d && n > 0) ? copy_out(out, d, (size_t)n) : CP0_MAP_E_NO_TILE;
        if (rc == CP0_MAP_OK) {
            if (stale) *stale = !has_state || exp <= now;
            if (expires) *expires = exp;
            if (has_state && now - used >= TOUCH_S) queue_touch(vc, z, x, y, now);
        }
    } else if (s == CP0_MAP_SQLITE_DONE) {
        rc = CP0_MAP_E_NO_TILE;
    } else {
        rc = cp0_map__sqlite_code(vc->sql->extended_errcode(vc->db));
        if (rc == CP0_MAP_OK) rc = CP0_MAP_E_SQLITE;
    }
    vc->sql->reset(vc->q_get);
done:
    if (rc == CP0_MAP_OK) vc->hits++;
    else if (rc == CP0_MAP_E_NO_TILE) vc->misses++;
    pthread_mutex_unlock(&vc->mu);
    return rc;
}

int cp0_map__vcache_lookup(cp0_map_vcache_t *vc, int z, uint32_t x, uint32_t y, cp0_map_vstate_t *st)
{
    memset(st, 0, sizeof *st);
    if (!vc) return CP0_MAP_E_INVALID;
    const int64_t now = cp0_map__now();
    pthread_mutex_lock(&vc->mu);
    int rc = CP0_MAP_OK, found = 0;
    for (int i = vc->npend - 1; i >= 0 && !found; i--) {
        const pend_t *p = &vc->pend[i];
        if (p->z != z || p->x != x || p->y != y || p->kind == P_TOUCH) continue;
        found = 1;
        if (p->kind == P_MISSING) st->missing = p->expires > now;
        else {
            st->have = 1;
            st->expires = p->expires;
            cp0_map__copy_str(st->etag, sizeof st->etag, p->etag, 0);
            cp0_map__copy_str(st->last_modified, sizeof st->last_modified, p->lm, 0);
        }
    }
    for (int i = 0; i < vc->nram && !found; i++)
        if (vc->ram[i].z == z && vc->ram[i].x == x && vc->ram[i].y == y) {
            found = 1;
            st->have = 1;
            st->expires = vc->ram[i].expires;
        }
    if (!found && db_open(vc, 0) == CP0_MAP_OK && vc->has_state) {
        bind_key(vc, vc->q_state, z, x, y);
        if (vc->sql->step(vc->q_state) == CP0_MAP_SQLITE_ROW) {
            st->have = 1;
            if (vc->sql->column_type(vc->q_state, 3) != CP0_MAP_SQLITE_NULL) {
                st->expires = vc->sql->column_int64(vc->q_state, 0);
                const char *e = (const char *)vc->sql->column_text(vc->q_state, 1);
                const char *l = (const char *)vc->sql->column_text(vc->q_state, 2);
                if (e) cp0_map__copy_str(st->etag, sizeof st->etag, e, 0);
                if (l) cp0_map__copy_str(st->last_modified, sizeof st->last_modified, l, 0);
            }
        }
        vc->sql->reset(vc->q_state);
        if (!st->have) {
            bind_key(vc, vc->q_missing, z, x, y);
            if (vc->sql->step(vc->q_missing) == CP0_MAP_SQLITE_ROW) st->missing = vc->sql->column_int64(vc->q_missing, 0) > now;
            vc->sql->reset(vc->q_missing);
        }
    } else if (!found && vc->db && !vc->has_state) {
        /* a plain MBTiles: present or not, expiry unknown */
        bind_key(vc, vc->q_get, z, x, y);
        st->have = vc->sql->step(vc->q_get) == CP0_MAP_SQLITE_ROW;
        vc->sql->reset(vc->q_get);
    }
    if (st->have) st->stale = st->expires <= now;
    pthread_mutex_unlock(&vc->mu);
    return rc;
}

int cp0_map__vcache_put(cp0_map_vcache_t *vc, const cp0_map_vput_t *in)
{
    if (!vc || !in) return CP0_MAP_E_INVALID;
    if (in->kind == CP0_MAP_VPUT_RAM) {
        uint8_t *copy = (uint8_t *)malloc(in->len ? in->len : 1);
        if (!copy) return CP0_MAP_E_NOMEM;
        memcpy(copy, in->data, in->len);
        pthread_mutex_lock(&vc->mu);
        int slot = -1;
        for (int i = 0; i < vc->nram && slot < 0; i++)
            if (vc->ram[i].z == in->z && vc->ram[i].x == in->x && vc->ram[i].y == in->y) slot = i;
        if (slot < 0) {
            if (vc->nram < RAM_MAX) slot = vc->nram++;
            else {
                slot = vc->ram_next;
                vc->ram_next = (vc->ram_next + 1) % RAM_MAX;
            }
        }
        free(vc->ram[slot].data);
        vc->ram[slot] = (ram_t){in->z, in->x, in->y, copy, in->len, in->hash, in->expires};
        pthread_mutex_unlock(&vc->mu);
        return CP0_MAP_OK;
    }
    if (!vc->writer) return CP0_MAP_E_READ_ONLY;
    if (in->kind == CP0_MAP_VPUT_TILE && cp0_map__vcset_full(vc->set)) return CP0_MAP_E_CACHE_FULL;
    uint8_t *copy = NULL;
    if (in->kind == CP0_MAP_VPUT_TILE) {
        copy = (uint8_t *)malloc(in->len ? in->len : 1);
        if (!copy) return CP0_MAP_E_NOMEM;
        memcpy(copy, in->data, in->len);
    }
    pthread_mutex_lock(&vc->mu);
    while (vc->npend >= PEND_MAX) pthread_cond_wait(&vc->room, &vc->mu);
    pend_t *p = &vc->pend[vc->npend++];
    memset(p, 0, sizeof *p);
    p->kind = in->kind == CP0_MAP_VPUT_TILE ? P_TILE : in->kind == CP0_MAP_VPUT_REFRESH ? P_REFRESH : P_MISSING;
    p->z = in->z;
    p->x = in->x;
    p->y = in->y;
    p->data = copy;
    p->len = in->len;
    p->hash = in->hash;
    p->expires = in->expires;
    p->stamp = cp0_map__now();
    if (in->etag) cp0_map__copy_str(p->etag, sizeof p->etag, in->etag, 1);
    if (in->last_modified) cp0_map__copy_str(p->lm, sizeof p->lm, in->last_modified, 1);
    if (vc->npend == 1) vc->oldest_ms = mono_ms();
    const int kick = vc->npend >= CP0_MAP_CACHE_BATCH;
    pthread_mutex_unlock(&vc->mu);
    if (kick) {
        pthread_mutex_lock(&vc->set->mu);
        pthread_cond_broadcast(&vc->set->cv);
        pthread_mutex_unlock(&vc->set->mu);
    }
    return CP0_MAP_OK;
}

int cp0_map__vcache_purge_hash(cp0_map_vcache_t *vc, uint64_t hash)
{
    if (!vc) return CP0_MAP_E_INVALID;
    int removed = 0;
    pthread_mutex_lock(&vc->mu);
    for (int i = 0; i < vc->npend;) {
        if (vc->pend[i].kind == P_TILE && vc->pend[i].hash == hash) {
            pend_free(&vc->pend[i]);
            memmove(&vc->pend[i], &vc->pend[i + 1], (size_t)(vc->npend - i - 1) * sizeof vc->pend[0]);
            vc->npend--;
            removed++;
        } else {
            i++;
        }
    }
    for (int i = 0; i < vc->nram;) {
        if (vc->ram[i].hash == hash) {
            free(vc->ram[i].data);
            vc->ram[i] = vc->ram[--vc->nram];
            vc->ram_next = 0;
            removed++;
        } else {
            i++;
        }
    }
    if (vc->writer && db_open(vc, 0) == CP0_MAP_OK && vc->has_state) {
        sqlite3_stmt *q = NULL, *dt = NULL, *ds = NULL;
        if (exec(vc, "BEGIN") == CP0_MAP_OK && prep(vc, "SELECT zoom_level, tile_column, tile_row, bytes FROM cp0_tile_state"
                                                        " WHERE hash = ?1", &q) == 0 &&
            prep(vc, "DELETE FROM tiles WHERE " KEY_WHERE, &dt) == 0 &&
            prep(vc, "DELETE FROM cp0_tile_state WHERE " KEY_WHERE, &ds) == 0) {
            vc->sql->bind_int64(q, 1, (long long)hash);
            while (vc->sql->step(q) == CP0_MAP_SQLITE_ROW) {
                const int z = vc->sql->column_int(q, 0);
                const long long x = vc->sql->column_int64(q, 1), row = vc->sql->column_int64(q, 2);
                const uint64_t b = (uint64_t)vc->sql->column_int64(q, 3);
                vc->sql->bind_int(dt, 1, z);
                vc->sql->bind_int64(dt, 2, x);
                vc->sql->bind_int64(dt, 3, row);
                vc->sql->bind_int(ds, 1, z);
                vc->sql->bind_int64(ds, 2, x);
                vc->sql->bind_int64(ds, 3, row);
                if (step_done(vc, dt) || step_done(vc, ds)) break;
                vc->tiles = vc->tiles ? vc->tiles - 1 : 0;
                vc->tile_bytes = vc->tile_bytes >= b ? vc->tile_bytes - b : 0;
                removed++;
            }
        }
        if (q) vc->sql->finalize(q);
        if (dt) vc->sql->finalize(dt);
        if (ds) vc->sql->finalize(ds);
        if (exec(vc, "COMMIT") != CP0_MAP_OK) exec(vc, "ROLLBACK");
    }
    pthread_cond_broadcast(&vc->room);
    pthread_mutex_unlock(&vc->mu);
    return removed;
}

int cp0_map__vcache_clear(cp0_map_vcache_t *vc)
{
    if (!vc) return CP0_MAP_E_INVALID;
    if (!vc->writer) return CP0_MAP_E_READ_ONLY;
    pthread_mutex_lock(&vc->mu);
    for (int i = 0; i < vc->npend; i++) pend_free(&vc->pend[i]);
    vc->npend = 0;
    for (int i = 0; i < vc->nram; i++) free(vc->ram[i].data);
    vc->nram = vc->ram_next = 0;
    db_close(vc);
    int rc = CP0_MAP_OK;
    char journal[CP0_MAP_PATH_MAX + 16];
    if (unlink(vc->path) != 0 && errno != ENOENT) rc = CP0_MAP_E_IO;
    if (!cp0_map__fmt(journal, sizeof journal, "%s-journal", vc->path)) unlink(journal);
    vc->tiles = vc->tile_bytes = 0;
    vc->counted = 0;
    pthread_cond_broadcast(&vc->room);
    pthread_mutex_unlock(&vc->mu);
    return rc;
}

void cp0_map__vcache_usage(cp0_map_vcache_t *vc, int count_stale, cp0_map_vusage_t *u)
{
    memset(u, 0, sizeof *u);
    if (!vc) return;
    pthread_mutex_lock(&vc->mu);
    if (db_open(vc, 0) == CP0_MAP_OK) {
        if (!vc->counted) count_rows(vc);
        if (count_stale && vc->has_state) {
            sqlite3_stmt *q = NULL;
            if (prep(vc, "SELECT COUNT(*) FROM cp0_tile_state WHERE expires <= ?1", &q) == 0) {
                vc->sql->bind_int64(q, 1, (long long)cp0_map__now());
                if (vc->sql->step(q) == CP0_MAP_SQLITE_ROW) u->stale = (uint64_t)vc->sql->column_int64(q, 0);
            }
            if (q) vc->sql->finalize(q);
        }
        u->tiles = vc->tiles;
    }
    u->bytes = file_size(vc->path);
    u->hits = vc->hits;
    u->misses = vc->misses;
    u->writer = vc->writer;
    __atomic_store_n(&vc->tiles_seen, u->tiles, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&vc->mu);
    u->full = vc->writer && cp0_map__vcset_full(vc->set);
}

uint64_t cp0_map__vcache_tiles_nowait(cp0_map_vcache_t *vc)
{
    if (!vc) return 0;
    if (pthread_mutex_trylock(&vc->mu) != 0) return __atomic_load_n(&vc->tiles_seen, __ATOMIC_RELAXED); /* a commit */
    uint64_t n = 0;
    if (db_open(vc, 0) == CP0_MAP_OK) {
        if (!vc->counted) count_rows(vc);
        n = vc->tiles;
    }
    __atomic_store_n(&vc->tiles_seen, n, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&vc->mu);
    return n;
}
