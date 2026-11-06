/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map_common.h: the types every part of the map engine shares (report 054 section 3.7, task 1 of phase 0.1):
 * result codes, tile formats, a bounding box, a growable byte buffer, the progress record of long jobs.
 *
 * The map engine is a module of cp0_lvgl compiled only with CONFIG_CP0_LVGL_MAP (Kconfig, default n). Include
 * "cp0_map.h" for the whole non-LVGL part. Plain C11 + POSIX, no LVGL, no global state, C++-compatible.
 *
 * Results: every function that can fail returns an int, CP0_MAP_OK (0) or one of the negative CP0_MAP_E_* codes below
 * (counts are returned as values >= 0). cp0_map_strerror() turns a code into a short English text for logs and
 * dialogs. CP0_MAP_E_NO_TILE is the normal answer for a tile an archive does not have; CP0_MAP_E_ARCHIVE_MISSING says
 * the archive file itself is gone (a USB stick pulled out), and the next call tries again.
 */
#ifndef CP0_MAP_COMMON_H
#define CP0_MAP_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version of this interface: raised when a call or a struct changes (calls are only added within one version). */
#define CP0_MAP_API_VERSION 1

#define CP0_MAP_PATH_MAX 512 /* paths the engine keeps or returns, including the terminating 0 */
#define CP0_MAP_ID_MAX 64    /* archive and source ids: [a-z0-9._-], including the terminating 0 */

enum {
    CP0_MAP_OK = 0,
    CP0_MAP_E_INVALID = -1,          /* a bad argument (NULL, a zoom or tile outside the world, a bad size) */
    CP0_MAP_E_NOMEM = -2,            /* out of memory, or a decode past its memory ceiling */
    CP0_MAP_E_IO = -3,               /* a read or write failed on a file that is still there */
    CP0_MAP_E_NO_TILE = -4,          /* the archive has no tile at z/x/y (normal, not an error) */
    CP0_MAP_E_ARCHIVE_MISSING = -5,  /* the archive file is gone (USB stick pulled): retried on the next call */
    CP0_MAP_E_FORMAT = -6,           /* not an MBTiles / PMTiles file, or not a PNG / JPEG tile */
    CP0_MAP_E_CORRUPT = -7,          /* a damaged or truncated archive or tile */
    CP0_MAP_E_UNSUPPORTED = -8,      /* a valid file the engine does not handle (vector tiles, WebP, brotli...) */
    CP0_MAP_E_SQLITE_MISSING = -9,   /* MBTiles needs libsqlite3.so.0 (package libsqlite3-0), which is not installed */
    CP0_MAP_E_SQLITE = -10,          /* SQLite reported an error */
    CP0_MAP_E_DECODE = -11,          /* the image decoder refused the tile */
    CP0_MAP_E_TOO_LARGE = -12,       /* a tile or picture over the engine's limits */
    CP0_MAP_E_READ_ONLY = -13,       /* the store was opened read-only (a second app) */
    CP0_MAP_E_EXISTS = -14,          /* an archive with that id (or that in-place path) is already in the store */
    CP0_MAP_E_NOT_FOUND = -15,       /* no archive or source with that id; an import path that does not exist */
    CP0_MAP_E_CANCELLED = -16,       /* a job stopped by its cancel flag (a folder import can be resumed) */
    CP0_MAP_E_FULL = -17,            /* RAM tile cache: every cached tile is in use, the new one does not fit */
    /* online sources (cp0_map_source.h, task 3): why a source does not fetch now, or how a fetch ended */
    CP0_MAP_E_REFUSED = -18,         /* refused by the source policy (an OSMF host, plain http, no area download...) */
    CP0_MAP_E_NETWORK = -19,         /* no connection, a timeout, a server error: cached tiles only for a while */
    CP0_MAP_E_CURL_MISSING = -20,    /* network unavailable: libcurl.so.4 (package libcurl4t64) is not installed */
    CP0_MAP_E_BLOCKED = -21,         /* the server answered with its "blocked" picture: the source is paused */
    CP0_MAP_E_CAP_REACHED = -22,     /* the source's daily request cap is reached until local midnight */
    CP0_MAP_E_NO_KEY = -23,          /* the source needs an API key (cp0_map_source_set_key) */
    CP0_MAP_E_DISABLED = -24,        /* the source is switched off (cp0_map_source_set_enabled) */
    CP0_MAP_E_OFFLINE = -25,         /* network off (cfg.network = 0, cp0_map_store_set_network): cached tiles only */
    CP0_MAP_E_BACKOFF = -26,         /* the server asked to slow down (429 / 503, Retry-After): paused for a while */
    CP0_MAP_E_NOT_IN_VERSION = -27,  /* not in this version of the engine (area download: phase 0.2) */
    CP0_MAP_E_CACHE_FULL = -28,      /* the viewing cache cannot grow (little free space, a failed write) */
    CP0_MAP_E_HTTP = -29,            /* an answer the engine does not accept (not an image, too many redirects...) */
    CP0_MAP_E_NO_USER_AGENT = -30,   /* no usable User-Agent (cfg.user_agent): the engine never fetches without one */
};

/* A short English text for a CP0_MAP_* code ("SQLite library missing (install libsqlite3-0)"), never NULL. */
const char *cp0_map_strerror(int code);

/* Tile image formats. MVT (vector) and WebP are recognised so they can be refused with CP0_MAP_E_UNSUPPORTED. */
typedef enum {
    CP0_MAP_FMT_UNKNOWN = 0,
    CP0_MAP_FMT_PNG,
    CP0_MAP_FMT_JPEG,
    CP0_MAP_FMT_WEBP,
    CP0_MAP_FMT_MVT,
} cp0_map_format_t;

/* "png", "jpg", "webp", "pbf" (the MBTiles metadata spellings), "" for unknown. */
const char *cp0_map_format_name(cp0_map_format_t fmt);
/* "png", "jpg", "jpeg", "webp", "pbf", "mvt" (any case) -> the format; UNKNOWN otherwise. */
cp0_map_format_t cp0_map_format_parse(const char *s);
/* From the first bytes of a tile: PNG and JPEG signatures, RIFF/WEBP, gzip or 0x1a (a compressed or raw MVT). */
cp0_map_format_t cp0_map_format_sniff(const uint8_t *data, size_t len);

/* A box in WGS84 degrees, west <= east (boxes across the antimeridian are not supported). */
typedef struct {
    double west, south, east, north;
} cp0_map_bbox_t;

/* A growable byte buffer. Start zeroed ({0}); the engine reallocates `data` as needed and sets `len`. Reuse one buffer
 * for many reads (fewer allocations on the board); free it with cp0_map_buf_free(). */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} cp0_map_buf_t;

void cp0_map_buf_free(cp0_map_buf_t *buf);
/* Makes room for `need` bytes (cap >= need). 0 or CP0_MAP_E_NOMEM. */
int cp0_map_buf_reserve(cp0_map_buf_t *buf, size_t need);

/* Progress of a long job (an import). Called on the job's thread: marshal to the LVGL thread before touching LVGL. */
typedef enum {
    CP0_MAP_PHASE_SCAN = 1,    /* counting the tiles of a folder */
    CP0_MAP_PHASE_COPY,        /* copying an archive file into the store (bytes) */
    CP0_MAP_PHASE_CONVERT,     /* writing a folder's tiles into one MBTiles (tiles) */
    CP0_MAP_PHASE_CHECK,       /* opening the result and reading its metadata */
} cp0_map_phase_t;

typedef struct {
    cp0_map_phase_t phase;
    uint64_t done, total;      /* tiles (scan, convert) or bytes (copy); total 0 = not known yet */
    uint64_t skipped;          /* convert: files that are not PNG / JPEG tiles (or over the size limit), left out */
    uint64_t resumed;          /* convert: tiles already in the partial file of an earlier, interrupted import */
} cp0_map_progress_t;

typedef void (*cp0_map_progress_cb)(void *user, const cp0_map_progress_t *progress);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_COMMON_H */
