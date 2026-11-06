/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map_overlay.h: the plain data of the map widget's overlays and events (report 054 sections 3.4 and 3.7): a
 * position fix, markers, the limits of the marker and track layers, tile events. No LVGL: an app can fill these on
 * any thread and hand them to the widget (cp0_map_widget.h) on the LVGL thread.
 */
#ifndef CP0_MAP_OVERLAY_H
#define CP0_MAP_OVERLAY_H

#include "cp0_map_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_MAP_MARKERS_MAX 2000        /* markers per marker layer (cp0_map_markers_set keeps the first ones) */
#define CP0_MAP_TRACK_POINTS_MAX 20000  /* points per track layer (cp0_map_track_append refuses more) */
#define CP0_MAP_LAYERS_MAX 8            /* marker and track layers per widget */
#define CP0_MAP_FIX_STALE_MS 10000      /* a fix older than this (CLOCK_MONOTONIC) is hidden */
#define CP0_MAP_HEADING_MIN_MPS 1.0f    /* the heading arrow shows above this speed */
#define CP0_MAP_LABEL_ZOOM 14           /* marker labels show from this zoom (and for the selected marker always) */
#define CP0_MAP_LABEL_MAX 32            /* bytes of a marker label, with the terminating 0 */

/* A position fix (the shape of position.state, report 054 6.3). */
typedef struct {
    double lat, lon;            /* degrees */
    float acc_m;                /* estimated horizontal error, m; <= 0 unknown: no accuracy circle */
    float speed_mps;            /* < 0 unknown */
    float course_deg;           /* 0 = north, clockwise; < 0 unknown */
    int64_t mono_ms;            /* CLOCK_MONOTONIC of the fix in ms (cp0_map_monotonic_ms()); 0 = now */
} cp0_map_fix_t;

typedef enum {
    CP0_MAP_MARKER_GENERIC = 0, /* a small ringed dot, grey */
    CP0_MAP_MARKER_NODE,        /* a mesh node: a dot, the accent blue */
    CP0_MAP_MARKER_REPEATER,    /* a diamond, green */
    CP0_MAP_MARKER_ROOM,        /* a square, amber */
    CP0_MAP_MARKER_WAYPOINT,    /* a pin (a triangle on its point), red */
    CP0_MAP_MARKER__COUNT
} cp0_map_marker_kind_t;

typedef struct {
    double lat, lon;
    cp0_map_marker_kind_t kind;
    char label[CP0_MAP_LABEL_MAX]; /* "" = no label; UTF-8, drawn in the caption font */
} cp0_map_marker_t;

/* Where a tile event comes from (task 3 added the fetch kinds). */
typedef enum {
    CP0_MAP_TILE_LOADED = 0,       /* the widget's loader read the store: result CP0_MAP_OK, CP0_MAP_E_NO_TILE,
                                      CP0_MAP_E_ARCHIVE_MISSING, other read errors */
    CP0_MAP_TILE_FETCHED = 1,      /* an online source's tile was stored (or was already fresh): read it again */
    CP0_MAP_TILE_REFUSED = 2,      /* the source does not fetch now; result says why (CP0_MAP_E_OFFLINE, _CAP_REACHED,
                                      _BLOCKED, _BACKOFF, _NETWORK, _NO_KEY, _DISABLED, _CURL_MISSING, _READ_ONLY,
                                      _CACHE_FULL, _NO_USER_AGENT, _REFUSED); it is fetched when that changes */
    CP0_MAP_TILE_FETCH_FAILED = 3, /* result CP0_MAP_E_NO_TILE (404 / 410: not asked again for 24 h),
                                      CP0_MAP_E_NETWORK, CP0_MAP_E_HTTP, CP0_MAP_E_TOO_LARGE (retried later) */
} cp0_map_tile_event_kind_t;

/* What happened to a tile the widget asked for, delivered on the LVGL thread (cp0_map_on_tile). */
typedef struct {
    int z;
    uint32_t x, y;              /* XYZ, the displayed tile (an over-zoom tile is read from its parent); fetch events
                                   carry the tile fetched (the parent for an over-zoom view) */
    int result;                 /* CP0_MAP_OK (loaded), CP0_MAP_E_NO_TILE, CP0_MAP_E_ARCHIVE_MISSING, other errors */
    char archive_id[CP0_MAP_ID_MAX]; /* the archive or source that answered (loaded and fetched tiles) */
    /* added by task 3 */
    cp0_map_tile_event_kind_t kind;
    int stale;                  /* LOADED from a viewing cache past the tile's expiry */
} cp0_map_tile_event_t;

typedef void (*cp0_map_tile_cb)(void *user, const cp0_map_tile_event_t *ev);

/* CLOCK_MONOTONIC in ms (the clock of cp0_map_fix_t.mono_ms). */
int64_t cp0_map_monotonic_ms(void);
/* 1 when the fix is set and not older than CP0_MAP_FIX_STALE_MS at now_ms (mono_ms 0 counts as fresh). */
int cp0_map_fix_fresh(const cp0_map_fix_t *fix, int64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_OVERLAY_H */
