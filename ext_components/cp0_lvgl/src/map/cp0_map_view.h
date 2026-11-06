/*
 * SPDX-License-Identifier: MIT
 *
 * The pure core of the map widget (src/map; no LVGL, no thread, no allocation): the view (centre, zoom, display scale,
 * pan, zoom at a point, the tiles that cover it in load order), the placement of the widget's own controls and their
 * hit test, track simplification, marker culling and label placement, the attribution text. cp0_map_widget.c draws
 * with these; tests/test_map_view.c checks them on the PC.
 */
#ifndef CP0_MAP_VIEW_H
#define CP0_MAP_VIEW_H

#include "cp0_map_internal.h"
#include "cp0_map_overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------------------- view */

typedef struct {
    double cx, cy;              /* the centre: world px of 256 px tiles at zoom z */
    int z;
    int scale_x2;               /* display scale in half steps: 2, 3, 4 */
    int w, h;                   /* the widget, px */
    int zmin, zmax;             /* zooms allowed (zmax includes the over-zoom level when there is one) */
    int data_max;               /* the deepest zoom with tiles; a tile of a deeper zoom is drawn from its parent */
} cp0_map_view_t;

void cp0_map_view_init(cp0_map_view_t *v, int w, int h);           /* centre 0 / 0, zoom 2, 1x, zooms 0..22 */
void cp0_map_view_set_size(cp0_map_view_t *v, int w, int h);
/* Zooms data_min..data_max with tiles, plus one level of over-zoom when `overzoom`; the zoom is clamped. */
void cp0_map_view_set_limits(cp0_map_view_t *v, int data_min, int data_max, int overzoom);
int cp0_map_view_set_zoom(cp0_map_view_t *v, int z);               /* keeps the centre; returns the zoom (clamped) */
/* Zoom by dz levels keeping the map point under widget px (sx, sy) in place. Returns the zoom. */
int cp0_map_view_zoom_at(cp0_map_view_t *v, int dz, double sx, double sy);
void cp0_map_view_set_center(cp0_map_view_t *v, double lat, double lon);
void cp0_map_view_center(const cp0_map_view_t *v, double *lat, double *lon);
void cp0_map_view_set_scale(cp0_map_view_t *v, int scale_x2);     /* keeps the centre */
/* Moves the map content by (dx, dy) widget px, as a finger drag does (dx > 0: the map moves right, the centre west).
 * x wraps around the world, y stays within it. */
void cp0_map_view_pan(cp0_map_view_t *v, double dx, double dy);
int cp0_map_view_tile_px(const cp0_map_view_t *v);                 /* screen px of one tile: 128 * scale_x2 */
int cp0_map_view_overzoom(const cp0_map_view_t *v);                /* 1 when z > data_max */
/* The integer screen origin: a tile (tx, ty) of the current zoom has its top-left corner at (tx * T - ox, ty * T - oy). */
void cp0_map_view_origin(const cp0_map_view_t *v, int64_t *ox, int64_t *oy);
/* World px at zoom 0 (the overlays' stored form) -> widget px; x picks the copy of the world nearest the centre. */
void cp0_map_view_world0_to_screen(const cp0_map_view_t *v, double wx0, double wy0, double *sx, double *sy);
void cp0_map_view_latlon_to_screen(const cp0_map_view_t *v, double lat, double lon, double *sx, double *sy);
void cp0_map_view_screen_to_latlon(const cp0_map_view_t *v, double sx, double sy, double *lat, double *lon);
double cp0_map_view_metres_per_px(const cp0_map_view_t *v);        /* at the centre */
/* Marker culling: 1 when the zoom-0 world point lands within the widget grown by `margin` px on every side (its
 * widget px in *sx, *sy either way). */
int cp0_map_view_visible(const cp0_map_view_t *v, double wx0, double wy0, int margin, double *sx, double *sy);

#define CP0_MAP_VIEW_MAX_TILES 160

typedef struct {
    int z;
    uint32_t x, y;              /* the tile to load (x wrapped into the world) */
    int sx, sy;                 /* its top-left corner in widget px */
    uint8_t visible;            /* 1: on screen; 0: the ring of one tile around it (loaded ahead, not drawn) */
    uint8_t over;               /* over-zoom: drawn from the parent's quadrant */
    uint32_t dist2;             /* squared distance of its centre from the view's centre, px (load order) */
} cp0_map_slot_t;

/* The tiles covering the view (and a ring of `ring` tiles around it), visible ones first, each group nearest the
 * centre first: the load order (the newest view's centre first). Rows outside the world are left out. Returns the
 * count (at most max). */
int cp0_map_view_tiles(const cp0_map_view_t *v, int ring, cp0_map_slot_t *out, int max);

/* ------------------------------------------------------------------------------------------------------ controls */

typedef enum {
    CP0_MAP_CTL_NONE = 0,
    CP0_MAP_CTL_ZOOM_IN,
    CP0_MAP_CTL_ZOOM_OUT,
    CP0_MAP_CTL_LOCATE,         /* centre on the position fix */
    CP0_MAP_CTL__COUNT
} cp0_map_ctl_t;

typedef struct {
    int x, y, w, h;             /* widget px; w == 0: not shown */
} cp0_map_rect_t;

typedef struct {
    int w, h;                   /* the widget */
    int target;                 /* the toolkit's TARGET token (7 mm, at least 44 px) */
    int space;                  /* the SPACE_M token */
    int radius;                 /* the RADIUS_S token */
    int text_h;                 /* line height of the caption font */
    int attribution_w;          /* width of the attribution text in the caption font, px (0 = none) */
    int badge_w;                /* width of the "overzoom" text, px */
    int strip_w;                /* width of the message strip's text, px */
    int locate;                 /* 1: the locate button shows (a fresh fix is set) */
} cp0_map_layout_in_t;

typedef struct {
    int btn;                    /* touch square of a button: TARGET (less only where neighbours would overlap) */
    int plate;                  /* the drawn button inside it: at most 15 % of the shorter side */
    int margin, pad;
    cp0_map_rect_t ctl[CP0_MAP_CTL__COUNT];    /* touch squares ([0] unused; inside the widget, never overlapping) */
    cp0_map_rect_t plates[CP0_MAP_CTL__COUNT]; /* the drawn plates, centred in their touch squares */
    cp0_map_rect_t scalebar;    /* its plate; the bar is at most scalebar_max_px long inside it */
    int scalebar_max_px;
    cp0_map_rect_t attribution; /* its plate; the text is cut to attribution_text_w */
    int attribution_text_w;
    cp0_map_rect_t badge;       /* "overzoom", top left */
    cp0_map_rect_t strip;       /* the message strip ("archive missing"), across the top */
} cp0_map_layout_t;

/* Places the controls inside the widget (never outside its rectangle, the drawn parts never over each other): the zoom
 * pair at the right edge centred vertically, one TARGET apart (moved or drawn closer when the top and bottom rows need
 * the room), the locate button above them, the scale bar bottom left, the attribution bottom right (one row higher,
 * full width, when it does not fit beside the bar). Each button: a plate of at most 15 % of the shorter side drawn in
 * a touch square of TARGET. */
void cp0_map_layout(const cp0_map_layout_in_t *in, cp0_map_layout_t *out);
/* The control whose touch square holds (x, y), or CP0_MAP_CTL_NONE. */
cp0_map_ctl_t cp0_map_layout_hit(const cp0_map_layout_t *l, int x, int y);
int cp0_map_rect_overlap(const cp0_map_rect_t *a, const cp0_map_rect_t *b); /* 1 when both are shown and overlap */

/* ------------------------------------------------------------------------------------------------- tracks, markers */

/* Douglas-Peucker on points (x[i], y[i]): keeps the first, the last and every point farther than eps from the chord of
 * its span. keep receives the kept indices ascending; mark (n bytes) and stack (2 * n ints) are scratch. Returns the
 * count. No recursion, no allocation. */
int cp0_map_simplify(const double *x, const double *y, int n, double eps, int *keep, unsigned char *mark, int *stack);

/* Label rectangles for markers at widget px (sx, sy) with text sizes (lw, lh): greedy in the given order, to the right
 * of the glyph (radius r), nudged below, above or to the left when that spot is taken by an earlier label or one of
 * the `avoid` rectangles (the widget's controls), or leaves the widget; left out (lx = -1) when every spot is taken.
 * Returns the number placed. */
int cp0_map_labels_place(const int *sx, const int *sy, const int *lw, const int *lh, int n, int r, int view_w,
                         int view_h, const cp0_map_rect_t *avoid, int navoid, int *lx, int *ly);

/* An attribution string as text: HTML tags removed, &copy; &amp; &lt; &gt; &quot; &#39; &nbsp; &#NNN; decoded (the
 * copyright sign as UTF-8), whitespace runs as one space, trimmed. */
void cp0_map_attribution_text(const char *in, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_VIEW_H */
