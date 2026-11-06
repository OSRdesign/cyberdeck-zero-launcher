/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: result texts, tile formats, byte buffers, gzip, JSON strings, ids and small file helpers.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* fsync, O_CLOEXEC */
#endif
#include "cp0_map_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

const char *cp0_map_strerror(int code)
{
    switch (code) {
    case CP0_MAP_OK: return "ok";
    case CP0_MAP_E_INVALID: return "invalid argument";
    case CP0_MAP_E_NOMEM: return "out of memory";
    case CP0_MAP_E_IO: return "read or write error";
    case CP0_MAP_E_NO_TILE: return "no tile here";
    case CP0_MAP_E_ARCHIVE_MISSING: return "archive missing";
    case CP0_MAP_E_FORMAT: return "not a map archive or tile";
    case CP0_MAP_E_CORRUPT: return "damaged or truncated file";
    case CP0_MAP_E_UNSUPPORTED: return "not supported (vector or WebP tiles, compression)";
    case CP0_MAP_E_SQLITE_MISSING: return "SQLite library missing (install libsqlite3-0)";
    case CP0_MAP_E_SQLITE: return "SQLite error";
    case CP0_MAP_E_DECODE: return "picture could not be decoded";
    case CP0_MAP_E_TOO_LARGE: return "too large";
    case CP0_MAP_E_READ_ONLY: return "map store is read-only";
    case CP0_MAP_E_EXISTS: return "already in the map store";
    case CP0_MAP_E_NOT_FOUND: return "not found";
    case CP0_MAP_E_CANCELLED: return "cancelled";
    case CP0_MAP_E_FULL: return "tile cache full";
    case CP0_MAP_E_REFUSED: return "refused by the source policy";
    case CP0_MAP_E_NETWORK: return "no connection to the tile server";
    case CP0_MAP_E_CURL_MISSING: return "network unavailable: library missing (install libcurl4t64)";
    case CP0_MAP_E_BLOCKED: return "source paused: the server refused tiles";
    case CP0_MAP_E_CAP_REACHED: return "daily tile limit reached until tomorrow";
    case CP0_MAP_E_NO_KEY: return "this source needs an API key";
    case CP0_MAP_E_DISABLED: return "source switched off";
    case CP0_MAP_E_OFFLINE: return "offline: saved tiles only";
    case CP0_MAP_E_BACKOFF: return "server busy: retrying later";
    case CP0_MAP_E_NOT_IN_VERSION: return "not in this version";
    case CP0_MAP_E_CACHE_FULL: return "map cache full";
    case CP0_MAP_E_HTTP: return "unexpected answer from the tile server";
    case CP0_MAP_E_NO_USER_AGENT: return "no User-Agent set: fetching refused";
    default: return "unknown error";
    }
}

const char *cp0_map_format_name(cp0_map_format_t fmt)
{
    switch (fmt) {
    case CP0_MAP_FMT_PNG: return "png";
    case CP0_MAP_FMT_JPEG: return "jpg";
    case CP0_MAP_FMT_WEBP: return "webp";
    case CP0_MAP_FMT_MVT: return "pbf";
    default: return "";
    }
}

cp0_map_format_t cp0_map_format_parse(const char *s)
{
    if (!s) return CP0_MAP_FMT_UNKNOWN;
    if (!strcasecmp(s, "png")) return CP0_MAP_FMT_PNG;
    if (!strcasecmp(s, "jpg") || !strcasecmp(s, "jpeg")) return CP0_MAP_FMT_JPEG;
    if (!strcasecmp(s, "webp")) return CP0_MAP_FMT_WEBP;
    if (!strcasecmp(s, "pbf") || !strcasecmp(s, "mvt")) return CP0_MAP_FMT_MVT;
    return CP0_MAP_FMT_UNKNOWN;
}

cp0_map_format_t cp0_map_format_sniff(const uint8_t *d, size_t n)
{
    if (!d) return CP0_MAP_FMT_UNKNOWN;
    if (n >= 8 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return CP0_MAP_FMT_PNG;
    if (n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff) return CP0_MAP_FMT_JPEG;
    if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return CP0_MAP_FMT_WEBP;
    /* vector tiles: usually gzip'd protobuf; a raw MVT starts with field 3 (layers), wire type 2 */
    if (n >= 2 && d[0] == 0x1f && d[1] == 0x8b) return CP0_MAP_FMT_MVT;
    if (n >= 1 && d[0] == 0x1a) return CP0_MAP_FMT_MVT;
    return CP0_MAP_FMT_UNKNOWN;
}

void cp0_map_buf_free(cp0_map_buf_t *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

int cp0_map_buf_reserve(cp0_map_buf_t *b, size_t need)
{
    if (!b) return CP0_MAP_E_INVALID;
    if (need <= b->cap && b->data) return CP0_MAP_OK;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) return CP0_MAP_E_NOMEM;
        cap *= 2;
    }
    uint8_t *p = (uint8_t *)realloc(b->data, cap);
    if (!p) return CP0_MAP_E_NOMEM;
    b->data = p;
    b->cap = cap;
    return CP0_MAP_OK;
}

/* ------------------------------------------------------------------------------------------------------- gzip */

int cp0_map__gunzip(const uint8_t *g, size_t n, size_t max_out, uint8_t **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    if (n < 18 || g[0] != 0x1f || g[1] != 0x8b || g[2] != 8) return CP0_MAP_E_CORRUPT;
    const uint8_t flg = g[3];
    if (flg & 0xe0) return CP0_MAP_E_CORRUPT;
    size_t i = 10;
    if (flg & 4) { /* FEXTRA */
        if (i + 2 > n) return CP0_MAP_E_CORRUPT;
        i += 2 + (size_t)(g[i] | (g[i + 1] << 8));
    }
    if (flg & 8) { /* FNAME */
        while (i < n && g[i]) i++;
        i++;
    }
    if (flg & 16) { /* FCOMMENT */
        while (i < n && g[i]) i++;
        i++;
    }
    if (flg & 2) i += 2; /* FHCRC */
    if (i > n - 8) return CP0_MAP_E_CORRUPT;
    const uint32_t isize = (uint32_t)g[n - 4] | ((uint32_t)g[n - 3] << 8) | ((uint32_t)g[n - 2] << 16) |
                           ((uint32_t)g[n - 1] << 24);
    const size_t in_len = n - 8 - i;
    /* deflate cannot expand more than about 1032:1; a bigger ISIZE is a lie */
    if ((uint64_t)isize > (uint64_t)in_len * 1032u + 64u) return CP0_MAP_E_CORRUPT;
    if (isize > max_out || isize > INT_MAX || in_len > INT_MAX) return CP0_MAP_E_TOO_LARGE;
    uint8_t *o = (uint8_t *)malloc(isize ? isize : 1);
    if (!o) return CP0_MAP_E_NOMEM;
    const int r = cp0_map__inflate_raw(o, (int)isize, g + i, (int)in_len);
    if (r != (int)isize) {
        free(o);
        return CP0_MAP_E_CORRUPT;
    }
    *out = o;
    *out_len = isize;
    return CP0_MAP_OK;
}

/* ------------------------------------------------------------------------------------------------- strings, ids */

int cp0_map__fmt(char *buf, size_t size, const char *fmt, ...)
{
    if (!buf || !size) return CP0_MAP_E_INVALID;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return (n < 0 || (size_t)n >= size) ? CP0_MAP_E_INVALID : CP0_MAP_OK;
}

void cp0_map__copy_str(char *dst, size_t size, const char *src, int clean)
{
    if (!dst || !size) return;
    size_t i = 0;
    if (src)
        for (; src[i] && i + 1 < size; i++) {
            const unsigned char c = (unsigned char)src[i];
            dst[i] = (clean && (c < 0x20 || c == 0x7f)) ? ' ' : (char)c;
        }
    dst[i] = '\0';
}

void cp0_map__make_id(const char *name, char *out, size_t size)
{
    size_t o = 0;
    int dash = 0;
    if (!out || size < 2) return;
    for (const char *p = name ? name : ""; *p && o + 1 < size && o + 1 < CP0_MAP_ID_MAX; p++) {
        const unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '_' || (c == '.' && o > 0)) {
            if (dash && o > 0 && o + 2 < size && o + 2 < CP0_MAP_ID_MAX) out[o++] = '-';
            dash = 0;
            out[o++] = (char)tolower(c);
        } else {
            dash = 1;
        }
    }
    while (o > 0 && (out[o - 1] == '.' || out[o - 1] == '-')) o--;
    out[o] = '\0';
    if (!o) cp0_map__copy_str(out, size, "map", 0);
}

int cp0_map__id_valid(const char *id)
{
    if (!id || !id[0] || id[0] == '.' || strlen(id) >= CP0_MAP_ID_MAX) return 0;
    for (const char *p = id; *p; p++) {
        const unsigned char c = (unsigned char)*p;
        if (!(isdigit(c) || (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

uint32_t cp0_map_source_hash(const char *id)
{
    if (!id) return 0;
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)id; *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------------------------------------------------- JSON */

typedef struct {
    const char *s;
    size_t n, i;
    int depth;
} json_t;

static void json_ws(json_t *j)
{
    while (j->i < j->n && (j->s[j->i] == ' ' || j->s[j->i] == '\t' || j->s[j->i] == '\n' || j->s[j->i] == '\r')) j->i++;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void put_utf8(char *out, size_t size, size_t *o, uint32_t cp)
{
    char tmp[4];
    int n;
    if (cp < 0x80) { tmp[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { tmp[0] = (char)(0xc0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
    else if (cp < 0x10000) {
        tmp[0] = (char)(0xe0 | (cp >> 12)); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        tmp[2] = (char)(0x80 | (cp & 0x3f)); n = 3;
    } else {
        tmp[0] = (char)(0xf0 | (cp >> 18)); tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
        tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); tmp[3] = (char)(0x80 | (cp & 0x3f)); n = 4;
    }
    if (out && *o + (size_t)n < size) {
        memcpy(out + *o, tmp, (size_t)n);
        *o += (size_t)n;
    }
}

static int json_u4(json_t *j, uint32_t *v)
{
    if (j->i + 4 > j->n) return -1;
    uint32_t r = 0;
    for (int k = 0; k < 4; k++) {
        const int h = hexval(j->s[j->i + (size_t)k]);
        if (h < 0) return -1;
        r = (r << 4) | (uint32_t)h;
    }
    j->i += 4;
    *v = r;
    return 0;
}

/* a string at j->i (the opening quote); decoded into out (may be NULL) */
static int json_str(json_t *j, char *out, size_t size)
{
    size_t o = 0;
    if (j->i >= j->n || j->s[j->i] != '"') return -1;
    j->i++;
    while (j->i < j->n) {
        const char c = j->s[j->i++];
        if (c == '"') {
            if (out && size) out[o] = '\0';
            return 0;
        }
        if ((unsigned char)c < 0x20) return -1;
        if (c != '\\') {
            if (out && o + 1 < size) out[o++] = c;
            continue;
        }
        if (j->i >= j->n) return -1;
        const char e = j->s[j->i++];
        uint32_t cp;
        switch (e) {
        case '"': case '\\': case '/': cp = (uint32_t)(unsigned char)e; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'n': cp = '\n'; break;
        case 'r': cp = '\r'; break;
        case 't': cp = '\t'; break;
        case 'u':
            if (json_u4(j, &cp)) return -1;
            if (cp >= 0xd800 && cp < 0xdc00) { /* a surrogate pair */
                uint32_t lo;
                if (j->i + 2 > j->n || j->s[j->i] != '\\' || j->s[j->i + 1] != 'u') return -1;
                j->i += 2;
                if (json_u4(j, &lo) || lo < 0xdc00 || lo > 0xdfff) return -1;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
            } else if (cp >= 0xdc00 && cp < 0xe000) {
                return -1;
            }
            break;
        default: return -1;
        }
        put_utf8(out, size, &o, cp);
    }
    return -1;
}

static int json_skip(json_t *j);

static int json_skip_container(json_t *j, char close)
{
    if (++j->depth > 64) return -1;
    j->i++;
    json_ws(j);
    if (j->i < j->n && j->s[j->i] == close) {
        j->i++;
        j->depth--;
        return 0;
    }
    for (;;) {
        if (close == '}') {
            if (json_str(j, NULL, 0)) return -1;
            json_ws(j);
            if (j->i >= j->n || j->s[j->i] != ':') return -1;
            j->i++;
        }
        if (json_skip(j)) return -1;
        json_ws(j);
        if (j->i >= j->n) return -1;
        if (j->s[j->i] == ',') {
            j->i++;
            json_ws(j);
            continue;
        }
        if (j->s[j->i] == close) {
            j->i++;
            j->depth--;
            return 0;
        }
        return -1;
    }
}

static int json_skip(json_t *j)
{
    json_ws(j);
    if (j->i >= j->n) return -1;
    const char c = j->s[j->i];
    if (c == '"') return json_str(j, NULL, 0);
    if (c == '{') return json_skip_container(j, '}');
    if (c == '[') return json_skip_container(j, ']');
    /* numbers, true, false, null: a run of their characters */
    const size_t start = j->i;
    while (j->i < j->n && (isalnum((unsigned char)j->s[j->i]) || j->s[j->i] == '-' || j->s[j->i] == '+' ||
                           j->s[j->i] == '.'))
        j->i++;
    return j->i > start ? 0 : -1;
}

int cp0_map__json_string(const char *json, size_t len, const char *key, char *out, size_t size)
{
    if (!json || !key || !out || !size) return -1;
    out[0] = '\0';
    json_t j = {json, len, 0, 0};
    json_ws(&j);
    if (j.i >= j.n || j.s[j.i] != '{') return -1;
    j.i++;
    json_ws(&j);
    if (j.i < j.n && j.s[j.i] == '}') return -1;
    char name[64];
    for (;;) {
        json_ws(&j);
        if (json_str(&j, name, sizeof name)) return -1;
        json_ws(&j);
        if (j.i >= j.n || j.s[j.i] != ':') return -1;
        j.i++;
        json_ws(&j);
        if (!strcmp(name, key)) {
            if (j.i < j.n && j.s[j.i] == '"') return json_str(&j, out, size);
            return -1;
        }
        if (json_skip(&j)) return -1;
        json_ws(&j);
        if (j.i >= j.n || j.s[j.i] != ',') return -1;
        j.i++;
    }
}

/* ------------------------------------------------------------------------------------------------------- files */

int cp0_map__pread_all(int fd, void *buf, size_t len, uint64_t off)
{
    size_t got = 0;
    while (got < len) {
        if (off + got > (uint64_t)INT64_MAX) return CP0_MAP_E_CORRUPT;
        const ssize_t r = pread(fd, (uint8_t *)buf + got, len - got, (off_t)(off + got));
        if (r < 0) {
            if (errno == EINTR) continue;
            return CP0_MAP_E_IO;
        }
        if (r == 0) return CP0_MAP_E_CORRUPT;
        got += (size_t)r;
    }
    return CP0_MAP_OK;
}

int cp0_map__mkdirs(const char *path, mode_t mode)
{
    char tmp[CP0_MAP_PATH_MAX];
    if (cp0_map__fmt(tmp, sizeof tmp, "%s", path)) return CP0_MAP_E_INVALID;
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, mode) != 0 && errno != EEXIST) return CP0_MAP_E_IO;
        *p = '/';
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return CP0_MAP_E_IO;
    struct stat st;
    return (stat(tmp, &st) == 0 && S_ISDIR(st.st_mode)) ? CP0_MAP_OK : CP0_MAP_E_IO;
}

int cp0_map__write_atomic(const char *path, const char *data, size_t len)
{
    char tmp[CP0_MAP_PATH_MAX + 32];
    if (cp0_map__fmt(tmp, sizeof tmp, "%s.tmp.%ld", path, (long)getpid())) return CP0_MAP_E_INVALID;
    const int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return CP0_MAP_E_IO;
    size_t done = 0;
    while (done < len) {
        const ssize_t w = write(fd, data + done, len - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp);
            return CP0_MAP_E_IO;
        }
        done += (size_t)w;
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        unlink(tmp);
        return CP0_MAP_E_IO;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return CP0_MAP_E_IO;
    }
    return CP0_MAP_OK;
}
