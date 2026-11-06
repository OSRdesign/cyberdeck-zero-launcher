/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map_tile.h: decoded tiles for the map engine: PNG / JPEG decode (stb_image), the pixel scalers of the display
 * scale and the RAM tile cache (report 054 section 3.2, measured in report 055).
 *
 * IMAGES. A cp0_map_image_t is one allocation (header + pixels) from cp0_map_image_new() or cp0_map_decode_tile().
 * RGB565 pixels are uint16_t in native byte order (LVGL's RGB565); ARGB8888 pixels are uint32_t 0xAARRGGBB (LVGL's
 * ARGB8888 byte order on little-endian), straight alpha.
 *
 * DECODE. cp0_map_decode_tile() is synchronous and thread-safe (any number of threads may decode at once; no shared
 * state): call it from a tile-load thread, never on the LVGL thread for a whole view. It refuses pictures over
 * CP0_MAP_DECODE_MAX_PX on a side and stops a decode that would allocate more than CP0_MAP_DECODE_MEM_CEILING (a
 * damaged or hostile PNG cannot blow up the app). RGB565 output: transparent pixels are composited over white;
 * conversion truncates (r >> 3, g >> 2, b >> 3), as LVGL does. ARGB8888 output keeps the alpha. stb_image 2.30 is built
 * with RSS Reader's flags (PNG + JPEG only, no SIMD: the code tested on the PC is the code run on the board) and
 * STB_IMAGE_STATIC (none of its symbols leave the engine).
 *
 * SCALERS (RGB565 in, RGB565 out, no allocation, any destination rectangle of the magnified tile):
 *   1x   a copy;
 *   2x   nearest: dst(X, Y) = src(X / 2, Y / 2) (0.8 ms per 256 px tile on the Zero 2 W, report 055);
 *   1.5x the fixed-phase path: dst(X, Y) samples src at (2X/3, 2Y/3), i.e. every 2 source pixels give 3, at the phases
 *        0, 2/3 and 4/3. Linear interpolation per 5/6/5 channel with the weights 1/3 and 2/3 quantised to 85 and 171
 *        of 256, rounded: mix(a, b, w) = (a * (256 - w) + b * w + 128) >> 8. Horizontal first, each pass rounded to the
 *        channel's bits, then vertical with the same rule on the horizontal results. Columns: X = 3j -> p[2j];
 *        X = 3j+1 -> mix(p[2j], p[2j+1], 171); X = 3j+2 -> mix(p[2j+1], p[2j+2], 85), with p[2j+2] clamped to the last
 *        column; rows alike. Source width and height must be even (tiles are 256 or 512). (6.3 ms per 256 px tile on
 *        the Zero 2 W against 10-18 ms for a generic bilinear, report 055.) The unit test checks this definition
 *        pixel for pixel against a plain reference.
 *
 * RAM CACHE. A least-recently-used cache of decoded tiles with a byte budget (the pixel bytes of its images), guarded
 * by a mutex: any thread may get, put and release. A hit returns the image PINNED: it stays valid (even if evicted or
 * replaced meanwhile) until cp0_map_cache_release(). Eviction takes the least recently used unpinned tiles; pinned
 * images that were evicted still count against the budget until released, so the bytes held never exceed it. A put
 * that cannot fit because everything else is pinned fails with CP0_MAP_E_FULL (the caller keeps and frees its image).
 * Default budgets (cp0_map_cache_default_budget(), from board.state ram_mb; report 054 3.2 and 055 3.4):
 *   ram_mb unknown or < 768 (the 512 MB boards)   8 MiB = 64 tiles of 256 px RGB565 at 1x (16 at 2x); keeps a kind A
 *                                                 app under the 25 MB VmRSS rule on the Zero 2 W (055: 9.1 MB probe)
 *   768 .. 3071 (Pi 4B, HackBerry CM5)            24 MiB
 *   3072 and more (Pi 5, the FZ-M1 tablet)        48 MiB
 * Never ARGB8888 or 128 tiles on the 512 MB boards (055: both fail the 25 MB rule). A second app (Mesh Hop) passes a
 * smaller budget (3 MiB, 055 recommendation 8).
 *
 * Task 2 (the LVGL widget) builds on these: a tile-load thread that reads (cp0_map_store_get_tile), decodes and puts;
 * the widget gets (pinned) for drawing and releases after the frame. The parent tile magnified as a placeholder,
 * newest-first queues and overlays are the widget's.
 */
#ifndef CP0_MAP_TILE_H
#define CP0_MAP_TILE_H

#include "cp0_map_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_MAP_DECODE_MAX_PX 1024                    /* largest decoded side (tiles are 256 or 512) */
#define CP0_MAP_DECODE_MEM_CEILING (24u * 1024 * 1024) /* per decode, stb_image's own buffers */
#define CP0_MAP_TILE_BYTES_MAX (4u * 1024 * 1024)     /* largest encoded tile the readers and the decoder accept */

typedef enum {
    CP0_MAP_PIX_RGB565 = 0,     /* 2 bytes per pixel: the default (half the RAM) */
    CP0_MAP_PIX_ARGB8888 = 1,   /* 4 bytes per pixel, with alpha */
} cp0_map_pixfmt_t;

typedef struct {
    int w, h;                   /* pixels */
    int stride;                 /* bytes per row (w * bytes per pixel) */
    cp0_map_pixfmt_t fmt;
    uint8_t *px;                /* the pixels, inside the same allocation */
} cp0_map_image_t;

/* A new w x h image (uninitialised pixels) or NULL. w, h 1..4096. */
cp0_map_image_t *cp0_map_image_new(int w, int h, cp0_map_pixfmt_t fmt);
/* Frees an image from cp0_map_image_new() / cp0_map_decode_tile() that is NOT in a cache. NULL is ignored. */
void cp0_map_image_free(cp0_map_image_t *img);
/* The bytes the RAM cache counts for it: w * h * bytes per pixel. */
size_t cp0_map_image_bytes(const cp0_map_image_t *img);

/* Reads only the header: format and size of an encoded tile. 0, CP0_MAP_E_INVALID, CP0_MAP_E_FORMAT (not PNG / JPEG;
 * CP0_MAP_E_UNSUPPORTED for WebP / MVT), CP0_MAP_E_DECODE (a bad header). */
int cp0_map_tile_probe(const uint8_t *data, size_t len, int *w, int *h, cp0_map_format_t *fmt);

/* Decodes a PNG or JPEG tile into a new image of `pixfmt`, magnified by scale_x2 / 2 (2 = 1x, 3 = 1.5x, 4 = 2x; 0 is
 * 1x; magnification is RGB565 only, and 1.5x needs even sides). *out is set on success, NULL otherwise.
 * 0, CP0_MAP_E_INVALID, CP0_MAP_E_FORMAT, CP0_MAP_E_UNSUPPORTED, CP0_MAP_E_TOO_LARGE, CP0_MAP_E_DECODE, CP0_MAP_E_NOMEM. */
int cp0_map_decode_tile(const uint8_t *data, size_t len, cp0_map_pixfmt_t pixfmt, int scale_x2, cp0_map_image_t **out);

/* Writes the rectangle (dx, dy, dw, dh) of `src` (sw x sh RGB565, src_stride bytes per row) magnified by scale_x2 / 2
 * (2, 3 or 4; the magnified tile is sw * scale_x2 / 2 px wide) into `dst` (dst_stride bytes per row; the rectangle's
 * top-left pixel goes to dst[0]). No allocation; the rules above. 0 or CP0_MAP_E_INVALID (bad sizes, a rectangle
 * outside the magnified tile, odd sides for 1.5x, sides over CP0_MAP_DECODE_MAX_PX). */
int cp0_map_scale_rgb565(const uint16_t *src, int sw, int sh, int src_stride, int scale_x2, int dx, int dy, int dw,
                         int dh, uint16_t *dst, int dst_stride);

/* ------------------------------------------------------------------------------------------------- RAM cache */

typedef struct cp0_map_cache cp0_map_cache_t;

/* The identity of a cached picture: which source, which tile, and how it was decoded (variant). */
typedef struct {
    uint32_t source;            /* cp0_map_source_hash() of the archive or source id */
    uint32_t x, y;
    uint8_t z;
    uint8_t variant;            /* cp0_map_tile_variant(): the scale and pixel format of the decode */
} cp0_map_tile_key_t;

uint32_t cp0_map_source_hash(const char *id);                        /* FNV-1a 32 of the id; 0 for NULL */
uint8_t cp0_map_tile_variant(int scale_x2, cp0_map_pixfmt_t pixfmt); /* (scale_x2 << 4) | pixfmt */

typedef struct {
    uint64_t hits, misses;      /* cp0_map_cache_get() answers */
    uint64_t inserts;           /* successful puts */
    uint64_t evictions;         /* tiles dropped to make room (or by a smaller budget) */
    uint64_t rejected;          /* puts refused: CP0_MAP_E_FULL or CP0_MAP_E_TOO_LARGE */
    size_t bytes;               /* pixel bytes held now: cached + evicted-but-pinned (never above budget) */
    size_t peak_bytes;          /* the most `bytes` has been */
    size_t budget;
    int entries;                /* tiles in the cache */
    int pinned;                 /* images handed out and not yet released (cached or evicted) */
} cp0_map_cache_stats_t;

/* The default budget for a board with ram_mb MB of RAM (table above); ram_mb <= 0 (unknown) gives the smallest. */
size_t cp0_map_cache_default_budget(int ram_mb);
/* The default budget of this board: board.state ram_mb through cp0_app_board_read() (the launcher's file); without a
 * live launcher, the RAM sysconf() reports. */
size_t cp0_map_cache_budget_from_board(void);

/* A new cache of `budget_bytes` (0: cp0_map_cache_budget_from_board()). NULL when out of memory. */
cp0_map_cache_t *cp0_map_cache_new(size_t budget_bytes);
/* Frees the cache and every image in it, pinned ones too: release them first. NULL is ignored. */
void cp0_map_cache_free(cp0_map_cache_t *cache);

/* Looks a tile up. A hit returns the image pinned and makes it the most recently used; a miss returns NULL. */
const cp0_map_image_t *cp0_map_cache_get(cp0_map_cache_t *cache, const cp0_map_tile_key_t *key);
/* 1 when the tile is cached (no pin, no statistics, the order unchanged): for a load queue's duplicate check. */
int cp0_map_cache_contains(cp0_map_cache_t *cache, const cp0_map_tile_key_t *key);
/* Adds `img` (from cp0_map_image_new() / cp0_map_decode_tile()) under `key`, evicting least recently used tiles as
 * needed; a tile already cached under the key is replaced. On success the cache owns `img`; with `pinned` non-NULL
 * the image comes back pinned in *pinned (release it). On failure the caller still owns `img`:
 * CP0_MAP_E_TOO_LARGE (bigger than the whole budget), CP0_MAP_E_FULL (the rest is pinned), CP0_MAP_E_INVALID. */
int cp0_map_cache_put(cp0_map_cache_t *cache, const cp0_map_tile_key_t *key, cp0_map_image_t *img,
                      const cp0_map_image_t **pinned);
/* Unpins an image from get / put. An evicted or replaced image is freed at its last release. NULL is ignored. */
void cp0_map_cache_release(cp0_map_cache_t *cache, const cp0_map_image_t *img);
/* Drops every tile of one source (an archive removed or changed); 0 = every source. Pinned images live on until
 * released. Returns the number of tiles dropped. */
int cp0_map_cache_drop(cp0_map_cache_t *cache, uint32_t source);
/* A new budget; evicts down to it (pinned images may keep `bytes` above it until released). */
void cp0_map_cache_set_budget(cp0_map_cache_t *cache, size_t budget_bytes);
void cp0_map_cache_get_stats(cp0_map_cache_t *cache, cp0_map_cache_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_TILE_H */
