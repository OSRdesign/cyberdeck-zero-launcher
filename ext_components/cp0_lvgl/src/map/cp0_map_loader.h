/*
 * SPDX-License-Identifier: MIT
 *
 * The map widget's tile-load thread (src/map; no LVGL): one worker per widget reads, decodes and scales the tiles the
 * view needs into the RAM cache, in the order the widget gives them (the newest view's centre first). The LVGL thread
 * never waits on it: submit() replaces the queue (stale requests of an older view are dropped; the one being loaded
 * finishes), poll() collects what is done. Thread rules: submit / poll / idle / free on one thread (the LVGL thread),
 * the fetch callback on the worker only.
 */
#ifndef CP0_MAP_LOADER_H
#define CP0_MAP_LOADER_H

#include "cp0_map_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_MAP_LOADER_QUEUE 160 /* requests per submit (cp0_map_view_tiles() gives at most CP0_MAP_VIEW_MAX_TILES) */
#define CP0_MAP_LOADER_DONE 256  /* completions kept between two polls (more set the overflow flag) */

typedef struct {
    cp0_map_tile_key_t key;      /* the RAM cache key the result goes under (z = the displayed zoom) */
    int z;
    uint32_t x, y;               /* the displayed tile */
    uint8_t over;                /* over-zoom: read (z - 1, x / 2, y / 2) and magnify its quadrant */
    uint8_t visible;             /* on screen (0: the ring around the view, read ahead from the store only) */
    uint8_t force;               /* load even when the key is cached (a newer tile was fetched) */
    int out_px;                  /* side of the image to produce (the view's tile_px: 256, 384, 512) */
    char source[CP0_MAP_ID_MAX]; /* "" = the first archive of the store that has it */
} cp0_map_req_t;

typedef struct {
    cp0_map_req_t req;
    int result;                  /* CP0_MAP_OK (in the cache now), CP0_MAP_E_NO_TILE, CP0_MAP_E_ARCHIVE_MISSING, ... */
    char archive_id[CP0_MAP_ID_MAX];
    int stale;                   /* a viewing-cache tile past its expiry */
} cp0_map_done_t;

/* Produces the image of one request (worker thread). 0 with *out set (the loader puts it into the cache), or an error;
 * archive_id (CP0_MAP_ID_MAX bytes) receives the archive that answered, *stale 1 for a viewing-cache tile past its
 * expiry. It reads the store only: the network is the fetch pool's (cp0_map_fetch.h). */
typedef int (*cp0_map_fetch_fn)(void *user, const cp0_map_req_t *req, cp0_map_image_t **out, char *archive_id,
                                int *stale);

typedef struct cp0_map_loader cp0_map_loader_t;

/* Starts the worker. cache: where results go (not owned). NULL when out of memory or the thread cannot start. */
cp0_map_loader_t *cp0_map_loader_new(cp0_map_cache_t *cache, cp0_map_fetch_fn fetch, void *user);
/* Stops the worker (after the tile in progress) and frees everything. NULL is ignored. */
void cp0_map_loader_free(cp0_map_loader_t *l);
/* Replaces the queue with reqs[0..n) (first = loaded first; n is cut to CP0_MAP_LOADER_QUEUE). A request whose key is
 * already cached is skipped by the worker without a completion, unless it has `force`. */
void cp0_map_loader_submit(cp0_map_loader_t *l, const cp0_map_req_t *reqs, int n);
/* Moves up to max completions into out; returns the count. *overflow (may be NULL) is set to 1 when completions were
 * dropped since the last poll (the caller then redraws everything). */
int cp0_map_loader_poll(cp0_map_loader_t *l, cp0_map_done_t *out, int max, int *overflow);
/* 1 when nothing is queued, nothing is being loaded and nothing waits to be polled. */
int cp0_map_loader_idle(cp0_map_loader_t *l);
/* Requests fetched since the start (the tests' counter). */
uint64_t cp0_map_loader_fetched(cp0_map_loader_t *l);

/* The fetch of a widget: user = a cp0_map_store_fetch_t. Reads the tile (or the over-zoom parent) from the store,
 * decodes it and scales it to out_px: 256 px tiles by 1x / 1.5x / 2x, 512 px ("@2x") tiles by 0.5x / 0.75x / 1x
 * (cp0_map_resample_rgb565), an over-zoom quadrant by twice that (2x then 2x, 1.5x then 2x), other sizes by nearest. */
typedef struct {
    cp0_map_store_t *store;
    cp0_map_buf_t buf;           /* the worker's read buffer (freed by cp0_map_store_fetch_done) */
} cp0_map_store_fetch_t;

int cp0_map_store_fetch(void *user, const cp0_map_req_t *req, cp0_map_image_t **out, char *archive_id, int *stale);
void cp0_map_store_fetch_done(cp0_map_store_fetch_t *f);
/* Scales a w x h RGB565 picture (src_stride bytes per row) to an out_px square: the exact steps above when the ratio
 * is one of them, else nearest. A new image, or NULL. Exposed for the tests. */
cp0_map_image_t *cp0_map_scale_to(const uint16_t *src, int w, int h, int src_stride, int out_px);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_LOADER_H */
