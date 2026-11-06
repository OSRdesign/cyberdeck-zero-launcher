/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: one archive file, MBTiles or PMTiles, with the "archive missing" state (see cp0_map_archive.h).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* O_CLOEXEC */
#endif
#include "cp0_map_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct cp0_map_archive {
    char path[CP0_MAP_PATH_MAX];
    cp0_map_archive_info_t info;
    cp0_map_mbtiles_t *mb;
    cp0_map_pmtiles_t *pm;
    dev_t dev;
    ino_t ino;
    int open;                    /* a back end is open */
};

const char *cp0_map_archive_kind_name(cp0_map_archive_kind_t kind)
{
    switch (kind) {
    case CP0_MAP_ARCHIVE_MBTILES: return "mbtiles";
    case CP0_MAP_ARCHIVE_PMTILES: return "pmtiles";
    default: return "";
    }
}

static int gone_errno(int e)
{
    return e == ENOENT || e == ENOTDIR || e == ENODEV || e == ENXIO || e == EIO || e == ESTALE || e == ENOMEDIUM;
}

int cp0_map_archive_sniff(const char *path, cp0_map_archive_kind_t *kind)
{
    if (!path || !kind) return CP0_MAP_E_INVALID;
    *kind = CP0_MAP_ARCHIVE_NONE;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return gone_errno(errno) ? CP0_MAP_E_ARCHIVE_MISSING : CP0_MAP_E_IO;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return CP0_MAP_E_FORMAT;
    }
    uint8_t h[16] = {0};
    const int rc = cp0_map__pread_all(fd, h, sizeof h, 0);
    close(fd);
    if (rc == CP0_MAP_E_IO) return CP0_MAP_E_IO;
    if (rc) return CP0_MAP_E_FORMAT; /* shorter than 16 bytes */
    if (!memcmp(h, "SQLite format 3\0", 16)) {
        *kind = CP0_MAP_ARCHIVE_MBTILES;
        return CP0_MAP_OK;
    }
    if (!memcmp(h, "PMTiles", 7)) {
        if (h[7] != 3) return CP0_MAP_E_UNSUPPORTED;
        *kind = CP0_MAP_ARCHIVE_PMTILES;
        return CP0_MAP_OK;
    }
    return CP0_MAP_E_FORMAT;
}

static void backend_close(cp0_map_archive_t *a)
{
    cp0_map__mbtiles_close(a->mb);
    cp0_map__pmtiles_close(a->pm);
    a->mb = NULL;
    a->pm = NULL;
    a->open = 0;
}

static int backend_open(cp0_map_archive_t *a)
{
    cp0_map_archive_kind_t kind;
    int rc = cp0_map_archive_sniff(a->path, &kind);
    if (rc) return rc;
    struct stat st;
    if (stat(a->path, &st) != 0) return gone_errno(errno) ? CP0_MAP_E_ARCHIVE_MISSING : CP0_MAP_E_IO;
    cp0_map_archive_info_t info;
    memset(&info, 0, sizeof info);
    info.kind = kind;
    info.minzoom = info.maxzoom = -1;
    info.bytes = (uint64_t)st.st_size;
    if (kind == CP0_MAP_ARCHIVE_MBTILES)
        rc = cp0_map__mbtiles_open(a->path, &a->mb, &info);
    else
        rc = cp0_map__pmtiles_open(a->path, &a->pm, &info);
    if (rc) return rc;
    a->info = info;
    a->dev = st.st_dev;
    a->ino = st.st_ino;
    a->open = 1;
    return CP0_MAP_OK;
}

int cp0_map_archive_open(const char *path, cp0_map_archive_t **out)
{
    if (!out) return CP0_MAP_E_INVALID;
    *out = NULL;
    if (!path || !path[0] || strlen(path) >= CP0_MAP_PATH_MAX) return CP0_MAP_E_INVALID;
    cp0_map_archive_t *a = (cp0_map_archive_t *)calloc(1, sizeof *a);
    if (!a) return CP0_MAP_E_NOMEM;
    cp0_map__copy_str(a->path, sizeof a->path, path, 0);
    const int rc = backend_open(a);
    if (rc) {
        backend_close(a);
        free(a);
        return rc;
    }
    *out = a;
    return CP0_MAP_OK;
}

void cp0_map_archive_close(cp0_map_archive_t *a)
{
    if (!a) return;
    backend_close(a);
    free(a);
}

int cp0_map_archive_info(const cp0_map_archive_t *a, cp0_map_archive_info_t *out)
{
    if (!a || !out) return CP0_MAP_E_INVALID;
    *out = a->info;
    return CP0_MAP_OK;
}

const char *cp0_map_archive_path(const cp0_map_archive_t *a)
{
    return a ? a->path : "";
}

int cp0_map_archive_check(cp0_map_archive_t *a)
{
    if (!a) return CP0_MAP_E_INVALID;
    struct stat st;
    if (stat(a->path, &st) != 0) {
        backend_close(a);
        return CP0_MAP_E_ARCHIVE_MISSING;
    }
    if (a->open && st.st_dev == a->dev && st.st_ino == a->ino) return CP0_MAP_OK;
    /* back after a removal, or another file under the same path: open it again */
    backend_close(a);
    return backend_open(a);
}

int cp0_map_archive_get_tile(cp0_map_archive_t *a, int z, uint32_t x, uint32_t y, cp0_map_buf_t *out,
                             cp0_map_format_t *fmt)
{
    if (fmt) *fmt = CP0_MAP_FMT_UNKNOWN;
    if (!a || !out) return CP0_MAP_E_INVALID;
    if (!cp0_map_tile_valid(z, x, y)) return CP0_MAP_E_INVALID;
    int rc = cp0_map_archive_check(a);
    if (rc) return rc;
    out->len = 0;
    rc = a->mb ? cp0_map__mbtiles_get(a->mb, z, x, y, out) : cp0_map__pmtiles_get(a->pm, z, x, y, out);
    if (rc == CP0_MAP_E_IO || rc == CP0_MAP_E_CORRUPT) {
        /* a read failing under us: the stick may have gone this very moment */
        struct stat st;
        if (stat(a->path, &st) != 0) {
            backend_close(a);
            return CP0_MAP_E_ARCHIVE_MISSING;
        }
    }
    if (rc == CP0_MAP_OK && fmt) *fmt = cp0_map_format_sniff(out->data, out->len);
    return rc;
}
