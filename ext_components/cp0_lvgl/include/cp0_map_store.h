/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map_store.h: the shared tile store of the map engine (report 054 sections 2.7, 3.7 and 6.2; decisions D3, D10).
 *
 * LAYOUT ($XDG_DATA_HOME/cyberdeckzero/maps/, default ~/.local/share/cyberdeckzero/maps/; any app may read it):
 *   archives/<id>.conf      one record per imported archive (below), written atomically by the importing app
 *   archives/<id>.mbtiles   an archive copied into the store, or a z/x/y folder converted into one MBTiles
 *   archives/<id>.pmtiles   a PMTiles archive copied into the store
 *   archives/<id>.mbtiles.part   a folder conversion in progress or interrupted (resumed by importing again)
 *   archives/<id>.<kind>.copy    a copy in progress (deleted when it fails; cp0_map_store_remove(id) clears one left
 *                                behind by a killed app)
 *   sources/<id>.conf       online source records and the user's choices for them (cp0_map_source.h)
 *   sources/<id>.key        a source's API key, mode 0600 (cp0_map_source_set_key; never logged)
 *   tiles/<id>.mbtiles      the persistent viewing cache of a source: a valid MBTiles 1.3 (any tool reads it) plus the
 *                           engine's tables cp0_tile_state (expiry, ETag, Last-Modified, last use, size, hash) and
 *                           cp0_missing (404 / 410 answers, remembered 24 h); rollback journal, batched commits
 *   tiles/<id>.lock         its writer lock (flock): the first app that opens the store read-write writes the cache
 *                           and fetches for the source; a second app reads it (read-only for that source)
 *   state/<id>.state        a provider's day, request count, pauses (blocked, back-off), written by its writer
 *   overlays/               points apps publish (054 6.2; not the engine's)
 * Imported archives live apart from the viewing caches so that cache eviction (the smaller of 512 MiB and 10 % of free
 * space) can never touch them: imported archives are never evicted, only removed on request.
 *
 * VIEWING CACHE (task 3, decisions D3 / D10). Only tiles a viewer showed are fetched and kept (cp0_map_fetch.h: the
 * widget asks only for the visible tiles its loader did not find, never its ring of one tile around the view), and
 * they stay after the app quits. The server's Cache-Control max-age / Expires are kept per tile (freshness clamped to
 * CP0_MAP_FRESH_FLOOR_S .. CP0_MAP_FRESH_CEIL_S, CP0_MAP_FRESH_DEFAULT_S without either header); an expired tile is
 * still SHOWN (marked stale: cp0_map_tile_meta_t.stale) and revalidated with If-None-Match / If-Modified-Since when
 * the network is on (304 refreshes it); offline or on errors the stale tile simply stays. Writes are batched (a commit
 * every CP0_MAP_CACHE_BATCH tiles or CP0_MAP_CACHE_COMMIT_MS, flushed on close and by cp0_map_store_flush()); a killed
 * app leaves a valid file (SQLite's rollback journal). Cap: cfg.cache_cap_bytes / cp0_map_store_set_cap(), default
 * the smaller of 512 MiB and 10 % of the free space of the store's file system (counted without the caches
 * themselves, so the cap does not shrink as the cache grows); over the cap the least recently used tiles of ALL the
 * viewing caches go first (last use kept in RAM, written at most once per tile per day), down to 90 % of the cap.
 * Below CP0_MAP_CACHE_MIN_FREE of free space, or after a failed write, the cache stops growing
 * (CP0_MAP_E_CACHE_FULL: nothing is fetched, the cache is still read).
 *
 * RECORD (archives/<id>.conf; key=value lines like the launcher's state files; readers ignore unknown keys):
 *   v=1  id=  title=  kind=mbtiles|pmtiles  mode=copy|inplace|converted  path=<the archive file, absolute>
 *   source_path=<what was imported>  format=png|jpg  tile_px=256|512  minzoom=  maxzoom=  bounds=w,s,e,n
 *   attribution=  licence=user supplied: terms are the importer's responsibility  bytes=  tiles=  imported=<UTC>
 *   zoom_extent=z,x0,y0,x1,y1   (a converted folder: one line per zoom that has tiles; the XYZ tile rectangle there)
 * BOUNDS AND PER-ZOOM COVERAGE. `bounds` / `minzoom` / `maxzoom` of a converted folder are the union over ALL its
 * zooms (the MBTiles metadata says the same, as the standard wants). A lookup across all archives (get_tile with id
 * NULL) skips an archive that cannot have the tile: with `zoom_extent=` lines each zoom is tested against its own
 * rectangle (a folder whose shallow levels cover the world and whose deepest covers a town is found at every zoom);
 * without them (a copied or in-place MBTiles / PMTiles, whose bounds the file declares, or a folder converted before
 * 2026-11-07, whose record says bounds of the deepest zoom only) the declared zooms and bounds apply as they always
 * did. Such an old folder record is recognisable (mode converted, has_zoom_extents 0, minzoom < maxzoom); the engine
 * does not repair it: importing the folder again does (remove the archive first). A damaged zoom_extent line makes
 * the engine ignore all of them. Readers ignore unknown keys, so older toolkits still read these records.
 * An in-place archive (a file on a USB stick) keeps only its record: its path points at the stick, and while the stick
 * is out the archive is listed with state CP0_MAP_E_ARCHIVE_MISSING and its tiles answer that code.
 *
 * IMPORT (cp0_map_store_import_ex; run it on a worker thread, it can take seconds to minutes):
 *   - an MBTiles or PMTiles file: copied into archives/ (recommended; progress in bytes) or registered in place;
 *   - a z/x/y tile folder (<z>/<x>/<y>.png|.jpg|.jpeg, also <y>.png.tile and the like, extensionless files; the
 *     format of each file from its bytes, mixed folders allowed): ALWAYS converted into one new MBTiles (one SQLite
 *     file instead of thousands of small ones: SD wear, D10), rollback journal (half the SD writes of WAL, 055),
 *     one transaction per `batch` tiles (256), rows in TMS order, metadata name / format / bounds / minzoom /
 *     maxzoom / attribution written at the end. TMS folders (a gdal2tiles tilemapresource.xml) are flipped.
 *     Cancelled or interrupted, the partial file stays; importing the same folder again resumes, skipping the rows
 *     already written. 784 tiles/s on the deck in report 055 (target >= 500).
 * Vector (pbf) and WebP archives and folders are refused with CP0_MAP_E_UNSUPPORTED (raster first, D4). The import
 * screen of the app says the user is responsible for the terms of what they import (D3); the record keeps that note.
 *
 * READ-ONLY (cfg.read_only = 1, a second app such as Mesh Hop, 054 6.2 "one writer per file"): listing and tiles work
 * (the viewing caches too), import, remove, the source switches, keys, the cap and clearing return CP0_MAP_E_READ_ONLY,
 * nothing is created, written or fetched.
 *
 * Threads: every call is safe from any thread (one mutex per store); an import holds it only to register its result,
 * so tiles keep coming while it runs. Tile reads of one store are serialised. Online sources add a fetch pool of
 * CP0_MAP_MAX_PARALLEL threads (only while cfg.network is on) and one cache-writer thread (unless read-only); calls
 * marked "may wait on the disk" can block for a commit (tens of ms): keep them off the LVGL thread when you can.
 *
 * NETWORK. libcurl (libcurl.so.4, Debian's libcurl4t64, the boards' TLS stack) is loaded at run time only when the
 * network is on, as Home Assistant does: without it the sources report CP0_MAP_E_CURL_MISSING and the caches and
 * archives keep working. An app with online sources lists "libcurl4t64, ca-certificates" in its package depends.
 * Requests: cfg.user_agent on every request (none: CP0_MAP_E_NO_USER_AGENT, nothing is fetched), HTTPS (the
 * local-host exception of cp0_map_source.h), connect timeout 5 s, total 15 s, at most 3 redirects each checked like
 * a template (HTTPS, never an OSMF host), at most CP0_MAP_HTTP_MAX_BYTES per answer, Content-Type image/png or
 * image/jpeg with matching bytes. 429 / 503 pause the provider for its Retry-After (60 s without one); repeated
 * network or server errors pause it 30 s, doubling to 1 h; a failing tile is retried no sooner than 5 s, doubling to
 * 1 h. Keys are never logged: messages carry the URL with the key as "***".
 *
 * THE THREE GUARDS (054 2.7). (1) Blocked picture: when the same bytes come back for CP0_MAP_BLOCKED_REPEAT different
 * tiles within CP0_MAP_BLOCKED_WINDOW_S (or bytes the record lists), and the picture is not one plain colour (open
 * sea), the provider is paused for CP0_MAP_BLOCKED_PAUSE_S (state CP0_MAP_E_BLOCKED, kept across restarts), the
 * bytes are not stored and copies already stored are removed. (2) Missing tiles: 404 / 410 answers are remembered
 * per source for 24 h and not asked again. (3) Daily cap: at daily_cap requests a provider stops until local
 * midnight (CP0_MAP_E_CAP_REACHED).
 */
#ifndef CP0_MAP_STORE_H
#define CP0_MAP_STORE_H

#include "cp0_map_archive.h"
#include "cp0_map_common.h"
#include "cp0_map_source.h"
#include "cp0_map_tile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_MAP_FRESH_FLOOR_S 3600              /* a tile is fresh at least 1 h, whatever max-age says */
#define CP0_MAP_FRESH_CEIL_S (30 * 86400)       /* and at most 30 days */
#define CP0_MAP_FRESH_DEFAULT_S (7 * 86400)     /* without Cache-Control max-age or Expires (the OSMF rule's 7 days) */
#define CP0_MAP_MISSING_TTL_S 86400             /* a 404 / 410 tile is not asked again for 24 h */
#define CP0_MAP_CACHE_BATCH 32                  /* tiles per commit */
#define CP0_MAP_CACHE_COMMIT_MS 2000            /* or this long after the first uncommitted tile */
#define CP0_MAP_CACHE_CAP_MAX (512ull << 20)    /* the default cap: at most 512 MiB ... */
#define CP0_MAP_CACHE_CAP_FREE_DIV 10           /* ... and at most 10 % of the free space */
#define CP0_MAP_CACHE_MIN_FREE (64ull << 20)    /* below this much free space the cache stops growing */
#define CP0_MAP_HTTP_MAX_BYTES (2u << 20)       /* the largest answer accepted */
#define CP0_MAP_BLOCKED_REPEAT 4                /* identical pictures for this many different tiles ... */
#define CP0_MAP_BLOCKED_WINDOW_S 120            /* ... within this window: the provider sends a "blocked" picture */
#define CP0_MAP_BLOCKED_PAUSE_S 3600            /* how long a blocked provider is paused */

typedef struct cp0_map_store cp0_map_store_t;

typedef struct {
    const char *store_dir;        /* NULL: $XDG_DATA_HOME/cyberdeckzero/maps/ (cp0_map_store_default_dir) */
    const char *sources_dir;      /* NULL: none; else extra source records an app ships (read-only, listed after
                                     <store_dir>/sources/, which wins on the same id; still only stricter for a
                                     shipped id) */
    const char *user_agent;       /* "<app-id>/<version> (+https://github.com/OSRdesign/cyberdeck-zero-apps)"
                                     (cp0_map_user_agent); required for any fetch */
    size_t ram_cache_bytes;       /* RAM tile cache budget; 0: from board.state ram_mb (cp0_map_tile.h) */
    int read_only;                /* 1: never writes, never fetches (a second app) */
    int network;                  /* 1: online sources fetch the visible tiles they lack; 0: caches and archives only
                                     (the app's offline mode; cp0_map_store_set_network() changes it) */
    /* added by task 3 (leave zero for the defaults) */
    uint64_t cache_cap_bytes;     /* viewing cache cap; 0: the default rule (above) */
    void (*log)(void *user, const char *line); /* engine messages (a source paused, a write failed); NULL: stderr.
                                     Any thread. Keys never appear in them. */
    void *log_user;
} cp0_map_store_cfg_t;

/* The default store folder: $XDG_DATA_HOME/cyberdeckzero/maps, else $HOME/.local/share/cyberdeckzero/maps (no
 * trailing slash). 0, or CP0_MAP_E_INVALID (no HOME, or buf too small). */
int cp0_map_store_default_dir(char *buf, size_t size);

/* Opens the store (cfg NULL: all defaults), creating its folders unless read-only, and reads the records.
 * 0 with *out set, or CP0_MAP_E_IO / CP0_MAP_E_NOMEM / CP0_MAP_E_INVALID (*out NULL). */
int cp0_map_store_open(const cp0_map_store_cfg_t *cfg, cp0_map_store_t **out);
void cp0_map_store_close(cp0_map_store_t *store);
const char *cp0_map_store_dir(const cp0_map_store_t *store);
/* Reads the records again (another app imported or removed something). 0 or an error. */
int cp0_map_store_refresh(cp0_map_store_t *store);

typedef enum {
    CP0_MAP_ENTRY_ARCHIVE = 1,    /* an imported archive */
    CP0_MAP_ENTRY_SOURCE = 2,     /* a source record (task 3) */
} cp0_map_entry_kind_t;

typedef enum {
    CP0_MAP_MODE_NONE = 0,
    CP0_MAP_MODE_COPY = 1,        /* copied into archives/ */
    CP0_MAP_MODE_INPLACE = 2,     /* read where it is (USB stick) */
    CP0_MAP_MODE_CONVERTED = 3,   /* a z/x/y folder converted into archives/<id>.mbtiles */
} cp0_map_import_mode_t;

typedef struct {
    cp0_map_entry_kind_t kind;
    char id[CP0_MAP_ID_MAX];
    char title[128];
    int state;                    /* archives: CP0_MAP_OK, or CP0_MAP_E_ARCHIVE_MISSING (in-place file not there).
                                     sources: CP0_MAP_OK (fetches what viewers lack), or why it does not now:
                                     CP0_MAP_E_REFUSED (a record the rules refuse: OSMF host, http, bad template),
                                     _DISABLED, _NO_KEY, _OFFLINE, _CURL_MISSING, _NO_USER_AGENT, _READ_ONLY (another
                                     app writes its cache), _BLOCKED, _CAP_REACHED, _BACKOFF, _NETWORK, _CACHE_FULL;
                                     its cached tiles are read in every state */
    cp0_map_archive_kind_t archive;
    cp0_map_import_mode_t mode;
    cp0_map_format_t format;
    int tile_px;                  /* 256 / 512; 0 unknown */
    int minzoom, maxzoom;         /* -1 unknown */
    int has_bounds;
    cp0_map_bbox_t bounds;
    char attribution[256];
    char path[CP0_MAP_PATH_MAX];  /* the archive file (sources: their viewing cache file, which may not exist) */
    uint64_t bytes;               /* the archive's size on disk (0 while missing) */
    /* archives only (added 2026-11-07; for sources: tiles -1, imported 0, source_path "", has_zoom_extents 0) */
    int64_t tiles;                /* tiles the archive held when it was imported; -1 unknown (no `tiles=` in its record) */
    int64_t imported;             /* the import's date, seconds since the Unix epoch; 0 unknown */
    char source_path[CP0_MAP_PATH_MAX]; /* what was imported (the original file or folder); "" unknown */
    int has_zoom_extents;         /* 1: the record keeps the tile rectangle of every zoom (a folder converted since
                                     2026-11-07), so a lookup across all archives tests each zoom on its own. 0: only
                                     the declared bounds are known (a copied or in-place archive, or a folder converted
                                     before: see BOUNDS in the layout comment) */
    /* online sources only (added by task 3; zero for archives) */
    int shipped;                  /* 1: a toolkit record (the user file can only make it stricter) */
    int enabled;                  /* the user's switch (shipped records start off) */
    int has_key;                  /* a key is set (never the key itself) */
    int64_t until;                /* _BLOCKED / _BACKOFF / _NETWORK: end of the pause; _CAP_REACHED: local midnight
                                     (seconds since the epoch); 0 otherwise */
    uint32_t fetches_today;       /* requests sent today (its provider's count) */
    uint64_t cache_tiles;         /* tiles in its viewing cache */
    cp0_map_source_policy_t policy; /* the flags in force */
    char url[CP0_MAP_URL_MAX];    /* the template ({key} not filled in) */
    char licence[160];
    char note[256];               /* "unverified: check before enabling ..." where a term is not checked */
} cp0_map_source_info_t;

/* Archives (sorted by id) then source records (the shipped ones too; sorted by id). Fills up to `max` entries of `out`
 * (out may be NULL with max 0) and returns how many there are in all, or an error. Missing in-place archives are
 * checked now. */
int cp0_map_store_sources(cp0_map_store_t *store, cp0_map_source_info_t *out, int max);
/* One entry by id. 0, CP0_MAP_E_NOT_FOUND, CP0_MAP_E_INVALID. */
int cp0_map_store_find(cp0_map_store_t *store, const char *id, cp0_map_source_info_t *out);

typedef enum {
    CP0_MAP_SCHEME_AUTO = 0,      /* a folder: TMS when it holds a tilemapresource.xml (gdal2tiles), else XYZ */
    CP0_MAP_SCHEME_XYZ,
    CP0_MAP_SCHEME_TMS,
} cp0_map_scheme_t;

typedef struct {
    int copy;                     /* an archive file: 1 copy into the store, 0 register in place; a folder is always
                                     converted */
    const char *id;               /* NULL: from the file or folder name ([a-z0-9._-], "-2", "-3"... when taken) */
    const char *title;            /* NULL: the archive's metadata name (a folder's metadata.json), else the name */
    const char *attribution;      /* NULL: the archive's metadata; a folder without it: "© OpenStreetMap
                                     contributors" (054 2.7) */
    cp0_map_scheme_t scheme;      /* folder rows */
    int batch;                    /* folder: tiles per transaction; 0 = 256 */
    cp0_map_progress_cb progress; /* may be NULL; called on the importing thread */
    void *user;
    const volatile int *cancel;   /* may be NULL; set it to non-zero from any thread to stop (CP0_MAP_E_CANCELLED) */
    char *out_id;                 /* may be NULL: receives the archive id */
    size_t out_id_size;
} cp0_map_import_opts_t;

/* Imports `path` (an MBTiles / PMTiles file or a z/x/y folder) as above. 0, or CP0_MAP_E_READ_ONLY,
 * CP0_MAP_E_NOT_FOUND (no such path), CP0_MAP_E_EXISTS (the id, or an in-place registration of the same file),
 * CP0_MAP_E_FORMAT, CP0_MAP_E_UNSUPPORTED, CP0_MAP_E_CORRUPT, CP0_MAP_E_SQLITE_MISSING, CP0_MAP_E_IO,
 * CP0_MAP_E_NO_TILE (a folder without one tile), CP0_MAP_E_CANCELLED. */
int cp0_map_store_import_ex(cp0_map_store_t *store, const char *path, const cp0_map_import_opts_t *opts);
/* The 3.7 sketch's shape: import_ex with only copy, progress and user set. */
int cp0_map_store_import(cp0_map_store_t *store, const char *path, int copy, cp0_map_progress_cb cb, void *user);
/* Removes an archive: its record, and its file when the store owns it (copy / converted; an in-place file is never
 * deleted), plus a leftover partial conversion or copy. Its tiles leave the RAM cache, and so do the tiles of the
 * "all archives" view (they cannot be told apart), as after every import into this store; an open map widget notices
 * and loads its view again. A second app's RAM cache is not touched (it reads the archives anew at its own
 * cp0_map_store_refresh() and keeps the tiles it has until they are evicted). 0, CP0_MAP_E_READ_ONLY,
 * CP0_MAP_E_NOT_FOUND, CP0_MAP_E_IO. */
int cp0_map_store_remove(cp0_map_store_t *store, const char *archive_id);

typedef struct {
    cp0_map_format_t format;      /* sniffed from the bytes */
    char archive_id[CP0_MAP_ID_MAX]; /* the archive (or source) that answered */
    int stale;                    /* a viewing-cache tile past its expiry (still shown; revalidated when online) */
    int64_t expires;              /* its expiry, seconds since the epoch (0: an archive, or not known) */
} cp0_map_tile_meta_t;

/* Reads tile z/x/y (XYZ) of `id`: an archive id; a source id (its viewing cache: tiles fetched and not yet committed
 * included; never the network); or NULL for the first archive, in id order, whose zooms and bounds cover the tile and
 * that has it. `meta` may be NULL. May wait on the disk.
 * 0, CP0_MAP_E_NO_TILE, CP0_MAP_E_ARCHIVE_MISSING (that archive, or for NULL a covering archive that is missing while
 * no other has the tile), CP0_MAP_E_NOT_FOUND (unknown id), CP0_MAP_E_SQLITE_MISSING, other read errors. */
int cp0_map_store_get_tile(cp0_map_store_t *store, const char *id, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out,
                           cp0_map_tile_meta_t *meta);

/* The store's RAM tile cache (budget cfg.ram_cache_bytes): for the widget's decoded tiles. Owned by the store. */
cp0_map_cache_t *cp0_map_store_ram_cache(cp0_map_store_t *store);

#define CP0_MAP_STATS_SOURCES_MAX 16

typedef struct {
    char id[CP0_MAP_ID_MAX];
    int state;                    /* as cp0_map_source_info_t.state */
    int64_t until;
    int writer;                   /* 1: this app writes the cache and fetches; 0: another app does (or read-only) */
    uint64_t tiles;               /* tiles in the viewing cache */
    uint64_t stale;               /* of which past their expiry */
    uint64_t bytes;               /* the cache file on disk */
    uint32_t fetches_today, daily_cap;
    uint64_t fetched, not_modified, missing, failed, refused; /* since the store was opened */
} cp0_map_source_stats_t;

typedef struct {
    cp0_map_cache_stats_t ram;    /* the RAM tile cache (tiles, hits, misses, bytes, budget) */
    uint64_t disk_bytes;          /* every viewing cache file (tiles/), never the imported archives */
    uint64_t cap;                 /* the cap in force */
    int cap_auto;                 /* 1: from the default rule */
    uint64_t disk_hits, disk_misses; /* viewing cache reads */
    uint64_t tiles, stale;        /* all viewing caches */
    uint32_t fetches_today;       /* all providers */
    int state;                    /* CP0_MAP_OK, CP0_MAP_E_CACHE_FULL, CP0_MAP_E_OFFLINE, CP0_MAP_E_READ_ONLY */
    int nsources;                 /* entries in sources[] (online sources, at most CP0_MAP_STATS_SOURCES_MAX) */
    cp0_map_source_stats_t sources[CP0_MAP_STATS_SOURCES_MAX];
} cp0_map_store_stats_t;

/* The RAM cache, the viewing caches (counted on disk now: may wait on the disk, call it off the LVGL thread or every
 * few seconds) and each online source. 0 or CP0_MAP_E_INVALID. */
int cp0_map_store_cache_stats(cp0_map_store_t *store, cp0_map_store_stats_t *out);

/* -------------------------------------------------------------------------------------------- online sources */

/* Saves the API key of source `id` in <store>/sources/<id>.key (mode 0600, atomically) and uses it from the next
 * request. key: 8..127 characters of [A-Za-z0-9._~-]. A user record on a shipped provider's host without its own key
 * uses the provider's. 0, CP0_MAP_E_INVALID (a bad key), CP0_MAP_E_NOT_FOUND, CP0_MAP_E_READ_ONLY, CP0_MAP_E_IO. */
int cp0_map_source_set_key(cp0_map_store_t *store, const char *id, const char *key);
/* Deletes the key file. 0 (also when there was none), CP0_MAP_E_NOT_FOUND, CP0_MAP_E_READ_ONLY, CP0_MAP_E_IO. */
int cp0_map_source_clear_key(cp0_map_store_t *store, const char *id);
/* Switches a source on or off (the line enabled= of sources/<id>.conf, created for a shipped record; the file's other
 * lines are kept). 0, CP0_MAP_E_NOT_FOUND, CP0_MAP_E_READ_ONLY, CP0_MAP_E_IO. */
int cp0_map_source_set_enabled(cp0_map_store_t *store, const char *id, int on);
/* Lowers a source's daily cap (the line daily_cap=); never above the shipped value (or the record's own). Returns the
 * cap in force (>= 0), or CP0_MAP_E_NOT_FOUND, CP0_MAP_E_READ_ONLY, CP0_MAP_E_IO. */
int cp0_map_source_set_daily_cap(cp0_map_store_t *store, const char *id, uint32_t cap);

/* Network on (1) or off (0): off never fetches and keeps reading the caches (the app's offline mode). 0 or
 * CP0_MAP_E_INVALID. */
int cp0_map_store_set_network(cp0_map_store_t *store, int on);
/* The viewing cache cap in bytes; 0: the default rule. A lower cap evicts at once (on the cache-writer thread;
 * cp0_map_store_flush() waits for it). Imported archives never count and are never touched. 0, CP0_MAP_E_INVALID,
 * CP0_MAP_E_READ_ONLY. */
int cp0_map_store_set_cap(cp0_map_store_t *store, uint64_t bytes);
/* The default rule on its own: the smaller of CP0_MAP_CACHE_CAP_MAX and free_bytes / CP0_MAP_CACHE_CAP_FREE_DIV. */
uint64_t cp0_map_store_default_cap(uint64_t free_bytes);
/* Empties the viewing cache of one source (NULL: of every source): its file is deleted, its missing-tile memory too.
 * 0, CP0_MAP_E_NOT_FOUND, CP0_MAP_E_READ_ONLY (another app writes it), CP0_MAP_E_IO. May wait on the disk. */
int cp0_map_store_clear_cache(cp0_map_store_t *store, const char *source_id);
/* Commits every fetched tile now and waits for a pending eviction (also done by cp0_map_store_close()). Call it from
 * the app's normal shutdown path after a SIGTERM (never from the signal handler itself). 0 or an error. */
int cp0_map_store_flush(cp0_map_store_t *store);

/* Area download (054 2.7 path 2): phase 0.2, not in this version. CP0_MAP_E_REFUSED for a source whose policy has no
 * bulk_ok (OpenTopoMap, Thunderforest), an unknown or refused record, an archive id; CP0_MAP_E_NOT_IN_VERSION for a
 * source that allows it (Geoapify); CP0_MAP_E_INVALID for bad arguments. Nothing is fetched. */
int cp0_map_store_download(cp0_map_store_t *store, const char *source_id, const cp0_map_bbox_t *box, int zmin, int zmax,
                           cp0_map_progress_cb cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_STORE_H */
