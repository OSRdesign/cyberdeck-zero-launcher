/*
 * SPDX-License-Identifier: MIT
 *
 * Private interfaces of the map engine's online part (task 3): source records and URL rules (cp0_map_source.c),
 * HTTP through libcurl loaded at run time (cp0_map_http.c), the persistent viewing cache (cp0_map_vcache.c) and the
 * fetch pool with the source policy and the three guards (cp0_map_fetch.c). Not installed.
 *
 * Lock order (never the other way round): store mutex -> the fetch pool's mutex -> one viewing cache's mutex -> the
 * cache set's mutex (a leaf). No mutex is held across a network request.
 */
#ifndef CP0_MAP_ONLINE_H
#define CP0_MAP_ONLINE_H

#include "cp0_map_internal.h"

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------------------------- clock, log */

/* Seconds since the epoch (time()) plus the tests' shift. */
CP0_MAP_HIDDEN int64_t cp0_map__now(void);
/* Tests: moves the engine's wall clock by secs (adds up). */
CP0_MAP_HIDDEN void cp0_map__test_shift_clock(int64_t secs);
/* YYYYMMDD of t in local time. */
CP0_MAP_HIDDEN int cp0_map__local_day(int64_t t);
/* The next local midnight after t. */
CP0_MAP_HIDDEN int64_t cp0_map__next_local_midnight(int64_t t);

#define CP0_MAP_SECRETS_MAX 16

typedef struct {
    void (*fn)(void *user, const char *line);
    void *user;
    pthread_mutex_t mu;
    char secrets[CP0_MAP_SECRETS_MAX][CP0_MAP_KEY_MAX]; /* replaced by "***" in every line */
    int nsecrets;
} cp0_map_logger_t;

CP0_MAP_HIDDEN void cp0_map__log_init(cp0_map_logger_t *lg, void (*fn)(void *, const char *), void *user);
CP0_MAP_HIDDEN void cp0_map__log_done(cp0_map_logger_t *lg);
CP0_MAP_HIDDEN void cp0_map__log_secret(cp0_map_logger_t *lg, const char *secret);
CP0_MAP_HIDDEN void cp0_map__log(cp0_map_logger_t *lg, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Replaces every occurrence of `secret` in s (in place) by "***". */
CP0_MAP_HIDDEN void cp0_map__redact(char *s, size_t size, const char *secret);

/* ---------------------------------------------------------------------------------- source records, URLs */

#define CP0_MAP_BLOCKED_LIST_MAX 8

typedef struct {
    char url[CP0_MAP_URL_MAX];
    char licence[160];
    char note[256];
    int enabled;                  /* -1: not set */
    int viewing_cache, bulk_ok, max_parallel, needs_key, ua_required, interval_ms; /* -1: not set */
    long long daily_cap;          /* -1: not set */
    uint64_t blocked_hashes[CP0_MAP_BLOCKED_LIST_MAX];
    int nhashes;
    uint32_t blocked_sizes[CP0_MAP_BLOCKED_LIST_MAX];
    int nsizes;
    /* after cp0_map__sources_finish */
    int shipped;                  /* the id is a shipped record's */
    char group[CP0_MAP_ID_MAX];   /* the provider: the shipped record of the same host, else the id itself */
    cp0_map_source_policy_t policy;
    int valid;                    /* CP0_MAP_OK, CP0_MAP_E_REFUSED, CP0_MAP_E_INVALID */
} cp0_map_srcrec_t;

CP0_MAP_HIDDEN void cp0_map__srcrec_init(cp0_map_srcrec_t *r);
/* One key=value line of a source record; 1 when it was a source key. */
CP0_MAP_HIDDEN int cp0_map__srcrec_kv(cp0_map_srcrec_t *r, const char *k, const char *v);

/* URL pieces: scheme (lower case), host (lower case, brackets of an IPv6 literal kept), port (-1 none), the rest
 * from the path on. 0, CP0_MAP_E_INVALID (no "scheme://", no host, too long), CP0_MAP_E_REFUSED (user info). */
CP0_MAP_HIDDEN int cp0_map__url_split(const char *url, char *scheme, size_t ssize, char *host, size_t hsize, int *port,
                                      const char **rest);
/* A concrete URL (a filled template, a redirect target): https or the local-host exception, never an OSMF host.
 * 0, CP0_MAP_E_REFUSED, CP0_MAP_E_INVALID. */
CP0_MAP_HIDDEN int cp0_map__url_check_concrete(const char *url);
/* Fills {z} {x} {y} {key}; key NULL writes "***" (the form for messages). 0 or CP0_MAP_E_INVALID. */
CP0_MAP_HIDDEN int cp0_map__url_expand(const char *tmpl, int z, uint32_t x, uint32_t y, const char *key, char *out,
                                       size_t size);
/* The record's host (lower case), "" when the URL has none. */
CP0_MAP_HIDDEN void cp0_map__url_host(const char *url, char *host, size_t size);

/* The shipped record of `id` (0 .. n-1) or -1, their count, and the shipped record whose URL host is `host`. */
CP0_MAP_HIDDEN int cp0_map__shipped_count(void);
CP0_MAP_HIDDEN int cp0_map__shipped_find(const char *id);
CP0_MAP_HIDDEN int cp0_map__shipped_by_host(const char *host);
/* Fills info + src of shipped record k (id, title, url, format, tile_px, zooms, attribution, licence, note, policy). */
CP0_MAP_HIDDEN void cp0_map__shipped_fill(int k, cp0_map_source_info_t *info, cp0_map_srcrec_t *src);
/* The rules of cp0_map_source.h on one parsed record (in place): a shipped id merged onto its shipped values (only
 * stricter), a record on a shipped host capped by it, defaults for the rest, the URL checked (valid set). */
CP0_MAP_HIDDEN void cp0_map__source_finish(cp0_map_source_info_t *info, cp0_map_srcrec_t *src);

/* API keys: <dir>/sources/<id>.key. */
CP0_MAP_HIDDEN int cp0_map__key_valid(const char *key);
/* 0 with key set, CP0_MAP_E_NOT_FOUND (no key file), CP0_MAP_E_IO / CP0_MAP_E_INVALID (unreadable, a bad key). A file
 * readable by others is made 0600 first. */
CP0_MAP_HIDDEN int cp0_map__key_read(const char *store_dir, const char *id, char *key, size_t size);
CP0_MAP_HIDDEN int cp0_map__key_write(const char *store_dir, const char *id, const char *key);
CP0_MAP_HIDDEN int cp0_map__key_remove(const char *store_dir, const char *id);
/* Sets key=value in a key=value file (other lines kept; v=1 first when it is new). 0 or CP0_MAP_E_IO. */
CP0_MAP_HIDDEN int cp0_map__conf_set(const char *path, const char *key, const char *value);

/* 64-bit FNV-1a, for the blocked-picture guard. */
CP0_MAP_HIDDEN uint64_t cp0_map__hash64(const uint8_t *p, size_t n);

/* ----------------------------------------------------------------------------------------------- HTTP */

typedef struct {
    int status;                   /* the HTTP status of the last answer; 0 when none */
    cp0_map_buf_t body;           /* 200 answers only */
    char content_type[64];        /* lower case, without parameters */
    char etag[128], last_modified[64], location[CP0_MAP_URL_MAX * 2];
    int64_t max_age, age, expires, date, retry_after_s, retry_after_at, content_length; /* -1: not sent */
    int no_store, no_cache;
    int curl_code;                /* the libcurl result of the transfer */
    int redirects;
} cp0_map_http_resp_t;

typedef struct {
    const char *url;              /* the filled URL (may hold the key) */
    const char *user_agent;
    const char *etag;             /* If-None-Match; NULL or "" none */
    const char *last_modified;    /* If-Modified-Since; NULL or "" none */
    const volatile int *cancel;   /* read during the transfer; non-zero aborts it */
} cp0_map_http_req_t;

typedef struct cp0_map_http cp0_map_http_t;

/* Loads libcurl once (any thread). 0 or CP0_MAP_E_CURL_MISSING. $CP0_MAP_CURL_LIB names another file (tests). */
CP0_MAP_HIDDEN int cp0_map__http_load(void);
CP0_MAP_HIDDEN cp0_map_http_t *cp0_map__http_new(void);
CP0_MAP_HIDDEN void cp0_map__http_free(cp0_map_http_t *h);
/* GET with the engine's rules (cp0_map_store.h, NETWORK). Redirects are followed here, each checked. Returns
 * CP0_MAP_OK when an HTTP answer came (resp->status set; a 200 image in body, checked: type and size), or
 * CP0_MAP_E_NETWORK (no answer, timeout), CP0_MAP_E_CANCELLED, CP0_MAP_E_TOO_LARGE, CP0_MAP_E_REFUSED (a redirect the
 * rules refuse), CP0_MAP_E_HTTP (too many redirects, a 200 that is not a PNG / JPEG), CP0_MAP_E_CURL_MISSING. */
CP0_MAP_HIDDEN int cp0_map__http_get(cp0_map_http_t *h, const cp0_map_http_req_t *req, cp0_map_http_resp_t *resp);
CP0_MAP_HIDDEN void cp0_map__http_resp_clear(cp0_map_http_resp_t *r); /* resets the fields, keeps the body buffer */
/* One response header line (no CR LF needed) into r. */
CP0_MAP_HIDDEN void cp0_map__http_header(cp0_map_http_resp_t *r, const char *line, size_t n);
/* An HTTP date (IMF-fixdate, RFC 850, asctime) -> seconds since the epoch, or -1. */
CP0_MAP_HIDDEN int64_t cp0_map__http_date(const char *s);
/* How long a 200 / 304 answer stays fresh, clamped to CP0_MAP_FRESH_FLOOR_S .. CP0_MAP_FRESH_CEIL_S. */
CP0_MAP_HIDDEN int64_t cp0_map__http_freshness(const cp0_map_http_resp_t *r);
/* Retry-After in seconds from now (-1 when absent). */
CP0_MAP_HIDDEN int64_t cp0_map__http_retry_after(const cp0_map_http_resp_t *r, int64_t now);
/* Tests: other timeouts (0 keeps a value). */
CP0_MAP_HIDDEN void cp0_map__http_test_timeouts(int connect_ms, int total_ms);

/* ------------------------------------------------------------------------------------------ viewing cache */

typedef struct cp0_map_vcache cp0_map_vcache_t;
typedef struct cp0_map_vcset cp0_map_vcset_t;

typedef struct {
    int have;                     /* the tile is in the cache (or pending) */
    int stale;                    /* past its expiry */
    int missing;                  /* a 404 / 410 remembered and not expired */
    int64_t expires;
    char etag[128], last_modified[64];
} cp0_map_vstate_t;

enum { CP0_MAP_VPUT_TILE = 1, CP0_MAP_VPUT_REFRESH, CP0_MAP_VPUT_MISSING, CP0_MAP_VPUT_RAM };

typedef struct {
    int kind;
    int z;
    uint32_t x, y;
    const uint8_t *data;          /* TILE, RAM */
    size_t len;
    uint64_t hash;
    int64_t expires;              /* TILE, REFRESH; MISSING: until */
    const char *etag, *last_modified;
} cp0_map_vput_t;

typedef struct {
    uint64_t tiles, stale, bytes;
    uint64_t hits, misses;
    int writer, full;
} cp0_map_vusage_t;

/* The set of a store's viewing caches, its cap and its cache-writer thread (none when read-only). */
CP0_MAP_HIDDEN cp0_map_vcset_t *cp0_map__vcset_new(const char *store_dir, int read_only, uint64_t cap,
                                                   cp0_map_logger_t *log);
CP0_MAP_HIDDEN void cp0_map__vcset_free(cp0_map_vcset_t *set);   /* flushes, stops the thread, closes every cache */
/* The cache of source `id` (created on first use; the writer lock taken unless read-only). NULL when out of memory. */
CP0_MAP_HIDDEN cp0_map_vcache_t *cp0_map__vcset_get(cp0_map_vcset_t *set, const char *id);
CP0_MAP_HIDDEN void cp0_map__vcset_set_cap(cp0_map_vcset_t *set, uint64_t cap); /* 0 = the default rule */
CP0_MAP_HIDDEN uint64_t cp0_map__vcset_cap(cp0_map_vcset_t *set, int *is_auto);
CP0_MAP_HIDDEN int cp0_map__vcset_flush(cp0_map_vcset_t *set);     /* commits all, evicts if needed, waits */
CP0_MAP_HIDDEN int cp0_map__vcset_full(cp0_map_vcset_t *set);      /* the cache cannot grow (CP0_MAP_E_CACHE_FULL) */
CP0_MAP_HIDDEN uint64_t cp0_map__vcset_disk_bytes(cp0_map_vcset_t *set);
/* Tests: pretend the file system has this much free space (0: the real figure). */
CP0_MAP_HIDDEN void cp0_map__vcset_test_free(cp0_map_vcset_t *set, uint64_t free_bytes);
CP0_MAP_HIDDEN uint64_t cp0_map__vcset_commits(cp0_map_vcset_t *set); /* transactions committed (tests) */
/* Tests: abort() inside the next commit after n writes (crash safety). */
CP0_MAP_HIDDEN void cp0_map__vcache_test_abort_after(int n);

/* Title, attribution, format and zooms for the MBTiles metadata of a cache file created later. */
CP0_MAP_HIDDEN void cp0_map__vcache_meta(cp0_map_vcache_t *vc, const char *title, const char *attribution,
                                         cp0_map_format_t format, int minzoom, int maxzoom);
CP0_MAP_HIDDEN int cp0_map__vcache_writer(cp0_map_vcache_t *vc);
/* A tile for the loader: pending and RAM tiles first, then the file. 0, CP0_MAP_E_NO_TILE, read errors. */
CP0_MAP_HIDDEN int cp0_map__vcache_get(cp0_map_vcache_t *vc, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out,
                                       int *stale, int64_t *expires);
/* What the cache knows of a tile (the fetch pool's check). 0 or a read error. */
CP0_MAP_HIDDEN int cp0_map__vcache_lookup(cp0_map_vcache_t *vc, int z, uint32_t x, uint32_t y, cp0_map_vstate_t *st);
/* Queues a write (waits while the queue is full). 0, CP0_MAP_E_READ_ONLY (not the writer; RAM puts always work),
 * CP0_MAP_E_CACHE_FULL, CP0_MAP_E_NOMEM. */
CP0_MAP_HIDDEN int cp0_map__vcache_put(cp0_map_vcache_t *vc, const cp0_map_vput_t *p);
/* Removes every tile whose bytes hash to `hash` (pending, RAM and file). Returns how many, or an error. */
CP0_MAP_HIDDEN int cp0_map__vcache_purge_hash(cp0_map_vcache_t *vc, uint64_t hash);
/* Deletes the cache file (writer only). 0, CP0_MAP_E_READ_ONLY, CP0_MAP_E_IO. */
CP0_MAP_HIDDEN int cp0_map__vcache_clear(cp0_map_vcache_t *vc);
/* Counts (stale needs a query: may wait on the disk). */
CP0_MAP_HIDDEN void cp0_map__vcache_usage(cp0_map_vcache_t *vc, int count_stale, cp0_map_vusage_t *u);
/* The tile count without waiting for a commit in progress (then the last count seen): for the listing, which the
 * widget reads on the LVGL thread. */
CP0_MAP_HIDDEN uint64_t cp0_map__vcache_tiles_nowait(cp0_map_vcache_t *vc);

/* ------------------------------------------------------------------------------------------- fetch pool */

typedef struct cp0_map_fetcher cp0_map_fetcher_t;

CP0_MAP_HIDDEN cp0_map_fetcher_t *cp0_map__fetcher_new(const char *store_dir, const cp0_map_store_cfg_t *cfg);
CP0_MAP_HIDDEN void cp0_map__fetcher_free(cp0_map_fetcher_t *f);
/* State, pause end, switch, key, counts of one source into its listing entry. */
CP0_MAP_HIDDEN void cp0_map__fetcher_fill(cp0_map_fetcher_t *f, cp0_map_source_info_t *info);
CP0_MAP_HIDDEN int cp0_map__fetcher_get_tile(cp0_map_fetcher_t *f, const char *id, int z, uint32_t x, uint32_t y,
                                             cp0_map_buf_t *out, cp0_map_tile_meta_t *meta);
CP0_MAP_HIDDEN cp0_map_vcset_t *cp0_map__fetcher_vcset(cp0_map_fetcher_t *f);
CP0_MAP_HIDDEN int cp0_map__fetcher_set_network(cp0_map_fetcher_t *f, int on);
CP0_MAP_HIDDEN int cp0_map__fetcher_key_changed(cp0_map_fetcher_t *f, const char *id); /* re-reads the key file */
CP0_MAP_HIDDEN int cp0_map__fetcher_clear(cp0_map_fetcher_t *f, const char *id, uint32_t *hashes_out, int max);
CP0_MAP_HIDDEN void cp0_map__fetcher_stats(cp0_map_fetcher_t *f, cp0_map_store_stats_t *out);
CP0_MAP_HIDDEN cp0_map_logger_t *cp0_map__fetcher_log(cp0_map_fetcher_t *f);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_ONLINE_H */
