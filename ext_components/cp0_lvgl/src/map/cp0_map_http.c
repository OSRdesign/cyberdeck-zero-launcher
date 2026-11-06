/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: HTTP GET through libcurl opened at run time (dlopen "libcurl.so.4", Debian's libcurl4t64 with the
 * boards' TLS stack), the pattern of Home Assistant (apps repo, src/core/curl_api.cpp) and of the engine's own SQLite
 * loader: the toolkit links no new library and needs no curl header; the few constants used are libcurl's stable C
 * ABI (curl/curl.h, unchanged since 7.x; CURLOPT_PROTOCOLS_STR since 7.85, with the older bit mask as a fallback).
 * Without the library every fetch answers CP0_MAP_E_CURL_MISSING ("network unavailable: library missing").
 * $CP0_MAP_CURL_LIB names another library file (the tests point it at a bogus name for the "missing" path).
 *
 * Rules (cp0_map_store.h, NETWORK): the app's User-Agent, connect timeout 5 s, total 15 s, no signals, redirects
 * followed here (at most 3), each target checked like a template (HTTPS or the local-host exception, never an OSMF
 * host), protocols limited to https (http only for the local-host exception), at most CP0_MAP_HTTP_MAX_BYTES, a 200
 * must be image/png or image/jpeg with matching bytes. TLS certificate checks stay libcurl's defaults (on).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* timegm, strcasecmp */
#endif
#include "cp0_map_online.h"

#include <ctype.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ---- the part of libcurl's ABI the engine uses */
typedef void CURL;
struct curl_slist;
typedef size_t (*curl_data_cb)(char *ptr, size_t size, size_t nmemb, void *user);
typedef int (*curl_xferinfo_cb)(void *user, long long dltotal, long long dlnow, long long ultotal, long long ulnow);

enum {
    C_OK = 0,
    C_WRITE_ERROR = 23,
    C_OPERATION_TIMEDOUT = 28,
    C_ABORTED_BY_CALLBACK = 42,
    C_FILESIZE_EXCEEDED = 63,
};
enum {
    O_WRITEDATA = 10001,
    O_URL = 10002,
    O_ERRORBUFFER = 10010,
    O_WRITEFUNCTION = 20011,
    O_USERAGENT = 10018,
    O_HTTPHEADER = 10023,
    O_HEADERDATA = 10029,
    O_NOPROGRESS = 43,
    O_FOLLOWLOCATION = 52,
    O_XFERINFODATA = 10057,
    O_HEADERFUNCTION = 20079,
    O_HTTPGET = 80,
    O_NOSIGNAL = 99,
    O_TIMEOUT_MS = 155,
    O_CONNECTTIMEOUT_MS = 156,
    O_PROTOCOLS = 181,
    O_XFERINFOFUNCTION = 20219,
    O_MAXFILESIZE_LARGE = 30117,
    O_PROTOCOLS_STR = 10318,
};
#define I_RESPONSE_CODE (0x200000 + 2)
#define PROTO_HTTP 1L
#define PROTO_HTTPS 2L
#define GLOBAL_DEFAULT 3L

typedef struct {
    int (*global_init)(long);
    CURL *(*easy_init)(void);
    int (*easy_setopt)(CURL *, int, ...);
    int (*easy_perform)(CURL *);
    void (*easy_cleanup)(CURL *);
    int (*easy_getinfo)(CURL *, int, ...);
    void (*easy_reset)(CURL *);
    struct curl_slist *(*slist_append)(struct curl_slist *, const char *);
    void (*slist_free_all)(struct curl_slist *);
} curl_api_t;

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static curl_api_t g_curl;
static int g_curl_ok;
static int g_connect_ms = 5000, g_total_ms = 15000;

static void load_curl(void)
{
    const char *name = getenv("CP0_MAP_CURL_LIB");
    if (!name || !name[0]) name = "libcurl.so.4";
    void *h = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!h) return;
    curl_api_t a;
    memset(&a, 0, sizeof a);
    int ok = 1;
#define SYM(field, sym)                                                                                               \
    do {                                                                                                              \
        void *p = dlsym(h, sym);                                                                                      \
        if (!p) ok = 0;                                                                                               \
        memcpy(&a.field, &p, sizeof p);                                                                               \
    } while (0)
    SYM(global_init, "curl_global_init");
    SYM(easy_init, "curl_easy_init");
    SYM(easy_setopt, "curl_easy_setopt");
    SYM(easy_perform, "curl_easy_perform");
    SYM(easy_cleanup, "curl_easy_cleanup");
    SYM(easy_getinfo, "curl_easy_getinfo");
    SYM(easy_reset, "curl_easy_reset");
    SYM(slist_append, "curl_slist_append");
    SYM(slist_free_all, "curl_slist_free_all");
#undef SYM
    if (!ok || a.global_init(GLOBAL_DEFAULT) != C_OK) {
        dlclose(h);
        return;
    }
    g_curl = a;
    g_curl_ok = 1;
}

int cp0_map__http_load(void)
{
    pthread_once(&g_once, load_curl);
    return g_curl_ok ? CP0_MAP_OK : CP0_MAP_E_CURL_MISSING;
}

void cp0_map__http_test_timeouts(int connect_ms, int total_ms)
{
    if (connect_ms > 0) __atomic_store_n(&g_connect_ms, connect_ms, __ATOMIC_RELAXED);
    if (total_ms > 0) __atomic_store_n(&g_total_ms, total_ms, __ATOMIC_RELAXED);
}

struct cp0_map_http {
    CURL *easy;
};

cp0_map_http_t *cp0_map__http_new(void)
{
    if (cp0_map__http_load() != CP0_MAP_OK) return NULL;
    cp0_map_http_t *h = (cp0_map_http_t *)calloc(1, sizeof *h);
    if (!h) return NULL;
    h->easy = g_curl.easy_init();
    if (!h->easy) {
        free(h);
        return NULL;
    }
    return h;
}

void cp0_map__http_free(cp0_map_http_t *h)
{
    if (!h) return;
    if (h->easy) g_curl.easy_cleanup(h->easy);
    free(h);
}

/* ------------------------------------------------------------------------------------------ headers, dates */

static int month_of(const char *m)
{
    static const char *const names[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                        "jul", "aug", "sep", "oct", "nov", "dec"};
    for (int i = 0; i < 12; i++)
        if (!strncasecmp(m, names[i], 3)) return i;
    return -1;
}

int64_t cp0_map__http_date(const char *s)
{
    if (!s) return -1;
    while (*s == ' ') s++;
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    char mon[4] = "";
    int d, y, H, M, S;
    if (sscanf(s, "%*3[A-Za-z], %d %3s %d %d:%d:%d GMT", &d, mon, &y, &H, &M, &S) == 6) {
        /* IMF-fixdate: Sun, 06 Nov 1994 08:49:37 GMT */
    } else if (sscanf(s, "%*[A-Za-z], %d-%3s-%d %d:%d:%d GMT", &d, mon, &y, &H, &M, &S) == 6) {
        /* RFC 850: Sunday, 06-Nov-94 08:49:37 GMT */
        if (y < 100) y += y >= 70 ? 1900 : 2000;
    } else if (sscanf(s, "%*3[A-Za-z] %3s %d %d:%d:%d %d", mon, &d, &H, &M, &S, &y) == 6) {
        /* asctime: Sun Nov  6 08:49:37 1994 */
    } else {
        return -1;
    }
    const int m = month_of(mon);
    if (m < 0 || d < 1 || d > 31 || y < 1970 || y > 9999 || H > 23 || M > 59 || S > 60) return -1;
    tm.tm_year = y - 1900;
    tm.tm_mon = m;
    tm.tm_mday = d;
    tm.tm_hour = H;
    tm.tm_min = M;
    tm.tm_sec = S;
    const time_t t = timegm(&tm);
    return t == (time_t)-1 ? -1 : (int64_t)t;
}

void cp0_map__http_resp_clear(cp0_map_http_resp_t *r)
{
    cp0_map_buf_t keep = r->body;
    const int redirects = r->redirects;
    memset(r, 0, sizeof *r);
    r->body = keep;
    r->body.len = 0;
    r->redirects = redirects;
    r->max_age = r->age = r->expires = r->date = r->retry_after_s = r->retry_after_at = r->content_length = -1;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

static int64_t parse_secs(const char *v)
{
    if (!isdigit((unsigned char)v[0])) return -1;
    char *end;
    const long long n = strtoll(v, &end, 10);
    while (*end == ' ') end++;
    return (*end || n < 0) ? -1 : (n > (1ll << 40) ? (1ll << 40) : n);
}

static void cache_control(cp0_map_http_resp_t *r, const char *v)
{
    char buf[512];
    cp0_map__copy_str(buf, sizeof buf, v, 1);
    char *save = NULL;
    for (char *t = strtok_r(buf, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        trim(t);
        if (!strncasecmp(t, "max-age=", 8)) {
            const char *n = t + 8;
            if (*n == '"') n++;
            if (isdigit((unsigned char)*n)) r->max_age = strtoll(n, NULL, 10);
        } else if (!strcasecmp(t, "no-store")) {
            r->no_store = 1;
        } else if (!strcasecmp(t, "no-cache")) {
            r->no_cache = 1;
        }
    }
}

void cp0_map__http_header(cp0_map_http_resp_t *r, const char *line, size_t n)
{
    char buf[1024];
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, line, n);
    buf[n] = '\0';
    trim(buf);
    if (!strncmp(buf, "HTTP/", 5)) {
        /* a new answer: everything before it (a 100 Continue) is forgotten */
        const char *sp = strchr(buf, ' ');
        cp0_map__http_resp_clear(r);
        r->status = sp ? atoi(sp + 1) : 0;
        return;
    }
    char *colon = strchr(buf, ':');
    if (!colon) return;
    *colon = '\0';
    char *v = colon + 1;
    trim(buf);
    trim(v);
    if (!strcasecmp(buf, "cache-control")) cache_control(r, v);
    else if (!strcasecmp(buf, "expires")) {
        const int64_t t = cp0_map__http_date(v);
        r->expires = t >= 0 ? t : 0; /* an invalid date means "already expired" */
    } else if (!strcasecmp(buf, "date")) r->date = cp0_map__http_date(v);
    else if (!strcasecmp(buf, "age")) r->age = parse_secs(v);
    else if (!strcasecmp(buf, "etag")) {
        if (strlen(v) < sizeof r->etag) cp0_map__copy_str(r->etag, sizeof r->etag, v, 1);
    } else if (!strcasecmp(buf, "last-modified")) {
        if (strlen(v) < sizeof r->last_modified) cp0_map__copy_str(r->last_modified, sizeof r->last_modified, v, 1);
    } else if (!strcasecmp(buf, "retry-after")) {
        r->retry_after_s = parse_secs(v);
        if (r->retry_after_s < 0) r->retry_after_at = cp0_map__http_date(v);
    } else if (!strcasecmp(buf, "content-type")) {
        size_t i = 0;
        for (; v[i] && v[i] != ';' && i + 1 < sizeof r->content_type; i++)
            r->content_type[i] = (char)tolower((unsigned char)v[i]);
        r->content_type[i] = '\0';
        trim(r->content_type);
    } else if (!strcasecmp(buf, "content-length")) r->content_length = parse_secs(v);
    else if (!strcasecmp(buf, "location")) cp0_map__copy_str(r->location, sizeof r->location, v, 1);
}

int64_t cp0_map__http_freshness(const cp0_map_http_resp_t *r)
{
    int64_t f;
    if (r->no_cache) f = 0;
    else if (r->max_age >= 0) f = r->max_age - (r->age > 0 ? r->age : 0);
    else if (r->expires >= 0) f = r->expires - (r->date >= 0 ? r->date : cp0_map__now());
    else f = CP0_MAP_FRESH_DEFAULT_S;
    if (f < CP0_MAP_FRESH_FLOOR_S) f = CP0_MAP_FRESH_FLOOR_S;
    if (f > CP0_MAP_FRESH_CEIL_S) f = CP0_MAP_FRESH_CEIL_S;
    return f;
}

int64_t cp0_map__http_retry_after(const cp0_map_http_resp_t *r, int64_t now)
{
    if (r->retry_after_s >= 0) return r->retry_after_s;
    if (r->retry_after_at >= 0) return r->retry_after_at > now ? r->retry_after_at - now : 0;
    return -1;
}

/* ------------------------------------------------------------------------------------------------- transfer */

typedef struct {
    cp0_map_http_resp_t *resp;
    const volatile int *cancel;
    int too_large;
} xfer_t;

static size_t on_header(char *buf, size_t size, size_t nitems, void *user)
{
    xfer_t *x = (xfer_t *)user;
    const size_t n = size * nitems;
    cp0_map__http_header(x->resp, buf, n);
    if (x->resp->status == 200 && x->resp->content_length > (int64_t)CP0_MAP_HTTP_MAX_BYTES) {
        x->too_large = 1;
        return 0;
    }
    return n;
}

static size_t on_data(char *ptr, size_t size, size_t nmemb, void *user)
{
    xfer_t *x = (xfer_t *)user;
    const size_t n = size * nmemb;
    cp0_map_buf_t *b = &x->resp->body;
    if (b->len + n > CP0_MAP_HTTP_MAX_BYTES) {
        x->too_large = 1;
        return 0;
    }
    if (x->resp->status != 200) return n; /* error bodies are read and dropped */
    if (cp0_map_buf_reserve(b, b->len + n) != CP0_MAP_OK) return 0;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    return n;
}

static int on_progress(void *user, long long dltotal, long long dlnow, long long ultotal, long long ulnow)
{
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    const xfer_t *x = (const xfer_t *)user;
    return (x->cancel && __atomic_load_n(x->cancel, __ATOMIC_RELAXED)) ? 1 : 0;
}

/* the next URL of a redirect; 0 or CP0_MAP_E_HTTP (a form not followed) */
static int resolve(const char *base, const char *loc, char *out, size_t size)
{
    if (!strncasecmp(loc, "https://", 8) || !strncasecmp(loc, "http://", 7))
        return cp0_map__fmt(out, size, "%s", loc) ? CP0_MAP_E_HTTP : CP0_MAP_OK;
    char scheme[16], host[256];
    int port;
    const char *rest;
    if (cp0_map__url_split(base, scheme, sizeof scheme, host, sizeof host, &port, &rest) != CP0_MAP_OK)
        return CP0_MAP_E_HTTP;
    if (loc[0] == '/' && loc[1] == '/') return cp0_map__fmt(out, size, "%s:%s", scheme, loc) ? CP0_MAP_E_HTTP : 0;
    if (loc[0] != '/') return CP0_MAP_E_HTTP;
    char portbuf[16] = "";
    if (port > 0) snprintf(portbuf, sizeof portbuf, ":%d", port);
    return cp0_map__fmt(out, size, "%s://%s%s%s", scheme, host, portbuf, loc) ? CP0_MAP_E_HTTP : CP0_MAP_OK;
}

static int set_protocols(CURL *e, int allow_http)
{
    if (g_curl.easy_setopt(e, O_PROTOCOLS_STR, allow_http ? "http,https" : "https") == C_OK) return 0;
    return g_curl.easy_setopt(e, O_PROTOCOLS, allow_http ? (PROTO_HTTP | PROTO_HTTPS) : PROTO_HTTPS) == C_OK ? 0 : -1;
}

int cp0_map__http_get(cp0_map_http_t *h, const cp0_map_http_req_t *req, cp0_map_http_resp_t *resp)
{
    if (!resp) return CP0_MAP_E_INVALID;
    resp->redirects = 0;
    cp0_map__http_resp_clear(resp);
    if (cp0_map__http_load() != CP0_MAP_OK) return CP0_MAP_E_CURL_MISSING;
    if (!h || !req || !req->url) return CP0_MAP_E_INVALID;
    if (!cp0_map_user_agent_ok(req->user_agent)) return CP0_MAP_E_NO_USER_AGENT;
    char url[2 * CP0_MAP_URL_MAX];
    if (cp0_map__fmt(url, sizeof url, "%s", req->url)) return CP0_MAP_E_INVALID;
    for (int hop = 0;; hop++) {
        const int chk = cp0_map__url_check_concrete(url);
        if (chk) return chk;
        char scheme[16], host[256];
        int port;
        cp0_map__url_split(url, scheme, sizeof scheme, host, sizeof host, &port, NULL);
        const int http = !strcmp(scheme, "http");

        CURL *e = h->easy;
        g_curl.easy_reset(e);
        xfer_t x = {resp, req->cancel, 0};
        cp0_map__http_resp_clear(resp);
        struct curl_slist *hdr = g_curl.slist_append(NULL, "Accept: image/png, image/jpeg");
        char line[256];
        int bad = !hdr;
        if (hdr && hop == 0 && req->etag && req->etag[0]) {
            snprintf(line, sizeof line, "If-None-Match: %s", req->etag);
            bad = bad || !g_curl.slist_append(hdr, line); /* appended at the end: the head stays */
        }
        if (hdr && hop == 0 && req->last_modified && req->last_modified[0]) {
            snprintf(line, sizeof line, "If-Modified-Since: %s", req->last_modified);
            bad = bad || !g_curl.slist_append(hdr, line);
        }
        char errbuf[256] = "";
        bad = bad || g_curl.easy_setopt(e, O_URL, url) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_USERAGENT, req->user_agent) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_HTTPHEADER, hdr) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_HTTPGET, 1L) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_NOSIGNAL, 1L) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_FOLLOWLOCATION, 0L) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_CONNECTTIMEOUT_MS, (long)__atomic_load_n(&g_connect_ms, __ATOMIC_RELAXED)) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_TIMEOUT_MS, (long)__atomic_load_n(&g_total_ms, __ATOMIC_RELAXED)) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_MAXFILESIZE_LARGE, (long long)CP0_MAP_HTTP_MAX_BYTES) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_ERRORBUFFER, errbuf) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_HEADERFUNCTION, (curl_data_cb)on_header) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_HEADERDATA, &x) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_WRITEFUNCTION, (curl_data_cb)on_data) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_WRITEDATA, &x) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_NOPROGRESS, 0L) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_XFERINFOFUNCTION, (curl_xferinfo_cb)on_progress) != C_OK;
        bad = bad || g_curl.easy_setopt(e, O_XFERINFODATA, &x) != C_OK;
        bad = bad || set_protocols(e, http) != 0;
        int rc = bad ? -1 : g_curl.easy_perform(e);
        if (hdr) g_curl.slist_free_all(hdr);
        g_curl.easy_setopt(e, O_ERRORBUFFER, (char *)NULL);
        g_curl.easy_setopt(e, O_HTTPHEADER, (struct curl_slist *)NULL);
        if (bad) return CP0_MAP_E_NETWORK;
        resp->curl_code = rc;
        if (x.too_large || rc == C_FILESIZE_EXCEEDED) return CP0_MAP_E_TOO_LARGE;
        if (rc == C_ABORTED_BY_CALLBACK) return CP0_MAP_E_CANCELLED;
        if (rc != C_OK) return CP0_MAP_E_NETWORK;
        long code = 0;
        g_curl.easy_getinfo(e, I_RESPONSE_CODE, &code);
        resp->status = (int)code;
        const int redirect = code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
        if (!redirect) break;
        if (!resp->location[0]) return CP0_MAP_E_HTTP;
        if (hop >= 3) return CP0_MAP_E_HTTP; /* more than 3 redirects */
        char next[2 * CP0_MAP_URL_MAX];
        if (resolve(url, resp->location, next, sizeof next) != CP0_MAP_OK) return CP0_MAP_E_HTTP;
        const int nchk = cp0_map__url_check_concrete(next);
        if (nchk) return nchk == CP0_MAP_E_INVALID ? CP0_MAP_E_HTTP : CP0_MAP_E_REFUSED;
        memcpy(url, next, sizeof url);
        resp->redirects++;
    }
    if (resp->status == 200) {
        const cp0_map_format_t f = cp0_map_format_sniff(resp->body.data, resp->body.len);
        const int png = !strcmp(resp->content_type, "image/png"), jpg = !strcmp(resp->content_type, "image/jpeg");
        if (!(png && f == CP0_MAP_FMT_PNG) && !(jpg && f == CP0_MAP_FMT_JPEG)) return CP0_MAP_E_HTTP;
    }
    return CP0_MAP_OK;
}
