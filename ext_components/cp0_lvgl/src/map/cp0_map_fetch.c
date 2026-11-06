/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: the fetch pool of the online sources (cp0_map_fetch.h, cp0_map_store.h NETWORK and THE THREE GUARDS).
 *
 * CP0_MAP_MAX_PARALLEL worker threads (started when the network is switched on and libcurl loads) take the tiles the
 * clients want, nearest the view's centre first, one request per tile across all clients, within each provider's
 * max_parallel, min_request_interval_ms, daily cap and pauses. Each worker owns one libcurl handle (connection reuse).
 * A worker checks the viewing cache before it asks the server (fresh: answered at once; a 404 remembered: answered at
 * once; stale: a conditional request), stores what comes back (cp0_map_vcache.c), and tells the clients.
 *
 * Providers ("groups"): a shipped record and the user records on its host share one daily count, one set of pauses
 * and one state file (state/<group>.state), written by the app that holds the group's cache lock.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "cp0_map_online.h"
#include "cp0_map_store_priv.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define WORKERS CP0_MAP_MAX_PARALLEL
#define FAIL_MAX 256
#define WINDOW 32
#define STATE_WRITE_MS 5000
#define STREAK_PAUSE 5          /* consecutive errors before a provider pauses */
#define PAUSE_FIRST_S 30
#define PAUSE_MAX_S 3600
#define RETRY_FIRST_MS 5000
#define RETRY_MAX_MS 3600000
#define RETRY_AFTER_DEFAULT_S 60

typedef struct grp {
    struct grp *next;
    char id[CP0_MAP_ID_MAX];
    char state_path[CP0_MAP_PATH_MAX];
    int loaded;
    int day;
    uint32_t count;
    int64_t blocked_until, pause_until;
    int pause_code;
    int err_streak, err_level;
    int inflight;
    int64_t next_ms;
    int dirty, urgent, cap_logged;
    int64_t written_ms;
    struct {
        uint64_t h;
        int z;
        uint32_t x, y;
        int64_t t;
    } win[WINDOW];
    int nwin, wnext;
} grp_t;

typedef struct src {
    struct src *next;
    char id[CP0_MAP_ID_MAX], group[CP0_MAP_ID_MAX];
    char title[128], url[CP0_MAP_URL_MAX];
    cp0_map_format_t format;
    int tile_px, minz, maxz;
    cp0_map_source_policy_t pol;
    int enabled, valid, present;
    uint64_t bh[CP0_MAP_BLOCKED_LIST_MAX];
    int nbh;
    uint32_t bs[CP0_MAP_BLOCKED_LIST_MAX];
    int nbs;
    char key[CP0_MAP_KEY_MAX];
    int has_key;
    grp_t *g;
    struct src *leader;
    cp0_map_vcache_t *vc;
    int last_state;
    uint64_t fetched, not_modified, missing, failed, refused;
} src_t;

typedef struct {
    int z;
    uint32_t x, y;
    int refused_sent;
} want_t;

struct cp0_map_client {
    cp0_map_fetcher_t *f;
    struct cp0_map_client *next;
    src_t *src;
    want_t w[CP0_MAP_CLIENT_TILES_MAX];
    int nw;
    cp0_map_tile_event_t ev[CP0_MAP_CLIENT_EVENTS_MAX];
    int ne;
};

typedef struct {
    int used;
    src_t *src;
    int z;
    uint32_t x, y;
    int cancel;                  /* read by libcurl's progress callback (atomics) */
} job_t;

typedef struct {
    src_t *src;
    int z;
    uint32_t x, y;
    int64_t next_ms;
    int fails;
} fail_t;

struct cp0_map_fetcher {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    char dir[CP0_MAP_PATH_MAX];
    int read_only, network, ua_ok, curl_rc;
    char ua[256];
    cp0_map_logger_t log;
    cp0_map_vcset_t *vcs;
    src_t *srcs;
    grp_t *grps;
    cp0_map_client_t *clients;
    job_t jobs[WORKERS];
    fail_t fails[FAIL_MAX];
    int nfails;
    pthread_t threads[WORKERS];
    int nthreads, stop;
};

/* ---------------------------------------------------------------------------------------- groups, state file */

static void state_read(cp0_map_fetcher_t *f, grp_t *g)
{
    char buf[512];
    const int fd = open(g->state_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    const ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    int v1 = 0, day = 0, code = 0, level = 0;
    long long count = 0, blocked = 0, pause = 0;
    for (char *save = NULL, *l = strtok_r(buf, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        if (!strcmp(l, "v=1")) v1 = 1;
        else if (!strncmp(l, "day=", 4)) day = atoi(l + 4);
        else if (!strncmp(l, "count=", 6)) count = atoll(l + 6);
        else if (!strncmp(l, "blocked_until=", 14)) blocked = atoll(l + 14);
        else if (!strncmp(l, "pause_until=", 12)) pause = atoll(l + 12);
        else if (!strncmp(l, "pause_code=", 11)) code = atoi(l + 11);
        else if (!strncmp(l, "err_level=", 10)) level = atoi(l + 10);
    }
    if (!v1) return;
    (void)f;
    g->day = day;
    g->count = count > 0 && count < 100000000 ? (uint32_t)count : 0;
    g->blocked_until = blocked;
    g->pause_until = pause;
    g->pause_code = (code == CP0_MAP_E_BACKOFF || code == CP0_MAP_E_NETWORK) ? code : CP0_MAP_E_BACKOFF;
    g->err_level = level >= 0 && level < 16 ? level : 0;
}

typedef struct {
    char path[CP0_MAP_PATH_MAX];
    int day;
    uint32_t count;
    int64_t blocked_until, pause_until;
    int pause_code, err_level;
} state_snap_t;

static void state_write(const char *dir, const state_snap_t *s)
{
    char sub[CP0_MAP_PATH_MAX], text[256];
    if (cp0_map__fmt(sub, sizeof sub, "%s/state", dir) || cp0_map__mkdirs(sub, 0755)) return;
    const int n = snprintf(text, sizeof text,
                           "v=1\nday=%d\ncount=%u\nblocked_until=%lld\npause_until=%lld\npause_code=%d\nerr_level=%d\n",
                           s->day, s->count, (long long)s->blocked_until, (long long)s->pause_until, s->pause_code,
                           s->err_level);
    if (n > 0) cp0_map__write_atomic(s->path, text, (size_t)n);
}

static grp_t *grp_get(cp0_map_fetcher_t *f, const char *id)
{
    for (grp_t *g = f->grps; g; g = g->next)
        if (!strcmp(g->id, id)) return g;
    grp_t *g = (grp_t *)calloc(1, sizeof *g);
    if (!g) return NULL;
    cp0_map__copy_str(g->id, sizeof g->id, id, 0);
    if (cp0_map__fmt(g->state_path, sizeof g->state_path, "%s/state/%s.state", f->dir, id)) g->state_path[0] = '\0';
    g->pause_code = CP0_MAP_E_BACKOFF;
    g->next = f->grps;
    f->grps = g;
    return g;
}

/* the day turns at local midnight */
static void roll_day(grp_t *g, int64_t now)
{
    const int today = cp0_map__local_day(now);
    if (g->day != today) {
        g->day = today;
        g->count = 0;
        g->cap_logged = 0;
        g->dirty = 1;
    }
}

/* ------------------------------------------------------------------------------------------- source state */

static const char *key_of(const src_t *s)
{
    if (s->has_key) return s->key;
    if (s->leader && s->leader != s && s->leader->has_key) return s->leader->key;
    return NULL;
}

static int owner(const cp0_map_fetcher_t *f, const src_t *s)
{
    return !f->read_only && cp0_map__vcache_writer(s->vc) && (!s->leader || cp0_map__vcache_writer(s->leader->vc));
}

/* why s does not fetch now (f->mu held); CP0_MAP_OK when it does */
static int src_state(cp0_map_fetcher_t *f, src_t *s, int64_t now, int64_t *until)
{
    if (until) *until = 0;
    if (!s->present || s->valid != CP0_MAP_OK) return CP0_MAP_E_REFUSED;
    if (!s->enabled) return CP0_MAP_E_DISABLED;
    if (s->pol.needs_key && !key_of(s)) return CP0_MAP_E_NO_KEY;
    if (!f->network) return CP0_MAP_E_OFFLINE;
    if (f->curl_rc) return f->curl_rc;
    if (!f->ua_ok) return CP0_MAP_E_NO_USER_AGENT;
    if (!owner(f, s)) return CP0_MAP_E_READ_ONLY;
    grp_t *g = s->g;
    if (!g) return CP0_MAP_E_REFUSED;
    roll_day(g, now);
    if (now < g->blocked_until) {
        if (until) *until = g->blocked_until;
        return CP0_MAP_E_BLOCKED;
    }
    if (g->count >= s->pol.daily_cap) {
        if (until) *until = cp0_map__next_local_midnight(now);
        return CP0_MAP_E_CAP_REACHED;
    }
    if (now < g->pause_until) {
        if (until) *until = g->pause_until;
        return g->pause_code;
    }
    if (s->pol.viewing_cache && cp0_map__vcset_full(f->vcs)) return CP0_MAP_E_CACHE_FULL;
    return CP0_MAP_OK;
}

/* ------------------------------------------------------------------------------------------------ events */

static void push_event(cp0_map_client_t *c, const src_t *s, int z, uint32_t x, uint32_t y, cp0_map_tile_event_kind_t kind,
                       int result)
{
    if (c->ne == CP0_MAP_CLIENT_EVENTS_MAX) {
        memmove(c->ev, c->ev + 1, (CP0_MAP_CLIENT_EVENTS_MAX - 1) * sizeof c->ev[0]);
        c->ne--;
    }
    cp0_map_tile_event_t *e = &c->ev[c->ne++];
    memset(e, 0, sizeof *e);
    e->z = z;
    e->x = x;
    e->y = y;
    e->kind = kind;
    e->result = result;
    cp0_map__copy_str(e->archive_id, sizeof e->archive_id, s->id, 0);
}

static int want_index(const cp0_map_client_t *c, int z, uint32_t x, uint32_t y)
{
    for (int i = 0; i < c->nw; i++)
        if (c->w[i].z == z && c->w[i].x == x && c->w[i].y == y) return i;
    return -1;
}

static void want_remove(cp0_map_client_t *c, int i)
{
    memmove(&c->w[i], &c->w[i + 1], (size_t)(c->nw - i - 1) * sizeof c->w[0]);
    c->nw--;
}

/* a final answer for (s, z, x, y): every client that wants it hears it and stops wanting it */
static void deliver(cp0_map_fetcher_t *f, const src_t *s, int z, uint32_t x, uint32_t y, cp0_map_tile_event_kind_t kind,
                    int result)
{
    for (cp0_map_client_t *c = f->clients; c; c = c->next) {
        if (c->src != s) continue;
        const int i = want_index(c, z, x, y);
        if (i < 0) continue;
        push_event(c, s, z, x, y, kind, result);
        want_remove(c, i);
    }
}

static int wanted(const cp0_map_fetcher_t *f, const src_t *s, int z, uint32_t x, uint32_t y)
{
    for (const cp0_map_client_t *c = f->clients; c; c = c->next)
        if (c->src == s && want_index(c, z, x, y) >= 0) return 1;
    return 0;
}

static int in_flight(const cp0_map_fetcher_t *f, const src_t *s, int z, uint32_t x, uint32_t y)
{
    for (int j = 0; j < WORKERS; j++) {
        const job_t *jb = &f->jobs[j];
        if (jb->used && jb->src == s && jb->z == z && jb->x == x && jb->y == y) return 1;
    }
    return 0;
}

/* sources that stop fetching tell their clients once (REFUSED; a tile already on the wire gets its own answer),
 * those that start again are fetched */
static void update_states(cp0_map_fetcher_t *f)
{
    const int64_t now = cp0_map__now();
    for (src_t *s = f->srcs; s; s = s->next) {
        int has_clients = 0;
        for (cp0_map_client_t *c = f->clients; c && !has_clients; c = c->next) has_clients = c->src == s;
        if (!has_clients) continue;
        const int st = src_state(f, s, now, NULL);
        if (st == s->last_state) continue;
        s->last_state = st;
        for (cp0_map_client_t *c = f->clients; c; c = c->next) {
            if (c->src != s) continue;
            for (int i = 0; i < c->nw; i++) {
                if (st == CP0_MAP_OK) {
                    c->w[i].refused_sent = 0;
                } else if (!c->w[i].refused_sent && !in_flight(f, s, c->w[i].z, c->w[i].x, c->w[i].y)) {
                    push_event(c, s, c->w[i].z, c->w[i].x, c->w[i].y, CP0_MAP_TILE_REFUSED, st);
                    c->w[i].refused_sent = 1;
                    s->refused++;
                }
            }
        }
        if (st == CP0_MAP_OK) pthread_cond_broadcast(&f->cv);
    }
}

static void cancel_unwanted(cp0_map_fetcher_t *f)
{
    for (int j = 0; j < WORKERS; j++) {
        job_t *jb = &f->jobs[j];
        if (jb->used && !wanted(f, jb->src, jb->z, jb->x, jb->y)) __atomic_store_n(&jb->cancel, 1, __ATOMIC_RELAXED);
    }
}

/* ---------------------------------------------------------------------------------------------- failing tiles */

static fail_t *fail_find(cp0_map_fetcher_t *f, const src_t *s, int z, uint32_t x, uint32_t y)
{
    for (int i = 0; i < f->nfails; i++)
        if (f->fails[i].src == s && f->fails[i].z == z && f->fails[i].x == x && f->fails[i].y == y) return &f->fails[i];
    return NULL;
}

static void fail_note(cp0_map_fetcher_t *f, src_t *s, int z, uint32_t x, uint32_t y)
{
    fail_t *e = fail_find(f, s, z, x, y);
    if (!e) {
        if (f->nfails < FAIL_MAX) e = &f->fails[f->nfails++];
        else {
            /* the one retried soonest makes room */
            e = &f->fails[0];
            for (int i = 1; i < FAIL_MAX; i++)
                if (f->fails[i].next_ms < e->next_ms) e = &f->fails[i];
        }
        memset(e, 0, sizeof *e);
        e->src = s;
        e->z = z;
        e->x = x;
        e->y = y;
    }
    e->fails++;
    int64_t wait = RETRY_FIRST_MS;
    for (int i = 1; i < e->fails && wait < RETRY_MAX_MS; i++) wait *= 2;
    if (wait > RETRY_MAX_MS) wait = RETRY_MAX_MS;
    e->next_ms = cp0_map_monotonic_ms() + wait;
}

static void fail_clear(cp0_map_fetcher_t *f, const src_t *s, int z, uint32_t x, uint32_t y)
{
    fail_t *e = fail_find(f, s, z, x, y);
    if (e) *e = f->fails[--f->nfails];
}

/* ----------------------------------------------------------------------------------------------- the worker */

typedef struct {
    src_t *src;
    grp_t *g;
    int z;
    uint32_t x, y;
    int job;
    char url[CP0_MAP_URL_MAX], key[CP0_MAP_KEY_MAX], ua[256];
    int viewing_cache;
    cp0_map_vcache_t *vc;
    uint64_t bh[CP0_MAP_BLOCKED_LIST_MAX];
    int nbh;
    uint32_t bs[CP0_MAP_BLOCKED_LIST_MAX];
    int nbs;
} task_t;

enum { OUT_FRESH = 1, OUT_STORED, OUT_REFRESHED, OUT_MISSING, OUT_FAILED, OUT_BACKOFF, OUT_BLOCKED, OUT_CANCELLED,
       OUT_REFUSED };

/* picks the next tile (f->mu held). 1 with t filled, 0 when nothing can start now (*wake_ms: when to look again) */
static int pick(cp0_map_fetcher_t *f, task_t *t, int64_t *wake_ms)
{
    const int64_t now = cp0_map__now(), nowms = cp0_map_monotonic_ms();
    int slot = -1;
    for (int j = 0; j < WORKERS && slot < 0; j++)
        if (!f->jobs[j].used) slot = j;
    if (slot < 0) return 0;
    for (cp0_map_client_t *c = f->clients; c; c = c->next) {
        src_t *s = c->src;
        if (!s || !c->nw || src_state(f, s, now, NULL) != CP0_MAP_OK) continue;
        grp_t *g = s->g;
        if (g->inflight >= s->pol.max_parallel) continue;
        if (nowms < g->next_ms) {
            if (g->next_ms < *wake_ms) *wake_ms = g->next_ms;
            continue;
        }
        for (int i = 0; i < c->nw; i++) {
            const want_t *w = &c->w[i];
            int busy = 0;
            for (int j = 0; j < WORKERS && !busy; j++)
                busy = f->jobs[j].used && f->jobs[j].src == s && f->jobs[j].z == w->z && f->jobs[j].x == w->x &&
                       f->jobs[j].y == w->y;
            if (busy) continue;
            const fail_t *fe = fail_find(f, s, w->z, w->x, w->y);
            if (fe && fe->next_ms > nowms) {
                if (fe->next_ms < *wake_ms) *wake_ms = fe->next_ms;
                continue;
            }
            job_t *jb = &f->jobs[slot];
            jb->used = 1;
            jb->src = s;
            jb->z = w->z;
            jb->x = w->x;
            jb->y = w->y;
            __atomic_store_n(&jb->cancel, 0, __ATOMIC_RELAXED);
            g->inflight++;
            g->next_ms = nowms + s->pol.min_request_interval_ms;
            memset(t, 0, sizeof *t);
            t->src = s;
            t->g = g;
            t->z = w->z;
            t->x = w->x;
            t->y = w->y;
            t->job = slot;
            cp0_map__copy_str(t->url, sizeof t->url, s->url, 0);
            const char *k = key_of(s);
            if (k) cp0_map__copy_str(t->key, sizeof t->key, k, 0);
            cp0_map__copy_str(t->ua, sizeof t->ua, f->ua, 0);
            t->viewing_cache = s->pol.viewing_cache;
            t->vc = s->vc;
            t->nbh = s->nbh;
            memcpy(t->bh, s->bh, sizeof t->bh);
            t->nbs = s->nbs;
            memcpy(t->bs, s->bs, sizeof t->bs);
            return 1;
        }
    }
    return 0;
}

/* one plain colour (open sea): never a "blocked" picture */
static int uniform(const uint8_t *d, size_t n)
{
    cp0_map_image_t *img = NULL;
    if (cp0_map_decode_tile(d, n, CP0_MAP_PIX_ARGB8888, 2, &img) != CP0_MAP_OK || !img) return 0;
    int same = 1;
    const uint32_t first = *(const uint32_t *)img->px;
    for (int y = 0; y < img->h && same; y++) {
        const uint32_t *row = (const uint32_t *)(img->px + (size_t)y * (size_t)img->stride);
        for (int x = 0; x < img->w && same; x++) same = row[x] == first;
    }
    cp0_map_image_free(img);
    return same;
}

static void fmt_time(int64_t t, char *buf, size_t size)
{
    const time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm) || !strftime(buf, size, "%Y-%m-%d %H:%M", &tm)) snprintf(buf, size, "%lld", (long long)t);
}

/* the network part of a task (no mutex held). Returns an OUT_* code; *result the CP0_MAP code for events */
static int run_task(cp0_map_fetcher_t *f, cp0_map_http_t *http, task_t *t, cp0_map_http_resp_t *resp, int *result,
                    int64_t *retry_s, uint64_t *blocked_hash)
{
    *result = CP0_MAP_OK;
    cp0_map_vstate_t st;
    cp0_map__vcache_lookup(t->vc, t->z, t->x, t->y, &st);
    if (st.missing) {
        *result = CP0_MAP_E_NO_TILE;
        return OUT_MISSING;
    }
    if (st.have && !st.stale) return OUT_FRESH;

    /* a request goes out: it counts (any answer costs the provider) */
    pthread_mutex_lock(&f->mu);
    const int64_t now = cp0_map__now();
    const int state = src_state(f, t->src, now, NULL);
    if (state == CP0_MAP_OK) {
        t->g->count++;
        t->g->dirty = 1;
    }
    pthread_mutex_unlock(&f->mu);
    if (state != CP0_MAP_OK) {
        *result = state;
        return OUT_REFUSED;
    }
    char url[2 * CP0_MAP_URL_MAX], shown[2 * CP0_MAP_URL_MAX];
    if (cp0_map__url_expand(t->url, t->z, t->x, t->y, t->key[0] ? t->key : NULL, url, sizeof url) ||
        cp0_map__url_expand(t->url, t->z, t->x, t->y, NULL, shown, sizeof shown)) {
        *result = CP0_MAP_E_INVALID;
        return OUT_FAILED;
    }
    cp0_map_http_req_t req;
    memset(&req, 0, sizeof req);
    req.url = url;
    req.user_agent = t->ua;
    req.cancel = &f->jobs[t->job].cancel;
    if (st.have) {
        req.etag = st.etag;
        req.last_modified = st.last_modified;
    }
    const int rc = cp0_map__http_get(http, &req, resp);
    memset(url, 0, sizeof url);
    if (rc == CP0_MAP_E_CANCELLED) return OUT_CANCELLED;
    if (rc != CP0_MAP_OK) {
        *result = rc;
        if (rc == CP0_MAP_E_REFUSED || rc == CP0_MAP_E_HTTP || rc == CP0_MAP_E_TOO_LARGE)
            cp0_map__log(&f->log, "map source %s: %s (%s, HTTP %d)", t->src->id, cp0_map_strerror(rc), shown, resp->status);
        return OUT_FAILED;
    }
    const int code = resp->status;
    if (code == 200) {
        const uint8_t *d = resp->body.data;
        const size_t n = resp->body.len;
        const uint64_t h = cp0_map__hash64(d, n);
        int known = 0, size_hint = 0;
        for (int i = 0; i < t->nbh; i++) known |= t->bh[i] == h;
        for (int i = 0; i < t->nbs; i++) size_hint |= t->bs[i] == (uint32_t)n;
        int repeats = 0;
        pthread_mutex_lock(&f->mu);
        grp_t *g = t->g;
        const int64_t wnow = cp0_map__now();
        for (int i = 0; i < g->nwin; i++) {
            if (g->win[i].h != h || wnow - g->win[i].t > CP0_MAP_BLOCKED_WINDOW_S) continue;
            if (g->win[i].z == t->z && g->win[i].x == t->x && g->win[i].y == t->y) continue;
            repeats++;
        }
        const int k = g->nwin < WINDOW ? g->nwin++ : g->wnext;
        g->wnext = (k + 1) % WINDOW;
        g->win[k].h = h;
        g->win[k].z = t->z;
        g->win[k].x = t->x;
        g->win[k].y = t->y;
        g->win[k].t = wnow;
        pthread_mutex_unlock(&f->mu);
        const int threshold = size_hint ? 2 : CP0_MAP_BLOCKED_REPEAT;
        if ((known || repeats + 1 >= threshold) && !uniform(d, n)) {
            *blocked_hash = h;
            *result = CP0_MAP_E_BLOCKED;
            cp0_map__log(&f->log,
                         "map source %s: the server sent the same picture (%zu bytes, hash %016llx) for %d different "
                         "tiles (%s): treated as a 'blocked' answer, not stored",
                         t->src->id, n, (unsigned long long)h, repeats + 1, shown);
            return OUT_BLOCKED;
        }
        const int64_t exp = cp0_map__now() + cp0_map__http_freshness(resp);
        cp0_map_vput_t p = {CP0_MAP_VPUT_TILE, t->z, t->x, t->y, d, n, h, exp, resp->etag, resp->last_modified};
        int prc = CP0_MAP_E_READ_ONLY;
        if (t->viewing_cache && !resp->no_store) prc = cp0_map__vcache_put(t->vc, &p);
        if (prc != CP0_MAP_OK) {
            /* not kept on disk (the source's terms, no-store, the cache full): shown from RAM */
            p.kind = CP0_MAP_VPUT_RAM;
            if (cp0_map__vcache_put(t->vc, &p) != CP0_MAP_OK) {
                *result = CP0_MAP_E_NOMEM;
                return OUT_FAILED;
            }
        }
        return OUT_STORED;
    }
    if (code == 304) {
        if (!st.have) {
            *result = CP0_MAP_E_HTTP;
            return OUT_FAILED;
        }
        cp0_map_vput_t p = {CP0_MAP_VPUT_REFRESH, t->z, t->x, t->y, NULL, 0, 0,
                            cp0_map__now() + cp0_map__http_freshness(resp), resp->etag, resp->last_modified};
        cp0_map__vcache_put(t->vc, &p);
        return OUT_REFRESHED;
    }
    if (code == 404 || code == 410) {
        cp0_map_vput_t p = {CP0_MAP_VPUT_MISSING, t->z, t->x, t->y, NULL, 0, 0, cp0_map__now() + CP0_MAP_MISSING_TTL_S,
                            NULL, NULL};
        cp0_map__vcache_put(t->vc, &p);
        *result = CP0_MAP_E_NO_TILE;
        return OUT_MISSING;
    }
    if (code == 429 || code == 503) {
        int64_t ra = cp0_map__http_retry_after(resp, cp0_map__now());
        if (ra < 0) ra = RETRY_AFTER_DEFAULT_S;
        if (ra < 1) ra = 1;
        if (ra > PAUSE_MAX_S) ra = PAUSE_MAX_S;
        *retry_s = ra;
        *result = CP0_MAP_E_BACKOFF;
        cp0_map__log(&f->log, "map source %s: HTTP %d, the server asks to wait %lld s (%s)", t->src->id, code,
                     (long long)ra, shown);
        return OUT_BACKOFF;
    }
    *result = code >= 500 ? CP0_MAP_E_NETWORK : CP0_MAP_E_HTTP;
    if (code == 401 || code == 403)
        cp0_map__log(&f->log, "map source %s: HTTP %d (a wrong key, or access refused) for %s", t->src->id, code, shown);
    return OUT_FAILED;
}

/* applies the outcome (f->mu held) */
static void finish(cp0_map_fetcher_t *f, task_t *t, int out, int result, int64_t retry_s, uint64_t blocked_hash,
                   cp0_map_vcache_t **purge, int *npurge)
{
    src_t *s = t->src;
    grp_t *g = t->g;
    job_t *jb = &f->jobs[t->job];
    const int was_cancelled = __atomic_load_n(&jb->cancel, __ATOMIC_RELAXED);
    jb->used = 0;
    g->inflight--;
    const int64_t now = cp0_map__now();
    char when[32];
    switch (out) {
    case OUT_FRESH:
    case OUT_STORED:
    case OUT_REFRESHED:
        g->err_streak = 0;
        g->err_level = 0;
        fail_clear(f, s, t->z, t->x, t->y);
        if (out == OUT_STORED) s->fetched++;
        if (out == OUT_REFRESHED) s->not_modified++;
        deliver(f, s, t->z, t->x, t->y, CP0_MAP_TILE_FETCHED, CP0_MAP_OK);
        break;
    case OUT_MISSING:
        g->err_streak = 0;
        s->missing++;
        fail_clear(f, s, t->z, t->x, t->y);
        deliver(f, s, t->z, t->x, t->y, CP0_MAP_TILE_FETCH_FAILED, CP0_MAP_E_NO_TILE);
        break;
    case OUT_BACKOFF:
        g->pause_until = now + retry_s;
        g->pause_code = CP0_MAP_E_BACKOFF;
        g->dirty = g->urgent = 1;
        break;
    case OUT_BLOCKED:
        g->blocked_until = now + CP0_MAP_BLOCKED_PAUSE_S;
        g->dirty = g->urgent = 1;
        fmt_time(g->blocked_until, when, sizeof when);
        cp0_map__log(&f->log, "map source %s: paused until %s (blocked picture)", g->id, when);
        for (src_t *o = f->srcs; o && *npurge < 32; o = o->next)
            if (o->g == g && o->vc) purge[(*npurge)++] = o->vc;
        (void)blocked_hash;
        break;
    case OUT_REFUSED:
        /* the source stopped while this tile was on its way: it waits like the others, told once */
        for (cp0_map_client_t *c = f->clients; c; c = c->next) {
            if (c->src != s) continue;
            const int i = want_index(c, t->z, t->x, t->y);
            if (i < 0 || c->w[i].refused_sent) continue;
            push_event(c, s, t->z, t->x, t->y, CP0_MAP_TILE_REFUSED, result);
            c->w[i].refused_sent = 1;
            s->refused++;
        }
        break;
    case OUT_CANCELLED:
        break;
    default: /* OUT_FAILED */
        s->failed++;
        if (!was_cancelled) {
            fail_note(f, s, t->z, t->x, t->y);
            if (++g->err_streak >= STREAK_PAUSE) {
                int64_t pause = PAUSE_FIRST_S;
                for (int i = 0; i < g->err_level && pause < PAUSE_MAX_S; i++) pause *= 2;
                if (pause > PAUSE_MAX_S) pause = PAUSE_MAX_S;
                if (g->err_level < 16) g->err_level++;
                g->err_streak = 0;
                g->pause_until = now + pause;
                g->pause_code = CP0_MAP_E_NETWORK;
                g->dirty = g->urgent = 1;
                cp0_map__log(&f->log, "map source %s: %d errors in a row (last: %s): paused %lld s", g->id,
                             STREAK_PAUSE, cp0_map_strerror(result), (long long)pause);
            }
            deliver(f, s, t->z, t->x, t->y, CP0_MAP_TILE_FETCH_FAILED, result);
        }
        break;
    }
    if (g->count >= s->pol.daily_cap && !g->cap_logged) {
        g->cap_logged = 1;
        g->urgent = 1;
        cp0_map__log(&f->log, "map source %s: daily cap of %u requests reached: paused until local midnight", g->id,
                     s->pol.daily_cap);
    }
    update_states(f);
    pthread_cond_broadcast(&f->cv);
}

static void maybe_write_states(cp0_map_fetcher_t *f, int force)
{
    /* f->mu held; released around the writes */
    const int64_t nowms = cp0_map_monotonic_ms();
    state_snap_t snaps[8];
    int n = 0;
    for (grp_t *g = f->grps; g && n < 8; g = g->next) {
        if (!g->dirty || !g->state_path[0]) continue;
        if (!force && !g->urgent && nowms - g->written_ms < STATE_WRITE_MS) continue;
        /* only the app that owns the provider's cache writes its state */
        int mine = 0;
        for (src_t *s = f->srcs; s && !mine; s = s->next) mine = s->g == g && owner(f, s);
        if (!mine) continue;
        state_snap_t *sn = &snaps[n++];
        cp0_map__copy_str(sn->path, sizeof sn->path, g->state_path, 0);
        sn->day = g->day;
        sn->count = g->count;
        sn->blocked_until = g->blocked_until;
        sn->pause_until = g->pause_until;
        sn->pause_code = g->pause_code;
        sn->err_level = g->err_level;
        g->dirty = g->urgent = 0;
        g->written_ms = nowms;
    }
    if (!n) return;
    pthread_mutex_unlock(&f->mu);
    for (int i = 0; i < n; i++) state_write(f->dir, &snaps[i]);
    pthread_mutex_lock(&f->mu);
}

static void *worker(void *arg)
{
    cp0_map_fetcher_t *f = (cp0_map_fetcher_t *)arg;
    cp0_map_http_t *http = cp0_map__http_new();
    cp0_map_http_resp_t resp;
    memset(&resp, 0, sizeof resp);
    pthread_mutex_lock(&f->mu);
    while (!f->stop) {
        update_states(f);
        maybe_write_states(f, 0);
        if (f->stop) break;
        task_t t;
        int64_t wake = cp0_map_monotonic_ms() + 1000;
        if (!http || !f->network || !pick(f, &t, &wake)) {
            int64_t wait_ms = wake - cp0_map_monotonic_ms();
            if (wait_ms < 5) wait_ms = 5;
            if (wait_ms > 1000) wait_ms = 1000;
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += wait_ms / 1000;
            ts.tv_nsec += (wait_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&f->cv, &f->mu, &ts);
            continue;
        }
        pthread_mutex_unlock(&f->mu);
        int result = CP0_MAP_OK;
        int64_t retry_s = 0;
        uint64_t bh = 0;
        const int out = run_task(f, http, &t, &resp, &result, &retry_s, &bh);
        memset(t.key, 0, sizeof t.key);
        pthread_mutex_lock(&f->mu);
        cp0_map_vcache_t *purge[32];
        int npurge = 0;
        finish(f, &t, out, result, retry_s, bh, purge, &npurge);
        if (npurge) {
            pthread_mutex_unlock(&f->mu);
            for (int i = 0; i < npurge; i++) cp0_map__vcache_purge_hash(purge[i], bh);
            pthread_mutex_lock(&f->mu);
        }
    }
    pthread_mutex_unlock(&f->mu);
    cp0_map_buf_free(&resp.body);
    cp0_map__http_free(http);
    return NULL;
}

/* starts the workers (f->mu held) */
static void start_workers(cp0_map_fetcher_t *f)
{
    if (f->nthreads || f->read_only || f->stop) return;
    f->curl_rc = cp0_map__http_load();
    if (f->curl_rc) return;
    for (int i = 0; i < WORKERS; i++)
        if (pthread_create(&f->threads[f->nthreads], NULL, worker, f) == 0) f->nthreads++;
}

/* ------------------------------------------------------------------------------------------- the pool's life */

cp0_map_fetcher_t *cp0_map__fetcher_new(const char *dir, const cp0_map_store_cfg_t *cfg)
{
    cp0_map_fetcher_t *f = (cp0_map_fetcher_t *)calloc(1, sizeof *f);
    if (!f) return NULL;
    cp0_map__copy_str(f->dir, sizeof f->dir, dir, 0);
    f->read_only = cfg->read_only != 0;
    f->network = cfg->network != 0;
    if (cfg->user_agent) cp0_map__copy_str(f->ua, sizeof f->ua, cfg->user_agent, 0);
    f->ua_ok = cp0_map_user_agent_ok(f->ua);
    cp0_map__log_init(&f->log, cfg->log, cfg->log_user);
    pthread_mutex_init(&f->mu, NULL);
    pthread_cond_init(&f->cv, NULL);
    f->vcs = cp0_map__vcset_new(dir, f->read_only, cfg->cache_cap_bytes, &f->log);
    if (!f->vcs) {
        pthread_cond_destroy(&f->cv);
        pthread_mutex_destroy(&f->mu);
        cp0_map__log_done(&f->log);
        free(f);
        return NULL;
    }
    if (f->network) {
        pthread_mutex_lock(&f->mu);
        start_workers(f);
        pthread_mutex_unlock(&f->mu);
    }
    return f;
}

void cp0_map__fetcher_free(cp0_map_fetcher_t *f)
{
    if (!f) return;
    pthread_mutex_lock(&f->mu);
    f->stop = 1;
    for (int j = 0; j < WORKERS; j++)
        if (f->jobs[j].used) __atomic_store_n(&f->jobs[j].cancel, 1, __ATOMIC_RELAXED);
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->mu);
    for (int i = 0; i < f->nthreads; i++) pthread_join(f->threads[i], NULL);
    pthread_mutex_lock(&f->mu);
    maybe_write_states(f, 1);
    pthread_mutex_unlock(&f->mu);
    cp0_map__vcset_free(f->vcs);
    while (f->clients) {
        cp0_map_client_t *c = f->clients;
        f->clients = c->next;
        free(c);
    }
    while (f->srcs) {
        src_t *s = f->srcs;
        f->srcs = s->next;
        memset(s->key, 0, sizeof s->key);
        free(s);
    }
    while (f->grps) {
        grp_t *g = f->grps;
        f->grps = g->next;
        free(g);
    }
    pthread_cond_destroy(&f->cv);
    pthread_mutex_destroy(&f->mu);
    cp0_map__log_done(&f->log);
    free(f);
}

static src_t *src_find(cp0_map_fetcher_t *f, const char *id)
{
    for (src_t *s = f->srcs; s; s = s->next)
        if (!strcmp(s->id, id)) return s;
    return NULL;
}

static void load_key(cp0_map_fetcher_t *f, src_t *s)
{
    char key[CP0_MAP_KEY_MAX];
    s->has_key = cp0_map__key_read(f->dir, s->id, key, sizeof key) == CP0_MAP_OK;
    memset(s->key, 0, sizeof s->key);
    if (s->has_key) {
        cp0_map__copy_str(s->key, sizeof s->key, key, 0);
        cp0_map__log_secret(&f->log, key);
    }
    memset(key, 0, sizeof key);
}

void cp0_map__fetcher_sync(cp0_map_fetcher_t *f, const cp0_map_rec_t *recs, int from, int n)
{
    if (!f) return;
    pthread_mutex_lock(&f->mu);
    for (src_t *s = f->srcs; s; s = s->next) s->present = 0;
    for (int r = from; r < n; r++) {
        const cp0_map_source_info_t *info = &recs[r].info;
        const cp0_map_srcrec_t *src = &recs[r].src;
        src_t *s = src_find(f, info->id);
        if (!s) {
            s = (src_t *)calloc(1, sizeof *s);
            if (!s) continue;
            cp0_map__copy_str(s->id, sizeof s->id, info->id, 0);
            s->last_state = 1; /* no state yet: the first evaluation always counts as a change */
            s->next = f->srcs;
            f->srcs = s;
        }
        s->present = 1;
        cp0_map__copy_str(s->title, sizeof s->title, info->title, 0);
        cp0_map__copy_str(s->url, sizeof s->url, src->url, 0);
        cp0_map__copy_str(s->group, sizeof s->group, src->group[0] ? src->group : info->id, 0);
        s->format = info->format;
        s->tile_px = info->tile_px;
        s->minz = info->minzoom;
        s->maxz = info->maxzoom;
        s->pol = src->policy;
        s->enabled = src->enabled == 1;
        s->valid = src->valid;
        s->nbh = src->nhashes;
        memcpy(s->bh, src->blocked_hashes, sizeof s->bh);
        s->nbs = src->nsizes;
        memcpy(s->bs, src->blocked_sizes, sizeof s->bs);
        if (!s->vc) s->vc = cp0_map__vcset_get(f->vcs, s->id);
        cp0_map__vcache_meta(s->vc, info->title, info->attribution, info->format, info->minzoom, info->maxzoom);
        load_key(f, s);
        grp_t *g = grp_get(f, s->group);
        s->g = g;
        if (g && (!g->loaded || f->read_only || !owner(f, s))) {
            /* a writer reads its state once; a reader follows the writer's file */
            state_read(f, g);
            g->loaded = 1;
        }
    }
    for (src_t *s = f->srcs; s; s = s->next) {
        s->leader = src_find(f, s->group);
        if (s->leader && !s->leader->present) s->leader = NULL;
    }
    update_states(f);
    cancel_unwanted(f);
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->mu);
}

void cp0_map__fetcher_fill(cp0_map_fetcher_t *f, cp0_map_source_info_t *info)
{
    if (!f || !info || info->kind != CP0_MAP_ENTRY_SOURCE) return;
    pthread_mutex_lock(&f->mu);
    src_t *s = src_find(f, info->id);
    if (s && s->present) {
        info->state = src_state(f, s, cp0_map__now(), &info->until);
        info->enabled = s->enabled;
        info->has_key = key_of(s) != NULL;
        info->fetches_today = s->g ? s->g->count : 0;
        info->policy = s->pol;
        cp0_map_vusage_t u;
        cp0_map__vcache_usage(s->vc, 0, &u);
        info->cache_tiles = u.tiles;
    }
    pthread_mutex_unlock(&f->mu);
}

int cp0_map__fetcher_get_tile(cp0_map_fetcher_t *f, const char *id, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out,
                              cp0_map_tile_meta_t *meta)
{
    if (!f) return CP0_MAP_E_NOT_FOUND;
    pthread_mutex_lock(&f->mu);
    src_t *s = src_find(f, id);
    cp0_map_vcache_t *vc = s ? s->vc : NULL;
    pthread_mutex_unlock(&f->mu);
    if (!vc) return CP0_MAP_E_NOT_FOUND;
    int stale = 0;
    int64_t exp = 0;
    const int rc = cp0_map__vcache_get(vc, z, x, y, out, &stale, &exp);
    if (rc == CP0_MAP_OK && meta) {
        cp0_map__copy_str(meta->archive_id, sizeof meta->archive_id, id, 0);
        meta->format = cp0_map_format_sniff(out->data, out->len);
        meta->stale = stale;
        meta->expires = exp;
    }
    return rc;
}

cp0_map_vcset_t *cp0_map__fetcher_vcset(cp0_map_fetcher_t *f)
{
    return f ? f->vcs : NULL;
}

cp0_map_logger_t *cp0_map__fetcher_log(cp0_map_fetcher_t *f)
{
    return f ? &f->log : NULL;
}

int cp0_map__fetcher_flush(cp0_map_fetcher_t *f)
{
    if (!f) return CP0_MAP_E_INVALID;
    pthread_mutex_lock(&f->mu);
    maybe_write_states(f, 1);
    pthread_mutex_unlock(&f->mu);
    return cp0_map__vcset_flush(f->vcs);
}

int cp0_map__fetcher_set_network(cp0_map_fetcher_t *f, int on)
{
    if (!f) return CP0_MAP_E_INVALID;
    pthread_mutex_lock(&f->mu);
    f->network = on != 0;
    if (f->network) start_workers(f);
    else
        for (int j = 0; j < WORKERS; j++)
            if (f->jobs[j].used) __atomic_store_n(&f->jobs[j].cancel, 1, __ATOMIC_RELAXED);
    update_states(f);
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->mu);
    return CP0_MAP_OK;
}

int cp0_map__fetcher_key_changed(cp0_map_fetcher_t *f, const char *id)
{
    if (!f) return CP0_MAP_E_INVALID;
    pthread_mutex_lock(&f->mu);
    src_t *s = src_find(f, id);
    if (s) load_key(f, s);
    update_states(f);
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->mu);
    return s ? CP0_MAP_OK : CP0_MAP_E_NOT_FOUND;
}

int cp0_map__fetcher_clear(cp0_map_fetcher_t *f, const char *id, uint32_t *hashes_out, int max)
{
    if (!f) return CP0_MAP_E_INVALID;
    cp0_map_vcache_t *vcs[64];
    int n = 0, found = 0;
    pthread_mutex_lock(&f->mu);
    for (src_t *s = f->srcs; s; s = s->next) {
        if (!s->present || (id && strcmp(s->id, id) != 0)) continue;
        found = 1;
        if (n < 64 && s->vc) {
            if (hashes_out && n < max) hashes_out[n] = cp0_map_source_hash(s->id);
            vcs[n++] = s->vc;
        }
    }
    pthread_mutex_unlock(&f->mu);
    if (!found) return CP0_MAP_E_NOT_FOUND;
    int rc = CP0_MAP_OK;
    for (int i = 0; i < n; i++) {
        const int e = cp0_map__vcache_clear(vcs[i]);
        if (e && rc == CP0_MAP_OK) rc = e;
    }
    return rc < 0 ? rc : n;
}

void cp0_map__fetcher_stats(cp0_map_fetcher_t *f, cp0_map_store_stats_t *out)
{
    if (!f) return;
    out->disk_bytes = cp0_map__vcset_disk_bytes(f->vcs);
    out->cap = cp0_map__vcset_cap(f->vcs, &out->cap_auto);
    const int full = cp0_map__vcset_full(f->vcs);
    pthread_mutex_lock(&f->mu);
    const int64_t now = cp0_map__now();
    out->state = f->read_only ? CP0_MAP_E_READ_ONLY : !f->network ? CP0_MAP_E_OFFLINE : full ? CP0_MAP_E_CACHE_FULL
                                                                                              : CP0_MAP_OK;
    /* one count per provider */
    for (grp_t *g = f->grps; g; g = g->next) {
        roll_day(g, now);
        out->fetches_today += g->count;
    }
    src_t *list[CP0_MAP_STATS_SOURCES_MAX];
    int n = 0;
    for (src_t *s = f->srcs; s && n < CP0_MAP_STATS_SOURCES_MAX; s = s->next)
        if (s->present) list[n++] = s;
    /* sorted by id, like the listing */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(list[j - 1]->id, list[j]->id) > 0; j--) {
            src_t *t = list[j];
            list[j] = list[j - 1];
            list[j - 1] = t;
        }
    out->nsources = n;
    for (int i = 0; i < n; i++) {
        src_t *s = list[i];
        cp0_map_source_stats_t *o = &out->sources[i];
        memset(o, 0, sizeof *o);
        cp0_map__copy_str(o->id, sizeof o->id, s->id, 0);
        o->state = src_state(f, s, now, &o->until);
        o->writer = owner(f, s);
        cp0_map_vusage_t u;
        cp0_map__vcache_usage(s->vc, 1, &u);
        o->tiles = u.tiles;
        o->stale = u.stale;
        o->bytes = u.bytes;
        o->fetches_today = s->g ? s->g->count : 0;
        o->daily_cap = s->pol.daily_cap;
        o->fetched = s->fetched;
        o->not_modified = s->not_modified;
        o->missing = s->missing;
        o->failed = s->failed;
        o->refused = s->refused;
        out->disk_hits += u.hits;
        out->disk_misses += u.misses;
        out->tiles += u.tiles;
        out->stale += u.stale;
    }
    pthread_mutex_unlock(&f->mu);
}

/* ----------------------------------------------------------------------------------------------- clients */

int cp0_map_client_new(cp0_map_store_t *store, cp0_map_client_t **out)
{
    if (!out) return CP0_MAP_E_INVALID;
    *out = NULL;
    cp0_map_fetcher_t *f = cp0_map__store_fetcher(store);
    if (!f) return CP0_MAP_E_INVALID;
    cp0_map_client_t *c = (cp0_map_client_t *)calloc(1, sizeof *c);
    if (!c) return CP0_MAP_E_NOMEM;
    c->f = f;
    pthread_mutex_lock(&f->mu);
    c->next = f->clients;
    f->clients = c;
    pthread_mutex_unlock(&f->mu);
    *out = c;
    return CP0_MAP_OK;
}

void cp0_map_client_free(cp0_map_client_t *c)
{
    if (!c) return;
    cp0_map_fetcher_t *f = c->f;
    pthread_mutex_lock(&f->mu);
    for (cp0_map_client_t **p = &f->clients; *p; p = &(*p)->next)
        if (*p == c) {
            *p = c->next;
            break;
        }
    cancel_unwanted(f);
    pthread_mutex_unlock(&f->mu);
    free(c);
}

int cp0_map_client_view(cp0_map_client_t *c, const char *source_id, const cp0_map_tile_id_t *tiles, int n)
{
    if (!c || n < 0 || (n > 0 && !tiles)) return CP0_MAP_E_INVALID;
    cp0_map_fetcher_t *f = c->f;
    pthread_mutex_lock(&f->mu);
    src_t *s = source_id ? src_find(f, source_id) : NULL;
    int rc = CP0_MAP_OK;
    if (source_id && (!s || !s->present)) {
        s = NULL;
        rc = CP0_MAP_E_NOT_FOUND;
    }
    want_t old[CP0_MAP_CLIENT_TILES_MAX];
    const int nold = c->src == s ? c->nw : 0;
    memcpy(old, c->w, (size_t)nold * sizeof old[0]);
    c->src = s;
    c->nw = 0;
    if (s) {
        if (n > CP0_MAP_CLIENT_TILES_MAX) n = CP0_MAP_CLIENT_TILES_MAX;
        for (int i = 0; i < n; i++) {
            const cp0_map_tile_id_t *t = &tiles[i];
            if (t->z < s->minz || t->z > s->maxz || !cp0_map_tile_valid(t->z, t->x, t->y)) continue;
            if (want_index(c, t->z, t->x, t->y) >= 0) continue;
            want_t *w = &c->w[c->nw++];
            w->z = t->z;
            w->x = t->x;
            w->y = t->y;
            w->refused_sent = 0;
            for (int k = 0; k < nold; k++)
                if (old[k].z == w->z && old[k].x == w->x && old[k].y == w->y) w->refused_sent = old[k].refused_sent;
        }
        const int st = src_state(f, s, cp0_map__now(), NULL);
        s->last_state = st;
        if (st != CP0_MAP_OK)
            for (int i = 0; i < c->nw; i++)
                if (!c->w[i].refused_sent && !in_flight(f, s, c->w[i].z, c->w[i].x, c->w[i].y)) {
                    push_event(c, s, c->w[i].z, c->w[i].x, c->w[i].y, CP0_MAP_TILE_REFUSED, st);
                    c->w[i].refused_sent = 1;
                    s->refused++;
                }
    }
    cancel_unwanted(f);
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->mu);
    return rc;
}

int cp0_map_client_poll(cp0_map_client_t *c, cp0_map_tile_event_t *out, int max)
{
    if (!c || !out || max <= 0) return 0;
    cp0_map_fetcher_t *f = c->f;
    pthread_mutex_lock(&f->mu);
    const int n = c->ne < max ? c->ne : max;
    memcpy(out, c->ev, (size_t)n * sizeof *out);
    memmove(c->ev, c->ev + n, (size_t)(c->ne - n) * sizeof c->ev[0]);
    c->ne -= n;
    pthread_mutex_unlock(&f->mu);
    return n;
}

int cp0_map_client_pending(cp0_map_client_t *c)
{
    if (!c) return 0;
    cp0_map_fetcher_t *f = c->f;
    pthread_mutex_lock(&f->mu);
    int n = 0;
    if (c->src && src_state(f, c->src, cp0_map__now(), NULL) == CP0_MAP_OK) n = c->nw;
    pthread_mutex_unlock(&f->mu);
    return n;
}
