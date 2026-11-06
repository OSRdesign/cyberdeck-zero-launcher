/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine, the fetch pool against the fake tile server (tests/fake_tile_server.py on 127.0.0.1: plain http, the
 * local-host exception; no real server is ever contacted). Task 3 of Deck Atlas 0.1:
 *   - a fetch: the app's User-Agent on every request, tiles stored in tiles/<id>.mbtiles (a valid MBTiles), read back,
 *     kept after a restart, statistics;
 *   - only the tiles a client asks for (never anything else), the parallel cap per source and in all (4), the minimum
 *     interval, de-duplication across clients, a request cancelled when its tile leaves the view;
 *   - HTTP caching: freshness, stale tiles shown and revalidated with If-None-Match (304), stale tiles served offline and
 *     on errors, Expires, no-store kept in RAM only;
 *   - back-off on 429 / 503 with Retry-After, a pause after repeated errors, no faster retry of a failing tile;
 *   - the three guards: the blocked picture (provider paused, copies removed, kept across a restart, open sea is not
 *     blocked), 404 / 410 remembered 24 h (across a restart), the daily cap (across a restart, a new day);
 *   - keys: sent to the server, never in a log line (the URL shows "***");
 *   - redirects (to an OSMF host, to plain http: refused; same host: followed; a loop: stopped), a 3 MB answer, HTML
 *     answers, a dropped connection;
 *   - no User-Agent, a library default User-Agent, libcurl missing, a second app (read-only for the source).
 * Also run under ThreadSanitizer by run_tests.sh.
 */
#define _GNU_SOURCE /* memmem */
#include "map_test_util.h"
#include "cp0_map_online.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>

#ifndef MAP_FAKE_SERVER
#error "MAP_FAKE_SERVER: the path of tests/fake_tile_server.py"
#endif

#define UA "cp0-map-test/1.0 (+https://github.com/OSRdesign/cyberdeck-zero-apps)"

static pid_t g_srv;
static int g_port;
static char g_srvlog[600];
static int64_t g_shift;

/* ---------------------------------------------------------------------------------------------- the server */

static void server_start(void)
{
    char portfile[600];
    tpath(portfile, sizeof portfile, "srv.port");
    tpath(g_srvlog, sizeof g_srvlog, "srv.log");
    g_srv = fork();
    CHECK(g_srv >= 0);
    if (g_srv == 0) {
        execlp("python3", "python3", "-I", MAP_FAKE_SERVER, portfile, g_srvlog, (char *)NULL);
        _exit(127);
    }
    for (int i = 0; i < 1000 && !file_exists(portfile); i++) usleep(10000);
    size_t n;
    char *t = (char *)read_file(portfile, &n), num[16] = "";
    CHECK(t != NULL && n < sizeof num);
    memcpy(num, t, n);
    g_port = atoi(num);
    free(t);
    CHECK(g_port > 0);
}

static void server_stop(void)
{
    if (g_srv > 0) {
        kill(g_srv, SIGTERM);
        waitpid(g_srv, NULL, 0);
    }
}

/* GET path on the fake server with a plain socket (control requests) */
static void server_ctl(const char *path)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)g_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(connect(fd, (struct sockaddr *)&a, sizeof a) == 0);
    char req[300], buf[512];
    const int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n", path);
    CHECK(write(fd, req, (size_t)n) == n);
    while (read(fd, buf, sizeof buf) > 0) {
    }
    close(fd);
}

typedef struct {
    char path[256], ua[200], inm[64];
    int conc;
    double t;
} srvreq_t;

static const char *json_str(const char *line, const char *key, char *out, size_t size)
{
    char k[32];
    snprintf(k, sizeof k, "\"%s\": \"", key);
    const char *p = strstr(line, k);
    size_t o = 0;
    if (size) out[0] = '\0';
    if (!p) return NULL;
    for (p += strlen(k); *p && *p != '"'; p++) {
        if (*p == '\\' && p[1]) p++;
        if (o + 1 < size) out[o++] = *p;
    }
    out[o] = '\0';
    return out;
}

/* the requests the server saw (all of them, oldest first) */
static int srv_requests(srvreq_t *out, int max)
{
    size_t len;
    char *text = (char *)read_file(g_srvlog, &len);
    if (!text) return 0;
    text = (char *)realloc(text, len + 1);
    text[len] = '\0';
    int n = 0;
    for (char *save = NULL, *l = strtok_r(text, "\n", &save); l && n < max; l = strtok_r(NULL, "\n", &save)) {
        srvreq_t *r = &out[n++];
        json_str(l, "path", r->path, sizeof r->path);
        json_str(l, "ua", r->ua, sizeof r->ua);
        json_str(l, "inm", r->inm, sizeof r->inm);
        const char *c = strstr(l, "\"conc\": ");
        r->conc = c ? atoi(c + 8) : 0;
        const char *t = strstr(l, "\"t\": ");
        r->t = t ? atof(t + 5) : 0;
    }
    free(text);
    return n;
}

static srvreq_t g_reqs[4096];

static int srv_count(void)
{
    return srv_requests(g_reqs, 4096);
}

/* requests since `from` whose path starts with prefix */
static int srv_count_prefix(int from, const char *prefix)
{
    const int n = srv_count();
    int k = 0;
    for (int i = from; i < n; i++) k += !strncmp(g_reqs[i].path, prefix, strlen(prefix));
    return k;
}

/* ---------------------------------------------------------------------------------------------- the engine */

static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_log[64 * 1024];
static size_t g_log_len;

static void capture_log(void *user, const char *line)
{
    (void)user;
    pthread_mutex_lock(&g_log_mu);
    const size_t n = strlen(line);
    if (g_log_len + n + 2 < sizeof g_log) {
        memcpy(g_log + g_log_len, line, n);
        g_log_len += n;
        g_log[g_log_len++] = '\n';
        g_log[g_log_len] = '\0';
    }
    pthread_mutex_unlock(&g_log_mu);
}

static int log_has(const char *needle)
{
    pthread_mutex_lock(&g_log_mu);
    const int yes = memmem(g_log, g_log_len, needle, strlen(needle)) != NULL;
    pthread_mutex_unlock(&g_log_mu);
    return yes;
}

static cp0_map_store_t *ostore_cfg(const char *dir, int network, const char *ua, int ro)
{
    cp0_map_store_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.store_dir = dir;
    cfg.network = network;
    cfg.user_agent = ua;
    cfg.read_only = ro;
    cfg.ram_cache_bytes = 1u << 20;
    cfg.log = capture_log;
    cp0_map_store_t *s = NULL;
    CHECK_RC(cp0_map_store_open(&cfg, &s), CP0_MAP_OK);
    return s;
}

static cp0_map_store_t *ostore(const char *dir)
{
    return ostore_cfg(dir, 1, UA, 0);
}

/* sources/<id>.conf on the fake server's mode path (url_tail after the mode, default the plain tile path) */
static void rec(const char *dir, const char *id, const char *mode, const char *extra)
{
    char p[700], text[1024];
    snprintf(p, sizeof p, "%s/sources/%s.conf", dir, id);
    snprintf(text, sizeof text, "v=1\nurl=http://127.0.0.1:%d/%s/{z}/{x}/{y}.png%s\nenabled=1\nmaxzoom=12\n%s\n", g_port,
             mode, strstr(extra ? extra : "", "{key}") ? "?apikey={key}" : "", extra ? extra : "");
    write_file(p, text, strlen(text));
}

static int wait_events(cp0_map_client_t *c, cp0_map_tile_event_t *ev, int max, int want, int timeout_ms)
{
    int n = 0;
    const double until = now_ms() + timeout_ms;
    while (n < want && now_ms() < until) {
        n += cp0_map_client_poll(c, ev + n, max - n);
        if (n < want) usleep(5000);
    }
    return n;
}

static void dump_events(const char *what, const cp0_map_tile_event_t *ev, int n)
{
    fprintf(stderr, "%s: %d events\n", what, n);
    for (int i = 0; i < n; i++)
        fprintf(stderr, "  %d/%u/%u kind %d result %d (%s)\n", ev[i].z, ev[i].x, ev[i].y, (int)ev[i].kind, ev[i].result,
                cp0_map_strerror(ev[i].result));
}

static int count_kind(const cp0_map_tile_event_t *ev, int n, cp0_map_tile_event_kind_t kind, int result)
{
    int k = 0;
    for (int i = 0; i < n; i++) k += ev[i].kind == kind && ev[i].result == result;
    return k;
}

static int state_of(cp0_map_store_t *s, const char *id, int64_t *until)
{
    cp0_map_source_info_t e;
    CHECK_RC(cp0_map_store_find(s, id, &e), CP0_MAP_OK);
    if (until) *until = e.until;
    return e.state;
}

static void shift(int64_t secs)
{
    cp0_map__test_shift_clock(secs);
    g_shift += secs;
}

static void unshift(void)
{
    cp0_map__test_shift_clock(-g_shift);
    g_shift = 0;
}

static const cp0_map_tile_id_t k_tiles[8] = {{5, 1, 1}, {5, 2, 1}, {5, 1, 2}, {5, 2, 2},
                                             {5, 3, 1}, {5, 3, 2}, {5, 1, 3}, {5, 2, 3}};

/* --------------------------------------------------------------------------------------------------- tests */

static void test_curl_missing(void)
{
    /* first, before this process loads libcurl: a child with a library that does not exist */
    char dir[600];
    tpath(dir, sizeof dir, "nocurl");
    rec(dir, "t-normal", "normal", NULL);
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        setenv("CP0_MAP_CURL_LIB", "libcurl-not-here.so.9", 1);
        cp0_map_store_t *s = ostore(dir);
        const int st = state_of(s, "t-normal", NULL);
        cp0_map_store_close(s);
        _exit(st == CP0_MAP_E_CURL_MISSING ? 0 : 1);
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(srv_count() == 0);
    printf("map fetch: libcurl missing: the source says so (network unavailable), nothing is asked\n");
}

static void test_basic(void)
{
    char dir[600], p[700];
    tpath(dir, sizeof dir, "basic");
    rec(dir, "t-normal", "normal", NULL);
    const int base = srv_count();
    cp0_map_store_t *s = ostore(dir);
    CHECK(state_of(s, "t-normal", NULL) == CP0_MAP_OK);
    cp0_map_client_t *c;
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_view(c, "t-normal", k_tiles, 3), CP0_MAP_OK);
    cp0_map_tile_event_t ev[32];
    int n = wait_events(c, ev, 32, 3, 10000);
    CHECK(n == 3 && count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 3 && !strcmp(ev[0].archive_id, "t-normal"));
    CHECK(cp0_map_client_pending(c) == 0);
    /* the server saw exactly the three tiles, each once, each with our User-Agent */
    const int total = srv_count();
    CHECK(total - base == 3);
    for (int i = base; i < total; i++) {
        CHECK(!strcmp(g_reqs[i].ua, UA));
        int known = 0;
        for (int k = 0; k < 3; k++) {
            char want[64];
            snprintf(want, sizeof want, "/normal/%d/%u/%u.png", k_tiles[k].z, k_tiles[k].x, k_tiles[k].y);
            known |= !strcmp(g_reqs[i].path, want);
        }
        CHECK(known);
    }
    /* readable at once (before any commit) and after one */
    cp0_map_buf_t b = {0};
    cp0_map_tile_meta_t meta;
    CHECK_RC(cp0_map_store_get_tile(s, "t-normal", 5, 1, 1, &b, &meta), CP0_MAP_OK);
    CHECK(meta.format == CP0_MAP_FMT_PNG && !meta.stale && meta.expires > cp0_map__now() + 86000);
    CHECK_RC(cp0_map_store_flush(s), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_get_tile(s, "t-normal", 5, 1, 1, &b, &meta), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_get_tile(s, "t-normal", 5, 3, 3, &b, NULL), CP0_MAP_E_NO_TILE);
    /* a fresh tile asked again: answered from the cache, nothing sent */
    CHECK_RC(cp0_map_client_view(c, "t-normal", k_tiles, 1), CP0_MAP_OK);
    n = wait_events(c, ev, 32, 1, 5000);
    CHECK(n == 1 && ev[0].kind == CP0_MAP_TILE_FETCHED && srv_count() - base == 3);
    /* statistics */
    cp0_map_store_stats_t st;
    CHECK_RC(cp0_map_store_cache_stats(s, &st), CP0_MAP_OK);
    CHECK(st.state == CP0_MAP_OK && st.cap > 0 && st.cap_auto && st.disk_bytes > 0 && st.tiles == 3);
    CHECK(st.fetches_today == 3 && st.nsources == 4 && st.ram.budget == 1u << 20);
    const cp0_map_source_stats_t *ss = NULL;
    for (int i = 0; i < st.nsources; i++)
        if (!strcmp(st.sources[i].id, "t-normal")) ss = &st.sources[i];
    CHECK(ss && ss->writer && ss->tiles == 3 && ss->fetched == 3 && ss->fetches_today == 3 && ss->stale == 0);
    CHECK(ss->daily_cap == CP0_MAP_DAILY_CAP_DEFAULT && ss->bytes > 0 && ss->state == CP0_MAP_OK);
    cp0_map_client_free(c);
    cp0_map_store_close(s);
    /* the file is a plain MBTiles other tools open */
    snprintf(p, sizeof p, "%s/tiles/t-normal.mbtiles", dir);
    cp0_map_archive_t *a;
    CHECK_RC(cp0_map_archive_open(p, &a), CP0_MAP_OK);
    cp0_map_archive_info_t ai;
    CHECK_RC(cp0_map_archive_info(a, &ai), CP0_MAP_OK);
    CHECK(ai.kind == CP0_MAP_ARCHIVE_MBTILES && ai.format == CP0_MAP_FMT_PNG && ai.minzoom == 0 && ai.maxzoom == 12);
    cp0_map_buf_t b2 = {0};
    CHECK_RC(cp0_map_archive_get_tile(a, 5, 1, 1, &b2, NULL), CP0_MAP_OK);
    CHECK(b2.len == b.len && !memcmp(b2.data, b.data, b.len));
    cp0_map_archive_close(a);
    /* persistent: still there after the app quit */
    s = ostore_cfg(dir, 0, UA, 0);
    CHECK_RC(cp0_map_store_get_tile(s, "t-normal", 5, 2, 1, &b2, NULL), CP0_MAP_OK);
    CHECK(state_of(s, "t-normal", NULL) == CP0_MAP_E_OFFLINE);
    cp0_map_store_close(s);
    cp0_map_buf_free(&b);
    cp0_map_buf_free(&b2);
    printf("map fetch: the app's User-Agent, only the tiles asked, stored in a valid MBTiles, read back, kept\n");
}

static int max_conc(int from, int to)
{
    int m = 0;
    for (int i = from; i < to; i++)
        if (g_reqs[i].conc > m) m = g_reqs[i].conc;
    return m;
}

static void test_parallel(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "parallel");
    rec(dir, "t-slow", "slow", "max_parallel=2");
    rec(dir, "t-slowa", "slow", "max_parallel=4");
    rec(dir, "t-slowb", "slow", "max_parallel=4");
    rec(dir, "t-gap", "normal", "max_parallel=4\nmin_request_interval_ms=300");
    cp0_map_store_t *s = ostore(dir);
    cp0_map_client_t *c, *c2;
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_new(s, &c2), CP0_MAP_OK);
    cp0_map_tile_event_t ev[64];
    /* one source, max_parallel 2 */
    int base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-slow", k_tiles, 6), CP0_MAP_OK);
    int n = wait_events(c, ev, 64, 6, 15000);
    CHECK(n == 6 && count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 6);
    int total = srv_count();
    CHECK(total - base == 6 && max_conc(base, total) <= 2 && max_conc(base, total) >= 1);
    /* two sources of 4: never more than 4 at once in all */
    base = total;
    CHECK_RC(cp0_map_client_view(c, "t-slowa", k_tiles, 8), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_view(c2, "t-slowb", k_tiles, 8), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 8, 20000);
    CHECK(n == 8);
    n = wait_events(c2, ev, 64, 8, 20000);
    CHECK(n == 8);
    total = srv_count();
    CHECK(total - base == 16 && max_conc(base, total) <= CP0_MAP_MAX_PARALLEL && max_conc(base, total) > 2);
    /* the minimum interval between two requests to a provider */
    base = total;
    CHECK_RC(cp0_map_client_view(c, "t-gap", k_tiles, 4), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 4, 10000);
    CHECK(n == 4);
    total = srv_count();
    CHECK(total - base == 4);
    for (int i = base + 1; i < total; i++) CHECK(g_reqs[i].t - g_reqs[i - 1].t >= 0.27);
    cp0_map_client_free(c);
    cp0_map_client_free(c2);
    cp0_map_store_close(s);
    printf("map fetch: at most max_parallel per source and 4 in all, the minimum interval between requests\n");
}

static void test_cancel_dedupe(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "cancel");
    rec(dir, "t-slow3", "slow3", "max_parallel=1");
    rec(dir, "t-dup", "slow", "max_parallel=4");
    cp0_map_store_t *s = ostore(dir);
    cp0_map_client_t *c, *c2, *c3;
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_new(s, &c2), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_new(s, &c3), CP0_MAP_OK);
    cp0_map_tile_event_t ev[64];
    /* tile A is asked, then leaves the view: its request is cancelled, B goes out at once (max_parallel 1) */
    const int base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-slow3", &k_tiles[0], 1), CP0_MAP_OK);
    for (int i = 0; i < 300 && srv_count() == base; i++) usleep(10000);
    CHECK(srv_count() == base + 1);
    const double moved = now_ms() / 1000.0;
    CHECK_RC(cp0_map_client_view(c, "t-slow3", &k_tiles[1], 1), CP0_MAP_OK);
    for (int i = 0; i < 300 && srv_count() == base + 1; i++) usleep(10000);
    CHECK(srv_count() == base + 2);
    /* the server's clock and ours are both wall clocks of this machine */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    const double wall_now = ts.tv_sec + ts.tv_nsec / 1e9, since_move = now_ms() / 1000.0 - moved;
    CHECK(wall_now - g_reqs[base + 1].t <= since_move + 0.05 && since_move < 2.0); /* B started well before A's 3 s */
    int n = wait_events(c, ev, 64, 1, 8000);
    CHECK(n == 1 && ev[0].kind == CP0_MAP_TILE_FETCHED && ev[0].x == k_tiles[1].x && ev[0].y == k_tiles[1].y);
    cp0_map_buf_t b = {0};
    CHECK_RC(cp0_map_store_get_tile(s, "t-slow3", k_tiles[0].z, k_tiles[0].x, k_tiles[0].y, &b, NULL), CP0_MAP_E_NO_TILE);
    /* two clients want the same tiles: each is asked once, both hear about it */
    const int b2 = srv_count();
    CHECK_RC(cp0_map_client_view(c2, "t-dup", k_tiles, 4), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_view(c3, "t-dup", k_tiles, 4), CP0_MAP_OK);
    n = wait_events(c2, ev, 64, 4, 10000);
    CHECK(n == 4 && count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 4);
    n = wait_events(c3, ev, 64, 4, 10000);
    CHECK(n == 4 && count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 4);
    CHECK(srv_count() - b2 == 4);
    cp0_map_buf_free(&b);
    cp0_map_client_free(c);
    cp0_map_client_free(c2);
    cp0_map_client_free(c3);
    cp0_map_store_close(s);
    printf("map fetch: a request cancelled when its tile leaves the view, one request per tile across clients\n");
}

static void test_caching(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "caching");
    rec(dir, "t-short", "short", NULL);
    rec(dir, "t-sw", "sw/flip", NULL);
    rec(dir, "t-nostore", "nostore", NULL);
    rec(dir, "t-expires", "expires", NULL);
    cp0_map_store_t *s = ostore(dir);
    cp0_map_client_t *c;
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    cp0_map_tile_event_t ev[64];
    cp0_map_buf_t b = {0};
    cp0_map_tile_meta_t meta;
    /* max-age=0: fresh for the 1 h floor, then stale (still shown), revalidated with If-None-Match -> 304 */
    int base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-short", k_tiles, 1), CP0_MAP_OK);
    CHECK(wait_events(c, ev, 64, 1, 5000) == 1 && ev[0].kind == CP0_MAP_TILE_FETCHED);
    CHECK_RC(cp0_map_store_flush(s), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_get_tile(s, "t-short", 5, 1, 1, &b, &meta), CP0_MAP_OK);
    CHECK(!meta.stale);
    shift(2 * 3600);
    CHECK_RC(cp0_map_store_get_tile(s, "t-short", 5, 1, 1, &b, &meta), CP0_MAP_OK);
    CHECK(meta.stale);
    cp0_map_store_stats_t st;
    CHECK_RC(cp0_map_store_cache_stats(s, &st), CP0_MAP_OK);
    CHECK(st.stale == 1);
    CHECK_RC(cp0_map_client_view(c, "t-short", k_tiles, 1), CP0_MAP_OK);
    CHECK(wait_events(c, ev, 64, 1, 5000) == 1 && ev[0].kind == CP0_MAP_TILE_FETCHED);
    int total = srv_count();
    CHECK(total - base == 2 && !strcmp(g_reqs[total - 1].inm, "\"5-1-1\""));
    CHECK_RC(cp0_map_store_flush(s), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_get_tile(s, "t-short", 5, 1, 1, &b, &meta), CP0_MAP_OK);
    CHECK(!meta.stale);
    /* offline: the stale tile is served, nothing is asked, the tile waits */
    shift(31 * 86400);
    CHECK_RC(cp0_map_store_set_network(s, 0), CP0_MAP_OK);
    CHECK(state_of(s, "t-short", NULL) == CP0_MAP_E_OFFLINE);
    CHECK_RC(cp0_map_store_get_tile(s, "t-short", 5, 1, 1, &b, &meta), CP0_MAP_OK);
    CHECK(meta.stale);
    base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-short", k_tiles, 1), CP0_MAP_OK);
    CHECK(wait_events(c, ev, 64, 1, 2000) == 1 && ev[0].kind == CP0_MAP_TILE_REFUSED && ev[0].result == CP0_MAP_E_OFFLINE);
    usleep(300000);
    CHECK(srv_count() == base && cp0_map_client_pending(c) == 0);
    /* back online: the waiting tile is revalidated */
    CHECK_RC(cp0_map_store_set_network(s, 1), CP0_MAP_OK);
    CHECK(wait_events(c, ev, 64, 1, 5000) == 1 && ev[0].kind == CP0_MAP_TILE_FETCHED);
    CHECK(srv_count() == base + 1);
    /* on errors the stale tile stays and is served */
    server_ctl("/_sw?name=flip&mode=normal");
    CHECK_RC(cp0_map_client_view(c, "t-sw", k_tiles, 1), CP0_MAP_OK);
    CHECK(wait_events(c, ev, 64, 1, 5000) == 1 && ev[0].kind == CP0_MAP_TILE_FETCHED);
    shift(31 * 86400);
    server_ctl("/_sw?name=flip&mode=e500");
    CHECK_RC(cp0_map_client_view(c, "t-sw", k_tiles, 1), CP0_MAP_OK);
    CHECK(wait_events(c, ev, 64, 1, 5000) == 1 && ev[0].kind == CP0_MAP_TILE_FETCH_FAILED &&
          ev[0].result == CP0_MAP_E_NETWORK);
    CHECK_RC(cp0_map_store_get_tile(s, "t-sw", 5, 1, 1, &b, &meta), CP0_MAP_OK);
    CHECK(meta.stale);
    unshift();
    /* no-store: shown (from RAM), never written to the file */
    CHECK_RC(cp0_map_client_view(c, "t-nostore", k_tiles, 1), CP0_MAP_OK);
    CHECK(wait_events(c, ev, 64, 1, 5000) == 1 && ev[0].kind == CP0_MAP_TILE_FETCHED);
    CHECK_RC(cp0_map_store_get_tile(s, "t-nostore", 5, 1, 1, &b, NULL), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_flush(s), CP0_MAP_OK);
    char p[700];
    snprintf(p, sizeof p, "%s/tiles/t-nostore.mbtiles", dir);
    CHECK(!file_exists(p));
    /* Expires / Date: one hour */
    CHECK_RC(cp0_map_client_view(c, "t-expires", k_tiles, 1), CP0_MAP_OK);
    CHECK(wait_events(c, ev, 64, 1, 5000) == 1 && ev[0].kind == CP0_MAP_TILE_FETCHED);
    CHECK_RC(cp0_map_store_get_tile(s, "t-expires", 5, 1, 1, &b, &meta), CP0_MAP_OK);
    CHECK(llabs((long long)(meta.expires - cp0_map__now() - 3600)) < 60);
    cp0_map_buf_free(&b);
    cp0_map_client_free(c);
    cp0_map_store_close(s);
    printf("map fetch: freshness floor, stale shown and revalidated (304), served offline and on errors, no-store, "
           "Expires\n");
}

static void test_backoff(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "backoff");
    rec(dir, "t-429", "sw/b429", "max_parallel=1");
    rec(dir, "t-503", "r503", "max_parallel=1");
    rec(dir, "t-500", "e500", "max_parallel=1");
    rec(dir, "t-500b", "e500", NULL);
    server_ctl("/_sw?name=b429&mode=r429");
    cp0_map_store_t *s = ostore(dir);
    cp0_map_client_t *c;
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    cp0_map_tile_event_t ev[64];
    /* 429 Retry-After: 1 -> paused, the tiles wait, then go */
    int base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-429", k_tiles, 2), CP0_MAP_OK);
    int n = wait_events(c, ev, 64, 2, 3000);
    CHECK(n == 2 && count_kind(ev, n, CP0_MAP_TILE_REFUSED, CP0_MAP_E_BACKOFF) == 2);
    int64_t until;
    CHECK(state_of(s, "t-429", &until) == CP0_MAP_E_BACKOFF && until >= cp0_map__now() && until <= cp0_map__now() + 2);
    CHECK(srv_count() == base + 1);
    server_ctl("/_sw?name=b429&mode=normal");
    n = wait_events(c, ev, 64, 2, 6000);
    CHECK(n == 2 && count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 2);
    CHECK(srv_count() == base + 3 && log_has("HTTP 429"));
    /* 503 with an HTTP date */
    CHECK_RC(cp0_map_client_view(c, "t-503", k_tiles, 1), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 1, 3000);
    CHECK(n == 1 && ev[0].kind == CP0_MAP_TILE_REFUSED && ev[0].result == CP0_MAP_E_BACKOFF);
    CHECK(state_of(s, "t-503", &until) == CP0_MAP_E_BACKOFF);
    CHECK_RC(cp0_map_client_view(c, NULL, NULL, 0), CP0_MAP_OK);
    /* five errors in a row: the provider pauses (30 s), the sixth tile is not asked */
    base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-500", k_tiles, 6), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 6, 8000);
    CHECK(count_kind(ev, n, CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_NETWORK) == 5);
    CHECK(state_of(s, "t-500", &until) == CP0_MAP_E_NETWORK && until > cp0_map__now() + 20);
    usleep(500000);
    CHECK(srv_count_prefix(base, "/e500/") == 5 && log_has("errors in a row"));
    /* a failing tile is not asked again sooner than its growing interval (5 s first), however often it is viewed */
    base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-500b", k_tiles, 1), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 1, 3000);
    CHECK(n >= 1 && ev[0].kind == CP0_MAP_TILE_FETCH_FAILED);
    for (int i = 0; i < 20; i++) {
        CHECK_RC(cp0_map_client_view(c, "t-500b", k_tiles, 1), CP0_MAP_OK);
        usleep(100000);
    }
    CHECK(srv_count() - base == 1);
    cp0_map_client_free(c);
    cp0_map_store_close(s);
    printf("map fetch: 429 / 503 Retry-After pauses, five errors pause the provider, no fast retry of a failing tile\n");
}

static void test_missing(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "missing");
    rec(dir, "t-odd", "odd", NULL);
    rec(dir, "t-gone", "gone", NULL);
    cp0_map_store_t *s = ostore(dir);
    cp0_map_client_t *c;
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    cp0_map_tile_event_t ev[64];
    /* (5,1,1) and (5,2,2) even, (5,2,1) and (5,1,2) odd: 404 */
    int base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-odd", k_tiles, 4), CP0_MAP_OK);
    int n = wait_events(c, ev, 64, 4, 8000);
    CHECK(n == 4 && count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 2 &&
          count_kind(ev, n, CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_NO_TILE) == 2);
    const cp0_map_tile_id_t odd[2] = {k_tiles[1], k_tiles[2]};
    CHECK_RC(cp0_map_client_view(c, "t-odd", odd, 2), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 2, 3000);
    CHECK(n == 2 && count_kind(ev, n, CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_NO_TILE) == 2);
    CHECK(srv_count() - base == 4);
    cp0_map_client_free(c);
    cp0_map_store_close(s);
    /* remembered across a restart */
    s = ostore(dir);
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_view(c, "t-odd", odd, 2), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 2, 3000);
    CHECK(n == 2 && count_kind(ev, n, CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_NO_TILE) == 2);
    CHECK(srv_count() - base == 4);
    /* for 24 h, then asked again */
    shift(25 * 3600);
    CHECK_RC(cp0_map_client_view(c, "t-odd", odd, 2), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 2, 5000);
    CHECK(n == 2 && srv_count() - base == 6);
    unshift();
    /* 410 like 404 */
    CHECK_RC(cp0_map_client_view(c, "t-gone", k_tiles, 1), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 1, 3000);
    CHECK(n == 1 && ev[0].kind == CP0_MAP_TILE_FETCH_FAILED && ev[0].result == CP0_MAP_E_NO_TILE);
    cp0_map_client_free(c);
    cp0_map_store_close(s);
    printf("map fetch: 404 / 410 remembered per source for 24 h (across a restart), then asked again\n");
}

static void test_blocked(void)
{
    char dir[600], p[700];
    tpath(dir, sizeof dir, "blocked");
    rec(dir, "t-blk", "blocked", "max_parallel=1\nurl_has={key}");
    rec(dir, "t-sea", "sea", "max_parallel=1");
    const char *key = "BlockKey0123456789";
    cp0_map_store_t *s = ostore(dir);
    CHECK(state_of(s, "t-blk", NULL) == CP0_MAP_E_NO_KEY);
    CHECK_RC(cp0_map_source_set_key(s, "t-blk", key), CP0_MAP_OK);
    CHECK(state_of(s, "t-blk", NULL) == CP0_MAP_OK);
    cp0_map_client_t *c;
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    cp0_map_tile_event_t ev[64];
    const int base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-blk", k_tiles, 6), CP0_MAP_OK);
    int n = wait_events(c, ev, 64, 6, 8000);
    /* three stored before the fourth identical picture told; then the provider is paused and the rest waits */
    CHECK(count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 3);
    CHECK(count_kind(ev, n, CP0_MAP_TILE_REFUSED, CP0_MAP_E_BLOCKED) == 3);
    int64_t until;
    CHECK(state_of(s, "t-blk", &until) == CP0_MAP_E_BLOCKED && until >= cp0_map__now() + CP0_MAP_BLOCKED_PAUSE_S - 5);
    usleep(300000);
    CHECK(srv_count() - base == 4);
    /* the key went to the server, never into a log line */
    for (int i = base; i < base + 4; i++) CHECK(strstr(g_reqs[i].path, key) != NULL);
    CHECK(log_has("blocked") && log_has("apikey=***") && !log_has(key));
    /* the copies stored before are gone */
    CHECK_RC(cp0_map_store_flush(s), CP0_MAP_OK);
    cp0_map_buf_t b = {0};
    for (int i = 0; i < 4; i++)
        CHECK_RC(cp0_map_store_get_tile(s, "t-blk", k_tiles[i].z, k_tiles[i].x, k_tiles[i].y, &b, NULL), CP0_MAP_E_NO_TILE);
    cp0_map_store_stats_t st;
    CHECK_RC(cp0_map_store_cache_stats(s, &st), CP0_MAP_OK);
    for (int i = 0; i < st.nsources; i++)
        if (!strcmp(st.sources[i].id, "t-blk")) CHECK(st.sources[i].tiles == 0 && st.sources[i].state == CP0_MAP_E_BLOCKED);
    /* open sea: identical plain pictures are tiles, not a blocked answer */
    CHECK_RC(cp0_map_client_view(c, "t-sea", k_tiles, 6), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 6, 8000);
    CHECK(n == 6 && count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 6 && state_of(s, "t-sea", NULL) == CP0_MAP_OK);
    cp0_map_client_free(c);
    cp0_map_store_close(s);
    /* the pause is kept across a restart */
    snprintf(p, sizeof p, "%s/state/t-blk.state", dir);
    CHECK(file_exists(p));
    s = ostore(dir);
    int64_t until2;
    CHECK(state_of(s, "t-blk", &until2) == CP0_MAP_E_BLOCKED && until2 == until);
    cp0_map_store_close(s);
    cp0_map_buf_free(&b);
    printf("map fetch: blocked picture: provider paused (kept across a restart), copies removed, key never logged; "
           "open sea is no block\n");
}

static void test_daily_cap(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "cap");
    rec(dir, "t-cap", "normal", "daily_cap=3\nmax_parallel=1");
    cp0_map_store_t *s = ostore(dir);
    cp0_map_client_t *c;
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    cp0_map_tile_event_t ev[64];
    int base = srv_count();
    CHECK_RC(cp0_map_client_view(c, "t-cap", k_tiles, 5), CP0_MAP_OK);
    int n = wait_events(c, ev, 64, 5, 8000);
    if (count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) != 3) dump_events("daily cap", ev, n);
    CHECK(count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 3 &&
          count_kind(ev, n, CP0_MAP_TILE_REFUSED, CP0_MAP_E_CAP_REACHED) == 2);
    int64_t until;
    CHECK(state_of(s, "t-cap", &until) == CP0_MAP_E_CAP_REACHED && until == cp0_map__next_local_midnight(cp0_map__now()));
    CHECK(srv_count() - base == 3 && log_has("daily cap of 3"));
    cp0_map_client_free(c);
    cp0_map_store_close(s);
    /* across a restart */
    s = ostore(dir);
    cp0_map_source_info_t e;
    CHECK_RC(cp0_map_store_find(s, "t-cap", &e), CP0_MAP_OK);
    CHECK(e.state == CP0_MAP_E_CAP_REACHED && e.fetches_today == 3);
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_view(c, "t-cap", &k_tiles[3], 2), CP0_MAP_OK);
    n = wait_events(c, ev, 64, 2, 2000);
    CHECK(n == 2 && count_kind(ev, n, CP0_MAP_TILE_REFUSED, CP0_MAP_E_CAP_REACHED) == 2);
    usleep(200000);
    CHECK(srv_count() - base == 3);
    /* the next day: the count starts again and the waiting tiles go */
    shift(86400);
    n = wait_events(c, ev, 64, 2, 5000);
    CHECK(n == 2 && count_kind(ev, n, CP0_MAP_TILE_FETCHED, CP0_MAP_OK) == 2);
    CHECK_RC(cp0_map_store_find(s, "t-cap", &e), CP0_MAP_OK);
    CHECK(e.state == CP0_MAP_OK && e.fetches_today == 2 && srv_count() - base == 5);
    unshift();
    cp0_map_client_free(c);
    cp0_map_store_close(s);
    printf("map fetch: the daily cap stops a provider until local midnight, kept across a restart\n");
}

static void test_answers(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "answers");
    static const struct {
        const char *id, *mode;
        int kind, result;
    } cases[] = {
        {"t-r-osmf", "redir-osmf", CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_REFUSED},
        {"t-r-http", "redir-http", CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_REFUSED},
        {"t-r-ok", "redir-ok", CP0_MAP_TILE_FETCHED, CP0_MAP_OK},
        {"t-r-loop", "redir-loop", CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_HTTP},
        {"t-big", "big", CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_TOO_LARGE},
        {"t-html", "html", CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_HTTP},
        {"t-liar", "liar", CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_HTTP},
        {"t-drop", "drop", CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_NETWORK},
    };
    const int nc = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < nc; i++) rec(dir, cases[i].id, cases[i].mode, "url_has={key}");
    cp0_map_store_t *s = ostore(dir);
    for (int i = 0; i < nc; i++) CHECK_RC(cp0_map_source_set_key(s, cases[i].id, "AnswerKey0123456789"), CP0_MAP_OK);
    cp0_map_client_t *c;
    CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
    cp0_map_tile_event_t ev[16];
    for (int i = 0; i < nc; i++) {
        const int base = srv_count();
        CHECK_RC(cp0_map_client_view(c, cases[i].id, k_tiles, 1), CP0_MAP_OK);
        const int n = wait_events(c, ev, 16, 1, 8000);
        if (n != 1 || (int)ev[0].kind != cases[i].kind || ev[0].result != cases[i].result)
            fprintf(stderr, "%s: n %d kind %d result %d (%s)\n", cases[i].id, n, n ? (int)ev[0].kind : -1,
                    n ? ev[0].result : 0, n ? cp0_map_strerror(ev[0].result) : "");
        CHECK(n == 1 && (int)ev[0].kind == cases[i].kind && ev[0].result == cases[i].result);
        const int sent = srv_count() - base;
        int want = 1;
        if (!strcmp(cases[i].mode, "redir-loop")) want = 4; /* the first request and 3 redirects */
        else if (!strcmp(cases[i].mode, "redir-ok")) want = 2;
        if (sent != want) fprintf(stderr, "%s: %d requests, want %d\n", cases[i].id, sent, want);
        /* libcurl itself retries once when a reused connection turns out dead (the dropped answer) */
        CHECK(sent == want || (!strcmp(cases[i].mode, "drop") && sent == 2));
        CHECK_RC(cp0_map_client_view(c, NULL, NULL, 0), CP0_MAP_OK);
    }
    CHECK(log_has("refused by the source policy") && !log_has("AnswerKey0123456789"));
    cp0_map_client_free(c);
    cp0_map_store_close(s);
    printf("map fetch: redirects checked (OSMF, plain http refused; same host followed; loops stopped), size and type "
           "checks, dropped connections\n");
}

static void test_user_agent_and_second_app(void)
{
    char dir[600];
    tpath(dir, sizeof dir, "second");
    rec(dir, "t-normal", "normal", NULL);
    const int base = srv_count();
    cp0_map_tile_event_t ev[16];
    /* no User-Agent, or a library's default: nothing is fetched */
    const char *bad[] = {NULL, "curl/8.14.1"};
    for (int i = 0; i < 2; i++) {
        cp0_map_store_t *s = ostore_cfg(dir, 1, bad[i], 0);
        CHECK(state_of(s, "t-normal", NULL) == CP0_MAP_E_NO_USER_AGENT);
        cp0_map_client_t *c;
        CHECK_RC(cp0_map_client_new(s, &c), CP0_MAP_OK);
        CHECK_RC(cp0_map_client_view(c, "t-normal", k_tiles, 1), CP0_MAP_OK);
        CHECK(wait_events(c, ev, 16, 1, 2000) == 1 && ev[0].kind == CP0_MAP_TILE_REFUSED &&
              ev[0].result == CP0_MAP_E_NO_USER_AGENT);
        cp0_map_client_free(c);
        cp0_map_store_close(s);
    }
    usleep(200000);
    CHECK(srv_count() == base);
    /* two apps: the first writes and fetches, the second reads (its source is read-only) */
    cp0_map_store_t *s1 = ostore(dir);
    cp0_map_store_t *s2 = ostore(dir);
    cp0_map_store_t *s3 = ostore_cfg(dir, 1, UA, 1);
    CHECK(state_of(s1, "t-normal", NULL) == CP0_MAP_OK);
    CHECK(state_of(s2, "t-normal", NULL) == CP0_MAP_E_READ_ONLY);
    CHECK(state_of(s3, "t-normal", NULL) == CP0_MAP_E_READ_ONLY);
    cp0_map_client_t *c1, *c2;
    CHECK_RC(cp0_map_client_new(s1, &c1), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_new(s2, &c2), CP0_MAP_OK);
    CHECK_RC(cp0_map_client_view(c2, "t-normal", k_tiles, 2), CP0_MAP_OK);
    CHECK(wait_events(c2, ev, 16, 2, 2000) == 2 && count_kind(ev, 2, CP0_MAP_TILE_REFUSED, CP0_MAP_E_READ_ONLY) == 2);
    CHECK_RC(cp0_map_client_view(c1, "t-normal", k_tiles, 2), CP0_MAP_OK);
    CHECK(wait_events(c1, ev, 16, 2, 5000) == 2);
    CHECK_RC(cp0_map_store_flush(s1), CP0_MAP_OK);
    cp0_map_buf_t b = {0};
    CHECK_RC(cp0_map_store_get_tile(s2, "t-normal", 5, 1, 1, &b, NULL), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_get_tile(s3, "t-normal", 5, 2, 1, &b, NULL), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_clear_cache(s2, "t-normal"), CP0_MAP_E_READ_ONLY);
    CHECK(srv_count() - base == 2);
    cp0_map_store_stats_t st;
    CHECK_RC(cp0_map_store_cache_stats(s2, &st), CP0_MAP_OK);
    for (int i = 0; i < st.nsources; i++)
        if (!strcmp(st.sources[i].id, "t-normal")) CHECK(!st.sources[i].writer && st.sources[i].tiles == 2);
    /* the writer clears its cache: the readers see it gone */
    CHECK_RC(cp0_map_store_clear_cache(s1, "t-normal"), CP0_MAP_OK);
    CHECK_RC(cp0_map_store_get_tile(s2, "t-normal", 5, 1, 1, &b, NULL), CP0_MAP_E_NO_TILE);
    CHECK_RC(cp0_map_store_get_tile(s1, "t-normal", 5, 1, 1, &b, NULL), CP0_MAP_E_NO_TILE);
    cp0_map_buf_free(&b);
    cp0_map_client_free(c1);
    cp0_map_client_free(c2);
    cp0_map_store_close(s3);
    cp0_map_store_close(s2);
    cp0_map_store_close(s1);
    printf("map fetch: no User-Agent or a library default: nothing fetched; a second app reads, never fetches\n");
}

int main(void)
{
    setenv("TZ", "UTC", 1);
    tzset();
    unsetenv("http_proxy");
    unsetenv("HTTP_PROXY");
    unsetenv("all_proxy");
    unsetenv("ALL_PROXY");
    tmp_init("cp0map-fetch");
    server_start();
    test_curl_missing();
    test_basic();
    test_parallel();
    test_cancel_dedupe();
    test_caching();
    test_backoff();
    test_missing();
    test_blocked();
    test_daily_cap();
    test_answers();
    test_user_agent_and_second_app();
    server_stop();
    tmp_done();
    printf("test_map_fetch: %ld checks passed\n", g_checks);
    return 0;
}
