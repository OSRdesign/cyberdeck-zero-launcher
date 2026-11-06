/* SPDX-License-Identifier: MIT
 * P0 spike (report 055): a minimal PMTiles v3 reader written from the spec (CC0), throwaway.
 * Directories are gzip; inflated with stb_image's zlib (stbi_zlib_decode_noheader_buffer). Reads by pread or mmap. */
#ifndef P0_PMT_H
#define P0_PMT_H
#include <stddef.h>
#include <stdint.h>

typedef struct { uint64_t tile_id, offset; uint32_t length, run_length; } pmt_entry;
typedef struct { pmt_entry *e; size_t n; } pmt_dir;

typedef struct {
    int fd;
    int use_mmap;
    const uint8_t *map;
    size_t size;
    uint64_t root_off, root_len, meta_off, meta_len, leaf_off, leaf_len, data_off, data_len;
    uint64_t n_addressed, n_entries, n_contents;
    uint8_t clustered, internal_comp, tile_comp, tile_type, minz, maxz;
    pmt_dir root;
    struct { uint64_t off; pmt_dir d; } leaf[32];
    int nleaf, leaf_next;
    uint64_t dir_bytes_read;
} pmt_t;

int pmt_open(pmt_t *p, const char *path, int use_mmap);   /* 0 ok */
void pmt_close(pmt_t *p);
uint64_t pmt_zxy_to_id(int z, uint32_t x, uint32_t y);
/* Finds a tile. On success returns its length and sets *off (absolute file offset). 0 = absent, -1 = error. */
long pmt_find(pmt_t *p, int z, uint32_t x, uint32_t y, uint64_t *off);
/* Reads (pread) or copies (mmap) the tile into buf (cap bytes). Returns length, 0 absent, -1 error. */
long pmt_get(pmt_t *p, int z, uint32_t x, uint32_t y, uint8_t *buf, size_t cap);
#endif
