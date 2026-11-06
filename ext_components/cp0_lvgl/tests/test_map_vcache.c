/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine, the persistent viewing cache without any network (src/map/cp0_map_vcache.c; task 3 of Deck Atlas 0.1):
 *   - batched writes (a commit per CP0_MAP_CACHE_BATCH tiles, not one per tile), tiles readable before the commit;
 *   - least-recently-used eviction across all the viewing caches when they pass the cap (last use written at most
 *     once a day per tile), down to 90 % of it; a lower cap shrinks the files at once; a higher one evicts nothing;
 *   - imported archives (archives/) are never evicted, whatever the cap;
 *   - one writer per cache file (flock): a second opener reads, its writes are refused;
 *   - a writer killed in the middle of a commit (abort, SIGKILL) leaves a valid MBTiles with whole batches only;
 *   - the cache stops growing on a nearly full file system (CP0_MAP_E_CACHE_FULL), and clears on request.
 */
#include "map_test_util.h"
#include "cp0_map_online.h"
#include "cp0_map_store_priv.h"

#include <signal.h>
#include <sys/wait.h>

#if defined(__SANITIZE_THREAD__)
#define MAP_TEST_TSAN 1 /* ThreadSanitizer cannot follow fork() of a threaded process: those parts are left out */
#else
#define MAP_TEST_TSAN 0
#endif

static cp0_map_logger_t g_log;

static void quiet(void *user, const char *line)
{
    (void)user;
    (void)line;
}

/* an incompressible "tile" of n bytes (the cache stores any bytes; sizes then add up predictably) */
static uint8_t *blob(size_t n, uint32_t seed)
{
    uint8_t *b = (uint8_t *)malloc(n);
    CHECK(b != NULL);
    rng_seed(seed + 1);
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)rng();
    return b;
}

static void put_tile(cp0_map_vcache_t *vc, int z, uint32_t x, uint32_t y, size_t n)
{
    uint8_t *b = blob(n, (uint32_t)(z * 100000 + x * 1000 + y));
    cp0_map_vput_t p = {CP0_MAP_VPUT_TILE, z, x, y, b, n, cp0_map__hash64(b, n), cp0_map__now() + 86400, "\"e\"", NULL};
    CHECK_RC(cp0_map__vcache_put(vc, &p), CP0_MAP_OK);
    free(b);
}

static int has_tile(cp0_map_vcache_t *vc, int z, uint32_t x, uint32_t y)
{
    cp0_map_buf_t b = {0};
    const int rc = cp0_map__vcache_get(vc, z, x, y, &b, NULL, NULL);
    cp0_map_buf_free(&b);
    CHECK(rc == CP0_MAP_OK || rc == CP0_MAP_E_NO_TILE);
    return rc == CP0_MAP_OK;
}

static uint64_t fsize(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0;
}

static void test_batches(void)
{
    char dir[600], p[700];
    tpath(dir, sizeof dir, "batches");
    cp0_map_vcset_t *set = cp0_map__vcset_new(dir, 0, 0, &g_log);
    cp0_map_vcache_t *vc = cp0_map__vcset_get(set, "src");
    CHECK(vc && cp0_map__vcache_writer(vc));
    cp0_map__vcache_meta(vc, "Source", "(C) test", CP0_MAP_FMT_PNG, 0, 14);
    const uint64_t c0 = cp0_map__vcset_commits(set);
    for (uint32_t i = 0; i < 100; i++) put_tile(vc, 10, i, 7, 2000);
    CHECK(has_tile(vc, 10, 99, 7)); /* readable before any commit */
    CHECK_RC(cp0_map__vcset_flush(set), CP0_MAP_OK);
    const uint64_t commits = cp0_map__vcset_commits(set) - c0;
    CHECK(commits >= 3 && commits <= 5); /* 100 tiles in batches of 32 (+ the rest), never 100 commits */
    cp0_map_vusage_t u;
    cp0_map__vcache_usage(vc, 1, &u);
    CHECK(u.tiles == 100 && u.stale == 0 && u.writer && u.bytes > 200000);
    /* a refresh (304) and a 404 memory go through the same queue */
    cp0_map_vput_t r = {CP0_MAP_VPUT_REFRESH, 10, 3, 7, NULL, 0, 0, cp0_map__now() - 5, "\"f\"", NULL};
    CHECK_RC(cp0_map__vcache_put(vc, &r), CP0_MAP_OK);
    cp0_map_vput_t m = {CP0_MAP_VPUT_MISSING, 10, 500, 7, NULL, 0, 0, cp0_map__now() + 3600, NULL, NULL};
    CHECK_RC(cp0_map__vcache_put(vc, &m), CP0_MAP_OK);
    CHECK_RC(cp0_map__vcset_flush(set), CP0_MAP_OK);
    cp0_map_vstate_t st;
    CHECK_RC(cp0_map__vcache_lookup(vc, 10, 3, 7, &st), CP0_MAP_OK);
    CHECK(st.have && st.stale && !strcmp(st.etag, "\"f\""));
    CHECK_RC(cp0_map__vcache_lookup(vc, 10, 500, 7, &st), CP0_MAP_OK);
    CHECK(!st.have && st.missing);
    cp0_map__vcache_usage(vc, 1, &u);
    CHECK(u.stale == 1);
    /* RAM-only tiles (no-store, viewing_cache=0): read, never written */
    uint8_t *b = blob(500, 9);
    cp0_map_vput_t ram = {CP0_MAP_VPUT_RAM, 11, 1, 1, b, 500, 1, cp0_map__now() + 60, NULL, NULL};
    CHECK_RC(cp0_map__vcache_put(vc, &ram), CP0_MAP_OK);
    free(b);
    CHECK(has_tile(vc, 11, 1, 1));
    cp0_map__vcset_free(set);
    /* after a restart: the file's tiles, not the RAM ones; a valid MBTiles with our metadata */
    snprintf(p, sizeof p, "%s/tiles/src.mbtiles", dir);
    cp0_map_archive_t *a;
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_archive_info_t ai;
    CHECK_RC(cp0_map_archive_info(a, &ai), CP0_MAP_OK);
    CHECK(!strcmp(ai.name, "Source") && !strcmp(ai.attribution, "(C) test") && ai.minzoom == 0 && ai.maxzoom == 14);
    cp0_map_buf_t got = {0};
    CHECK_RC(cp0_map_archive_get_tile(a, 10, 42, 7, &got, NULL), CP0_MAP_OK);
    CHECK(got.len == 2000);
    CHECK_RC(cp0_map_archive_get_tile(a, 11, 1, 1, &got, NULL), CP0_MAP_E_NO_TILE);
    cp0_map_buf_free(&got);
    cp0_map_archive_close(a);
    set = cp0_map__vcset_new(dir, 0, 0, &g_log);
    vc = cp0_map__vcset_get(set, "src");
    CHECK(!has_tile(vc, 11, 1, 1) && has_tile(vc, 10, 0, 7));
    CHECK_RC(cp0_map__vcache_clear(vc), CP0_MAP_OK);
    CHECK(!file_exists(p) && !has_tile(vc, 10, 0, 7));
    cp0_map__vcset_free(set);
    printf("map cache: batched commits, readable before the commit, refresh and 404 memory, RAM-only tiles, clear\n");
}

static void test_eviction(void)
{
    char dir[600], pa[700], pb[700];
    tpath(dir, sizeof dir, "evict");
    cp0_map_vcset_t *set = cp0_map__vcset_new(dir, 0, 0, &g_log);
    cp0_map_vcache_t *a = cp0_map__vcset_get(set, "a"), *b = cp0_map__vcset_get(set, "b");
    snprintf(pa, sizeof pa, "%s/tiles/a.mbtiles", dir);
    snprintf(pb, sizeof pb, "%s/tiles/b.mbtiles", dir);
    /* 40 tiles of 32 KB in a and b, one simulated day apart in pairs: a0 b0 a1 b1 ... (fetched = last use) */
    for (uint32_t i = 0; i < 20; i++) {
        put_tile(a, 12, i, 1, 32768);
        put_tile(b, 12, i, 1, 32768);
        CHECK_RC(cp0_map__vcset_flush(set), CP0_MAP_OK);
        cp0_map__test_shift_clock(86400);
    }
    /* reading a0..a3 again a day later makes them the most recent (last use is written once a day) */
    for (uint32_t i = 0; i < 4; i++) CHECK(has_tile(a, 12, i, 1));
    CHECK_RC(cp0_map__vcset_flush(set), CP0_MAP_OK);
    const uint64_t before = fsize(pa) + fsize(pb);
    CHECK(before > 40 * 32768);
    /* a cap of about half: the oldest go, across both files, the recently read ones stay */
    const uint64_t cap = before / 2;
    cp0_map__vcset_set_cap(set, cap);
    CHECK_RC(cp0_map__vcset_flush(set), CP0_MAP_OK);
    const uint64_t after = fsize(pa) + fsize(pb);
    CHECK(after <= cap && after >= cap / 2);
    int is_auto = 1;
    CHECK(cp0_map__vcset_cap(set, &is_auto) == cap && !is_auto);
    for (uint32_t i = 0; i < 4; i++) CHECK(has_tile(a, 12, i, 1)); /* read last: kept */
    CHECK(!has_tile(b, 12, 0, 1) && !has_tile(b, 12, 1, 1) && !has_tile(a, 12, 4, 1)); /* oldest: gone */
    CHECK(has_tile(a, 12, 19, 1) && has_tile(b, 12, 19, 1));                           /* newest: kept */
    /* the order is by last use across both files: whatever is left of b is never older than what went */
    int first_kept_b = -1, last_gone_a = -1;
    for (uint32_t i = 0; i < 20; i++) {
        if (first_kept_b < 0 && has_tile(b, 12, i, 1)) first_kept_b = (int)i;
        if (i >= 4 && !has_tile(a, 12, i, 1)) last_gone_a = (int)i;
    }
    CHECK(first_kept_b > 0 && last_gone_a >= 0 && last_gone_a <= first_kept_b);
    /* a higher cap evicts nothing more; a lower one shrinks at once */
    cp0_map_vusage_t ua, ub;
    cp0_map__vcache_usage(a, 0, &ua);
    cp0_map__vcache_usage(b, 0, &ub);
    const uint64_t kept = ua.tiles + ub.tiles;
    cp0_map__vcset_set_cap(set, before * 4);
    CHECK_RC(cp0_map__vcset_flush(set), CP0_MAP_OK);
    cp0_map__vcache_usage(a, 0, &ua);
    cp0_map__vcache_usage(b, 0, &ub);
    CHECK(ua.tiles + ub.tiles == kept);
    cp0_map__vcset_set_cap(set, cap / 3);
    CHECK_RC(cp0_map__vcset_flush(set), CP0_MAP_OK);
    CHECK(fsize(pa) + fsize(pb) <= cap / 3);
    CHECK(has_tile(a, 12, 0, 1) || has_tile(b, 12, 19, 1) || has_tile(a, 12, 19, 1));
    /* the default rule: 10 % of the free space counted without the caches, at most 512 MiB */
    cp0_map__vcset_set_cap(set, 0);
    cp0_map__vcset_test_free(set, 3000ull << 20);
    CHECK(cp0_map__vcset_cap(set, &is_auto) == cp0_map_store_default_cap((3000ull << 20) + fsize(pa) + fsize(pb)) && is_auto);
    cp0_map__vcset_test_free(set, 0);
    cp0_map__vcset_free(set);
    cp0_map__test_shift_clock(-20 * 86400);
    printf("map cache: least recently used out across the caches, down to 90 %% of the cap, shrink at once, default "
           "rule\n");
}

static void test_archives_untouched(void)
{
    char store[600], src[600], p[700];
    tpath(store, sizeof store, "untouched");
    cp0_map_store_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.store_dir = store;
    cfg.ram_cache_bytes = 1u << 20;
    cfg.log = quiet;
    cp0_map_store_t *s;
    CHECK_RC(cp0_map_store_open(&cfg, &s), CP0_MAP_OK);
    /* an imported archive (copied into archives/) */
    tileset_t ts;
    tiles_make(&ts, 4, 64);
    tpath(src, sizeof src, "dl/town.pmtiles");
    pm_write(src, ts.t, ts.n, 1, 0, 2, NULL);
    CHECK_RC(cp0_map_store_import(s, src, 1, NULL, NULL), CP0_MAP_OK);
    snprintf(p, sizeof p, "%s/archives/town.pmtiles", store);
    size_t alen;
    uint8_t *abytes = read_file(p, &alen);
    CHECK(abytes != NULL);
    /* the viewing cache of a shipped source, filled, then a cap far below everything */
    cp0_map_vcset_t *set = cp0_map__fetcher_vcset(cp0_map__store_fetcher(s));
    cp0_map_vcache_t *vc = cp0_map__vcset_get(set, "opentopomap");
    CHECK(vc && cp0_map__vcache_writer(vc));
    for (uint32_t i = 0; i < 30; i++) put_tile(vc, 13, i, 2, 4096);
    CHECK_RC(cp0_map_store_flush(s), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_set_cap(s, 4096), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_flush(s), CP0_MAP_OK);
    cp0_map_vusage_t u;
    cp0_map__vcache_usage(vc, 0, &u);
    CHECK(u.tiles == 0);
    /* the archive: the same bytes, still listed, its tiles still there */
    size_t alen2;
    uint8_t *abytes2 = read_file(p, &alen2);
    CHECK(abytes2 && alen2 == alen && !memcmp(abytes, abytes2, alen));
    cp0_map_source_info_t e;
    CHECK_RC(cp0_map_store_find(s, "town", &e), CP0_MAP_OK);
    CHECK(e.kind == CP0_MAP_ENTRY_ARCHIVE && e.state == CP0_MAP_OK && e.bytes == alen);
    cp0_map_buf_t b = {0};
    CHECK_RC(cp0_map_store_get_tile(s, "town", ts.t[ts.n - 1].z, ts.t[ts.n - 1].x, ts.t[ts.n - 1].y, &b, NULL), CP0_MAP_OK);
    cp0_map_store_stats_t st;
    CHECK_RC(cp0_map_store_cache_stats(s, &st), CP0_MAP_OK);
    CHECK(st.cap == 4096 && !st.cap_auto && st.disk_bytes < alen + 200000);
    CHECK_RC(cp0_map_store_clear_cache(s, NULL), CP0_MAP_OK);
    CHECK(file_exists(p));
    CHECK_RC(cp0_map_store_clear_cache(s, "town"), CP0_MAP_E_NOT_FOUND); /* an archive is not a cache */
    cp0_map_buf_free(&b);
    free(abytes);
    free(abytes2);
    tiles_free(&ts);
    cp0_map_store_close(s);
    printf("map cache: imported archives never evicted nor cleared, whatever the cap\n");
}

static void test_single_writer(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "writer");
    cp0_map_vcset_t *s1 = cp0_map__vcset_new(dir, 0, 0, &g_log);
    cp0_map_vcset_t *s2 = cp0_map__vcset_new(dir, 0, 0, &g_log);
    cp0_map_vcset_t *ro = cp0_map__vcset_new(dir, 1, 0, &g_log);
    cp0_map_vcache_t *w = cp0_map__vcset_get(s1, "x"), *r = cp0_map__vcset_get(s2, "x"), *r2 = cp0_map__vcset_get(ro, "x");
    CHECK(cp0_map__vcache_writer(w) && !cp0_map__vcache_writer(r) && !cp0_map__vcache_writer(r2));
    put_tile(w, 9, 1, 1, 1000);
    uint8_t one = 1;
    cp0_map_vput_t p = {CP0_MAP_VPUT_TILE, 9, 2, 2, &one, 1, 0, 0, NULL, NULL};
    CHECK_RC(cp0_map__vcache_put(r, &p), CP0_MAP_E_READ_ONLY);
    CHECK_RC(cp0_map__vcache_clear(r), CP0_MAP_E_READ_ONLY);
    CHECK(!has_tile(r, 9, 1, 1)); /* not committed yet: the other app does not see it */
    CHECK_RC(cp0_map__vcset_flush(s1), CP0_MAP_OK);
    CHECK(has_tile(r, 9, 1, 1) && has_tile(r2, 9, 1, 1));
    /* a second process too (flock is per open file: the same holds in another app) */
    const pid_t pid = MAP_TEST_TSAN ? -2 : fork();
    CHECK(pid >= 0 || pid == -2);
    if (pid == 0) {
        cp0_map_vcset_t *c = cp0_map__vcset_new(dir, 0, 0, &g_log);
        cp0_map_vcache_t *cv = cp0_map__vcset_get(c, "x");
        const int ok = !cp0_map__vcache_writer(cv) && has_tile(cv, 9, 1, 1);
        cp0_map__vcset_free(c);
        _exit(ok ? 0 : 1);
    }
    int status = 0;
    CHECK(pid == -2 || (waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0));
    /* the writer leaves: the next app to open becomes the writer */
    cp0_map__vcset_free(s1);
    cp0_map_vcset_t *s3 = cp0_map__vcset_new(dir, 0, 0, &g_log);
    CHECK(cp0_map__vcache_writer(cp0_map__vcset_get(s3, "x")));
    cp0_map__vcset_free(s3);
    cp0_map__vcset_free(s2);
    cp0_map__vcset_free(ro);
    printf("map cache: one writer per file (another opener, another process: read-only), the next opener writes\n");
}

static long long sql_int(const char *path, const char *q, char *text, size_t tsize)
{
    const cp0_map_sqlite_api_t *sql = cp0_map__sqlite();
    sqlite3 *db = NULL;
    CHECK(sql->open_v2(path, &db, CP0_MAP_SQLITE_OPEN_READWRITE, NULL) == CP0_MAP_SQLITE_OK); /* rolls a hot journal back */
    sqlite3_stmt *st = NULL;
    CHECK(sql->prepare_v2(db, q, -1, &st, NULL) == CP0_MAP_SQLITE_OK);
    long long v = -1;
    if (sql->step(st) == CP0_MAP_SQLITE_ROW) {
        v = sql->column_int64(st, 0);
        if (text) {
            const unsigned char *t = sql->column_text(st, 0);
            snprintf(text, tsize, "%s", t ? (const char *)t : "");
        }
    }
    sql->finalize(st);
    sql->close_v2(db);
    return v;
}

static void test_crash(void)
{
    char dir[600], p[700], j[720], ok[64];
    tpath(dir, sizeof dir, "crash");
    snprintf(p, sizeof p, "%s/tiles/c.mbtiles", dir);
    snprintf(j, sizeof j, "%s-journal", p);
    /* abort() in the middle of the second commit (10 of its 32 rows written) */
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        cp0_map_vcset_t *set = cp0_map__vcset_new(dir, 0, 0, &g_log);
        cp0_map_vcache_t *vc = cp0_map__vcset_get(set, "c");
        for (uint32_t i = 0; i < 32; i++) put_tile(vc, 8, i, 0, 3000);
        cp0_map__vcset_flush(set);
        cp0_map__vcache_test_abort_after(10);
        for (uint32_t i = 0; i < 32; i++) put_tile(vc, 8, i, 1, 3000);
        cp0_map__vcset_flush(set);
        _exit(3); /* not reached */
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    CHECK(file_exists(j)); /* the hot journal of the interrupted commit */
    CHECK(sql_int(p, "PRAGMA integrity_check", ok, sizeof ok) == 0 && !strcmp(ok, "ok"));
    CHECK(sql_int(p, "SELECT COUNT(*) FROM tiles", NULL, 0) == 32);
    CHECK(sql_int(p, "SELECT COUNT(*) FROM cp0_tile_state", NULL, 0) == 32);
    cp0_map_archive_t *a;
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_archive_close(a);
    /* SIGKILL at a random moment while batches go in: whole batches only, always a valid file */
    for (int round = 0; round < 3; round++) {
        pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            cp0_map_vcset_t *set = cp0_map__vcset_new(dir, 0, 0, &g_log);
            cp0_map_vcache_t *vc = cp0_map__vcset_get(set, "c");
            for (uint32_t row = 2 + (uint32_t)round * 1000;; row++) {
                for (uint32_t i = 0; i < 32; i++) put_tile(vc, 8, i, row, 3000);
                cp0_map__vcset_flush(set);
            }
        }
        usleep(150000 + (useconds_t)round * 70000);
        kill(pid, SIGKILL);
        CHECK(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status));
        CHECK(sql_int(p, "PRAGMA integrity_check", ok, sizeof ok) == 0 && !strcmp(ok, "ok"));
        const long long n = sql_int(p, "SELECT COUNT(*) FROM tiles", NULL, 0);
        CHECK(n >= 32 && n % 32 == 0 && n == sql_int(p, "SELECT COUNT(*) FROM cp0_tile_state", NULL, 0));
    }
    /* and the next app writes on */
    cp0_map_vcset_t *set = cp0_map__vcset_new(dir, 0, 0, &g_log);
    cp0_map_vcache_t *vc = cp0_map__vcset_get(set, "c");
    CHECK(cp0_map__vcache_writer(vc) && has_tile(vc, 8, 5, 0));
    put_tile(vc, 8, 0, 999999, 100);
    CHECK_RC(cp0_map__vcset_flush(set), CP0_MAP_OK);
    CHECK(has_tile(vc, 8, 0, 999999));
    cp0_map__vcset_free(set);
    printf("map cache: a writer killed mid-commit (abort, SIGKILL) leaves a valid MBTiles of whole batches\n");
}

static void test_cache_full(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "full");
    char p[700];
    snprintf(p, sizeof p, "%s/sources/mine.conf", dir);
    const char *r = "v=1\nurl=http://127.0.0.1:9/{z}/{x}/{y}.png\nenabled=1\n";
    write_file(p, r, strlen(r));
    cp0_map_store_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.store_dir = dir;
    cfg.user_agent = "cp0-map-test/1.0 (+https://github.com/OSRdesign/cyberdeck-zero-apps)";
    cfg.log = quiet;
    cfg.ram_cache_bytes = 1u << 20;
    cp0_map_store_t *s;
    CHECK_RC(cp0_map_store_open(&cfg, &s), CP0_MAP_OK);
    cp0_map_vcset_t *set = cp0_map__fetcher_vcset(cp0_map__store_fetcher(s));
    cp0_map_vcache_t *vc = cp0_map__vcset_get(set, "mine");
    cp0_map__vcset_test_free(set, 10ull << 20); /* 10 MiB free: below the 64 MiB floor */
    CHECK(cp0_map__vcset_full(set));
    uint8_t one = 1;
    cp0_map_vput_t put = {CP0_MAP_VPUT_TILE, 9, 2, 2, &one, 1, 0, 0, NULL, NULL};
    CHECK_RC(cp0_map__vcache_put(vc, &put), CP0_MAP_E_CACHE_FULL);
    CHECK_RC(cp0_map_store_set_network(s, 1), CP0_MAP_OK);
    cp0_map_source_info_t e;
    CHECK_RC(cp0_map_store_find(s, "mine", &e), CP0_MAP_OK);
    CHECK(e.state == CP0_MAP_E_CACHE_FULL || e.state == CP0_MAP_E_CURL_MISSING);
    cp0_map_store_stats_t st;
    CHECK_RC(cp0_map_store_cache_stats(s, &st), CP0_MAP_OK);
    CHECK(st.state == CP0_MAP_E_CACHE_FULL);
    cp0_map__vcset_test_free(set, 1ull << 40);
    CHECK(!cp0_map__vcset_full(set));
    CHECK_RC(cp0_map__vcache_put(vc, &put), CP0_MAP_OK);
    cp0_map_store_close(s);
    printf("map cache: below 64 MiB of free space the cache stops growing (cache full), then grows again\n");
}

int main(void)
{
    setenv("TZ", "UTC", 1);
    tzset();
    tmp_init("cp0map-vcache");
    cp0_map__log_init(&g_log, quiet, NULL);
    if (!cp0_map__sqlite()) {
        printf("SKIP test_map_vcache (libsqlite3.so.0 missing)\n");
        return 0;
    }
    test_batches();
    test_eviction();
    test_archives_untouched();
    test_single_writer();
    if (!MAP_TEST_TSAN) test_crash();
    test_cache_full();
    cp0_map__log_done(&g_log);
    tmp_done();
    printf("test_map_vcache: %ld checks passed\n", g_checks);
    return 0;
}
