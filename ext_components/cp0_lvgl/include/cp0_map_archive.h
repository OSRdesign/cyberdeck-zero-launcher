/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map_archive.h: one tile archive file, read-only, behind one small interface (report 054 sections 2.4 and 3.7,
 * read paths measured in report 055 section 3.3).
 *
 *   MBTiles 1.3   an SQLite file: `metadata` (name, value) and `tiles` (zoom_level, tile_column, tile_row in TMS
 *                 order, tile_data), a table or a view (mbutil's deduplicated layout works). Read with plain SQLite
 *                 opened read-only, a 256 KiB page cache, no mmap. SQLite is libsqlite3.so.0 loaded at run time
 *                 (dlopen): without it MBTiles calls fail with CP0_MAP_E_SQLITE_MISSING and PMTiles still work. The
 *                 environment variable CP0_MAP_SQLITE_LIB names another library (tests point it at a bogus name).
 *   PMTiles v3    one file: header, varint directories (root, leaves; none or gzip compression, inflated by
 *                 stb_image's zlib), tiles by offset. Read with pread() only, never mmap (055: mmap counts the pages
 *                 touched in VmRSS). Tile types PNG and JPEG; MVT / WebP / AVIF are reported, and get_tile returns
 *                 their bytes, for the caller to refuse.
 *
 * Both return a tile's raw bytes (PNG / JPEG as stored) and its format, sniffed from the bytes. Rows are always XYZ
 * (y from the north); the MBTiles reader flips them.
 *
 * ARCHIVE MISSING. Every get_tile checks that the path still names the file that was opened (stat: same device and
 * inode). When the file is gone (a USB stick pulled, the file deleted) the call returns CP0_MAP_E_ARCHIVE_MISSING and
 * the reader closes its handle; the next call looks again and reopens the file when it is back. A file replaced by
 * another one under the same path is reopened. Never a crash: damaged and truncated files give CP0_MAP_E_CORRUPT /
 * CP0_MAP_E_FORMAT.
 *
 * Threads: one archive handle is not thread-safe; use it from one thread at a time (the store serialises its own).
 */
#ifndef CP0_MAP_ARCHIVE_H
#define CP0_MAP_ARCHIVE_H

#include "cp0_map_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cp0_map_archive cp0_map_archive_t;

typedef enum {
    CP0_MAP_ARCHIVE_NONE = 0,
    CP0_MAP_ARCHIVE_MBTILES = 1,
    CP0_MAP_ARCHIVE_PMTILES = 2,
} cp0_map_archive_kind_t;

typedef struct {
    cp0_map_archive_kind_t kind;
    cp0_map_format_t format;    /* declared tile format (MBTiles `format`, PMTiles tile type), else from a sample */
    int tile_px;                /* 256 or 512 (from one sample tile's header); 0 when no tile could be read */
    int minzoom, maxzoom;       /* -1 when the archive has no tiles */
    int has_bounds;             /* 1: `bounds` is set (MBTiles metadata, PMTiles header) */
    cp0_map_bbox_t bounds;
    char name[128];             /* MBTiles `name`, PMTiles metadata "name"; "" when absent */
    char attribution[256];      /* MBTiles `attribution`, PMTiles metadata "attribution" (may hold HTML) */
    uint64_t bytes;             /* file size */
} cp0_map_archive_info_t;

/* "mbtiles", "pmtiles", "". */
const char *cp0_map_archive_kind_name(cp0_map_archive_kind_t kind);
/* What the file is, from its first bytes ("SQLite format 3", "PMTiles" v3). 0, CP0_MAP_E_ARCHIVE_MISSING (no such
 * file), CP0_MAP_E_IO, CP0_MAP_E_FORMAT (neither; a PMTiles of another version is CP0_MAP_E_UNSUPPORTED). */
int cp0_map_archive_sniff(const char *path, cp0_map_archive_kind_t *kind);

/* Opens an archive read-only and reads its metadata. 0 with *out set, or an error (*out NULL):
 * CP0_MAP_E_ARCHIVE_MISSING, CP0_MAP_E_IO, CP0_MAP_E_FORMAT, CP0_MAP_E_CORRUPT, CP0_MAP_E_UNSUPPORTED (a PMTiles
 * with brotli / zstd directories), CP0_MAP_E_SQLITE_MISSING, CP0_MAP_E_SQLITE, CP0_MAP_E_NOMEM. */
int cp0_map_archive_open(const char *path, cp0_map_archive_t **out);
void cp0_map_archive_close(cp0_map_archive_t *archive);
/* The metadata read at open. 0 or CP0_MAP_E_INVALID. */
int cp0_map_archive_info(const cp0_map_archive_t *archive, cp0_map_archive_info_t *out);
const char *cp0_map_archive_path(const cp0_map_archive_t *archive);

/* Reads tile z/x/y (XYZ) into `out` (len set) and its sniffed format into *fmt (may be NULL). 0, CP0_MAP_E_NO_TILE,
 * CP0_MAP_E_ARCHIVE_MISSING, CP0_MAP_E_INVALID (outside the world), CP0_MAP_E_CORRUPT, CP0_MAP_E_TOO_LARGE (over
 * CP0_MAP_TILE_BYTES_MAX), CP0_MAP_E_IO, CP0_MAP_E_SQLITE, CP0_MAP_E_NOMEM. */
int cp0_map_archive_get_tile(cp0_map_archive_t *archive, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out,
                             cp0_map_format_t *fmt);
/* CP0_MAP_OK while the file is there (reopening it if it came back), else CP0_MAP_E_ARCHIVE_MISSING. */
int cp0_map_archive_check(cp0_map_archive_t *archive);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_ARCHIVE_H */
