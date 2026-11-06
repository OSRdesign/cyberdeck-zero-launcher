/*
 * SPDX-License-Identifier: MIT
 *
 * Map widget, the tile-load thread (src/map/cp0_map_loader.h), with a fake fetch and with the real store fetch:
 *   - order: tiles are fetched in the order submitted (the view's centre first), each once, results in the cache;
 *   - cancellation: a new submit drops the rest of the old queue (at most the tile in progress finishes);
 *   - cached keys are skipped without a fetch, errors come back as completions, the completion buffer's overflow;
 *   - stress: rapid submits and polls from the "LVGL thread" while the worker runs, freeing it while busy (built with
 *     ThreadSanitizer by run_tests.sh as well);
 *   - the store fetch: 256 px tiles at 1x / 1.5x / 2x, the over-zoom quadrant, a 512 px archive at 0.5x / 0.75x / 1x,
 *     a missing tile, all compared with a direct decode + resample.
 */
#include "map_test_util.h"

#include "cp0_map_loader.h"

#include <pthread.h>

typedef struct {
    pthread_mutex_t mu;
    int order[4096];
    int n;
    int delay_us;
    int fail_every;          /* every Nth x answers NO_TILE (0: never) */
} fake_t;

static int fake_fetch(void *user, const cp0_map_req_t *req, cp0_map_image_t **out, char *archive_id, int *stale)
{
    fake_t *f = (fake_t *)user;
    (void)stale;
    pthread_mutex_lock(&f->mu);
    if (f->n < 4096) f->order[f->n++] = (int)req->x;
    const int delay = f->delay_us, fail = f->fail_every;
    pthread_mutex_unlock(&f->mu);
    if (delay) usleep((useconds_t)delay);
    if (fail && req->x % (uint32_t)fail == 0) return CP0_MAP_E_NO_TILE;
    cp0_map_image_t *img = cp0_map_image_new(8, 8, CP0_MAP_PIX_RGB565);
    if (!img) return CP0_MAP_E_NOMEM;
    for (int i = 0; i < 64; i++) ((uint16_t *)img->px)[i] = (uint16_t)req->x;
    snprintf(archive_id, CP0_MAP_ID_MAX, "fake");
    *out = img;
    return CP0_MAP_OK;
}

static cp0_map_req_t req_of(uint32_t x)
{
    cp0_map_req_t r;
    memset(&r, 0, sizeof r);
    r.key.source = 1;
    r.key.x = x;
    r.key.z = 10;
    r.z = 10;
    r.x = x;
    r.out_px = 8;
    return r;
}

static int drain(cp0_map_loader_t *l, cp0_map_done_t *all, int max)
{
    int n = 0, k;
    cp0_map_done_t d[32];
    while ((k = cp0_map_loader_poll(l, d, 32, NULL)) > 0)
        for (int i = 0; i < k; i++)
            if (all && n < max) all[n++] = d[i];
    return n;
}

/* waits until the loader is idle, collecting the completions into kept[*nkept..max) (kept may be NULL) */
static void wait_idle_keep(cp0_map_loader_t *l, cp0_map_done_t *kept, int *nkept, int max)
{
    const double t0 = now_ms();
    for (;;) {
        const int n = drain(l, kept ? kept + *nkept : NULL, kept ? max - *nkept : 0);
        if (nkept) *nkept += n;
        if (cp0_map_loader_idle(l)) return;
        if (now_ms() - t0 > 20000) CHECK(0);
        usleep(200);
    }
}

static void wait_idle(cp0_map_loader_t *l)
{
    wait_idle_keep(l, NULL, NULL, 0);
}

static void test_order_and_skip(void)
{
    cp0_map_cache_t *cache = cp0_map_cache_new(1 << 20);
    fake_t f;
    memset(&f, 0, sizeof f);
    pthread_mutex_init(&f.mu, NULL);
    cp0_map_loader_t *l = cp0_map_loader_new(cache, fake_fetch, &f);
    CHECK(l != NULL && cp0_map_loader_idle(l));
    cp0_map_req_t r[60];
    for (int i = 0; i < 60; i++) r[i] = req_of((uint32_t)(1000 + (i * 37) % 60)); /* a shuffled order */
    cp0_map_loader_submit(l, r, 60);
    /* the completions come while it works: poll until all 60 are in */
    cp0_map_done_t all[128];
    int n = 0;
    const double t0 = now_ms();
    while (n < 60 && now_ms() - t0 < 20000) {
        n += drain(l, all + n, 128 - n);
        usleep(100);
    }
    CHECK(n == 60);
    wait_idle(l);
    pthread_mutex_lock(&f.mu);
    CHECK(f.n == 60);
    for (int i = 0; i < 60; i++) CHECK(f.order[i] == (int)r[i].x); /* fetched in the submitted order */
    pthread_mutex_unlock(&f.mu);
    for (int i = 0; i < 60; i++) {
        CHECK(all[i].result == CP0_MAP_OK && all[i].req.x == r[i].x && !strcmp(all[i].archive_id, "fake"));
        CHECK(cp0_map_cache_contains(cache, &r[i].key));
    }
    /* again: everything is cached, nothing is fetched, nothing reported */
    cp0_map_loader_submit(l, r, 60);
    wait_idle(l);
    pthread_mutex_lock(&f.mu);
    const int fetched_again = f.n;
    pthread_mutex_unlock(&f.mu);
    CHECK(drain(l, all, 128) == 0 && fetched_again == 60 && cp0_map_loader_fetched(l) == 60);
    /* errors come back */
    pthread_mutex_lock(&f.mu);
    f.fail_every = 2;
    pthread_mutex_unlock(&f.mu);
    cp0_map_req_t e[10];
    for (int i = 0; i < 10; i++) e[i] = req_of((uint32_t)(2000 + i));
    cp0_map_loader_submit(l, e, 10);
    n = 0;
    while (n < 10) {
        n += drain(l, all + n, 128 - n);
        usleep(100);
    }
    for (int i = 0; i < 10; i++) CHECK(all[i].result == (all[i].req.x % 2 == 0 ? CP0_MAP_E_NO_TILE : CP0_MAP_OK));
    cp0_map_loader_free(l);
    cp0_map_cache_free(cache);
    pthread_mutex_destroy(&f.mu);
    printf("map loader: fetched in the submitted order, cached keys skipped, errors reported\n");
}

static void test_cancel(void)
{
    cp0_map_cache_t *cache = cp0_map_cache_new(1 << 22);
    fake_t f;
    memset(&f, 0, sizeof f);
    pthread_mutex_init(&f.mu, NULL);
    f.delay_us = 3000;
    cp0_map_loader_t *l = cp0_map_loader_new(cache, fake_fetch, &f);
    cp0_map_req_t a[100], b[20];
    for (int i = 0; i < 100; i++) a[i] = req_of((uint32_t)(3000 + i));
    for (int i = 0; i < 20; i++) b[i] = req_of((uint32_t)(5000 + i));
    cp0_map_loader_submit(l, a, 100);
    for (;;) {
        pthread_mutex_lock(&f.mu);
        const int n = f.n;
        pthread_mutex_unlock(&f.mu);
        if (n >= 3) break;
        usleep(200);
    }
    /* the view moved: the old queue goes */
    pthread_mutex_lock(&f.mu);
    const int before = f.n;
    pthread_mutex_unlock(&f.mu);
    cp0_map_loader_submit(l, b, 20);
    cp0_map_done_t all[256];
    int ndone = 0;
    wait_idle_keep(l, all, &ndone, 256);
    pthread_mutex_lock(&f.mu);
    int old_after = 0, first_new = -1;
    for (int i = before; i < f.n; i++) {
        if (f.order[i] < 5000) old_after++;
        else if (first_new < 0) first_new = i;
    }
    CHECK(old_after <= 1);                      /* at most the one in progress */
    CHECK(f.n - before - old_after == 20);      /* all of the new view */
    for (int i = 0; i < 20; i++) CHECK(f.order[first_new + i] == 5000 + i);
    const int total = f.n;
    pthread_mutex_unlock(&f.mu);
    CHECK(ndone == total);
    /* an empty submit clears the queue */
    cp0_map_loader_submit(l, a, 100);
    cp0_map_loader_submit(l, NULL, 0);
    wait_idle(l);
    pthread_mutex_lock(&f.mu);
    CHECK(f.n <= total + 1 + 3); /* nothing more than what was in flight */
    pthread_mutex_unlock(&f.mu);
    cp0_map_loader_free(l);
    cp0_map_cache_free(cache);
    pthread_mutex_destroy(&f.mu);
    printf("map loader: a new view drops the old queue (%d old tile(s) finished after the switch)\n", old_after);
}

static void test_overflow_and_stress(void)
{
    cp0_map_cache_t *cache = cp0_map_cache_new(1 << 24);
    fake_t f;
    memset(&f, 0, sizeof f);
    pthread_mutex_init(&f.mu, NULL);
    cp0_map_loader_t *l = cp0_map_loader_new(cache, fake_fetch, &f);
    /* more completions than the buffer holds, never polled: the overflow flag */
    cp0_map_req_t r[CP0_MAP_LOADER_QUEUE];
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < CP0_MAP_LOADER_QUEUE; i++) r[i] = req_of((uint32_t)(10000 + round * 1000 + i));
        cp0_map_loader_submit(l, r, CP0_MAP_LOADER_QUEUE);
        while (1) {
            pthread_mutex_lock(&f.mu);
            const int n = f.n;
            pthread_mutex_unlock(&f.mu);
            if (n >= (round + 1) * CP0_MAP_LOADER_QUEUE) break;
            usleep(200);
        }
    }
    usleep(2000);
    cp0_map_done_t d[CP0_MAP_LOADER_DONE];
    int overflow = 0;
    const int got = cp0_map_loader_poll(l, d, CP0_MAP_LOADER_DONE, &overflow);
    CHECK(got == CP0_MAP_LOADER_DONE && overflow == 1);
    /* stress: submits and polls from this thread while the worker runs */
    rng_seed(5);
    pthread_mutex_lock(&f.mu);
    f.delay_us = 50;
    pthread_mutex_unlock(&f.mu);
    for (int k = 0; k < 300; k++) {
        const int n = 1 + (int)(rng() % 40);
        for (int i = 0; i < n; i++) r[i] = req_of(20000 + rng() % 500);
        cp0_map_loader_submit(l, r, n);
        cp0_map_loader_poll(l, d, 16, NULL);
        if (k % 7 == 0) usleep(100);
    }
    /* freed while it is busy: joins after the tile in progress */
    for (int i = 0; i < 40; i++) r[i] = req_of(30000 + (uint32_t)i);
    pthread_mutex_lock(&f.mu);
    f.delay_us = 2000;
    pthread_mutex_unlock(&f.mu);
    cp0_map_loader_submit(l, r, 40);
    usleep(3000);
    cp0_map_loader_free(l);
    cp0_map_cache_stats_t st;
    cp0_map_cache_get_stats(cache, &st);
    CHECK(st.pinned == 0 && st.bytes <= st.budget);
    cp0_map_cache_free(cache);
    pthread_mutex_destroy(&f.mu);
    printf("map loader: completion overflow flagged, 300 rapid submits + polls, freed while busy\n");
}

/* ------------------------------------------------------------------------------------------- the store fetch */

static void folder_png(const char *dir, int z, uint32_t x, uint32_t y, int px)
{
    bb_t t = {0};
    png_tile_small(&t, px, z, x, y);
    char p[700];
    snprintf(p, sizeof p, "%s/%d/%u/%u.png", dir, z, x, y);
    write_file(p, t.d, t.n);
    free(t.d);
}

static cp0_map_image_t *direct(const char *store_dir, const char *id, int z, uint32_t x, uint32_t y, int quad,
                               uint32_t qx, uint32_t qy, int out_px)
{
    (void)store_dir;
    cp0_map_store_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.store_dir = store_dir;
    cfg.read_only = 1;
    cp0_map_store_t *s;
    CHECK_RC(cp0_map_store_open(&cfg, &s), CP0_MAP_OK);
    cp0_map_buf_t b = {0};
    CHECK_RC(cp0_map_store_get_tile(s, id, z, x, y, &b, NULL), CP0_MAP_OK);
    cp0_map_image_t *img;
    CHECK_RC(cp0_map_decode_tile(b.data, b.len, CP0_MAP_PIX_RGB565, 2, &img), CP0_MAP_OK);
    const int w = quad ? img->w / 2 : img->w;
    const uint16_t *src = (const uint16_t *)(img->px + (size_t)(qy ? w : 0) * (size_t)img->stride) + (qx ? w : 0);
    cp0_map_image_t *o = cp0_map_scale_to(quad ? src : (const uint16_t *)img->px, w, w, img->stride, out_px);
    cp0_map_image_free(img);
    cp0_map_buf_free(&b);
    cp0_map_store_close(s);
    return o;
}

static void test_store_fetch(void)
{
    char dir256[600], dir512[600], store[600];
    tpath(dir256, sizeof dir256, "tiles256");
    tpath(dir512, sizeof dir512, "tiles512");
    tpath(store, sizeof store, "store");
    for (int z = 0; z <= 2; z++)
        for (uint32_t x = 0; x < (1u << z); x++)
            for (uint32_t y = 0; y < (1u << z); y++) {
                if (z == 2 && x == 3 && y == 3) continue; /* a hole */
                folder_png(dir256, z, x, y, 256);
                if (z >= 1) folder_png(dir512, z, x, y, 512);
            }
    cp0_map_store_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.store_dir = store;
    cfg.ram_cache_bytes = 8u << 20;
    cp0_map_store_t *s;
    CHECK_RC(cp0_map_store_open(&cfg, &s), CP0_MAP_OK);
    cp0_map_import_opts_t o;
    memset(&o, 0, sizeof o);
    o.id = "t256";
    CHECK_RC(cp0_map_store_import_ex(s, dir256, &o), CP0_MAP_OK);
    o.id = "t512";
    CHECK_RC(cp0_map_store_import_ex(s, dir512, &o), CP0_MAP_OK);
    cp0_map_store_fetch_t f;
    memset(&f, 0, sizeof f);
    f.store = s;
    const struct {
        const char *id;
        int z;
        uint32_t x, y;
        int over, out_px;
    } cases[] = {{"t256", 2, 1, 2, 0, 256}, {"t256", 2, 1, 2, 0, 384}, {"t256", 2, 1, 2, 0, 512},
                 {"t256", 3, 3, 5, 1, 256}, {"t256", 3, 3, 5, 1, 384}, {"t256", 3, 3, 5, 1, 512},
                 {"t512", 2, 2, 1, 0, 256}, {"t512", 2, 2, 1, 0, 384}, {"t512", 2, 2, 1, 0, 512},
                 {"t512", 3, 4, 3, 1, 384}, {"", 1, 1, 1, 0, 384}};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        cp0_map_req_t r;
        memset(&r, 0, sizeof r);
        r.z = cases[i].z;
        r.x = cases[i].x;
        r.y = cases[i].y;
        r.over = (uint8_t)cases[i].over;
        r.out_px = cases[i].out_px;
        snprintf(r.source, sizeof r.source, "%s", cases[i].id);
        cp0_map_image_t *img = NULL;
        char aid[CP0_MAP_ID_MAX] = "";
        CHECK_RC(cp0_map_store_fetch(&f, &r, &img, aid, NULL), CP0_MAP_OK);
        CHECK(img && img->w == cases[i].out_px && img->h == cases[i].out_px);
        CHECK(!strcmp(aid, cases[i].id[0] ? cases[i].id : "t256"));
        const int rz = r.over ? r.z - 1 : r.z;
        const uint32_t rx = r.over ? r.x >> 1 : r.x, ry = r.over ? r.y >> 1 : r.y;
        cp0_map_image_t *want = direct(store, cases[i].id[0] ? cases[i].id : NULL, rz, rx, ry, r.over, r.x & 1, r.y & 1,
                                       cases[i].out_px);
        CHECK(!memcmp(want->px, img->px, (size_t)img->stride * (size_t)img->h));
        cp0_map_image_free(want);
        cp0_map_image_free(img);
    }
    cp0_map_req_t hole;
    memset(&hole, 0, sizeof hole);
    hole.z = 2;
    hole.x = 3;
    hole.y = 3;
    hole.out_px = 256;
    snprintf(hole.source, sizeof hole.source, "t256");
    cp0_map_image_t *img = NULL;
    char aid[CP0_MAP_ID_MAX];
    CHECK_RC(cp0_map_store_fetch(&f, &hole, &img, aid, NULL), CP0_MAP_E_NO_TILE);
    CHECK(img == NULL);
    /* through the loader with the real fetch */
    cp0_map_loader_t *l = cp0_map_loader_new(cp0_map_store_ram_cache(s), cp0_map_store_fetch, &f);
    cp0_map_req_t r[4];
    for (int i = 0; i < 4; i++) {
        memset(&r[i], 0, sizeof r[i]);
        r[i].z = 2;
        r[i].x = (uint32_t)i;
        r[i].y = 3;
        r[i].out_px = 384;
        r[i].key = (cp0_map_tile_key_t){cp0_map_source_hash("t256"), (uint32_t)i, 3, 2, cp0_map_tile_variant(3, 0)};
        snprintf(r[i].source, sizeof r[i].source, "t256");
    }
    cp0_map_loader_submit(l, r, 4);
    cp0_map_done_t d[8];
    int n = 0;
    while (n < 4) {
        n += cp0_map_loader_poll(l, d + n, 8 - n, NULL);
        usleep(200);
    }
    for (int i = 0; i < 4; i++) CHECK(d[i].result == (d[i].req.x == 3 ? CP0_MAP_E_NO_TILE : CP0_MAP_OK));
    cp0_map_loader_free(l);
    cp0_map_store_fetch_done(&f);
    cp0_map_store_close(s);
    printf("map loader: store fetch (256 px x1 / 1.5 / 2, over-zoom quadrants, 512 px x0.5 / 0.75 / 1, any archive, a "
           "hole) equals decode + resample\n");
}

int main(void)
{
    tmp_init("cp0-map-loader");
    test_order_and_skip();
    test_cancel();
    test_overflow_and_stress();
    test_store_fetch();
    tmp_done();
    printf("map loader OK (%ld checks)\n", g_checks);
    return 0;
}
