/*
 * SPDX-License-Identifier: MIT
 *
 * Private state of the map store (src/map/cp0_map_store.c and cp0_map_import.c).
 */
#ifndef CP0_MAP_STORE_PRIV_H
#define CP0_MAP_STORE_PRIV_H

#include "cp0_map_internal.h"
#include "cp0_map_online.h"

#include <pthread.h>

#define CP0_MAP_LICENCE_NOTE "user supplied: terms are the importer's responsibility"
#define CP0_MAP_FOLDER_ATTRIBUTION "\xc2\xa9 OpenStreetMap contributors" /* 054 2.7: an imported folder, source unknown */
#define CP0_MAP_RECORD_MAX (16 * 1024)

typedef struct {
    cp0_map_source_info_t info;  /* what the listing shows (state and bytes refreshed when listed) */
    char source_path[CP0_MAP_PATH_MAX];
    long long tiles;             /* converted folders: tiles written; -1 unknown */
    cp0_map_archive_t *archive;  /* opened on the first tile read */
    cp0_map_srcrec_t src;        /* online sources: URL, policy, the user's switches (task 3) */
} cp0_map_rec_t;

struct cp0_map_store {
    pthread_mutex_t mu;          /* records and archive handles */
    pthread_mutex_t import_mu;   /* one import (or remove) at a time */
    char dir[CP0_MAP_PATH_MAX];
    char sources_dir[CP0_MAP_PATH_MAX]; /* "" = none */
    int read_only;
    cp0_map_rec_t *recs;         /* archives (sorted by id), then sources (sorted by id) */
    int nrecs, narchives;
    cp0_map_cache_t *ram;
    cp0_map_fetcher_t *fetcher;  /* online sources: the fetch pool and the viewing caches (task 3) */
};

/* <store>/archives/<id><suffix> into buf. 0 or CP0_MAP_E_INVALID. */
CP0_MAP_HIDDEN int cp0_map__archive_file(const cp0_map_store_t *s, const char *id, const char *suffix, char *buf,
                                         size_t size);
/* Writes the record of an imported archive (archives/<id>.conf, atomically). 0 or an error. */
CP0_MAP_HIDDEN int cp0_map__record_write(const cp0_map_store_t *s, const cp0_map_rec_t *r);
/* Reads the records again; call with s->mu held. 0 or an error. */
CP0_MAP_HIDDEN int cp0_map__store_reload_locked(cp0_map_store_t *s);
/* 1 when an archive record with this id exists (a .conf file); s->mu not needed (looks at the disk). */
CP0_MAP_HIDDEN int cp0_map__record_exists(const cp0_map_store_t *s, const char *id);

/* The store's fetch pool (cp0_map_fetch.h clients), or NULL. */
CP0_MAP_HIDDEN cp0_map_fetcher_t *cp0_map__store_fetcher(cp0_map_store_t *s);
/* The source records recs[from .. n) changed (store mutex held by the caller): sources added, updated, gone. */
CP0_MAP_HIDDEN void cp0_map__fetcher_sync(cp0_map_fetcher_t *f, const cp0_map_rec_t *recs, int from, int n);
/* Writes the providers' state files and commits the viewing caches now. */
CP0_MAP_HIDDEN int cp0_map__fetcher_flush(cp0_map_fetcher_t *f);

#endif /* CP0_MAP_STORE_PRIV_H */
