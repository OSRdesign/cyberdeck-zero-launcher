/*
 * SPDX-License-Identifier: MIT
 *
 * Map widget: the tile-load thread and the store fetch (see cp0_map_loader.h).
 */
#include "cp0_map_loader.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct cp0_map_loader {
    pthread_t thread;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int stop;
    cp0_map_cache_t *cache;
    cp0_map_fetch_fn fetch;
    void *user;
    cp0_map_req_t queue[CP0_MAP_LOADER_QUEUE];
    int qn, qhead;               /* pending: queue[qhead .. qn) */
    int busy;                    /* a request is being fetched */
    cp0_map_done_t done[CP0_MAP_LOADER_DONE];
    int dn;
    int overflow;
    uint64_t fetched;
};

static void *worker(void *arg)
{
    cp0_map_loader_t *l = (cp0_map_loader_t *)arg;
    pthread_mutex_lock(&l->mu);
    for (;;) {
        while (!l->stop && l->qhead >= l->qn) pthread_cond_wait(&l->cv, &l->mu);
        if (l->stop) break;
        const cp0_map_req_t req = l->queue[l->qhead++];
        if (!req.force && cp0_map_cache_contains(l->cache, &req.key)) continue; /* drawn already: nothing to tell */
        l->busy = 1;
        pthread_mutex_unlock(&l->mu);

        cp0_map_image_t *img = NULL;
        char archive[CP0_MAP_ID_MAX] = "";
        int stale = 0;
        int rc = l->fetch(l->user, &req, &img, archive, &stale);
        if (rc == CP0_MAP_OK) {
            if (!img) rc = CP0_MAP_E_DECODE;
            else if ((rc = cp0_map_cache_put(l->cache, &req.key, img, NULL)) != CP0_MAP_OK) cp0_map_image_free(img);
        }

        pthread_mutex_lock(&l->mu);
        l->busy = 0;
        l->fetched++;
        if (l->dn < CP0_MAP_LOADER_DONE) {
            cp0_map_done_t *d = &l->done[l->dn++];
            d->req = req;
            d->result = rc;
            d->stale = stale;
            memcpy(d->archive_id, archive, sizeof d->archive_id);
        } else {
            l->overflow = 1;
        }
    }
    pthread_mutex_unlock(&l->mu);
    return NULL;
}

cp0_map_loader_t *cp0_map_loader_new(cp0_map_cache_t *cache, cp0_map_fetch_fn fetch, void *user)
{
    if (!cache || !fetch) return NULL;
    cp0_map_loader_t *l = (cp0_map_loader_t *)calloc(1, sizeof *l);
    if (!l) return NULL;
    l->cache = cache;
    l->fetch = fetch;
    l->user = user;
    if (pthread_mutex_init(&l->mu, NULL) != 0) {
        free(l);
        return NULL;
    }
    if (pthread_cond_init(&l->cv, NULL) != 0 || pthread_create(&l->thread, NULL, worker, l) != 0) {
        pthread_mutex_destroy(&l->mu);
        free(l);
        return NULL;
    }
    return l;
}

void cp0_map_loader_free(cp0_map_loader_t *l)
{
    if (!l) return;
    pthread_mutex_lock(&l->mu);
    l->stop = 1;
    pthread_cond_signal(&l->cv);
    pthread_mutex_unlock(&l->mu);
    pthread_join(l->thread, NULL);
    pthread_cond_destroy(&l->cv);
    pthread_mutex_destroy(&l->mu);
    free(l);
}

void cp0_map_loader_submit(cp0_map_loader_t *l, const cp0_map_req_t *reqs, int n)
{
    if (!l) return;
    if (n < 0 || !reqs) n = 0;
    if (n > CP0_MAP_LOADER_QUEUE) n = CP0_MAP_LOADER_QUEUE;
    pthread_mutex_lock(&l->mu);
    if (n) memcpy(l->queue, reqs, (size_t)n * sizeof *reqs);
    l->qn = n;
    l->qhead = 0;
    if (n) pthread_cond_signal(&l->cv);
    pthread_mutex_unlock(&l->mu);
}

int cp0_map_loader_poll(cp0_map_loader_t *l, cp0_map_done_t *out, int max, int *overflow)
{
    if (overflow) *overflow = 0;
    if (!l || !out || max <= 0) return 0;
    pthread_mutex_lock(&l->mu);
    const int n = l->dn < max ? l->dn : max;
    memcpy(out, l->done, (size_t)n * sizeof *out);
    memmove(l->done, l->done + n, (size_t)(l->dn - n) * sizeof *out);
    l->dn -= n;
    if (overflow) *overflow = l->overflow;
    l->overflow = 0;
    pthread_mutex_unlock(&l->mu);
    return n;
}

int cp0_map_loader_idle(cp0_map_loader_t *l)
{
    if (!l) return 1;
    pthread_mutex_lock(&l->mu);
    const int idle = l->qhead >= l->qn && !l->busy && l->dn == 0;
    pthread_mutex_unlock(&l->mu);
    return idle;
}

uint64_t cp0_map_loader_fetched(cp0_map_loader_t *l)
{
    if (!l) return 0;
    pthread_mutex_lock(&l->mu);
    const uint64_t n = l->fetched;
    pthread_mutex_unlock(&l->mu);
    return n;
}

/* -------------------------------------------------------------------------------------------------- the fetch */

static cp0_map_image_t *nearest(const uint16_t *src, int w, int h, int stride, int out_px)
{
    cp0_map_image_t *o = cp0_map_image_new(out_px, out_px, CP0_MAP_PIX_RGB565);
    if (!o) return NULL;
    for (int y = 0; y < out_px; y++) {
        const uint16_t *s = (const uint16_t *)((const uint8_t *)src + (size_t)((int64_t)y * h / out_px) * (size_t)stride);
        uint16_t *d = (uint16_t *)(o->px + (size_t)y * (size_t)o->stride);
        for (int x = 0; x < out_px; x++) d[x] = s[(int64_t)x * w / out_px];
    }
    return o;
}

static cp0_map_image_t *resample(const uint16_t *src, int w, int stride, int x4, int out_px)
{
    cp0_map_image_t *o = cp0_map_image_new(out_px, out_px, CP0_MAP_PIX_RGB565);
    if (!o) return NULL;
    if (cp0_map_resample_rgb565(src, w, w, stride, x4, 0, 0, out_px, out_px, (uint16_t *)o->px, o->stride)) {
        cp0_map_image_free(o);
        return NULL;
    }
    return o;
}

cp0_map_image_t *cp0_map_scale_to(const uint16_t *src, int w, int h, int stride, int out_px)
{
    if (!src || w <= 0 || h <= 0 || out_px <= 0 || out_px > 4096) return NULL;
    if (w == h) {
        /* out_px / w in quarter steps */
        if ((out_px * 4) % w == 0) {
            const int x4 = out_px * 4 / w;
            if (x4 == 2 || x4 == 3 || x4 == 4 || x4 == 6 || x4 == 8) {
                cp0_map_image_t *o = resample(src, w, stride, x4, out_px);
                if (o) return o;
            } else if (x4 == 12 || x4 == 16) {
                /* an over-zoom quadrant at 1.5x or 2x display scale: 1.5x or 2x first, then 2x */
                const int mid = w * (x4 == 12 ? 6 : 8) / 4;
                cp0_map_image_t *m = resample(src, w, stride, x4 == 12 ? 6 : 8, mid);
                if (m) {
                    cp0_map_image_t *o = resample((const uint16_t *)m->px, mid, m->stride, 8, out_px);
                    cp0_map_image_free(m);
                    if (o) return o;
                }
            }
        }
    }
    return nearest(src, w, h, stride, out_px);
}

int cp0_map_store_fetch(void *user, const cp0_map_req_t *req, cp0_map_image_t **out, char *archive_id, int *stale)
{
    cp0_map_store_fetch_t *f = (cp0_map_store_fetch_t *)user;
    *out = NULL;
    if (stale) *stale = 0;
    const int z = req->over ? req->z - 1 : req->z;
    const uint32_t x = req->over ? req->x >> 1 : req->x, y = req->over ? req->y >> 1 : req->y;
    cp0_map_tile_meta_t meta;
    int rc = cp0_map_store_get_tile(f->store, req->source[0] ? req->source : NULL, z, x, y, &f->buf, &meta);
    if (rc) return rc;
    cp0_map__copy_str(archive_id, CP0_MAP_ID_MAX, meta.archive_id, 0);
    if (stale) *stale = meta.stale;
    cp0_map_image_t *img;
    rc = cp0_map_decode_tile(f->buf.data, f->buf.len, CP0_MAP_PIX_RGB565, 2, &img);
    if (rc) return rc;
    const uint16_t *src = (const uint16_t *)img->px;
    int w = img->w, h = img->h;
    if (req->over) {
        w /= 2;
        h /= 2;
        if (w < 1 || h < 1) {
            cp0_map_image_free(img);
            return CP0_MAP_E_DECODE;
        }
        src = (const uint16_t *)(img->px + (size_t)((req->y & 1) ? h : 0) * (size_t)img->stride) + ((req->x & 1) ? w : 0);
    }
    *out = (w == req->out_px && h == req->out_px && !req->over) ? img : cp0_map_scale_to(src, w, h, img->stride, req->out_px);
    if (*out != img) cp0_map_image_free(img);
    return *out ? CP0_MAP_OK : CP0_MAP_E_NOMEM;
}

void cp0_map_store_fetch_done(cp0_map_store_fetch_t *f)
{
    if (f) cp0_map_buf_free(&f->buf);
}
