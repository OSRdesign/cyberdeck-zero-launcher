/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine: Web Mercator, tile maths, boxes, the scale bar and the display scale (see cp0_map_geo.h).
 */
#include "cp0_map_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int clamp_zoom(int zoom)
{
    return zoom < 0 ? 0 : zoom > CP0_MAP_MAX_ZOOM ? CP0_MAP_MAX_ZOOM : zoom;
}

double cp0_map_clamp_lat(double lat)
{
    if (isnan(lat)) return 0.0;
    return lat < -CP0_MAP_MAX_LAT ? -CP0_MAP_MAX_LAT : lat > CP0_MAP_MAX_LAT ? CP0_MAP_MAX_LAT : lat;
}

double cp0_map_wrap_lon(double lon)
{
    if (!isfinite(lon)) return 0.0;
    if (lon >= -180.0 && lon < 180.0) return lon;
    double r = fmod(lon + 180.0, 360.0);
    if (r < 0) r += 360.0;
    r -= 180.0;
    return r >= 180.0 ? -180.0 : r;
}

void cp0_map_latlon_to_world_px(double lat, double lon, int zoom, double *x, double *y)
{
    const double size = (double)CP0_MAP_TILE_PX * ldexp(1.0, clamp_zoom(zoom));
    const double phi = cp0_map_clamp_lat(lat) * M_PI / 180.0;
    const double fx = (cp0_map_wrap_lon(lon) + 180.0) / 360.0;
    double fy = (1.0 - log(tan(phi) + 1.0 / cos(phi)) / M_PI) / 2.0;
    if (fy < 0.0) fy = 0.0;
    if (fy > 1.0) fy = 1.0;
    if (x) *x = fx * size;
    if (y) *y = fy * size;
}

void cp0_map_world_px_to_latlon(double x, double y, int zoom, double *lat, double *lon)
{
    const double size = (double)CP0_MAP_TILE_PX * ldexp(1.0, clamp_zoom(zoom));
    double fx = x / size, fy = y / size;
    if (!isfinite(fx)) fx = 0.5;
    if (!isfinite(fy)) fy = 0.5;
    if (fy < 0.0) fy = 0.0;
    if (fy > 1.0) fy = 1.0;
    if (lon) *lon = cp0_map_wrap_lon(fx * 360.0 - 180.0);
    if (lat) *lat = cp0_map_clamp_lat(atan(sinh(M_PI * (1.0 - 2.0 * fy))) * 180.0 / M_PI);
}

double cp0_map_metres_per_px(double lat, int zoom, float scale)
{
    const double s = (scale > 0.0f && isfinite(scale)) ? (double)scale : 1.0;
    const double world_px = (double)CP0_MAP_TILE_PX * ldexp(1.0, clamp_zoom(zoom));
    return 2.0 * M_PI * CP0_MAP_EARTH_RADIUS_M * cos(cp0_map_clamp_lat(lat) * M_PI / 180.0) / (world_px * s);
}

int cp0_map_latlon_to_tile(double lat, double lon, int zoom, uint32_t *x, uint32_t *y)
{
    if (zoom < 0 || zoom > CP0_MAP_MAX_ZOOM) return CP0_MAP_E_INVALID;
    double wx, wy;
    cp0_map_latlon_to_world_px(lat, lon, zoom, &wx, &wy);
    const double n = ldexp(1.0, zoom);
    double tx = floor(wx / CP0_MAP_TILE_PX), ty = floor(wy / CP0_MAP_TILE_PX);
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx > n - 1) tx = n - 1;
    if (ty > n - 1) ty = n - 1;
    if (x) *x = (uint32_t)tx;
    if (y) *y = (uint32_t)ty;
    return CP0_MAP_OK;
}

void cp0_map_tile_to_latlon(int zoom, double x, double y, double *lat, double *lon)
{
    cp0_map_world_px_to_latlon(x * CP0_MAP_TILE_PX, y * CP0_MAP_TILE_PX, zoom, lat, lon);
    /* the east edge of the last column is +180 (wrap would make it -180) */
    if (lon && x * CP0_MAP_TILE_PX >= (double)CP0_MAP_TILE_PX * ldexp(1.0, clamp_zoom(zoom))) *lon = 180.0;
}

int cp0_map_tile_valid(int zoom, uint32_t x, uint32_t y)
{
    if (zoom < 0 || zoom > 30) return 0;
    const uint32_t n = 1u << zoom;
    return x < n && y < n;
}

int cp0_map_tile_bbox(int zoom, uint32_t x, uint32_t y, cp0_map_bbox_t *out)
{
    if (!out || zoom > CP0_MAP_MAX_ZOOM || !cp0_map_tile_valid(zoom, x, y)) return CP0_MAP_E_INVALID;
    cp0_map_tile_to_latlon(zoom, x, y, &out->north, &out->west);
    cp0_map_tile_to_latlon(zoom, (double)x + 1, (double)y + 1, &out->south, &out->east);
    return CP0_MAP_OK;
}

uint32_t cp0_map_tms_row(int zoom, uint32_t y)
{
    if (zoom < 0 || zoom > 31) return y;
    return (uint32_t)((((uint64_t)1 << zoom) - 1) - y);
}

/* ------------------------------------------------------------------------------------------------------- boxes */

static int parse_double(const char **p, double *v)
{
    char *end;
    while (**p == ' ' || **p == '\t') (*p)++;
    *v = strtod(*p, &end);
    if (end == *p || !isfinite(*v)) return -1;
    *p = end;
    while (**p == ' ' || **p == '\t') (*p)++;
    return 0;
}

int cp0_map_bbox_parse(const char *s, cp0_map_bbox_t *out)
{
    if (!s || !out) return CP0_MAP_E_INVALID;
    double v[4];
    const char *p = s;
    for (int k = 0; k < 4; k++) {
        if (parse_double(&p, &v[k])) return CP0_MAP_E_INVALID;
        if (k < 3) {
            if (*p != ',') return CP0_MAP_E_INVALID;
            p++;
        }
    }
    if (*p) return CP0_MAP_E_INVALID;
    if (v[0] > v[2] || v[1] > v[3] || v[0] < -180.0 || v[2] > 180.0 || v[1] < -90.0 || v[3] > 90.0)
        return CP0_MAP_E_INVALID;
    out->west = v[0];
    out->south = v[1];
    out->east = v[2];
    out->north = v[3];
    return CP0_MAP_OK;
}

int cp0_map_bbox_format(const cp0_map_bbox_t *b, char *buf, size_t size)
{
    if (!b || !buf || !size) return CP0_MAP_E_INVALID;
    const int n = snprintf(buf, size, "%.6f,%.6f,%.6f,%.6f", b->west, b->south, b->east, b->north);
    return (n < 0 || (size_t)n >= size) ? CP0_MAP_E_INVALID : n;
}

int cp0_map_bbox_contains(const cp0_map_bbox_t *b, double lat, double lon)
{
    return b && lat >= b->south && lat <= b->north && lon >= b->west && lon <= b->east;
}

int cp0_map_bbox_intersects(const cp0_map_bbox_t *a, const cp0_map_bbox_t *b)
{
    return a && b && a->west <= b->east && b->west <= a->east && a->south <= b->north && b->south <= a->north;
}

void cp0_map_bbox_union(cp0_map_bbox_t *acc, const cp0_map_bbox_t *b)
{
    if (!acc || !b) return;
    if (b->west < acc->west) acc->west = b->west;
    if (b->south < acc->south) acc->south = b->south;
    if (b->east > acc->east) acc->east = b->east;
    if (b->north > acc->north) acc->north = b->north;
}

int cp0_map_bbox_tiles(const cp0_map_bbox_t *b, int zoom, uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1)
{
    if (!b || zoom < 0 || zoom > CP0_MAP_MAX_ZOOM || !(b->west <= b->east) || !(b->south <= b->north))
        return CP0_MAP_E_INVALID;
    uint32_t ax, ay, bx, by;
    cp0_map_latlon_to_tile(b->north, b->west, zoom, &ax, &ay);
    /* the east edge at +180 belongs to the last column (wrap would send it to column 0) */
    const double east = b->east >= 180.0 ? 179.9999999999 : b->east;
    cp0_map_latlon_to_tile(b->south, east, zoom, &bx, &by);
    if (x0) *x0 = ax;
    if (y0) *y0 = ay;
    if (x1) *x1 = bx;
    if (y1) *y1 = by;
    return CP0_MAP_OK;
}

uint64_t cp0_map_bbox_tile_count(const cp0_map_bbox_t *b, int zmin, int zmax)
{
    if (!b || zmin < 0 || zmax > CP0_MAP_MAX_ZOOM || zmin > zmax) return 0;
    uint64_t n = 0;
    for (int z = zmin; z <= zmax; z++) {
        uint32_t x0, y0, x1, y1;
        if (cp0_map_bbox_tiles(b, z, &x0, &y0, &x1, &y1)) return 0;
        n += (uint64_t)(x1 - x0 + 1) * (uint64_t)(y1 - y0 + 1);
    }
    return n;
}

/* --------------------------------------------------------------------------------------------------- scale bar */

double cp0_map_nice_step(double max)
{
    if (!(max > 0.0) || !isfinite(max)) return 0.0;
    double p = pow(10.0, floor(log10(max)));
    /* log10 rounding: make sure p <= max < 10 p */
    if (p > max) p /= 10.0;
    if (p * 10.0 <= max) p *= 10.0;
    if (5.0 * p <= max) return 5.0 * p;
    if (2.0 * p <= max) return 2.0 * p;
    return p;
}

static void format_value(char *buf, size_t size, double v, const char *unit)
{
    if (v >= 1.0)
        snprintf(buf, size, "%.0f %s", v, unit);
    else
        snprintf(buf, size, "%g %s", v, unit);
}

int cp0_map_scalebar(double mpp, int max_px, cp0_map_units_t units, cp0_map_scalebar_t *out)
{
    if (!out || !(mpp > 0.0) || !isfinite(mpp) || max_px <= 0) return CP0_MAP_E_INVALID;
    memset(out, 0, sizeof *out);
    const double max_m = mpp * max_px;
    double unit_m, big_unit_m;
    const char *unit, *big_unit;
    if (units == CP0_MAP_UNITS_IMPERIAL) {
        unit_m = 0.3048;
        big_unit_m = 1609.344;
        unit = "ft";
        big_unit = "mi";
    } else {
        unit_m = 1.0;
        big_unit_m = 1000.0;
        unit = "m";
        big_unit = "km";
    }
    double value;
    const char *u;
    double per;
    if (max_m >= big_unit_m) {
        per = big_unit_m;
        u = big_unit;
    } else {
        per = unit_m;
        u = unit;
    }
    value = cp0_map_nice_step(max_m / per);
    if (!(value > 0.0)) return CP0_MAP_E_INVALID;
    /* 1 mi is 5280 ft: below a mile the imperial bar stays in feet (1-2-5 steps up to 5000 ft) */
    out->value = value;
    out->length_m = value * per;
    out->px = (int)floor(out->length_m / mpp + 0.5);
    if (out->px > max_px) out->px = max_px;
    if (out->px <= 0) return CP0_MAP_E_INVALID;
    format_value(out->label, sizeof out->label, value, u);
    return CP0_MAP_OK;
}

/* ----------------------------------------------------------------------------------------------- display scale */

float cp0_map_choose_scale(int ppmm_x100)
{
    const int p = ppmm_x100 > 0 ? ppmm_x100 : 1130; /* CP0_UI_DEFAULT_PPMM_X100 */
    /* ratio = p / 380; nearest of 1, 1.5, 2: thresholds 1.25 and 1.75, in integers: 4 p vs 5 * 380, 4 p vs 7 * 380 */
    if (4L * p < 5L * CP0_MAP_WEB_PPMM_X100) return 1.0f;
    if (4L * p < 7L * CP0_MAP_WEB_PPMM_X100) return 1.5f;
    return 2.0f;
}

int cp0_map_scale_x2(float scale)
{
    if (!(scale > 0.0f)) return 2;
    if (scale < 1.25f) return 2;
    if (scale < 1.75f) return 3;
    return 4;
}

float cp0_map_resolve_scale(float requested, int ppmm_x100)
{
    if (!(requested > 0.0f)) return cp0_map_choose_scale(ppmm_x100);
    return (float)cp0_map_scale_x2(requested) / 2.0f;
}

const char *cp0_map_scale_name(float scale)
{
    if (!(scale > 0.0f)) return "auto";
    switch (cp0_map_scale_x2(scale)) {
    case 3: return "1.5x";
    case 4: return "2x";
    default: return "1x";
    }
}

int cp0_map_scale_parse(const char *s, float *out)
{
    if (!s || !out) return CP0_MAP_E_INVALID;
    static const struct {
        const char *name;
        float v;
    } names[] = {{"auto", 0.0f}, {"0", 0.0f},     {"1x", 1.0f}, {"1", 1.0f},
                 {"1.5x", 1.5f}, {"1.5", 1.5f},   {"2x", 2.0f}, {"2", 2.0f}};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcasecmp(s, names[i].name)) {
            *out = names[i].v;
            return CP0_MAP_OK;
        }
    return CP0_MAP_E_INVALID;
}
