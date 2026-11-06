/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: the shared store, its records, the listing, tile lookup and removal (see cp0_map_store.h). Imports are
 * in cp0_map_import.c.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* realpath, O_CLOEXEC */
#endif
#include "cp0_map_store_priv.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int cp0_map_store_default_dir(char *buf, size_t size)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] == '/') return cp0_map__fmt(buf, size, "%s/cyberdeckzero/maps", xdg);
    const char *home = getenv("HOME");
    if (!home || !home[0]) return CP0_MAP_E_INVALID;
    return cp0_map__fmt(buf, size, "%s/.local/share/cyberdeckzero/maps", home);
}

int cp0_map__archive_file(const cp0_map_store_t *s, const char *id, const char *suffix, char *buf, size_t size)
{
    return cp0_map__fmt(buf, size, "%s/archives/%s%s", s->dir, id, suffix);
}

int cp0_map__record_exists(const cp0_map_store_t *s, const char *id)
{
    char path[CP0_MAP_PATH_MAX];
    struct stat st;
    return cp0_map__archive_file(s, id, ".conf", path, sizeof path) == 0 && stat(path, &st) == 0;
}

/* ------------------------------------------------------------------------------------------------------ records */

static const char *mode_name(cp0_map_import_mode_t m)
{
    switch (m) {
    case CP0_MAP_MODE_COPY: return "copy";
    case CP0_MAP_MODE_INPLACE: return "inplace";
    case CP0_MAP_MODE_CONVERTED: return "converted";
    default: return "";
    }
}

static cp0_map_import_mode_t mode_parse(const char *s)
{
    if (!strcmp(s, "copy")) return CP0_MAP_MODE_COPY;
    if (!strcmp(s, "inplace")) return CP0_MAP_MODE_INPLACE;
    if (!strcmp(s, "converted")) return CP0_MAP_MODE_CONVERTED;
    return CP0_MAP_MODE_NONE;
}

/* appends "key=value\n" with control characters as spaces; 0 or -1 when it does not fit */
static int put_kv(char *buf, size_t size, size_t *len, const char *key, const char *value)
{
    char clean[CP0_MAP_PATH_MAX + 64];
    cp0_map__copy_str(clean, sizeof clean, value, 1);
    const int n = snprintf(buf + *len, size - *len, "%s=%s\n", key, clean);
    if (n < 0 || (size_t)n >= size - *len) return -1;
    *len += (size_t)n;
    return 0;
}

int cp0_map__record_write(const cp0_map_store_t *s, const cp0_map_rec_t *r)
{
    char buf[CP0_MAP_RECORD_MAX], num[64], path[CP0_MAP_PATH_MAX];
    size_t len = 0;
    const cp0_map_source_info_t *i = &r->info;
    int bad = put_kv(buf, sizeof buf, &len, "v", "1") || put_kv(buf, sizeof buf, &len, "id", i->id) ||
              put_kv(buf, sizeof buf, &len, "title", i->title) ||
              put_kv(buf, sizeof buf, &len, "kind", cp0_map_archive_kind_name(i->archive)) ||
              put_kv(buf, sizeof buf, &len, "mode", mode_name(i->mode)) ||
              put_kv(buf, sizeof buf, &len, "path", i->path) ||
              put_kv(buf, sizeof buf, &len, "source_path", r->source_path) ||
              put_kv(buf, sizeof buf, &len, "format", cp0_map_format_name(i->format));
    snprintf(num, sizeof num, "%d", i->tile_px);
    bad = bad || put_kv(buf, sizeof buf, &len, "tile_px", num);
    snprintf(num, sizeof num, "%d", i->minzoom);
    bad = bad || put_kv(buf, sizeof buf, &len, "minzoom", num);
    snprintf(num, sizeof num, "%d", i->maxzoom);
    bad = bad || put_kv(buf, sizeof buf, &len, "maxzoom", num);
    if (i->has_bounds && cp0_map_bbox_format(&i->bounds, num, sizeof num) > 0)
        bad = bad || put_kv(buf, sizeof buf, &len, "bounds", num);
    bad = bad || put_kv(buf, sizeof buf, &len, "attribution", i->attribution) ||
          put_kv(buf, sizeof buf, &len, "licence", CP0_MAP_LICENCE_NOTE);
    snprintf(num, sizeof num, "%llu", (unsigned long long)i->bytes);
    bad = bad || put_kv(buf, sizeof buf, &len, "bytes", num);
    if (r->tiles >= 0) {
        snprintf(num, sizeof num, "%lld", r->tiles);
        bad = bad || put_kv(buf, sizeof buf, &len, "tiles", num);
    }
    const time_t now = time(NULL);
    struct tm tm;
    if (gmtime_r(&now, &tm) && strftime(num, sizeof num, "%Y-%m-%dT%H:%M:%SZ", &tm))
        bad = bad || put_kv(buf, sizeof buf, &len, "imported", num);
    if (bad) return CP0_MAP_E_INVALID;
    if (cp0_map__archive_file(s, i->id, ".conf", path, sizeof path)) return CP0_MAP_E_INVALID;
    return cp0_map__write_atomic(path, buf, len);
}

static int read_small_file(const char *path, char *buf, size_t size, size_t *len)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t n = 0;
    while (n + 1 < size) {
        const ssize_t r = read(fd, buf + n, size - 1 - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        n += (size_t)r;
    }
    close(fd);
    buf[n] = '\0';
    *len = n;
    return 0;
}

static int parse_int(const char *s, int lo, int hi, int def)
{
    char *end;
    const long v = strtol(s, &end, 10);
    return (end == s || *end || v < lo || v > hi) ? def : (int)v;
}

/* one record file -> r. 0, or -1 (not v=1, no usable id) */
static int record_parse(const char *text, const char *stem, cp0_map_entry_kind_t kind, const cp0_map_store_t *s,
                        cp0_map_rec_t *r)
{
    memset(r, 0, sizeof *r);
    r->tiles = -1;
    cp0_map_source_info_t *i = &r->info;
    i->kind = kind;
    i->minzoom = i->maxzoom = -1;
    int v1 = 0;
    char id[CP0_MAP_ID_MAX] = "";
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        const size_t n = eol ? (size_t)(eol - p) : strlen(p);
        char line[CP0_MAP_PATH_MAX + 64];
        if (n < sizeof line) {
            memcpy(line, p, n);
            line[n] = '\0';
            if (n && line[n - 1] == '\r') line[n - 1] = '\0';
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                const char *k = line, *v = eq + 1;
                if (!strcmp(k, "v")) v1 = !strcmp(v, "1");
                else if (!strcmp(k, "id")) cp0_map__copy_str(id, sizeof id, v, 1);
                else if (!strcmp(k, "title") || (!strcmp(k, "name") && !i->title[0]))
                    cp0_map__copy_str(i->title, sizeof i->title, v, 1);
                else if (!strcmp(k, "kind")) {
                    if (!strcmp(v, "mbtiles")) i->archive = CP0_MAP_ARCHIVE_MBTILES;
                    else if (!strcmp(v, "pmtiles")) i->archive = CP0_MAP_ARCHIVE_PMTILES;
                } else if (!strcmp(k, "mode")) i->mode = mode_parse(v);
                else if (!strcmp(k, "path")) cp0_map__copy_str(i->path, sizeof i->path, v, 1);
                else if (!strcmp(k, "source_path")) cp0_map__copy_str(r->source_path, sizeof r->source_path, v, 1);
                else if (!strcmp(k, "format")) i->format = cp0_map_format_parse(v);
                else if (!strcmp(k, "tile_px")) i->tile_px = parse_int(v, 0, 4096, 0);
                else if (!strcmp(k, "minzoom")) i->minzoom = parse_int(v, -1, 30, -1);
                else if (!strcmp(k, "maxzoom")) i->maxzoom = parse_int(v, -1, 30, -1);
                else if (!strcmp(k, "bounds")) i->has_bounds = cp0_map_bbox_parse(v, &i->bounds) == CP0_MAP_OK;
                else if (!strcmp(k, "attribution")) cp0_map__copy_str(i->attribution, sizeof i->attribution, v, 1);
                else if (!strcmp(k, "tiles")) r->tiles = strtoll(v, NULL, 10);
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    if (!v1) return -1;
    /* the file name is the id; an id key that disagrees is ignored */
    if (!cp0_map__id_valid(stem)) return -1;
    (void)id;
    cp0_map__copy_str(i->id, sizeof i->id, stem, 0);
    if (!i->title[0]) cp0_map__copy_str(i->title, sizeof i->title, stem, 0);
    if (kind == CP0_MAP_ENTRY_SOURCE) {
        /* a source's tiles: its viewing cache store (task 3) */
        if (cp0_map__fmt(i->path, sizeof i->path, "%s/tiles/%s.mbtiles", s->dir, stem)) return -1;
        i->archive = CP0_MAP_ARCHIVE_MBTILES;
    } else if (!i->path[0] || i->archive == CP0_MAP_ARCHIVE_NONE) {
        return -1;
    }
    return 0;
}

static int cmp_rec(const void *a, const void *b)
{
    return strcmp(((const cp0_map_rec_t *)a)->info.id, ((const cp0_map_rec_t *)b)->info.id);
}

static int has_id(const cp0_map_rec_t *recs, int from, int to, const char *id)
{
    for (int k = from; k < to; k++)
        if (!strcmp(recs[k].info.id, id)) return 1;
    return 0;
}

/* appends the records of the .conf files in dir; 0 or CP0_MAP_E_NOMEM */
static int load_dir(const cp0_map_store_t *s, const char *dir, cp0_map_entry_kind_t kind, cp0_map_rec_t **recs,
                    int *n, int *cap, int group_start)
{
    DIR *d = opendir(dir);
    if (!d) return CP0_MAP_OK;
    struct dirent *e;
    char path[CP0_MAP_PATH_MAX], *text = (char *)malloc(CP0_MAP_RECORD_MAX);
    if (!text) {
        closedir(d);
        return CP0_MAP_E_NOMEM;
    }
    int rc = CP0_MAP_OK;
    while ((e = readdir(d))) {
        const size_t len = strlen(e->d_name);
        if (len < 6 || e->d_name[0] == '.' || strcmp(e->d_name + len - 5, ".conf") != 0) continue;
        char stem[CP0_MAP_ID_MAX];
        if (len - 5 >= sizeof stem) continue;
        memcpy(stem, e->d_name, len - 5);
        stem[len - 5] = '\0';
        if (has_id(*recs, group_start, *n, stem)) continue; /* an earlier folder wins */
        size_t tl;
        if (cp0_map__fmt(path, sizeof path, "%s/%s", dir, e->d_name) || read_small_file(path, text, CP0_MAP_RECORD_MAX, &tl))
            continue;
        if (*n == *cap) {
            const int nc = *cap ? *cap * 2 : 16;
            cp0_map_rec_t *nr = (cp0_map_rec_t *)realloc(*recs, (size_t)nc * sizeof **recs);
            if (!nr) {
                rc = CP0_MAP_E_NOMEM;
                break;
            }
            *recs = nr;
            *cap = nc;
        }
        if (record_parse(text, stem, kind, s, &(*recs)[*n]) == 0) (*n)++;
    }
    free(text);
    closedir(d);
    return rc;
}

static void close_archives(cp0_map_store_t *s)
{
    for (int k = 0; k < s->nrecs; k++) {
        cp0_map_archive_close(s->recs[k].archive);
        s->recs[k].archive = NULL;
    }
}

int cp0_map__store_reload_locked(cp0_map_store_t *s)
{
    cp0_map_rec_t *recs = NULL;
    int n = 0, cap = 0;
    char dir[CP0_MAP_PATH_MAX];
    int rc = cp0_map__fmt(dir, sizeof dir, "%s/archives", s->dir);
    if (!rc) rc = load_dir(s, dir, CP0_MAP_ENTRY_ARCHIVE, &recs, &n, &cap, 0);
    const int narch = n;
    if (!rc) rc = cp0_map__fmt(dir, sizeof dir, "%s/sources", s->dir);
    if (!rc) rc = load_dir(s, dir, CP0_MAP_ENTRY_SOURCE, &recs, &n, &cap, narch);
    if (!rc && s->sources_dir[0]) rc = load_dir(s, s->sources_dir, CP0_MAP_ENTRY_SOURCE, &recs, &n, &cap, narch);
    if (rc) {
        free(recs);
        return rc;
    }
    if (narch > 1) qsort(recs, (size_t)narch, sizeof *recs, cmp_rec);
    if (n - narch > 1) qsort(recs + narch, (size_t)(n - narch), sizeof *recs, cmp_rec);
    close_archives(s);
    free(s->recs);
    s->recs = recs;
    s->nrecs = n;
    s->narchives = narch;
    return CP0_MAP_OK;
}

/* -------------------------------------------------------------------------------------------------- open, close */

int cp0_map_store_open(const cp0_map_store_cfg_t *cfg, cp0_map_store_t **out)
{
    if (!out) return CP0_MAP_E_INVALID;
    *out = NULL;
    cp0_map_store_cfg_t defaults;
    memset(&defaults, 0, sizeof defaults);
    if (!cfg) cfg = &defaults;
    cp0_map_store_t *s = (cp0_map_store_t *)calloc(1, sizeof *s);
    if (!s) return CP0_MAP_E_NOMEM;
    int rc = cfg->store_dir && cfg->store_dir[0] ? cp0_map__fmt(s->dir, sizeof s->dir, "%s", cfg->store_dir)
                                                 : cp0_map_store_default_dir(s->dir, sizeof s->dir);
    if (!rc && cfg->sources_dir && cfg->sources_dir[0])
        rc = cp0_map__fmt(s->sources_dir, sizeof s->sources_dir, "%s", cfg->sources_dir);
    size_t dl = strlen(s->dir);
    while (dl > 1 && s->dir[dl - 1] == '/') s->dir[--dl] = '\0';
    s->read_only = cfg->read_only != 0;
    if (!rc && !s->read_only) {
        char sub[CP0_MAP_PATH_MAX];
        rc = cp0_map__fmt(sub, sizeof sub, "%s/archives", s->dir);
        if (!rc) rc = cp0_map__mkdirs(sub, 0755);
        if (!rc) rc = cp0_map__fmt(sub, sizeof sub, "%s/sources", s->dir);
        if (!rc) rc = cp0_map__mkdirs(sub, 0755);
    }
    if (!rc) {
        /* records keep absolute paths: resolve a relative store folder once */
        char abs[CP0_MAP_PATH_MAX * 2];
        if (s->dir[0] != '/' && realpath(s->dir, abs) && strlen(abs) < sizeof s->dir)
            cp0_map__copy_str(s->dir, sizeof s->dir, abs, 0);
    }
    if (rc) {
        free(s);
        return rc;
    }
    s->ram = cp0_map_cache_new(cfg->ram_cache_bytes);
    if (!s->ram || pthread_mutex_init(&s->mu, NULL) != 0) {
        cp0_map_cache_free(s->ram);
        free(s);
        return CP0_MAP_E_NOMEM;
    }
    pthread_mutex_init(&s->import_mu, NULL);
    pthread_mutex_lock(&s->mu);
    rc = cp0_map__store_reload_locked(s);
    pthread_mutex_unlock(&s->mu);
    if (rc) {
        cp0_map_store_close(s);
        return rc;
    }
    *out = s;
    return CP0_MAP_OK;
}

void cp0_map_store_close(cp0_map_store_t *s)
{
    if (!s) return;
    pthread_mutex_lock(&s->mu);
    close_archives(s);
    free(s->recs);
    s->recs = NULL;
    s->nrecs = s->narchives = 0;
    pthread_mutex_unlock(&s->mu);
    cp0_map_cache_free(s->ram);
    pthread_mutex_destroy(&s->mu);
    pthread_mutex_destroy(&s->import_mu);
    free(s);
}

const char *cp0_map_store_dir(const cp0_map_store_t *s)
{
    return s ? s->dir : "";
}

int cp0_map_store_refresh(cp0_map_store_t *s)
{
    if (!s) return CP0_MAP_E_INVALID;
    pthread_mutex_lock(&s->mu);
    const int rc = cp0_map__store_reload_locked(s);
    pthread_mutex_unlock(&s->mu);
    return rc;
}

cp0_map_cache_t *cp0_map_store_ram_cache(cp0_map_store_t *s)
{
    return s ? s->ram : NULL;
}

int cp0_map_store_cache_stats(cp0_map_store_t *s, cp0_map_cache_stats_t *out)
{
    if (!s || !out) return CP0_MAP_E_INVALID;
    cp0_map_cache_get_stats(s->ram, out);
    return CP0_MAP_OK;
}

/* ------------------------------------------------------------------------------------------------------ listing */

static void fill_state(cp0_map_rec_t *r)
{
    struct stat st;
    if (stat(r->info.path, &st) == 0 && S_ISREG(st.st_mode)) {
        r->info.state = CP0_MAP_OK;
        r->info.bytes = (uint64_t)st.st_size;
    } else {
        /* a source without a viewing cache yet is not "missing": it has no tiles */
        r->info.state = r->info.kind == CP0_MAP_ENTRY_SOURCE ? CP0_MAP_OK : CP0_MAP_E_ARCHIVE_MISSING;
        r->info.bytes = 0;
    }
}

int cp0_map_store_sources(cp0_map_store_t *s, cp0_map_source_info_t *out, int max)
{
    if (!s || max < 0 || (max > 0 && !out)) return CP0_MAP_E_INVALID;
    pthread_mutex_lock(&s->mu);
    for (int k = 0; k < s->nrecs; k++) {
        fill_state(&s->recs[k]);
        if (k < max) out[k] = s->recs[k].info;
    }
    const int n = s->nrecs;
    pthread_mutex_unlock(&s->mu);
    return n;
}

static cp0_map_rec_t *find_rec(cp0_map_store_t *s, const char *id)
{
    for (int k = 0; k < s->nrecs; k++)
        if (!strcmp(s->recs[k].info.id, id)) return &s->recs[k];
    return NULL;
}

int cp0_map_store_find(cp0_map_store_t *s, const char *id, cp0_map_source_info_t *out)
{
    if (!s || !id || !out) return CP0_MAP_E_INVALID;
    pthread_mutex_lock(&s->mu);
    cp0_map_rec_t *r = find_rec(s, id);
    if (r) {
        fill_state(r);
        *out = r->info;
    }
    pthread_mutex_unlock(&s->mu);
    return r ? CP0_MAP_OK : CP0_MAP_E_NOT_FOUND;
}

/* ---------------------------------------------------------------------------------------------------- tile read */

static int rec_get(cp0_map_rec_t *r, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out, cp0_map_format_t *fmt)
{
    if (!r->archive) {
        if (r->info.kind == CP0_MAP_ENTRY_SOURCE) {
            struct stat st;
            if (stat(r->info.path, &st) != 0) return CP0_MAP_E_NO_TILE; /* no viewing cache yet */
        }
        const int rc = cp0_map_archive_open(r->info.path, &r->archive);
        if (rc) return (rc == CP0_MAP_E_ARCHIVE_MISSING && r->info.kind == CP0_MAP_ENTRY_SOURCE) ? CP0_MAP_E_NO_TILE : rc;
    }
    return cp0_map_archive_get_tile(r->archive, z, x, y, out, fmt);
}

static int covers(const cp0_map_rec_t *r, int z, uint32_t x, uint32_t y)
{
    if (r->info.minzoom >= 0 && z < r->info.minzoom) return 0;
    if (r->info.maxzoom >= 0 && z > r->info.maxzoom) return 0;
    if (r->info.has_bounds && z <= CP0_MAP_MAX_ZOOM) {
        cp0_map_bbox_t t;
        if (cp0_map_tile_bbox(z, x, y, &t) == CP0_MAP_OK && !cp0_map_bbox_intersects(&t, &r->info.bounds)) return 0;
    }
    return 1;
}

int cp0_map_store_get_tile(cp0_map_store_t *s, const char *id, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out,
                           cp0_map_tile_meta_t *meta)
{
    if (meta) memset(meta, 0, sizeof *meta);
    if (!s || !out) return CP0_MAP_E_INVALID;
    if (!cp0_map_tile_valid(z, x, y)) return CP0_MAP_E_INVALID;
    cp0_map_format_t fmt = CP0_MAP_FMT_UNKNOWN;
    int rc;
    pthread_mutex_lock(&s->mu);
    if (id) {
        cp0_map_rec_t *r = find_rec(s, id);
        rc = r ? rec_get(r, z, x, y, out, &fmt) : CP0_MAP_E_NOT_FOUND;
        if (rc == CP0_MAP_OK && meta) cp0_map__copy_str(meta->archive_id, sizeof meta->archive_id, r->info.id, 0);
    } else {
        int missing = 0, other = CP0_MAP_OK;
        rc = CP0_MAP_E_NO_TILE;
        for (int k = 0; k < s->narchives; k++) {
            cp0_map_rec_t *r = &s->recs[k];
            if (!covers(r, z, x, y)) continue;
            const int e = rec_get(r, z, x, y, out, &fmt);
            if (e == CP0_MAP_OK) {
                rc = CP0_MAP_OK;
                if (meta) cp0_map__copy_str(meta->archive_id, sizeof meta->archive_id, r->info.id, 0);
                break;
            }
            if (e == CP0_MAP_E_ARCHIVE_MISSING) missing = 1;
            else if (e != CP0_MAP_E_NO_TILE && other == CP0_MAP_OK) other = e;
        }
        if (rc != CP0_MAP_OK) rc = missing ? CP0_MAP_E_ARCHIVE_MISSING : other != CP0_MAP_OK ? other : CP0_MAP_E_NO_TILE;
    }
    pthread_mutex_unlock(&s->mu);
    if (rc == CP0_MAP_OK && meta) meta->format = fmt;
    return rc;
}

/* ------------------------------------------------------------------------------------------------------- remove */

static int inside_archives(const cp0_map_store_t *s, const char *path)
{
    char prefix[CP0_MAP_PATH_MAX];
    if (cp0_map__fmt(prefix, sizeof prefix, "%s/archives/", s->dir)) return 0;
    return strncmp(path, prefix, strlen(prefix)) == 0 && !strstr(path, "/../");
}

int cp0_map_store_remove(cp0_map_store_t *s, const char *id)
{
    if (!s || !id) return CP0_MAP_E_INVALID;
    if (s->read_only) return CP0_MAP_E_READ_ONLY;
    if (!cp0_map__id_valid(id)) return CP0_MAP_E_NOT_FOUND;
    char conf[CP0_MAP_PATH_MAX], part[CP0_MAP_PATH_MAX], file[CP0_MAP_PATH_MAX] = "";
    if (cp0_map__archive_file(s, id, ".conf", conf, sizeof conf) ||
        cp0_map__archive_file(s, id, ".mbtiles.part", part, sizeof part))
        return CP0_MAP_E_INVALID;
    pthread_mutex_lock(&s->import_mu);
    pthread_mutex_lock(&s->mu);
    int found = 0, rc = CP0_MAP_OK;
    for (int k = 0; k < s->narchives; k++) {
        cp0_map_rec_t *r = &s->recs[k];
        if (strcmp(r->info.id, id) != 0) continue;
        found = 1;
        cp0_map_archive_close(r->archive);
        r->archive = NULL;
        if (r->info.mode != CP0_MAP_MODE_INPLACE && inside_archives(s, r->info.path))
            cp0_map__copy_str(file, sizeof file, r->info.path, 0);
    }
    if (unlink(conf) == 0) found = 1;
    else if (errno != ENOENT) rc = CP0_MAP_E_IO;
    if (!rc && file[0] && unlink(file) != 0 && errno != ENOENT) rc = CP0_MAP_E_IO;
    if (unlink(part) == 0) found = 1;
    /* a copy that never finished (the app was killed during the import) */
    static const char *const copies[] = {".mbtiles.copy", ".pmtiles.copy"};
    for (size_t k = 0; k < sizeof copies / sizeof copies[0]; k++)
        if (cp0_map__archive_file(s, id, copies[k], part, sizeof part) == 0 && unlink(part) == 0) found = 1;
    cp0_map_cache_drop(s->ram, cp0_map_source_hash(id));
    const int rl = cp0_map__store_reload_locked(s);
    pthread_mutex_unlock(&s->mu);
    pthread_mutex_unlock(&s->import_mu);
    if (rc) return rc;
    if (!found) return CP0_MAP_E_NOT_FOUND;
    return rl;
}
