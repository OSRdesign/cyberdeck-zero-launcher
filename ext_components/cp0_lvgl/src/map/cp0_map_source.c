/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine, online sources: the shipped records, the record rules (user files only stricter), URL checks (HTTPS,
 * never an OSM Foundation host), URL filling, API key files, the engine's clock and log (see cp0_map_source.h).
 *
 * The shipped records (report 054 2.2, 2.6, 2.7 and the owner decision D3 of 2026-11-05). URLs follow each provider's
 * documentation; none was contacted while this was written (no network in development or tests). Daily caps: Geoapify
 * from its free plan (3,000 credits a day, 0.25 credit a tile: 12,000 tiles, 054 2.2 [Geoapify-price]); Thunderforest
 * from its free Hobby plan (150,000 tiles a month, 054 2.2 [TF-price]: 5,000 a day); OpenTopoMap states no number
 * ("provided the server is not overloaded", 054 2.2 [OTM]): 2,000 a day, a day of normal use in 054 2.7 (about
 * 30 MB). Every shipped record starts switched off (opt-in, D3).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "cp0_map_online.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------------------------------------ clock, log */

static int64_t g_clock_shift;

int64_t cp0_map__now(void)
{
    return (int64_t)time(NULL) + __atomic_load_n(&g_clock_shift, __ATOMIC_RELAXED);
}

void cp0_map__test_shift_clock(int64_t secs)
{
    __atomic_add_fetch(&g_clock_shift, secs, __ATOMIC_RELAXED);
}

int cp0_map__local_day(int64_t t)
{
    const time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm)) return 0;
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

int64_t cp0_map__next_local_midnight(int64_t t)
{
    const time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm)) return t + 86400;
    tm.tm_mday += 1;
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const time_t m = mktime(&tm);
    return m > tt ? (int64_t)m : t + 86400;
}

static void log_stderr(void *user, const char *line)
{
    (void)user;
    fprintf(stderr, "cp0_map: %s\n", line);
}

void cp0_map__log_init(cp0_map_logger_t *lg, void (*fn)(void *, const char *), void *user)
{
    memset(lg, 0, sizeof *lg);
    lg->fn = fn ? fn : log_stderr;
    lg->user = user;
    pthread_mutex_init(&lg->mu, NULL);
}

void cp0_map__log_done(cp0_map_logger_t *lg)
{
    pthread_mutex_destroy(&lg->mu);
    memset(lg->secrets, 0, sizeof lg->secrets);
}

void cp0_map__log_secret(cp0_map_logger_t *lg, const char *secret)
{
    if (!lg || !secret || strlen(secret) < 4) return;
    pthread_mutex_lock(&lg->mu);
    int found = 0;
    for (int i = 0; i < lg->nsecrets && !found; i++) found = !strcmp(lg->secrets[i], secret);
    if (!found) {
        const int k = lg->nsecrets < CP0_MAP_SECRETS_MAX ? lg->nsecrets++ : CP0_MAP_SECRETS_MAX - 1;
        cp0_map__copy_str(lg->secrets[k], sizeof lg->secrets[k], secret, 0);
    }
    pthread_mutex_unlock(&lg->mu);
}

void cp0_map__redact(char *s, size_t size, const char *secret)
{
    if (!s || !secret || !secret[0]) return;
    const size_t sl = strlen(secret);
    char *p;
    while ((p = strstr(s, secret)) != NULL) {
        const size_t tail = strlen(p + sl);
        if ((size_t)(p - s) + 3 + tail + 1 > size) {
            memset(p, '*', sl); /* no room to shorten: mask in place */
            continue;
        }
        memmove(p + 3, p + sl, tail + 1);
        memcpy(p, "***", 3);
    }
}

void cp0_map__log(cp0_map_logger_t *lg, const char *fmt, ...)
{
    if (!lg || !lg->fn) return;
    char line[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    for (char *p = line; *p; p++)
        if ((unsigned char)*p < 0x20) *p = ' ';
    pthread_mutex_lock(&lg->mu);
    for (int i = 0; i < lg->nsecrets; i++) cp0_map__redact(line, sizeof line, lg->secrets[i]);
    pthread_mutex_unlock(&lg->mu);
    lg->fn(lg->user, line);
}

uint64_t cp0_map__hash64(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

/* ---------------------------------------------------------------------------------------------------- URLs */

int cp0_map_source_osmf_host(const char *host)
{
    if (!host) return 0;
    char h[256];
    size_t n = strlen(host);
    if (n == 0 || n >= sizeof h) return 0;
    for (size_t i = 0; i < n; i++) h[i] = (char)tolower((unsigned char)host[i]);
    h[n] = '\0';
    while (n > 0 && h[n - 1] == '.') h[--n] = '\0';
    static const char *const bad[] = {"openstreetmap.org", "osm.org"};
    for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        const size_t bl = strlen(bad[k]);
        if (n == bl && !strcmp(h, bad[k])) return 1;
        if (n > bl && h[n - bl - 1] == '.' && !strcmp(h + n - bl, bad[k])) return 1;
    }
    return 0;
}

int cp0_map__url_split(const char *url, char *scheme, size_t ssize, char *host, size_t hsize, int *port,
                       const char **rest)
{
    if (!url) return CP0_MAP_E_INVALID;
    const char *sep = strstr(url, "://");
    if (!sep || sep == url || (size_t)(sep - url) >= ssize) return CP0_MAP_E_INVALID;
    for (const char *p = url; p < sep; p++)
        if (!isalnum((unsigned char)*p) && *p != '+' && *p != '-' && *p != '.') return CP0_MAP_E_INVALID;
    for (size_t i = 0; i < (size_t)(sep - url); i++) scheme[i] = (char)tolower((unsigned char)url[i]);
    scheme[sep - url] = '\0';
    const char *a = sep + 3;
    const char *end = a + strcspn(a, "/?#");
    for (const char *p = a; p < end; p++)
        if (*p == '@') return CP0_MAP_E_REFUSED; /* user info: never credentials in a URL, never a host disguise */
    const char *hend;
    if (*a == '[') {
        const char *rb = memchr(a, ']', (size_t)(end - a));
        if (!rb) return CP0_MAP_E_INVALID;
        hend = rb + 1;
    } else {
        hend = a;
        while (hend < end && *hend != ':') hend++;
    }
    const size_t hl = (size_t)(hend - a);
    if (hl == 0 || hl >= hsize) return CP0_MAP_E_INVALID;
    for (size_t i = 0; i < hl; i++) host[i] = (char)tolower((unsigned char)a[i]);
    host[hl] = '\0';
    *port = -1;
    if (hend < end) {
        if (*hend != ':') return CP0_MAP_E_INVALID;
        char *pe;
        const long v = strtol(hend + 1, &pe, 10);
        if (pe != end || v <= 0 || v > 65535) return CP0_MAP_E_INVALID;
        *port = (int)v;
    }
    if (rest) *rest = end;
    return CP0_MAP_OK;
}

static int local_host(const char *host)
{
    return !strcmp(host, "127.0.0.1") || !strcmp(host, "localhost") || !strcmp(host, "[::1]");
}

static int host_chars_ok(const char *host)
{
    if (host[0] == '[') {
        for (const char *p = host + 1; *p && *p != ']'; p++)
            if (!isxdigit((unsigned char)*p) && *p != ':' && *p != '.') return 0;
        return 1;
    }
    for (const char *p = host; *p; p++)
        if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) && *p != '.' && *p != '-') return 0;
    return 1;
}

/* scheme and host rules shared by templates and concrete URLs */
static int check_origin(const char *url, const char **rest)
{
    char scheme[16], host[256];
    int port;
    const int rc = cp0_map__url_split(url, scheme, sizeof scheme, host, sizeof host, &port, rest);
    if (rc) return rc;
    if (!host_chars_ok(host)) return CP0_MAP_E_INVALID;
    if (cp0_map_source_osmf_host(host)) return CP0_MAP_E_REFUSED;
    if (!strcmp(scheme, "https")) return CP0_MAP_OK;
    if (!strcmp(scheme, "http") && local_host(host)) return CP0_MAP_OK;
    return CP0_MAP_E_REFUSED;
}

int cp0_map_source_check_url(const char *url)
{
    if (!url || strlen(url) >= CP0_MAP_URL_MAX) return CP0_MAP_E_INVALID;
    for (const char *p = url; *p; p++)
        if ((unsigned char)*p <= 0x20 || (unsigned char)*p >= 0x7f) return CP0_MAP_E_INVALID;
    const char *rest;
    const int rc = check_origin(url, &rest);
    if (rc) return rc;
    int z = 0, x = 0, y = 0;
    for (const char *p = rest; *p; p++) {
        if (*p == '}') return CP0_MAP_E_INVALID;
        if (*p != '{') continue;
        const char *c = strchr(p, '}');
        if (!c) return CP0_MAP_E_INVALID;
        const size_t n = (size_t)(c - p - 1);
        if (n == 1 && p[1] == 'z') z++;
        else if (n == 1 && p[1] == 'x') x++;
        else if (n == 1 && p[1] == 'y') y++;
        else if (n == 3 && !strncmp(p + 1, "key", 3)) {
        } else
            return CP0_MAP_E_INVALID;
        p = c;
    }
    return (z && x && y) ? CP0_MAP_OK : CP0_MAP_E_INVALID;
}

int cp0_map__url_check_concrete(const char *url)
{
    if (!url || strlen(url) >= 2 * CP0_MAP_URL_MAX) return CP0_MAP_E_INVALID;
    for (const char *p = url; *p; p++)
        if ((unsigned char)*p <= 0x20 || (unsigned char)*p >= 0x7f || *p == '{' || *p == '}') return CP0_MAP_E_INVALID;
    return check_origin(url, NULL);
}

void cp0_map__url_host(const char *url, char *host, size_t size)
{
    char scheme[16];
    int port;
    if (size) host[0] = '\0';
    if (cp0_map__url_split(url, scheme, sizeof scheme, host, size, &port, NULL) != CP0_MAP_OK && size) host[0] = '\0';
}

int cp0_map__url_expand(const char *tmpl, int z, uint32_t x, uint32_t y, const char *key, char *out, size_t size)
{
    size_t o = 0;
    for (const char *p = tmpl; *p;) {
        char num[32];
        const char *put = NULL;
        size_t skip = 1;
        if (!strncmp(p, "{z}", 3)) {
            snprintf(num, sizeof num, "%d", z);
            put = num;
            skip = 3;
        } else if (!strncmp(p, "{x}", 3)) {
            snprintf(num, sizeof num, "%u", x);
            put = num;
            skip = 3;
        } else if (!strncmp(p, "{y}", 3)) {
            snprintf(num, sizeof num, "%u", y);
            put = num;
            skip = 3;
        } else if (!strncmp(p, "{key}", 5)) {
            put = key ? key : "***";
            skip = 5;
        }
        if (put) {
            const size_t n = strlen(put);
            if (o + n >= size) return CP0_MAP_E_INVALID;
            memcpy(out + o, put, n);
            o += n;
            p += skip;
        } else {
            if (o + 1 >= size) return CP0_MAP_E_INVALID;
            out[o++] = *p++;
        }
    }
    if (o >= size) return CP0_MAP_E_INVALID;
    out[o] = '\0';
    return CP0_MAP_OK;
}

/* ------------------------------------------------------------------------------------------- User-Agent */

int cp0_map_user_agent_ok(const char *ua)
{
    if (!ua) return 0;
    const size_t n = strlen(ua);
    if (n < 8 || n > 255) return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)ua[i] < 0x20 || (unsigned char)ua[i] > 0x7e) return 0;
    if (!isalnum((unsigned char)ua[0])) return 0;
    const char *slash = strchr(ua, '/');
    if (!slash || strcspn(ua, " ") < (size_t)(slash - ua) || !slash[1] || slash[1] == ' ') return 0;
    static const char *const defaults[] = {"curl/", "libcurl", "wget", "python-", "go-http-client", "java/"};
    for (size_t k = 0; k < sizeof defaults / sizeof defaults[0]; k++)
        if (!strncasecmp(ua, defaults[k], strlen(defaults[k]))) return 0;
    return 1;
}

int cp0_map_user_agent(char *buf, size_t size, const char *app_id, const char *version)
{
    if (!buf || !app_id || !version || !cp0_map__id_valid(app_id) || !version[0]) return CP0_MAP_E_INVALID;
    for (const char *p = version; *p; p++)
        if ((unsigned char)*p <= 0x20 || (unsigned char)*p >= 0x7f || *p == '(' || *p == ')') return CP0_MAP_E_INVALID;
    return cp0_map__fmt(buf, size, "%s/%s (+https://github.com/OSRdesign/cyberdeck-zero-apps)", app_id, version);
}

/* ------------------------------------------------------------------------------------------ shipped records */

typedef struct {
    const char *id, *title, *url;
    cp0_map_format_t format;
    int tile_px, minzoom, maxzoom;
    const char *attribution, *licence, *note;
    cp0_map_source_policy_t policy;
} shipped_t;

/* {viewing_cache, bulk_ok, max_parallel, daily_cap, needs_key, user_agent_required, min_request_interval_ms} */
static const shipped_t k_shipped[] = {
    {"geoapify", "Geoapify OSM Bright", "https://maps.geoapify.com/v1/tile/osm-bright/{z}/{x}/{y}.png?apiKey={key}",
     CP0_MAP_FMT_PNG, 256, 0, 20, "Powered by Geoapify | \xc2\xa9 OpenMapTiles \xc2\xa9 OpenStreetMap contributors",
     "Geoapify terms; free plan 3,000 credits a day (0.25 credit a tile); attribution mandatory on the free plan; "
     "OpenMapTiles design CC-BY, map data ODbL",
     "Caching and storing tiles allowed (054 2.2 [Geoapify-tiles]); area download allowed (phase 0.2, not in this "
     "version). unverified: check before enabling: caching headers and the free plan's request rate (054 12.5).",
     {1, 1, 2, 12000, 1, 1, 100}},
    {"opentopomap", "OpenTopoMap", "https://tile.opentopomap.org/{z}/{x}/{y}.png", CP0_MAP_FMT_PNG, 256, 0, 17,
     "Map data: \xc2\xa9 OpenStreetMap contributors, SRTM | Map style: \xc2\xa9 OpenTopoMap (CC-BY-SA)",
     "Map style CC-BY-SA (OpenTopoMap); map data ODbL (OpenStreetMap contributors); elevation SRTM",
     "Viewing cache only: apps are allowed if the small server is not overloaded, no mass downloads (054 2.2 [OTM]). "
     "unverified: check before enabling: OpenTopoMap's caching headers (054 section 10).",
     {1, 0, 2, 2000, 0, 1, 200}},
    {"thunderforest", "Thunderforest Outdoors", "https://tile.thunderforest.com/outdoors/{z}/{x}/{y}.png?apikey={key}",
     CP0_MAP_FMT_PNG, 256, 0, 22, "Maps \xc2\xa9 Thunderforest, Data \xc2\xa9 OpenStreetMap contributors",
     "Thunderforest terms; free Hobby plan 150,000 tiles a month with the user's own key; map data ODbL",
     "Viewing cache with the user's own key; area download only on paid plans (054 2.2 [TF-terms], [TF-price]). "
     "unverified: check before enabling: the offline-caching term for an app's cache and the caching headers "
     "(054 section 10).",
     {1, 0, 2, 5000, 1, 1, 0}},
};

int cp0_map__shipped_count(void)
{
    return (int)(sizeof k_shipped / sizeof k_shipped[0]);
}

int cp0_map__shipped_find(const char *id)
{
    for (int k = 0; id && k < cp0_map__shipped_count(); k++)
        if (!strcmp(k_shipped[k].id, id)) return k;
    return -1;
}

int cp0_map__shipped_by_host(const char *host)
{
    if (!host || !host[0]) return -1;
    for (int k = 0; k < cp0_map__shipped_count(); k++) {
        char h[256];
        cp0_map__url_host(k_shipped[k].url, h, sizeof h);
        if (!strcmp(h, host)) return k;
    }
    return -1;
}

void cp0_map__srcrec_init(cp0_map_srcrec_t *r)
{
    memset(r, 0, sizeof *r);
    r->enabled = r->viewing_cache = r->bulk_ok = r->max_parallel = r->needs_key = r->ua_required = r->interval_ms = -1;
    r->daily_cap = -1;
}

void cp0_map__shipped_fill(int k, cp0_map_source_info_t *info, cp0_map_srcrec_t *src)
{
    const shipped_t *s = &k_shipped[k];
    cp0_map__copy_str(info->id, sizeof info->id, s->id, 0);
    cp0_map__copy_str(info->title, sizeof info->title, s->title, 0);
    info->kind = CP0_MAP_ENTRY_SOURCE;
    info->format = s->format;
    info->tile_px = s->tile_px;
    info->minzoom = s->minzoom;
    info->maxzoom = s->maxzoom;
    cp0_map__copy_str(info->attribution, sizeof info->attribution, s->attribution, 0);
    cp0_map__srcrec_init(src);
    cp0_map__copy_str(src->url, sizeof src->url, s->url, 0);
    cp0_map__copy_str(src->licence, sizeof src->licence, s->licence, 0);
    cp0_map__copy_str(src->note, sizeof src->note, s->note, 0);
    src->policy = s->policy;
    src->shipped = 1;
    cp0_map__copy_str(src->group, sizeof src->group, s->id, 0);
}

static int parse_flag(const char *v)
{
    if (!strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on")) return 1;
    if (!strcmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off")) return 0;
    return -1;
}

static long long parse_ll(const char *v, long long lo, long long hi)
{
    char *end;
    errno = 0;
    const long long n = strtoll(v, &end, 10);
    return (end == v || *end || errno || n < lo || n > hi) ? -1 : n;
}

int cp0_map__srcrec_kv(cp0_map_srcrec_t *r, const char *k, const char *v)
{
    if (!strcmp(k, "url")) cp0_map__copy_str(r->url, sizeof r->url, v, 1);
    else if (!strcmp(k, "licence") || !strcmp(k, "license")) cp0_map__copy_str(r->licence, sizeof r->licence, v, 1);
    else if (!strcmp(k, "note")) cp0_map__copy_str(r->note, sizeof r->note, v, 1);
    else if (!strcmp(k, "enabled")) r->enabled = parse_flag(v);
    else if (!strcmp(k, "viewing_cache")) r->viewing_cache = parse_flag(v);
    else if (!strcmp(k, "bulk_ok")) r->bulk_ok = parse_flag(v);
    else if (!strcmp(k, "needs_key")) r->needs_key = parse_flag(v);
    else if (!strcmp(k, "user_agent_required")) r->ua_required = parse_flag(v);
    else if (!strcmp(k, "max_parallel")) r->max_parallel = (int)parse_ll(v, 1, CP0_MAP_MAX_PARALLEL);
    else if (!strcmp(k, "min_request_interval_ms")) r->interval_ms = (int)parse_ll(v, 0, 600000);
    else if (!strcmp(k, "daily_cap")) r->daily_cap = parse_ll(v, 0, 10000000);
    else if (!strcmp(k, "blocked_hashes") || !strcmp(k, "blocked_sizes")) {
        const int hashes = k[8] == 'h';
        const char *p = v;
        while (*p) {
            char *end;
            const unsigned long long n = strtoull(p, &end, hashes ? 16 : 10);
            if (end == p) break;
            if (hashes && r->nhashes < CP0_MAP_BLOCKED_LIST_MAX) r->blocked_hashes[r->nhashes++] = (uint64_t)n;
            if (!hashes && r->nsizes < CP0_MAP_BLOCKED_LIST_MAX && n > 0 && n < (1ull << 31))
                r->blocked_sizes[r->nsizes++] = (uint32_t)n;
            p = end;
            while (*p == ',' || *p == ' ') p++;
        }
    } else
        return 0;
    return 1;
}

/* the user's values on top of p, only where they are stricter */
static void stricter(cp0_map_source_policy_t *p, const cp0_map_srcrec_t *u)
{
    if (u->viewing_cache == 0) p->viewing_cache = 0;
    if (u->bulk_ok == 0) p->bulk_ok = 0;
    if (u->max_parallel >= 1 && u->max_parallel < p->max_parallel) p->max_parallel = u->max_parallel;
    if (u->daily_cap >= 0 && (uint64_t)u->daily_cap < p->daily_cap) p->daily_cap = (uint32_t)u->daily_cap;
    if (u->needs_key == 1) p->needs_key = 1;
    if (u->ua_required == 1) p->user_agent_required = 1;
    if (u->interval_ms > p->min_request_interval_ms) p->min_request_interval_ms = u->interval_ms;
}

void cp0_map__source_finish(cp0_map_source_info_t *info, cp0_map_srcrec_t *src)
{
    const cp0_map_srcrec_t user = *src;
    const int user_min = info->minzoom, user_max = info->maxzoom;
    const int k = cp0_map__shipped_find(info->id);
    if (k >= 0) {
        /* a shipped id: the toolkit's values; the user's file only switches it and tightens it */
        char path[CP0_MAP_PATH_MAX];
        cp0_map__copy_str(path, sizeof path, info->path, 0);
        cp0_map__shipped_fill(k, info, src);
        cp0_map__copy_str(info->path, sizeof info->path, path, 0);
        stricter(&src->policy, &user);
        src->enabled = user.enabled == 1 ? 1 : 0;
    } else {
        char host[256];
        cp0_map__url_host(user.url, host, sizeof host);
        const int g = cp0_map__shipped_by_host(host);
        cp0_map__copy_str(src->group, sizeof src->group, info->id, 0);
        if (g >= 0) {
            /* another style of a shipped provider: its flags are the ceiling, its attribution is shown */
            cp0_map_source_info_t base;
            cp0_map_srcrec_t bsrc;
            memset(&base, 0, sizeof base);
            cp0_map__shipped_fill(g, &base, &bsrc);
            src->policy = bsrc.policy;
            stricter(&src->policy, &user);
            cp0_map__copy_str(src->group, sizeof src->group, base.id, 0);
            cp0_map__copy_str(info->attribution, sizeof info->attribution, base.attribution, 0);
            if (!src->licence[0]) cp0_map__copy_str(src->licence, sizeof src->licence, bsrc.licence, 0);
            if (!src->note[0]) cp0_map__copy_str(src->note, sizeof src->note, bsrc.note, 0);
            if (info->maxzoom < 0 || info->maxzoom > base.maxzoom) info->maxzoom = base.maxzoom;
        } else {
            cp0_map_source_policy_t p = {1, 0, 2, CP0_MAP_DAILY_CAP_DEFAULT, 0, 1, 0};
            if (user.viewing_cache >= 0) p.viewing_cache = user.viewing_cache;
            if (user.bulk_ok >= 0) p.bulk_ok = user.bulk_ok;
            if (user.max_parallel >= 1) p.max_parallel = user.max_parallel;
            if (user.daily_cap >= 0) p.daily_cap = (uint32_t)user.daily_cap;
            if (user.needs_key >= 0) p.needs_key = user.needs_key;
            if (user.interval_ms >= 0) p.min_request_interval_ms = user.interval_ms;
            src->policy = p;
        }
        if (strstr(src->url, "{key}")) src->policy.needs_key = 1;
        src->enabled = user.enabled == 0 ? 0 : 1;
        src->shipped = 0;
        if (info->format == CP0_MAP_FMT_UNKNOWN) info->format = CP0_MAP_FMT_PNG;
        if (info->tile_px != 512) info->tile_px = 256;
        if (info->minzoom < 0) info->minzoom = 0;
        if (info->maxzoom < 0) info->maxzoom = 18;
    }
    /* fewer zooms than the record are allowed */
    if (k >= 0) {
        if (user_min > info->minzoom && user_min <= info->maxzoom) info->minzoom = user_min;
        if (user_max >= info->minzoom && user_max < info->maxzoom) info->maxzoom = user_max;
    }
    if (info->maxzoom > CP0_MAP_MAX_ZOOM) info->maxzoom = CP0_MAP_MAX_ZOOM;
    /* the guard lists add up (a shipped record has none of its own yet) */
    src->nhashes = user.nhashes;
    memcpy(src->blocked_hashes, user.blocked_hashes, sizeof src->blocked_hashes);
    src->nsizes = user.nsizes;
    memcpy(src->blocked_sizes, user.blocked_sizes, sizeof src->blocked_sizes);
    src->policy.user_agent_required = 1; /* the engine always sends one */
    if (src->policy.max_parallel < 1) src->policy.max_parallel = 1;
    if (src->policy.max_parallel > CP0_MAP_MAX_PARALLEL) src->policy.max_parallel = CP0_MAP_MAX_PARALLEL;
    src->valid = cp0_map_source_check_url(src->url);
    if (src->valid == CP0_MAP_OK && src->policy.needs_key && !strstr(src->url, "{key}")) src->valid = CP0_MAP_E_INVALID;
    if (src->valid == CP0_MAP_OK && (info->format == CP0_MAP_FMT_WEBP || info->format == CP0_MAP_FMT_MVT))
        src->valid = CP0_MAP_E_INVALID;
}

/* -------------------------------------------------------------------------------------------------- keys */

int cp0_map__key_valid(const char *key)
{
    if (!key) return 0;
    const size_t n = strlen(key);
    if (n < 8 || n >= CP0_MAP_KEY_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)key[i];
        if (!isalnum(c) && c != '.' && c != '_' && c != '~' && c != '-') return 0;
    }
    return 1;
}

static int key_path(const char *dir, const char *id, char *buf, size_t size)
{
    return cp0_map__fmt(buf, size, "%s/sources/%s.key", dir, id);
}

int cp0_map__key_read(const char *dir, const char *id, char *key, size_t size)
{
    char path[CP0_MAP_PATH_MAX];
    if (size) key[0] = '\0';
    if (key_path(dir, id, path, sizeof path)) return CP0_MAP_E_INVALID;
    const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? CP0_MAP_E_NOT_FOUND : CP0_MAP_E_IO;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return CP0_MAP_E_IO;
    }
    if ((st.st_mode & 077) && fchmod(fd, 0600) != 0) { /* a key others could read: closed first, or not used */
        close(fd);
        return CP0_MAP_E_IO;
    }
    char buf[CP0_MAP_KEY_MAX + 8];
    size_t n = 0;
    while (n < sizeof buf - 1) {
        const ssize_t r = read(fd, buf + n, sizeof buf - 1 - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        n += (size_t)r;
    }
    close(fd);
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = '\0';
    const int ok = cp0_map__key_valid(buf) && n < size;
    if (ok) memcpy(key, buf, n + 1);
    memset(buf, 0, sizeof buf);
    return ok ? CP0_MAP_OK : CP0_MAP_E_INVALID;
}

int cp0_map__key_write(const char *dir, const char *id, const char *key)
{
    char path[CP0_MAP_PATH_MAX], tmp[CP0_MAP_PATH_MAX + 32];
    if (!cp0_map__key_valid(key) || key_path(dir, id, path, sizeof path) ||
        cp0_map__fmt(tmp, sizeof tmp, "%s.tmp.%ld", path, (long)getpid()))
        return CP0_MAP_E_INVALID;
    unlink(tmp);
    const int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return CP0_MAP_E_IO;
    char line[CP0_MAP_KEY_MAX + 2];
    const int n = snprintf(line, sizeof line, "%s\n", key);
    int ok = fchmod(fd, 0600) == 0 && n > 0 && write(fd, line, (size_t)n) == n && fsync(fd) == 0;
    memset(line, 0, sizeof line);
    ok = close(fd) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return CP0_MAP_E_IO;
    }
    return CP0_MAP_OK;
}

int cp0_map__key_remove(const char *dir, const char *id)
{
    char path[CP0_MAP_PATH_MAX];
    if (key_path(dir, id, path, sizeof path)) return CP0_MAP_E_INVALID;
    if (unlink(path) != 0 && errno != ENOENT) return CP0_MAP_E_IO;
    return CP0_MAP_OK;
}

int cp0_map__conf_set(const char *path, const char *key, const char *value)
{
    char old[16 * 1024] = "", out[16 * 1024 + 300];
    size_t olen = 0, o = 0;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        for (;;) {
            const ssize_t r = read(fd, old + olen, sizeof old - 1 - olen);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            olen += (size_t)r;
        }
        close(fd);
        old[olen] = '\0';
    } else if (errno != ENOENT) {
        return CP0_MAP_E_IO;
    }
    const size_t kl = strlen(key);
    int done = 0, has_v = 0;
    for (const char *p = old; *p;) {
        const char *eol = strchr(p, '\n');
        const size_t n = eol ? (size_t)(eol - p) : strlen(p);
        if (n > kl && !strncmp(p, key, kl) && p[kl] == '=') {
            if (!done) o += (size_t)snprintf(out + o, sizeof out - o, "%s=%s\n", key, value);
            done = 1;
        } else if (n > 0) {
            if (n >= 2 && !strncmp(p, "v=", 2)) has_v = 1;
            if (o + n + 2 >= sizeof out) return CP0_MAP_E_IO;
            memcpy(out + o, p, n);
            o += n;
            out[o++] = '\n';
        }
        p = eol ? eol + 1 : p + n;
    }
    if (!has_v) {
        /* a new record (or one without its version line): v=1 first */
        char tmp[sizeof out];
        const int n = snprintf(tmp, sizeof tmp, "v=1\n%.*s", (int)o, out);
        if (n < 0 || (size_t)n >= sizeof out) return CP0_MAP_E_IO;
        memcpy(out, tmp, (size_t)n);
        o = (size_t)n;
    }
    if (!done) {
        const int n = snprintf(out + o, sizeof out - o, "%s=%s\n", key, value);
        if (n < 0 || (size_t)n >= sizeof out - o) return CP0_MAP_E_IO;
        o += (size_t)n;
    }
    return cp0_map__write_atomic(path, out, o);
}
