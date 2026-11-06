/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: the PMTiles v3 reader, written from the specification (CC0, github.com/protomaps/PMTiles spec/v3);
 * cleaned up from the P0 probe of report 055 (docs/dev/reports/055-assets/probe/pmt.c, ours). pread() only, never
 * mmap. Directories: none or gzip compression (inflated by stb_image's zlib, so no zlib dependency); leaf directories
 * kept in a small round-robin cache. Every length, count and offset read from the file is checked before use (the
 * unit test feeds it truncated and mutated files).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* O_CLOEXEC */
#endif
#include "cp0_map_internal.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEADER_LEN 127
#define DIR_MAX_BYTES (16u * 1024 * 1024)    /* one directory, compressed or not */
#define DIR_MAX_ENTRIES (4u * 1024 * 1024)
#define META_MAX_BYTES (1024u * 1024)
#define LEAF_CACHE 16
#define MAX_DEPTH 4

enum { COMP_UNKNOWN = 0, COMP_NONE = 1, COMP_GZIP = 2, COMP_BROTLI = 3, COMP_ZSTD = 4 };
enum { TYPE_UNKNOWN = 0, TYPE_MVT = 1, TYPE_PNG = 2, TYPE_JPEG = 3, TYPE_WEBP = 4, TYPE_AVIF = 5 };

typedef struct {
    uint64_t tile_id, offset;
    uint32_t length, run_length;
} pm_entry_t;

typedef struct {
    pm_entry_t *e;
    size_t n;
} pm_dir_t;

struct cp0_map_pmtiles {
    int fd;
    uint64_t size;
    uint64_t root_off, root_len, meta_off, meta_len, leaf_off, leaf_len, data_off, data_len;
    uint8_t internal_comp, tile_comp, tile_type;
    pm_dir_t root;
    struct {
        uint64_t off;
        pm_dir_t d;
        int used;
    } leaf[LEAF_CACHE];
    int leaf_next;
};

static uint64_t rd64(const uint8_t *b)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | b[i];
    return v;
}

static int32_t rd32s(const uint8_t *b)
{
    const uint32_t v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    int32_t s;
    memcpy(&s, &v, sizeof s);
    return s;
}

static int range_ok(const cp0_map_pmtiles_t *p, uint64_t off, uint64_t len)
{
    return off <= p->size && len <= p->size - off;
}

/* reads [off, off+len) and undoes the internal compression into a new buffer */
static int read_section(cp0_map_pmtiles_t *p, uint64_t off, uint64_t len, size_t max_out, uint8_t **out, size_t *n)
{
    *out = NULL;
    *n = 0;
    if (!range_ok(p, off, len)) return CP0_MAP_E_CORRUPT;
    if (len > DIR_MAX_BYTES) return CP0_MAP_E_TOO_LARGE;
    uint8_t *raw = (uint8_t *)malloc(len ? (size_t)len : 1);
    if (!raw) return CP0_MAP_E_NOMEM;
    int rc = cp0_map__pread_all(p->fd, raw, (size_t)len, off);
    if (rc) {
        free(raw);
        return rc;
    }
    if (p->internal_comp == COMP_GZIP) {
        rc = cp0_map__gunzip(raw, (size_t)len, max_out, out, n);
        free(raw);
        return rc;
    }
    *out = raw;
    *n = (size_t)len;
    return CP0_MAP_OK;
}

static int varint(const uint8_t *b, size_t n, size_t *i, uint64_t *v)
{
    uint64_t r = 0;
    for (int s = 0; s < 64; s += 7) {
        if (*i >= n) return -1;
        const uint8_t c = b[(*i)++];
        r |= (uint64_t)(c & 0x7f) << s;
        if (!(c & 0x80)) {
            *v = r;
            return 0;
        }
    }
    return -1;
}

static int parse_dir(cp0_map_pmtiles_t *p, uint64_t off, uint64_t len, pm_dir_t *d)
{
    d->e = NULL;
    d->n = 0;
    uint8_t *b;
    size_t n;
    int rc = read_section(p, off, len, DIR_MAX_BYTES, &b, &n);
    if (rc) return rc;
    size_t i = 0;
    uint64_t cnt, v;
    /* each entry takes at least 4 bytes of varints */
    if (varint(b, n, &i, &cnt) || cnt > DIR_MAX_ENTRIES || cnt > (n - i) / 4 + 1) {
        free(b);
        return CP0_MAP_E_CORRUPT;
    }
    d->e = (pm_entry_t *)calloc(cnt ? (size_t)cnt : 1, sizeof(pm_entry_t));
    if (!d->e) {
        free(b);
        return CP0_MAP_E_NOMEM;
    }
    d->n = (size_t)cnt;
    uint64_t last = 0;
    for (uint64_t k = 0; k < cnt; k++) {
        if (varint(b, n, &i, &v) || (k > 0 && v == 0) || v > UINT64_MAX - last) goto bad; /* ids strictly increase */
        last += v;
        d->e[k].tile_id = last;
    }
    for (uint64_t k = 0; k < cnt; k++) {
        if (varint(b, n, &i, &v) || v > UINT32_MAX) goto bad;
        d->e[k].run_length = (uint32_t)v;
    }
    for (uint64_t k = 0; k < cnt; k++) {
        if (varint(b, n, &i, &v) || v > UINT32_MAX) goto bad;
        d->e[k].length = (uint32_t)v;
    }
    for (uint64_t k = 0; k < cnt; k++) {
        if (varint(b, n, &i, &v)) goto bad;
        if (v == 0 && k > 0) {
            const uint64_t o = d->e[k - 1].offset + d->e[k - 1].length;
            if (o < d->e[k - 1].offset) goto bad;
            d->e[k].offset = o;
        } else {
            if (v == 0) goto bad; /* the first entry needs an explicit offset */
            d->e[k].offset = v - 1;
        }
    }
    free(b);
    return CP0_MAP_OK;
bad:
    free(b);
    free(d->e);
    d->e = NULL;
    d->n = 0;
    return CP0_MAP_E_CORRUPT;
}

uint64_t cp0_map__pmtiles_tile_id(int z, uint32_t x, uint32_t y)
{
    uint64_t acc = (((uint64_t)1 << (2 * z)) - 1) / 3;
    uint64_t d = 0;
    const uint64_t n = (uint64_t)1 << z;
    uint64_t tx = x, ty = y;
    for (uint64_t s = n / 2; s > 0; s /= 2) {
        const uint64_t rx = (tx & s) ? 1 : 0, ry = (ty & s) ? 1 : 0;
        d += s * s * ((3 * rx) ^ ry);
        if (ry == 0) {
            if (rx == 1) {
                tx = s - 1 - (tx & (s - 1));
                ty = s - 1 - (ty & (s - 1));
            }
            const uint64_t t = tx;
            tx = ty;
            ty = t;
        }
    }
    return acc + d;
}

static const pm_entry_t *find_entry(const pm_dir_t *d, uint64_t id)
{
    size_t lo = 0, hi = d->n;
    while (lo < hi) {
        const size_t m = lo + (hi - lo) / 2;
        if (d->e[m].tile_id <= id)
            lo = m + 1;
        else
            hi = m;
    }
    if (lo == 0) return NULL;
    const pm_entry_t *e = &d->e[lo - 1];
    if (e->tile_id == id) return e;
    if (e->run_length == 0) return e; /* a leaf directory covering id */
    if (id - e->tile_id < e->run_length) return e;
    return NULL;
}

static const pm_dir_t *leaf_dir(cp0_map_pmtiles_t *p, uint64_t off, uint64_t len, int *rc)
{
    for (int i = 0; i < LEAF_CACHE; i++)
        if (p->leaf[i].used && p->leaf[i].off == off) return &p->leaf[i].d;
    if (off > p->leaf_len || len > p->leaf_len - off) {
        *rc = CP0_MAP_E_CORRUPT;
        return NULL;
    }
    const int slot = p->leaf_next;
    p->leaf_next = (p->leaf_next + 1) % LEAF_CACHE;
    if (p->leaf[slot].used) free(p->leaf[slot].d.e);
    p->leaf[slot].used = 0;
    *rc = parse_dir(p, p->leaf_off + off, len, &p->leaf[slot].d);
    if (*rc) return NULL;
    p->leaf[slot].off = off;
    p->leaf[slot].used = 1;
    return &p->leaf[slot].d;
}

/* the first tile entry of zoom z, descending into the first leaf that covers it */
static int sample_entry(cp0_map_pmtiles_t *p, int z, pm_entry_t *found)
{
    const uint64_t first = cp0_map__pmtiles_tile_id(z, 0, 0);
    const uint64_t end = z < 30 ? cp0_map__pmtiles_tile_id(z + 1, 0, 0) : UINT64_MAX;
    const pm_dir_t *d = &p->root;
    for (int depth = 0; depth < MAX_DEPTH; depth++) {
        size_t k = 0;
        while (k + 1 < d->n && d->e[k + 1].tile_id <= first) k++;
        pm_entry_t leaf = {0, 0, 0, 0};
        int have_leaf = 0;
        for (; k < d->n && d->e[k].tile_id < end; k++) {
            const pm_entry_t *e = &d->e[k];
            if (e->run_length > 0) {
                if (e->length && e->tile_id + e->run_length > first) {
                    *found = *e;
                    return 1;
                }
                continue;
            }
            leaf = *e;
            have_leaf = 1;
            break;
        }
        if (!have_leaf) return 0;
        int rc = CP0_MAP_OK;
        d = leaf_dir(p, leaf.offset, leaf.length, &rc);
        if (!d) return 0;
    }
    return 0;
}

void cp0_map__pmtiles_close(cp0_map_pmtiles_t *p)
{
    if (!p) return;
    free(p->root.e);
    for (int i = 0; i < LEAF_CACHE; i++)
        if (p->leaf[i].used) free(p->leaf[i].d.e);
    if (p->fd >= 0) close(p->fd);
    free(p);
}

static void read_meta(cp0_map_pmtiles_t *p, cp0_map_archive_info_t *info)
{
    if (!p->meta_len || p->meta_len > META_MAX_BYTES) return;
    uint8_t *b;
    size_t n;
    if (read_section(p, p->meta_off, p->meta_len, META_MAX_BYTES, &b, &n)) return;
    char tmp[256];
    if (cp0_map__json_string((const char *)b, n, "name", tmp, sizeof tmp) == 0)
        cp0_map__copy_str(info->name, sizeof info->name, tmp, 1);
    if (cp0_map__json_string((const char *)b, n, "attribution", tmp, sizeof tmp) == 0)
        cp0_map__copy_str(info->attribution, sizeof info->attribution, tmp, 1);
    free(b);
}

int cp0_map__pmtiles_open(const char *path, cp0_map_pmtiles_t **out, cp0_map_archive_info_t *info)
{
    *out = NULL;
    cp0_map_pmtiles_t *p = (cp0_map_pmtiles_t *)calloc(1, sizeof *p);
    if (!p) return CP0_MAP_E_NOMEM;
    p->fd = open(path, O_RDONLY | O_CLOEXEC);
    if (p->fd < 0) {
        free(p);
        return CP0_MAP_E_IO;
    }
    struct stat st;
    if (fstat(p->fd, &st) != 0) {
        cp0_map__pmtiles_close(p);
        return CP0_MAP_E_IO;
    }
    p->size = (uint64_t)st.st_size;
    uint8_t h[HEADER_LEN];
    int rc = p->size < HEADER_LEN ? CP0_MAP_E_FORMAT : cp0_map__pread_all(p->fd, h, HEADER_LEN, 0);
    if (rc == CP0_MAP_OK && memcmp(h, "PMTiles", 7) != 0) rc = CP0_MAP_E_FORMAT;
    if (rc == CP0_MAP_OK && h[7] != 3) rc = CP0_MAP_E_UNSUPPORTED;
    if (rc) {
        cp0_map__pmtiles_close(p);
        return rc;
    }
    p->root_off = rd64(h + 8);
    p->root_len = rd64(h + 16);
    p->meta_off = rd64(h + 24);
    p->meta_len = rd64(h + 32);
    p->leaf_off = rd64(h + 40);
    p->leaf_len = rd64(h + 48);
    p->data_off = rd64(h + 56);
    p->data_len = rd64(h + 64);
    p->internal_comp = h[97];
    p->tile_comp = h[98];
    p->tile_type = h[99];
    if (!range_ok(p, p->root_off, p->root_len) || !range_ok(p, p->leaf_off, p->leaf_len) ||
        !range_ok(p, p->data_off, p->data_len) || !p->root_len) {
        cp0_map__pmtiles_close(p);
        return CP0_MAP_E_CORRUPT;
    }
    if (p->internal_comp == COMP_UNKNOWN) p->internal_comp = COMP_NONE;
    if (p->internal_comp != COMP_NONE && p->internal_comp != COMP_GZIP) {
        cp0_map__pmtiles_close(p);
        return CP0_MAP_E_UNSUPPORTED;
    }
    rc = parse_dir(p, p->root_off, p->root_len, &p->root);
    if (rc) {
        cp0_map__pmtiles_close(p);
        return rc;
    }
    info->minzoom = h[100] <= 30 ? h[100] : -1;
    info->maxzoom = h[101] <= 30 ? h[101] : -1;
    if (info->minzoom > info->maxzoom) info->minzoom = info->maxzoom = -1;
    switch (p->tile_type) {
    case TYPE_PNG: info->format = CP0_MAP_FMT_PNG; break;
    case TYPE_JPEG: info->format = CP0_MAP_FMT_JPEG; break;
    case TYPE_WEBP: info->format = CP0_MAP_FMT_WEBP; break;
    case TYPE_MVT: info->format = CP0_MAP_FMT_MVT; break;
    default: info->format = CP0_MAP_FMT_UNKNOWN; break;
    }
    const double w = rd32s(h + 102) / 1e7, s = rd32s(h + 106) / 1e7, e = rd32s(h + 110) / 1e7, n = rd32s(h + 114) / 1e7;
    if (w >= -180.0 && e <= 180.0 && s >= -90.0 && n <= 90.0 && w < e && s < n) {
        info->has_bounds = 1;
        info->bounds.west = w;
        info->bounds.south = s;
        info->bounds.east = e;
        info->bounds.north = n;
    }
    read_meta(p, info);
    *out = p;
    /* the tile size (and the format, when the header does not say) from the first tile of the deepest zoom */
    pm_entry_t en = {0, 0, 0, 0};
    if (info->maxzoom >= 0 && info->format != CP0_MAP_FMT_MVT && sample_entry(p, info->maxzoom, &en)) {
        cp0_map_buf_t b = {0};
        if (en.length <= CP0_MAP_TILE_BYTES_MAX && en.offset <= p->data_len && en.length <= p->data_len - en.offset &&
            cp0_map_buf_reserve(&b, en.length) == CP0_MAP_OK &&
            cp0_map__pread_all(p->fd, b.data, en.length, p->data_off + en.offset) == CP0_MAP_OK) {
            int tw = 0, th = 0;
            cp0_map_format_t f = CP0_MAP_FMT_UNKNOWN;
            if (cp0_map_tile_probe(b.data, en.length, &tw, &th, &f) == CP0_MAP_OK) info->tile_px = tw;
            if (info->format == CP0_MAP_FMT_UNKNOWN) info->format = cp0_map_format_sniff(b.data, en.length);
        }
        cp0_map_buf_free(&b);
    }
    return CP0_MAP_OK;
}

int cp0_map__pmtiles_get(cp0_map_pmtiles_t *p, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out)
{
    const uint64_t id = cp0_map__pmtiles_tile_id(z, x, y);
    const pm_dir_t *d = &p->root;
    for (int depth = 0; depth < MAX_DEPTH; depth++) {
        const pm_entry_t *e = find_entry(d, id);
        if (!e) return CP0_MAP_E_NO_TILE;
        if (e->run_length > 0) {
            if (!e->length) return CP0_MAP_E_NO_TILE;
            if (e->length > CP0_MAP_TILE_BYTES_MAX) return CP0_MAP_E_TOO_LARGE;
            if (e->offset > p->data_len || e->length > p->data_len - e->offset) return CP0_MAP_E_CORRUPT;
            int rc = cp0_map_buf_reserve(out, e->length);
            if (rc) return rc;
            rc = cp0_map__pread_all(p->fd, out->data, e->length, p->data_off + e->offset);
            if (rc) return rc;
            out->len = e->length;
            if (p->tile_comp == COMP_GZIP && out->len >= 2 && out->data[0] == 0x1f && out->data[1] == 0x8b &&
                p->tile_type != TYPE_MVT) {
                uint8_t *raw;
                size_t n;
                rc = cp0_map__gunzip(out->data, out->len, CP0_MAP_TILE_BYTES_MAX, &raw, &n);
                if (rc) return rc;
                rc = cp0_map_buf_reserve(out, n);
                if (rc == CP0_MAP_OK) {
                    memcpy(out->data, raw, n);
                    out->len = n;
                }
                free(raw);
                return rc;
            }
            return CP0_MAP_OK;
        }
        int rc = CP0_MAP_OK;
        d = leaf_dir(p, e->offset, e->length, &rc);
        if (!d) return rc ? rc : CP0_MAP_E_CORRUPT;
    }
    return CP0_MAP_E_CORRUPT;
}
