/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: the RAM tile cache, an LRU with a byte budget, pins and one mutex (see cp0_map_tile.h).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* sysconf names */
#endif
#include "cp0_map_image_priv.h"

#include "cp0_app_contract.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct entry {
    cp0_map_tile_key_t key;
    cp0_map_image_t *img;
    size_t bytes;
    int pins;
    int cached;                  /* 1 while in the table and the list; 0 once evicted / replaced (freed at release) */
    struct entry *hnext;
    struct entry *prev, *next;   /* LRU list: head = most recently used */
} entry_t;

struct cp0_map_cache {
    pthread_mutex_t mu;
    entry_t **buckets;
    size_t nbuckets;             /* a power of two */
    entry_t lru;                 /* sentinel */
    entry_t orphans;             /* sentinel: evicted or replaced entries still pinned */
    size_t budget;
    size_t cached_bytes;         /* entries in the table */
    size_t orphan_bytes;         /* evicted or replaced, still pinned */
    size_t peak;
    int entries, pinned;
    uint64_t hits, misses, inserts, evictions, rejected;
};

#define MIB ((size_t)1024 * 1024)

size_t cp0_map_cache_default_budget(int ram_mb)
{
    if (ram_mb < 768) return 8 * MIB; /* also unknown (<= 0): the 512 MB boards and the 25 MB rule */
    if (ram_mb < 3072) return 24 * MIB;
    return 48 * MIB;
}

size_t cp0_map_cache_budget_from_board(void)
{
    cp0_app_board_t board;
    if (cp0_app_board_read(NULL, &board) == 0 && board.ram_mb > 0) return cp0_map_cache_default_budget(board.ram_mb);
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0) return cp0_map_cache_default_budget((int)((double)pages * (double)page / (double)MIB));
    return cp0_map_cache_default_budget(0);
}

uint8_t cp0_map_tile_variant(int scale_x2, cp0_map_pixfmt_t pixfmt)
{
    return (uint8_t)(((unsigned)scale_x2 & 15u) << 4 | ((unsigned)pixfmt & 15u));
}

static size_t key_hash(const cp0_map_tile_key_t *k)
{
    uint64_t h = 1469598103934665603ull;
    const uint64_t parts[4] = {k->source, k->x, k->y, ((uint64_t)k->z << 8) | k->variant};
    for (int i = 0; i < 4; i++) {
        h ^= parts[i];
        h *= 1099511628211ull;
        h ^= h >> 29;
    }
    return (size_t)h;
}

static int key_eq(const cp0_map_tile_key_t *a, const cp0_map_tile_key_t *b)
{
    return a->source == b->source && a->x == b->x && a->y == b->y && a->z == b->z && a->variant == b->variant;
}

cp0_map_cache_t *cp0_map_cache_new(size_t budget)
{
    cp0_map_cache_t *c = (cp0_map_cache_t *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->nbuckets = 256;
    c->buckets = (entry_t **)calloc(c->nbuckets, sizeof(entry_t *));
    if (!c->buckets || pthread_mutex_init(&c->mu, NULL) != 0) {
        free(c->buckets);
        free(c);
        return NULL;
    }
    c->lru.next = c->lru.prev = &c->lru;
    c->orphans.next = c->orphans.prev = &c->orphans;
    c->budget = budget ? budget : cp0_map_cache_budget_from_board();
    return c;
}

static void list_unlink(entry_t *e)
{
    e->prev->next = e->next;
    e->next->prev = e->prev;
    e->prev = e->next = NULL;
}

static void list_push(entry_t *head, entry_t *e)
{
    e->next = head->next;
    e->prev = head;
    head->next->prev = e;
    head->next = e;
}

static void list_push_front(cp0_map_cache_t *c, entry_t *e)
{
    list_push(&c->lru, e);
}

static entry_t **find_slot(cp0_map_cache_t *c, const cp0_map_tile_key_t *k)
{
    entry_t **pp = &c->buckets[key_hash(k) & (c->nbuckets - 1)];
    while (*pp && !key_eq(&(*pp)->key, k)) pp = &(*pp)->hnext;
    return pp;
}

static void free_entry(entry_t *e)
{
    cp0_map_image_block_t *b = cp0_map__image_block(e->img);
    if (b) b->owner = NULL;
    cp0_map_image_free(e->img);
    free(e);
}

/* takes a cached entry out of the table and the list: freed now, or kept as an orphan until its last release */
static void uncache(cp0_map_cache_t *c, entry_t *e)
{
    entry_t **pp = find_slot(c, &e->key);
    if (*pp == e) *pp = e->hnext;
    e->hnext = NULL;
    list_unlink(e);
    e->cached = 0;
    c->entries--;
    c->cached_bytes -= e->bytes;
    if (e->pins > 0) {
        c->orphan_bytes += e->bytes;
        list_push(&c->orphans, e);
    } else {
        free_entry(e);
    }
}

/* evicts least recently used unpinned entries until `extra` more bytes fit; 1 when they do */
static int make_room(cp0_map_cache_t *c, size_t extra)
{
    entry_t *e = c->lru.prev;
    while (c->cached_bytes + c->orphan_bytes + extra > c->budget && e != &c->lru) {
        entry_t *prev = e->prev;
        if (e->pins == 0) {
            uncache(c, e);
            c->evictions++;
        }
        e = prev;
    }
    return c->cached_bytes + c->orphan_bytes + extra <= c->budget;
}

static void grow(cp0_map_cache_t *c)
{
    if ((size_t)c->entries < c->nbuckets) return;
    const size_t nb = c->nbuckets * 2;
    entry_t **b = (entry_t **)calloc(nb, sizeof(entry_t *));
    if (!b) return; /* longer chains, still correct */
    for (size_t i = 0; i < c->nbuckets; i++)
        for (entry_t *e = c->buckets[i]; e;) {
            entry_t *n = e->hnext;
            const size_t h = key_hash(&e->key) & (nb - 1);
            e->hnext = b[h];
            b[h] = e;
            e = n;
        }
    free(c->buckets);
    c->buckets = b;
    c->nbuckets = nb;
}

void cp0_map_cache_free(cp0_map_cache_t *c)
{
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    while (c->lru.next != &c->lru) {
        entry_t *e = c->lru.next;
        e->pins = 0;
        uncache(c, e);
    }
    /* images still pinned (the API says release them first) are freed too */
    while (c->orphans.next != &c->orphans) {
        entry_t *e = c->orphans.next;
        list_unlink(e);
        free_entry(e);
    }
    pthread_mutex_unlock(&c->mu);
    pthread_mutex_destroy(&c->mu);
    free(c->buckets);
    free(c);
}

const cp0_map_image_t *cp0_map_cache_get(cp0_map_cache_t *c, const cp0_map_tile_key_t *k)
{
    if (!c || !k) return NULL;
    pthread_mutex_lock(&c->mu);
    entry_t *e = *find_slot(c, k);
    const cp0_map_image_t *img = NULL;
    if (e) {
        list_unlink(e);
        list_push_front(c, e);
        if (e->pins++ == 0) c->pinned++;
        c->hits++;
        img = e->img;
    } else {
        c->misses++;
    }
    pthread_mutex_unlock(&c->mu);
    return img;
}

int cp0_map_cache_contains(cp0_map_cache_t *c, const cp0_map_tile_key_t *k)
{
    if (!c || !k) return 0;
    pthread_mutex_lock(&c->mu);
    const int found = *find_slot(c, k) != NULL;
    pthread_mutex_unlock(&c->mu);
    return found;
}

int cp0_map_cache_put(cp0_map_cache_t *c, const cp0_map_tile_key_t *k, cp0_map_image_t *img,
                      const cp0_map_image_t **pinned)
{
    if (pinned) *pinned = NULL;
    cp0_map_image_block_t *b = cp0_map__image_block(img);
    if (!c || !k || !b || b->owner) return CP0_MAP_E_INVALID;
    const size_t bytes = cp0_map_image_bytes(img);
    entry_t *e = (entry_t *)calloc(1, sizeof *e);
    if (!e) return CP0_MAP_E_NOMEM;
    pthread_mutex_lock(&c->mu);
    if (bytes > c->budget) {
        c->rejected++;
        pthread_mutex_unlock(&c->mu);
        free(e);
        return CP0_MAP_E_TOO_LARGE;
    }
    entry_t *old = *find_slot(c, k);
    if (old) uncache(c, old); /* replaced: freed, or an orphan until released */
    if (!make_room(c, bytes)) {
        c->rejected++;
        pthread_mutex_unlock(&c->mu);
        free(e);
        return CP0_MAP_E_FULL;
    }
    e->key = *k;
    e->img = img;
    e->bytes = bytes;
    e->cached = 1;
    b->owner = e;
    entry_t **slot = find_slot(c, k);
    e->hnext = *slot;
    *slot = e;
    list_push_front(c, e);
    c->entries++;
    c->cached_bytes += bytes;
    c->inserts++;
    if (pinned) {
        e->pins = 1;
        c->pinned++;
        *pinned = img;
    }
    if (c->cached_bytes + c->orphan_bytes > c->peak) c->peak = c->cached_bytes + c->orphan_bytes;
    grow(c);
    pthread_mutex_unlock(&c->mu);
    return CP0_MAP_OK;
}

void cp0_map_cache_release(cp0_map_cache_t *c, const cp0_map_image_t *img)
{
    cp0_map_image_block_t *b = cp0_map__image_block(img);
    if (!c || !b) return;
    pthread_mutex_lock(&c->mu);
    entry_t *e = (entry_t *)b->owner;
    if (e && e->pins > 0) {
        if (--e->pins == 0) {
            c->pinned--;
            if (!e->cached) {
                c->orphan_bytes -= e->bytes;
                list_unlink(e);
                free_entry(e);
            }
        }
    }
    pthread_mutex_unlock(&c->mu);
}

int cp0_map_cache_drop(cp0_map_cache_t *c, uint32_t source)
{
    if (!c) return 0;
    int n = 0;
    pthread_mutex_lock(&c->mu);
    for (entry_t *e = c->lru.next; e != &c->lru;) {
        entry_t *next = e->next;
        if (!source || e->key.source == source) {
            uncache(c, e);
            n++;
        }
        e = next;
    }
    pthread_mutex_unlock(&c->mu);
    return n;
}

void cp0_map_cache_set_budget(cp0_map_cache_t *c, size_t budget)
{
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    c->budget = budget;
    make_room(c, 0);
    pthread_mutex_unlock(&c->mu);
}

void cp0_map_cache_get_stats(cp0_map_cache_t *c, cp0_map_cache_stats_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    out->hits = c->hits;
    out->misses = c->misses;
    out->inserts = c->inserts;
    out->evictions = c->evictions;
    out->rejected = c->rejected;
    out->bytes = c->cached_bytes + c->orphan_bytes;
    out->peak_bytes = c->peak;
    out->budget = c->budget;
    out->entries = c->entries;
    out->pinned = c->pinned;
    pthread_mutex_unlock(&c->mu);
}
