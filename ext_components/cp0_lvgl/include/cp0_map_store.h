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
 *   sources/<id>.conf       tile source records (task 3: online sources and their policy; listed, not used yet)
 *   tiles/<id>.mbtiles      the persistent viewing cache of a source (task 3; read when present)
 *   overlays/               points apps publish (054 6.2; not the engine's)
 * Imported archives live apart from the viewing caches so that cache eviction (task 3: the smaller of 512 MB and 10 %
 * of free space) can never touch them: imported archives are never evicted, only removed on request.
 *
 * RECORD (archives/<id>.conf; key=value lines like the launcher's state files; readers ignore unknown keys):
 *   v=1  id=  title=  kind=mbtiles|pmtiles  mode=copy|inplace|converted  path=<the archive file, absolute>
 *   source_path=<what was imported>  format=png|jpg  tile_px=256|512  minzoom=  maxzoom=  bounds=w,s,e,n
 *   attribution=  licence=user supplied: terms are the importer's responsibility  bytes=  tiles=  imported=<UTC>
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
 * READ-ONLY (cfg.read_only = 1, a second app such as Mesh Hop, 054 6.2 "one writer per file"): listing and tiles work,
 * import and remove return CP0_MAP_E_READ_ONLY, nothing is created or written.
 *
 * Threads: every call is safe from any thread (one mutex per store); an import holds it only to register its result,
 * so tiles keep coming while it runs. Tile reads of one store are serialised.
 *
 * Extension points (not in this version): task 2 adds the LVGL widget (cp0_map_widget.h: cp0_map_create(),
 * set_center / zoom / scale, overlays, the tile-load thread over cp0_map_store_get_tile() and the RAM cache);
 * task 3 adds the online sources (the source records' policy flags, cp0_map_store_download(), the persistent viewing
 * cache in tiles/, cp0_map_store_set_cap() and cache eviction, the User-Agent of cfg.user_agent, cfg.network = 1).
 */
#ifndef CP0_MAP_STORE_H
#define CP0_MAP_STORE_H

#include "cp0_map_archive.h"
#include "cp0_map_common.h"
#include "cp0_map_tile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cp0_map_store cp0_map_store_t;

typedef struct {
    const char *store_dir;        /* NULL: $XDG_DATA_HOME/cyberdeckzero/maps/ (cp0_map_store_default_dir) */
    const char *sources_dir;      /* NULL: none; else extra source records an app ships (read-only, listed after
                                     <store_dir>/sources/, which wins on the same id) */
    const char *user_agent;       /* "<app-id>/<version> (+https://github.com/OSRdesign/cyberdeck-zero-apps)": kept
                                     for task 3; no fetching in this version */
    size_t ram_cache_bytes;       /* RAM tile cache budget; 0: from board.state ram_mb (cp0_map_tile.h) */
    int read_only;                /* 1: never writes (a second app) */
    int network;                  /* accepted for task 3; this version never fetches */
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
    int state;                    /* CP0_MAP_OK, or CP0_MAP_E_ARCHIVE_MISSING (in-place file not there) */
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
} cp0_map_source_info_t;

/* Archives (sorted by id) then source records (sorted by id). Fills up to `max` entries of `out` (out may be NULL
 * with max 0) and returns how many there are in all, or an error. Missing in-place archives are checked now. */
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
 * deleted), plus a leftover partial conversion or copy. Its tiles leave the RAM cache. 0, CP0_MAP_E_READ_ONLY,
 * CP0_MAP_E_NOT_FOUND, CP0_MAP_E_IO. */
int cp0_map_store_remove(cp0_map_store_t *store, const char *archive_id);

typedef struct {
    cp0_map_format_t format;      /* sniffed from the bytes */
    char archive_id[CP0_MAP_ID_MAX]; /* the archive (or source) that answered */
} cp0_map_tile_meta_t;

/* Reads tile z/x/y (XYZ) of `id`: an archive id; a source id (its viewing cache, task 3); or NULL for the first
 * archive, in id order, whose zooms and bounds cover the tile and that has it. `meta` may be NULL.
 * 0, CP0_MAP_E_NO_TILE, CP0_MAP_E_ARCHIVE_MISSING (that archive, or for NULL a covering archive that is missing while
 * no other has the tile), CP0_MAP_E_NOT_FOUND (unknown id), CP0_MAP_E_SQLITE_MISSING, other read errors. */
int cp0_map_store_get_tile(cp0_map_store_t *store, const char *id, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out,
                           cp0_map_tile_meta_t *meta);

/* The store's RAM tile cache (budget cfg.ram_cache_bytes): for the widget's decoded tiles. Owned by the store. */
cp0_map_cache_t *cp0_map_store_ram_cache(cp0_map_store_t *store);
/* Its statistics (the disk cache of task 3 is not counted yet). 0 or CP0_MAP_E_INVALID. */
int cp0_map_store_cache_stats(cp0_map_store_t *store, cp0_map_cache_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_STORE_H */
