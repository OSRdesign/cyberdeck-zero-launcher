/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map_geo.h: Web Mercator (EPSG:3857) helpers, the scale bar step and the display scale of the map engine
 * (report 054 sections 3.2 and 3.4). Pure functions: no state, no allocation, usable from any thread.
 *
 * WORLD PIXELS. Positions are "world pixels" of 256 px web tiles at a zoom level: the world is 256 * 2^zoom px wide
 * and high, x grows east from the antimeridian, y grows south from latitude +85.0511 (CP0_MAP_MAX_LAT). A widget
 * multiplies by its display scale to get screen pixels (a 256 px tile is drawn 256 * scale px wide). Latitudes are
 * clamped to +-CP0_MAP_MAX_LAT, longitudes wrapped into [-180, 180).
 *
 * TILES. XYZ order (the web's and PMTiles'): x 0..2^z-1 west to east, y 0..2^z-1 north to south. MBTiles stores rows
 * in TMS order (y from the south): cp0_map_tms_row() converts both ways.
 *
 * DISPLAY SCALE (decision D5, report 054 3.2, measured in report 055 section 4). Web tiles are drawn for about
 * 3.8 px/mm (96 dpi, CP0_MAP_WEB_PPMM_X100); the engine magnifies them so their text is about the size the web
 * designs for: scale = ppmm / 3.8, rounded to the NEAREST of 1x, 1.5x and 2x (midpoints 1.25 and 1.75, so below
 * 4.75 px/mm 1x, below 6.65 px/mm 1.5x, 2x above; 2x is the cap). The panel density is the launcher's
 * APPLAUNCH_SCREEN_PPMM / screen.state ppmm (x100). Our boards (ppmm from board.conf APPLAUNCH_PANEL_MM, else the
 * driver's size, else the launcher's assumed 11.3 px/mm):
 *
 *   board                         panel        ppmm (px/mm)          ppmm / 3.8   scale
 *   zero8 / Pi 3A+ 3.5" SPI       480x320      6.30 (79x49 mm)         1.66       1.5x
 *   Pi 4B 5" DSI                  800x480      7.40 (108x65 mm)        1.95       2x
 *   Pi 3A+ 4.3" DSI               800x480      8.54 (94x56 mm)         2.25       2x
 *   Panasonic FZ-M1 (amd64)       1280x800     8.57 (150x93 mm)        2.26       2x
 *   Pi 5 HyperPixel 4"            800x480      11.3 assumed (~9.2)     2.97       2x
 *   HackBerry CM5                 720x720      11.3 assumed (~10)      2.97       2x
 *   the deck (Waveshare 2.8")     640x480      11.3 (assumed default)  2.97       2x
 *
 * The user's override (Settings: Auto / 1x / 1.5x / 2x) replaces the automatic choice: cp0_map_resolve_scale().
 */
#ifndef CP0_MAP_GEO_H
#define CP0_MAP_GEO_H

#include "cp0_map_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_MAP_MAX_LAT 85.05112877980659  /* atan(sinh(pi)) in degrees: the square Web Mercator world */
#define CP0_MAP_MAX_ZOOM 24                /* the deepest zoom the helpers accept (world 2^32 px wide) */
#define CP0_MAP_TILE_PX 256                /* the web tile the world pixels are counted in */
#define CP0_MAP_EARTH_RADIUS_M 6378137.0   /* WGS84 semi-major axis, the sphere of EPSG:3857 */
#define CP0_MAP_WEB_PPMM_X100 380          /* density web tiles are drawn for: 3.8 px/mm (96 dpi is 3.78) */

double cp0_map_clamp_lat(double lat);      /* to [-CP0_MAP_MAX_LAT, +CP0_MAP_MAX_LAT]; NaN -> 0 */
double cp0_map_wrap_lon(double lon);       /* to [-180, 180); NaN -> 0 */

/* lat/lon (degrees) -> world pixels at `zoom` (0..CP0_MAP_MAX_ZOOM, clamped). x in [0, 256*2^zoom), y in [0, 256*2^zoom]. */
void cp0_map_latlon_to_world_px(double lat, double lon, int zoom, double *x, double *y);
/* World pixels -> lat/lon (the inverse; x wraps, y is clamped to the world). */
void cp0_map_world_px_to_latlon(double x, double y, int zoom, double *lat, double *lon);

/* Ground metres per SCREEN pixel at `lat`, for 256 px tiles at `zoom` drawn at display `scale` (1, 1.5, 2; <= 0
 * counts as 1): 2 pi R cos(lat) / (256 * 2^zoom * scale). 156543.03 m at zoom 0 on the equator, scale 1. */
double cp0_map_metres_per_px(double lat, int zoom, float scale);

/* The tile holding lat/lon at `zoom` (XYZ). 0, or CP0_MAP_E_INVALID for a zoom outside 0..CP0_MAP_MAX_ZOOM. */
int cp0_map_latlon_to_tile(double lat, double lon, int zoom, uint32_t *x, uint32_t *y);
/* The north-west corner of tile (x, y) at `zoom` (fractional x, y give points inside the tile). */
void cp0_map_tile_to_latlon(int zoom, double x, double y, double *lat, double *lon);
/* The box a tile covers. 0, or CP0_MAP_E_INVALID for a tile outside the world. */
int cp0_map_tile_bbox(int zoom, uint32_t x, uint32_t y, cp0_map_bbox_t *out);
/* 1 when zoom is 0..30 and x, y < 2^zoom (what the archive readers accept). */
int cp0_map_tile_valid(int zoom, uint32_t x, uint32_t y);
/* XYZ row <-> TMS row (MBTiles): 2^zoom - 1 - y. Its own inverse. */
uint32_t cp0_map_tms_row(int zoom, uint32_t y);

/* "west,south,east,north" (the MBTiles `bounds` value; spaces allowed) -> box. 0, or CP0_MAP_E_INVALID (also for a
 * box with west > east or south > north, or values outside the world). */
int cp0_map_bbox_parse(const char *s, cp0_map_bbox_t *out);
/* "w,s,e,n" with 6 decimals into buf. Length written, or CP0_MAP_E_INVALID when it does not fit. */
int cp0_map_bbox_format(const cp0_map_bbox_t *box, char *buf, size_t size);
int cp0_map_bbox_contains(const cp0_map_bbox_t *box, double lat, double lon);       /* 1 inside or on the edge */
int cp0_map_bbox_intersects(const cp0_map_bbox_t *a, const cp0_map_bbox_t *b);       /* 1 when they overlap or touch */
void cp0_map_bbox_union(cp0_map_bbox_t *acc, const cp0_map_bbox_t *b);                /* acc grows to hold b */
/* The inclusive tile range covering `box` at `zoom` (latitudes clamped). 0 or CP0_MAP_E_INVALID. */
int cp0_map_bbox_tiles(const cp0_map_bbox_t *box, int zoom, uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1);
/* Tiles covering `box` over zooms zmin..zmax (what an area download would fetch); 0 for an invalid box or range. */
uint64_t cp0_map_bbox_tile_count(const cp0_map_bbox_t *box, int zmin, int zmax);

/* ---------------------------------------------------------------------------------------------------- scale bar */

typedef enum {
    CP0_MAP_UNITS_METRIC = 0,   /* m below 1 km, then km */
    CP0_MAP_UNITS_IMPERIAL,     /* ft below 1 mi, then mi */
} cp0_map_units_t;

typedef struct {
    double length_m;            /* the bar's ground length in metres */
    double value;               /* the same in the label's unit: 1, 2 or 5 x 10^n */
    int px;                     /* its length in screen px (<= max_px) */
    char label[24];             /* "500 m", "2 km", "0.5 m", "200 ft", "1 mi" */
} cp0_map_scalebar_t;

/* The largest 1, 2 or 5 x 10^n that is <= `max` (max > 0); 0 for max <= 0 or not finite. */
double cp0_map_nice_step(double max);
/* The longest 1-2-5 bar that fits in max_px at `metres_per_px` (cp0_map_metres_per_px() at the screen centre).
 * 0, or CP0_MAP_E_INVALID (bad arguments, or not even the smallest unit fits). */
int cp0_map_scalebar(double metres_per_px, int max_px, cp0_map_units_t units, cp0_map_scalebar_t *out);

/* ------------------------------------------------------------------------------------------------ display scale */

/* The automatic display scale for a panel density in px/mm x 100 (1130 = 11.3): 1.0f, 1.5f or 2.0f (rule and board
 * table above). ppmm_x100 <= 0 (unknown) counts as the launcher's default 11.3 px/mm. */
float cp0_map_choose_scale(int ppmm_x100);
/* The scale to use: `requested` 0 (Auto) -> cp0_map_choose_scale(ppmm_x100); anything else snapped to the nearest of
 * 1, 1.5 and 2. */
float cp0_map_resolve_scale(float requested, int ppmm_x100);
/* A scale as half-steps, what the pixel scalers take: 2 (1x), 3 (1.5x) or 4 (2x); snapped like resolve. */
int cp0_map_scale_x2(float scale);
/* "auto" (0), "1x", "1.5x", "2x" (snapped): the Settings spelling. */
const char *cp0_map_scale_name(float scale);
/* "auto", "1x", "1.5x", "2x" (also "1", "1.5", "2", "0"; any case) -> 0, 1, 1.5, 2. 0 or CP0_MAP_E_INVALID. */
int cp0_map_scale_parse(const char *s, float *out);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_GEO_H */
