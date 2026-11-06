/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map_fetch.h: how a viewer asks the store's fetch pool for the tiles of an online source it shows and does not
 * have (report 054 2.7, Deck Atlas 0.1 task 3). The map widget uses it (cp0_map_widget.h); an app that draws its own
 * map can too.
 *
 * ONLY WHAT IS ON SCREEN. A client tells the pool, at every view change, the tiles of the source that are VISIBLE and
 * that its own read of the store did not find (or found stale): cp0_map_client_view() replaces the previous list, so
 * tiles that left the screen are dropped before they are asked for, and a request already on the wire for a tile no
 * client wants any more is cancelled. Never pass tiles around the view (a prefetch ring): the viewing-cache terms of
 * every shipped source allow only the tiles a user is looking at. The pool de-duplicates tiles that several clients
 * want, checks the viewing cache again before asking the server (a fresh tile is answered at once, a 404 remembered
 * for 24 h too), sends If-None-Match / If-Modified-Since for a stale one, and keeps the source's policy (cp0_map_source.h:
 * parallel requests, interval, daily cap, pauses, the three guards).
 *
 * EVENTS. cp0_map_client_poll() hands out what happened (cp0_map_tile_event_t of cp0_map_overlay.h, kind FETCHED,
 * REFUSED or FETCH_FAILED, source id in archive_id): FETCHED means the tile is in the store now, read it again (the
 * widget does). REFUSED comes once for a tile that enters the list while the source cannot fetch (offline, cap reached,
 * blocked...), and once more for the whole list when the source stops; such tiles stay in the list and are fetched
 * when the source can again.
 *
 * Threads: every call from any one thread per client (the widget: the LVGL thread); none blocks on the network or the
 * disk. A client must be freed before its store is closed.
 */
#ifndef CP0_MAP_FETCH_H
#define CP0_MAP_FETCH_H

#include "cp0_map_overlay.h"
#include "cp0_map_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_MAP_CLIENT_TILES_MAX 160   /* tiles per view (cp0_map_client_view keeps the first ones) */
#define CP0_MAP_CLIENT_EVENTS_MAX 256  /* events kept between two polls (the oldest are dropped beyond) */

typedef struct cp0_map_client cp0_map_client_t;

typedef struct {
    int z;
    uint32_t x, y;                /* XYZ */
} cp0_map_tile_id_t;

/* A new client of the store's fetch pool. 0 with *out set, CP0_MAP_E_INVALID, CP0_MAP_E_NOMEM. */
int cp0_map_client_new(cp0_map_store_t *store, cp0_map_client_t **out);
/* Drops its list (cancelling what only it wanted) and frees it. NULL is ignored. */
void cp0_map_client_free(cp0_map_client_t *client);
/* Replaces the client's list with tiles[0..n) of `source_id` (an online source of the store), first = most wanted
 * (the view's centre first). n = 0 or source_id NULL clears it. Tiles outside the source's zooms are ignored.
 * 0, CP0_MAP_E_NOT_FOUND (no such online source; the list is cleared), CP0_MAP_E_INVALID. */
int cp0_map_client_view(cp0_map_client_t *client, const char *source_id, const cp0_map_tile_id_t *tiles, int n);
/* Moves up to max events into out; returns how many. */
int cp0_map_client_poll(cp0_map_client_t *client, cp0_map_tile_event_t *out, int max);
/* Tiles of the list not answered yet (0 when nothing is pending, or the source cannot fetch now). */
int cp0_map_client_pending(cp0_map_client_t *client);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_FETCH_H */
