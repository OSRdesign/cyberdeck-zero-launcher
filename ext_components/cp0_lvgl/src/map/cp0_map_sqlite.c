/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: SQLite (public domain) loaded at run time with dlopen("libsqlite3.so.0"), the pattern Home Assistant uses
 * for libcurl. Report 055 measured +50 KB per app binary this way against +0.71-1.04 MB for a static SQLite, and every
 * board (Debian 13, Raspberry Pi OS) has the library (package libsqlite3-0, also pulled in by many base packages).
 * Without it, every MBTiles call answers CP0_MAP_E_SQLITE_MISSING and PMTiles keep working.
 *
 * $CP0_MAP_SQLITE_LIB names another library file (the tests point it at a bogus name for the "missing" path). Loaded
 * once per process (pthread_once), never unloaded, RTLD_LOCAL so its symbols never meet an app's own SQLite.
 */
#include "cp0_map_internal.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static cp0_map_sqlite_api_t g_api;
static int g_loaded;

static void load(void)
{
    const char *name = getenv(CP0_MAP_SQLITE_LIB_ENV);
    if (!name || !name[0]) name = CP0_MAP_SQLITE_LIB_DEFAULT;
    void *h = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!h) return;
    cp0_map_sqlite_api_t a;
    memset(&a, 0, sizeof a);
    int ok = 1;
#define SYM(field, name)                                                                                              \
    do {                                                                                                              \
        void *p = dlsym(h, name);                                                                                     \
        if (!p) ok = 0;                                                                                               \
        memcpy(&a.field, &p, sizeof p);                                                                               \
    } while (0)
    SYM(open_v2, "sqlite3_open_v2");
    SYM(close_v2, "sqlite3_close_v2");
    SYM(prepare_v2, "sqlite3_prepare_v2");
    SYM(step, "sqlite3_step");
    SYM(reset, "sqlite3_reset");
    SYM(finalize, "sqlite3_finalize");
    SYM(bind_int, "sqlite3_bind_int");
    SYM(bind_int64, "sqlite3_bind_int64");
    SYM(bind_blob, "sqlite3_bind_blob");
    SYM(bind_text, "sqlite3_bind_text");
    SYM(column_int, "sqlite3_column_int");
    SYM(column_int64, "sqlite3_column_int64");
    SYM(column_blob, "sqlite3_column_blob");
    SYM(column_bytes, "sqlite3_column_bytes");
    SYM(column_text, "sqlite3_column_text");
    SYM(column_type, "sqlite3_column_type");
    SYM(exec, "sqlite3_exec");
    SYM(errmsg, "sqlite3_errmsg");
    SYM(extended_errcode, "sqlite3_extended_errcode");
    SYM(busy_timeout, "sqlite3_busy_timeout");
    SYM(libversion, "sqlite3_libversion");
#undef SYM
    if (!ok) {
        dlclose(h);
        return;
    }
    g_api = a;
    g_loaded = 1;
}

const cp0_map_sqlite_api_t *cp0_map__sqlite(void)
{
    pthread_once(&g_once, load);
    return g_loaded ? &g_api : NULL;
}

int cp0_map__sqlite_code(int rc)
{
    switch (rc & 0xff) {
    case 0: return CP0_MAP_OK;
    case 7: return CP0_MAP_E_NOMEM;     /* SQLITE_NOMEM */
    case 10: return CP0_MAP_E_IO;       /* SQLITE_IOERR */
    case 11: return CP0_MAP_E_CORRUPT;  /* SQLITE_CORRUPT */
    case 13: return CP0_MAP_E_IO;       /* SQLITE_FULL */
    case 14: return CP0_MAP_E_IO;       /* SQLITE_CANTOPEN */
    case 26: return CP0_MAP_E_FORMAT;   /* SQLITE_NOTADB */
    case 8: return CP0_MAP_E_READ_ONLY; /* SQLITE_READONLY */
    default: return CP0_MAP_E_SQLITE;
    }
}
