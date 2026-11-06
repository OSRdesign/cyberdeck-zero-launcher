/*
 * SPDX-License-Identifier: MIT
 *
 * The map engine's only copy of stb_image 2.30 (third_party/stb/stb_image.h, public domain or MIT, Sean Barrett;
 * credited in docs/OPEN_SOURCE_COMPONENTS.md). Built like RSS Reader's (PNG + JPEG only, no stdio, no float / HDR, no
 * SIMD) plus STB_IMAGE_STATIC: every stb function is file-local, so no stbi_* symbol leaves this file and an app that
 * has its own stb_image (RSS Reader) links without clashes. The engine reaches it through the cp0_map__* wrappers below.
 *
 * Memory ceiling: stb_image's allocations go through a counting allocator with a per-thread total, so a damaged or
 * hostile PNG (a zlib stream that expands without end) fails with "outofmem" past CP0_MAP_DECODE_MEM_CEILING instead
 * of taking the board's RAM. Thread-local: several tile-load threads may decode at once.
 */
#include "cp0_map_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAP_STB_HDR 16
static _Thread_local size_t g_stb_live;

static void *map_stb_malloc(size_t n)
{
    if (n > (size_t)PTRDIFF_MAX - MAP_STB_HDR || g_stb_live + n > CP0_MAP_DECODE_MEM_CEILING) return NULL;
    unsigned char *p = (unsigned char *)malloc(n + MAP_STB_HDR);
    if (!p) return NULL;
    memcpy(p, &n, sizeof n);
    g_stb_live += n;
    return p + MAP_STB_HDR;
}

static void map_stb_free(void *q)
{
    if (!q) return;
    unsigned char *p = (unsigned char *)q - MAP_STB_HDR;
    size_t n;
    memcpy(&n, p, sizeof n);
    g_stb_live -= n;
    free(p);
}

static void *map_stb_realloc(void *q, size_t newsz)
{
    if (!q) return map_stb_malloc(newsz);
    size_t old;
    memcpy(&old, (unsigned char *)q - MAP_STB_HDR, sizeof old);
    void *r = map_stb_malloc(newsz);
    if (!r) return NULL;
    memcpy(r, q, old < newsz ? old : newsz);
    map_stb_free(q);
    return r;
}

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 4096
#define STBI_MALLOC(sz) map_stb_malloc(sz)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) map_stb_realloc(p, newsz)
#define STBI_FREE(p) map_stb_free(p)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
/* STB_IMAGE_STATIC declares the GIF entry point (compiled out by STBI_ONLY_*) static: GCC reports it at the end of the
 * file, after the pop below, so this one stays off for the whole file */
#pragma GCC diagnostic ignored "-Wunused-function"
/* stb_image's JPEG IDCT multiplies ints that a broken stream makes overflow: wrapping is what the code relies on, so
 * make it the defined behaviour for the functions of this one header (the file stays the unmodified upstream copy;
 * RSS Reader does the same). */
#pragma GCC push_options
#pragma GCC optimize("wrapv")
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#include "../../third_party/stb/stb_image.h"
#pragma GCC diagnostic pop
#pragma GCC pop_options

int cp0_map__stbi_info(const uint8_t *data, int len, int *w, int *h, int *comp)
{
    return stbi_info_from_memory(data, len, w, h, comp);
}

uint8_t *cp0_map__stbi_load(const uint8_t *data, int len, int *w, int *h, int *comp, int req)
{
    return stbi_load_from_memory(data, len, w, h, comp, req);
}

void cp0_map__stbi_free(void *p)
{
    stbi_image_free(p);
}

int cp0_map__inflate_raw(uint8_t *out, int olen, const uint8_t *in, int ilen)
{
    if (!out || !in || olen < 0 || ilen < 0) return -1;
    return stbi_zlib_decode_noheader_buffer((char *)out, olen, (const char *)in, ilen);
}
