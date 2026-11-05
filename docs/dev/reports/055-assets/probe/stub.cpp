// SPDX-License-Identifier: MIT
// P0 spike (report 055), M4: the weight of SQLite + the PNG/JPEG decoder in a second app. Throwaway.
// Built three ways with the apps' link style (C++, -O2, dynamic glibc/libstdc++, -Wl,--as-needed):
//   STUB_EMPTY   : a C++ main with the usual std containers, nothing else (the base)
//   STUB_STATIC  : + SQLite amalgamation linked in + stb_image (PNG/JPEG), reads MBTiles and fills a tile cache
//   STUB_DLOPEN  : + stb_image, SQLite through dlopen("libsqlite3.so.0") (the Home Assistant libcurl pattern)
// usage: stub [FILE.mbtiles CACHE_MB]   prints VmRSS at each step
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if !defined(STUB_EMPTY)
#include <dlfcn.h>
#include "../third_party/sqlite/sqlite-amalgamation-3500400/sqlite3.h"
extern "C" unsigned char *stbi_load_from_memory(const unsigned char *b, int n, int *w, int *h, int *c, int want);
extern "C" void stbi_image_free(void *p);
#endif

static long status_kb(const char *key)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long v = -1;
    size_t kl = strlen(key);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == ':') { v = atol(line + kl + 1); break; }
    fclose(f);
    return v;
}

static void mem(const char *tag)
{
    printf("RESULT stubmem tag=%s VmRSS_kb=%ld VmHWM_kb=%ld RssAnon_kb=%ld RssFile_kb=%ld\n", tag, status_kb("VmRSS"),
           status_kb("VmHWM"), status_kb("RssAnon"), status_kb("RssFile"));
}

#if !defined(STUB_EMPTY)
struct Sql {
    int (*open_v2)(const char *, sqlite3 **, int, const char *);
    int (*prepare_v2)(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
    int (*step)(sqlite3_stmt *);
    int (*column_bytes)(sqlite3_stmt *, int);
    const void *(*column_blob)(sqlite3_stmt *, int);
    int (*column_int)(sqlite3_stmt *, int);
    int (*finalize)(sqlite3_stmt *);
    int (*close)(sqlite3 *);
};

static bool load_sql(Sql &s)
{
#if defined(STUB_DLOPEN)
    void *h = dlopen("libsqlite3.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return false; }
#define L(f) *(void **)(&s.f) = dlsym(h, "sqlite3_" #f); if (!s.f) return false
    L(open_v2); L(prepare_v2); L(step); L(column_bytes); L(column_blob); L(column_int); L(finalize); L(close);
#undef L
#else
    s.open_v2 = sqlite3_open_v2; s.prepare_v2 = sqlite3_prepare_v2; s.step = sqlite3_step;
    s.column_bytes = sqlite3_column_bytes; s.column_blob = sqlite3_column_blob; s.column_int = sqlite3_column_int;
    s.finalize = sqlite3_finalize; s.close = sqlite3_close;
#endif
    return true;
}
#endif

int main(int argc, char **argv)
{
    std::vector<std::string> names = {"chats", "contacts", "tools", "settings"};
    std::string joined;
    for (auto &n : names) joined += n + ",";
    mem("start");
#if defined(STUB_EMPTY)
    (void)argc; (void)argv;
    printf("RESULT stub kind=empty base=%zu\n", joined.size());
#else
    if (argc < 3) { fprintf(stderr, "usage: stub FILE.mbtiles CACHE_MB\n"); return 2; }
    Sql s;
    if (!load_sql(s)) return 1;
    mem("sql_loaded");
    sqlite3 *db = nullptr;
    sqlite3_stmt *st = nullptr;
    if (s.open_v2(argv[1], &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) return 1;
    // read the tiles of zoom 14 (the deepest), the way a map widget fills its cache
    if (s.prepare_v2(db, "SELECT tile_data FROM tiles WHERE zoom_level=14", -1, &st, nullptr) != SQLITE_OK) return 1;
    mem("sql_open");
    size_t slots = (size_t)atoi(argv[2]) * 1024 * 1024 / (256 * 256 * 2);
    std::vector<uint16_t *> cache;
    while (cache.size() < slots && s.step(st) == SQLITE_ROW) {
        int w, h, c;
        unsigned char *rgb = stbi_load_from_memory((const unsigned char *)s.column_blob(st, 0), s.column_bytes(st, 0),
                                                   &w, &h, &c, 3);
        if (!rgb) continue;
        uint16_t *d = (uint16_t *)malloc((size_t)w * h * 2);
        for (int i = 0; i < w * h; i++)
            d[i] = (uint16_t)(((rgb[3 * i] & 0xf8) << 8) | ((rgb[3 * i + 1] & 0xfc) << 3) | (rgb[3 * i + 2] >> 3));
        stbi_image_free(rgb);
        cache.push_back(d);
    }
    mem("cache_filled");
    printf("RESULT stub kind=%s cache_mb=%s slots=%zu filled=%zu\n",
#if defined(STUB_DLOPEN)
           "dlopen",
#else
           "static",
#endif
           argv[2], slots, cache.size());
    s.finalize(st);
    s.close(db);
#endif
    return 0;
}
