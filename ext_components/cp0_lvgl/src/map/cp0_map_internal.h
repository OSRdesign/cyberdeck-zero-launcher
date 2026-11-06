/*
 * SPDX-License-Identifier: MIT
 *
 * Private helpers of the map engine (src/map). Not installed, not part of the interface.
 */
#ifndef CP0_MAP_INTERNAL_H
#define CP0_MAP_INTERNAL_H

#include "cp0_map.h"

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#define CP0_MAP_HIDDEN __attribute__((visibility("hidden")))
#else
#define CP0_MAP_HIDDEN
#endif

/* ---- stb_image, compiled once in cp0_map_stb.c with STB_IMAGE_STATIC (thread-local memory ceiling) */
CP0_MAP_HIDDEN int cp0_map__stbi_info(const uint8_t *data, int len, int *w, int *h, int *comp);
CP0_MAP_HIDDEN uint8_t *cp0_map__stbi_load(const uint8_t *data, int len, int *w, int *h, int *comp, int req);
CP0_MAP_HIDDEN void cp0_map__stbi_free(void *p);
/* Raw deflate (no zlib header) into out[olen]; the bytes written, or -1 (bad stream, or more than olen). */
CP0_MAP_HIDDEN int cp0_map__inflate_raw(uint8_t *out, int olen, const uint8_t *in, int ilen);
/* A gzip member -> a new malloc'd buffer of its ISIZE bytes (at most max_out). 0, CP0_MAP_E_CORRUPT, NOMEM, TOO_LARGE. */
CP0_MAP_HIDDEN int cp0_map__gunzip(const uint8_t *in, size_t len, size_t max_out, uint8_t **out, size_t *out_len);

/* ---- small file helpers (cp0_map_util.c) */
/* pread until len bytes or EOF: 0 (all read), CP0_MAP_E_CORRUPT (short: past the end), CP0_MAP_E_IO (errno). */
CP0_MAP_HIDDEN int cp0_map__pread_all(int fd, void *buf, size_t len, uint64_t off);
/* Writes `data` to `path` atomically (temporary file, fsync, rename). 0 or CP0_MAP_E_IO. */
CP0_MAP_HIDDEN int cp0_map__write_atomic(const char *path, const char *data, size_t len);
/* mkdir -p. 0 or CP0_MAP_E_IO. */
CP0_MAP_HIDDEN int cp0_map__mkdirs(const char *path, mode_t mode);
/* snprintf that reports truncation: 0, or CP0_MAP_E_INVALID when it did not fit. */
CP0_MAP_HIDDEN int cp0_map__fmt(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* strncpy that always terminates; control characters (< 0x20, 0x7f) become spaces when `clean`. */
CP0_MAP_HIDDEN void cp0_map__copy_str(char *dst, size_t size, const char *src, int clean);
/* The top-level string value of `key` in a JSON object (escapes decoded, \u to UTF-8). 0, or -1 (absent, not a
 * string, malformed JSON). */
CP0_MAP_HIDDEN int cp0_map__json_string(const char *json, size_t len, const char *key, char *out, size_t size);
/* "name.ext" -> an id: lower-case [a-z0-9._-], other runs become '-', no leading '.', at most CP0_MAP_ID_MAX - 1;
 * "map" when nothing is left. */
CP0_MAP_HIDDEN void cp0_map__make_id(const char *name, char *out, size_t size);
CP0_MAP_HIDDEN int cp0_map__id_valid(const char *id);

/* ---- SQLite loaded at run time (cp0_map_sqlite.c). Only what the engine uses, with its own declarations: no
 * sqlite3.h is needed to build. */
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
typedef void (*cp0_map_sqlite_dtor)(void *);

#define CP0_MAP_SQLITE_OK 0
#define CP0_MAP_SQLITE_BUSY 5
#define CP0_MAP_SQLITE_ROW 100
#define CP0_MAP_SQLITE_DONE 101
#define CP0_MAP_SQLITE_BLOB 4
#define CP0_MAP_SQLITE_NULL 5
#define CP0_MAP_SQLITE_OPEN_READONLY 0x00000001
#define CP0_MAP_SQLITE_OPEN_READWRITE 0x00000002
#define CP0_MAP_SQLITE_OPEN_CREATE 0x00000004
#define CP0_MAP_SQLITE_OPEN_NOMUTEX 0x00008000
#define CP0_MAP_SQLITE_STATIC ((cp0_map_sqlite_dtor)0)
#define CP0_MAP_SQLITE_TRANSIENT ((cp0_map_sqlite_dtor)-1)

typedef struct {
    int (*open_v2)(const char *, sqlite3 **, int, const char *);
    int (*close_v2)(sqlite3 *);
    int (*prepare_v2)(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
    int (*step)(sqlite3_stmt *);
    int (*reset)(sqlite3_stmt *);
    int (*finalize)(sqlite3_stmt *);
    int (*bind_int)(sqlite3_stmt *, int, int);
    int (*bind_int64)(sqlite3_stmt *, int, long long);
    int (*bind_blob)(sqlite3_stmt *, int, const void *, int, cp0_map_sqlite_dtor);
    int (*bind_text)(sqlite3_stmt *, int, const char *, int, cp0_map_sqlite_dtor);
    int (*column_int)(sqlite3_stmt *, int);
    long long (*column_int64)(sqlite3_stmt *, int);
    const void *(*column_blob)(sqlite3_stmt *, int);
    int (*column_bytes)(sqlite3_stmt *, int);
    const unsigned char *(*column_text)(sqlite3_stmt *, int);
    int (*column_type)(sqlite3_stmt *, int);
    int (*exec)(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *, char **);
    const char *(*errmsg)(sqlite3 *);
    int (*extended_errcode)(sqlite3 *);
    int (*busy_timeout)(sqlite3 *, int);
    const char *(*libversion)(void);
} cp0_map_sqlite_api_t;

#define CP0_MAP_SQLITE_LIB_ENV "CP0_MAP_SQLITE_LIB"
#define CP0_MAP_SQLITE_LIB_DEFAULT "libsqlite3.so.0"

/* The loaded library (once per process, thread-safe), or NULL when it cannot be loaded (CP0_MAP_E_SQLITE_MISSING). */
CP0_MAP_HIDDEN const cp0_map_sqlite_api_t *cp0_map__sqlite(void);
/* An SQLite result -> a CP0_MAP code (IOERR and friends: CP0_MAP_E_IO; CORRUPT / NOTADB: CP0_MAP_E_CORRUPT). */
CP0_MAP_HIDDEN int cp0_map__sqlite_code(int rc);

/* ---- archive back ends (cp0_map_mbtiles.c, cp0_map_pmtiles.c); the front end (cp0_map_archive.c) does the
 * "archive missing" logic around them */
typedef struct cp0_map_mbtiles cp0_map_mbtiles_t;
typedef struct cp0_map_pmtiles cp0_map_pmtiles_t;

CP0_MAP_HIDDEN int cp0_map__mbtiles_open(const char *path, cp0_map_mbtiles_t **out, cp0_map_archive_info_t *info);
CP0_MAP_HIDDEN void cp0_map__mbtiles_close(cp0_map_mbtiles_t *m);
CP0_MAP_HIDDEN int cp0_map__mbtiles_get(cp0_map_mbtiles_t *m, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out);
CP0_MAP_HIDDEN int cp0_map__pmtiles_open(const char *path, cp0_map_pmtiles_t **out, cp0_map_archive_info_t *info);
CP0_MAP_HIDDEN void cp0_map__pmtiles_close(cp0_map_pmtiles_t *p);
CP0_MAP_HIDDEN int cp0_map__pmtiles_get(cp0_map_pmtiles_t *p, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out);
/* PMTiles Hilbert tile id (spec v3). */
CP0_MAP_HIDDEN uint64_t cp0_map__pmtiles_tile_id(int z, uint32_t x, uint32_t y);

/* ---- the folder converter (cp0_map_import.c) */
typedef struct {
    int tiles;                    /* tiles in the result */
    int minzoom, maxzoom;
    cp0_map_bbox_t bounds;
    cp0_map_format_t format;      /* the majority format */
    int tile_px;
} cp0_map_convert_result_t;

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_INTERNAL_H */
