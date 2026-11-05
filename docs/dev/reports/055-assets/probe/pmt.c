/* SPDX-License-Identifier: MIT
 * P0 spike (report 055): minimal PMTiles v3 reader from the spec (CC0). Throwaway probe code. */
#define _GNU_SOURCE
#include "pmt.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* from stb_image.h (compiled in stbimpl.c) */
int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

static uint64_t rd64(const uint8_t *b) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | b[i]; return v; }

static int read_at(pmt_t *p, uint64_t off, uint64_t len, uint8_t **out, int *owned)
{
    if (off + len > p->size) return -1;
    if (p->use_mmap) { *out = (uint8_t *)p->map + off; *owned = 0; return 0; }
    uint8_t *b = (uint8_t *)malloc(len ? len : 1);
    if (!b) return -1;
    size_t got = 0;
    while (got < len) {
        ssize_t r = pread(p->fd, b + got, len - got, (off_t)(off + got));
        if (r <= 0) { free(b); return -1; }
        got += (size_t)r;
    }
    *out = b; *owned = 1;
    return 0;
}

/* gzip member -> raw bytes (malloc). */
static uint8_t *gunzip(const uint8_t *g, size_t n, size_t *outn)
{
    if (n < 18 || g[0] != 0x1f || g[1] != 0x8b || g[2] != 8) return NULL;
    uint8_t flg = g[3];
    size_t i = 10;
    if (flg & 4) { if (i + 2 > n) return NULL; i += 2 + (g[i] | (g[i + 1] << 8)); }
    if (flg & 8) { while (i < n && g[i]) i++; i++; }
    if (flg & 16) { while (i < n && g[i]) i++; i++; }
    if (flg & 2) i += 2;
    if (i >= n - 8) return NULL;
    uint32_t isize = g[n - 4] | (g[n - 3] << 8) | (g[n - 2] << 16) | ((uint32_t)g[n - 1] << 24);
    uint8_t *o = (uint8_t *)malloc(isize ? isize : 1);
    if (!o) return NULL;
    int r = stbi_zlib_decode_noheader_buffer((char *)o, (int)isize, (const char *)g + i, (int)(n - 8 - i));
    if (r != (int)isize) { free(o); return NULL; }
    *outn = isize;
    return o;
}

static int varint(const uint8_t *b, size_t n, size_t *i, uint64_t *v)
{
    uint64_t r = 0; int s = 0;
    while (*i < n) {
        uint8_t c = b[(*i)++];
        r |= (uint64_t)(c & 0x7f) << s;
        if (!(c & 0x80)) { *v = r; return 0; }
        s += 7;
        if (s > 63) return -1;
    }
    return -1;
}

static int parse_dir(pmt_t *p, uint64_t off, uint64_t len, pmt_dir *d)
{
    uint8_t *raw; int owned;
    if (read_at(p, off, len, &raw, &owned)) return -1;
    p->dir_bytes_read += len;
    size_t n = 0;
    uint8_t *b = NULL;
    if (p->internal_comp == 2) b = gunzip(raw, len, &n);
    else if (p->internal_comp == 1) { b = (uint8_t *)malloc(len); if (b) { memcpy(b, raw, len); n = len; } }
    if (owned) free(raw);
    if (!b) return -1;
    size_t i = 0; uint64_t cnt, v;
    if (varint(b, n, &i, &cnt) || cnt > 50000000) { free(b); return -1; }
    d->e = (pmt_entry *)calloc(cnt ? cnt : 1, sizeof(pmt_entry));
    d->n = cnt;
    uint64_t last = 0;
    for (uint64_t k = 0; k < cnt; k++) { if (varint(b, n, &i, &v)) goto bad; last += v; d->e[k].tile_id = last; }
    for (uint64_t k = 0; k < cnt; k++) { if (varint(b, n, &i, &v)) goto bad; d->e[k].run_length = (uint32_t)v; }
    for (uint64_t k = 0; k < cnt; k++) { if (varint(b, n, &i, &v)) goto bad; d->e[k].length = (uint32_t)v; }
    for (uint64_t k = 0; k < cnt; k++) {
        if (varint(b, n, &i, &v)) goto bad;
        if (v == 0 && k > 0) d->e[k].offset = d->e[k - 1].offset + d->e[k - 1].length;
        else d->e[k].offset = v - 1;
    }
    free(b);
    return 0;
bad:
    free(b); free(d->e); d->e = NULL; d->n = 0;
    return -1;
}

int pmt_open(pmt_t *p, const char *path, int use_mmap)
{
    memset(p, 0, sizeof *p);
    p->fd = open(path, O_RDONLY | O_CLOEXEC);
    if (p->fd < 0) return -1;
    struct stat st;
    if (fstat(p->fd, &st)) return -1;
    p->size = (size_t)st.st_size;
    p->use_mmap = use_mmap;
    if (use_mmap) {
        p->map = (const uint8_t *)mmap(NULL, p->size, PROT_READ, MAP_SHARED, p->fd, 0);
        if (p->map == MAP_FAILED) return -1;
    }
    uint8_t *h; int owned;
    if (read_at(p, 0, 127, &h, &owned)) return -1;
    int ok = memcmp(h, "PMTiles", 7) == 0 && h[7] == 3;
    if (ok) {
        p->root_off = rd64(h + 8); p->root_len = rd64(h + 16);
        p->meta_off = rd64(h + 24); p->meta_len = rd64(h + 32);
        p->leaf_off = rd64(h + 40); p->leaf_len = rd64(h + 48);
        p->data_off = rd64(h + 56); p->data_len = rd64(h + 64);
        p->n_addressed = rd64(h + 72); p->n_entries = rd64(h + 80); p->n_contents = rd64(h + 88);
        p->clustered = h[96]; p->internal_comp = h[97]; p->tile_comp = h[98]; p->tile_type = h[99];
        p->minz = h[100]; p->maxz = h[101];
    }
    if (owned) free(h);
    if (!ok) return -1;
    return parse_dir(p, p->root_off, p->root_len, &p->root);
}

void pmt_close(pmt_t *p)
{
    free(p->root.e);
    for (int i = 0; i < p->nleaf; i++) free(p->leaf[i].d.e);
    if (p->use_mmap && p->map && p->map != MAP_FAILED) munmap((void *)p->map, p->size);
    if (p->fd >= 0) close(p->fd);
    memset(p, 0, sizeof *p);
    p->fd = -1;
}

uint64_t pmt_zxy_to_id(int z, uint32_t x, uint32_t y)
{
    uint64_t acc = (((uint64_t)1 << (2 * z)) - 1) / 3;
    uint64_t d = 0;
    uint64_t n = (uint64_t)1 << z;
    for (uint64_t s = n / 2; s > 0; s /= 2) {
        uint64_t rx = (x & s) ? 1 : 0, ry = (y & s) ? 1 : 0;
        d += s * s * ((3 * rx) ^ ry);
        if (ry == 0) {
            if (rx == 1) { x = (uint32_t)(s - 1 - x); y = (uint32_t)(s - 1 - y); }
            uint32_t t = x; x = y; y = t;
        }
    }
    return acc + d;
}

static const pmt_entry *find_entry(const pmt_dir *d, uint64_t id)
{
    long lo = 0, hi = (long)d->n - 1;
    while (lo <= hi) {
        long m = (lo + hi) / 2;
        if (d->e[m].tile_id < id) lo = m + 1;
        else if (d->e[m].tile_id > id) hi = m - 1;
        else return &d->e[m];
    }
    if (hi >= 0) {
        const pmt_entry *e = &d->e[hi];
        if (e->run_length == 0) return e;
        if (id - e->tile_id < e->run_length) return e;
    }
    return NULL;
}

static const pmt_dir *leaf_dir(pmt_t *p, uint64_t off, uint64_t len)
{
    for (int i = 0; i < p->nleaf; i++) if (p->leaf[i].off == off) return &p->leaf[i].d;
    int slot;
    if (p->nleaf < 32) slot = p->nleaf++;
    else { slot = p->leaf_next; p->leaf_next = (p->leaf_next + 1) % 32; free(p->leaf[slot].d.e); }
    p->leaf[slot].off = off;
    if (parse_dir(p, p->leaf_off + off, len, &p->leaf[slot].d)) { p->leaf[slot].d.e = NULL; p->leaf[slot].d.n = 0; return NULL; }
    return &p->leaf[slot].d;
}

long pmt_find(pmt_t *p, int z, uint32_t x, uint32_t y, uint64_t *off)
{
    uint64_t id = pmt_zxy_to_id(z, x, y);
    const pmt_dir *d = &p->root;
    for (int depth = 0; depth < 4; depth++) {
        const pmt_entry *e = find_entry(d, id);
        if (!e) return 0;
        if (e->run_length > 0) { *off = p->data_off + e->offset; return (long)e->length; }
        d = leaf_dir(p, e->offset, e->length);
        if (!d) return -1;
    }
    return -1;
}

long pmt_get(pmt_t *p, int z, uint32_t x, uint32_t y, uint8_t *buf, size_t cap)
{
    uint64_t off;
    long n = pmt_find(p, z, x, y, &off);
    if (n <= 0) return n;
    if ((size_t)n > cap || off + (uint64_t)n > p->size) return -1;
    if (p->use_mmap) { memcpy(buf, p->map + off, (size_t)n); return n; }
    size_t got = 0;
    while (got < (size_t)n) {
        ssize_t r = pread(p->fd, buf + got, (size_t)n - got, (off_t)(off + got));
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return n;
}
