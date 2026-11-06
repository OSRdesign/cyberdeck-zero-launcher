/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map_widget.h: the map widget of the map engine (report 054 section 3.7, Deck Atlas 0.1 task 2): an LVGL object
 * that shows the tiles of a cp0_map_store_t with its own controls and overlay layers. Only with CONFIG_CP0_LVGL_MAP.
 * Every call on the LVGL thread.
 *
 * DRAWING. The widget blits the visible tiles (RGB565, the display's format) from the store's RAM cache in its own draw
 * callback; nothing is allocated per frame by the widget. A tile not in the cache yet shows its parent (one zoom up)
 * magnified when that is cached, else a neutral grid; a source whose archive is missing (a USB stick pulled) shows a
 * message strip over the grid instead of tiles. The zoom is limited to the source's zooms plus one level of over-zoom
 * (the deepest tiles magnified, marked "overzoom"). The display scale (cp0_map_set_scale) is the nearest of 1x / 1.5x /
 * 2x to the panel density (cp0_map_choose_scale with cp0_ui_metrics_get()->ppmm_x100) unless the app sets one; 256 px
 * tiles are magnified by it, 512 px ("@2x") tiles drawn at half of it (0.5x / 0.75x / 1x).
 *
 * THREADS. One tile-load thread per widget reads, decodes and scales the visible tiles and a ring of one tile around
 * them into the cache, the view's centre first; a view change replaces its queue (stale requests are dropped). The
 * LVGL thread never waits for I/O or decoding: a 40 ms LVGL timer collects finished tiles, redraws only their area,
 * calls cp0_map_on_tile() callbacks, and redraws a moving view at most 20 times a second (1 s when idle).
 *
 * CONTROLS (drawn by the widget, touch-first, laid out inside the widget's own rectangle; each touch square is the
 * TARGET token but at most 15 % of the widget's shorter side): zoom + and - at the right edge, centred vertically; a
 * "centre on me" button above them while a fresh position fix is set; the scale bar at the bottom left (1 / 2 / 5 x
 * 10^n m, then km; imperial on request); the attribution of the archive in view at the bottom right (one row higher
 * across the width when it does not fit beside the scale bar). Colours from the toolkit's palette (dark plates, white
 * text), day mode only.
 *
 * INPUT. Touch: drag pans (no inertia), a double tap zooms in one level at the point, a tap on a control acts. Keys,
 * while the widget has the focus of its LVGL group (it listens to the toolkit's LV_EVENT_KEYBOARD on its screen; an app
 * may also pass keys through cp0_map_handle_key()): arrows pan by 1/8 of the view (with Shift 1/2; the deck's F/X/Z/C
 * navigation aliases count as arrows), + (=, keypad +) and Page Up zoom in, - (keypad -) and Page Down zoom out, Home
 * centres on the position fix. A mouse or trackpoint reaches apps as arrow keys and Enter (pointer navigation): its
 * wheel arrives as Up / Down and pans. The widget uses its object's user data: do not change it.
 */
#ifndef CP0_MAP_WIDGET_H
#define CP0_MAP_WIDGET_H

#include "cp0_map.h"
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cp0_map_layer cp0_map_layer_t;

/* A new map widget on `parent` (the parent's full size until the app sizes it) over `store` (not owned; it must
 * outlive the widget). Source: every archive of the store (cp0_map_set_source NULL). NULL when out of memory. */
lv_obj_t *cp0_map_create(lv_obj_t *parent, cp0_map_store_t *store);
/* 1 when obj is a map widget. */
int cp0_map_is_map(const lv_obj_t *obj);

/* What to show: an archive or source id of the store, or NULL for every archive (the first that has each tile).
 * Sets the zoom range from it. 0, CP0_MAP_E_NOT_FOUND (unknown id: the widget keeps its source). */
int cp0_map_set_source(lv_obj_t *map, const char *source_id);
void cp0_map_set_center(lv_obj_t *map, double lat, double lon);
void cp0_map_get_center(lv_obj_t *map, double *lat, double *lon);
void cp0_map_set_zoom(lv_obj_t *map, int zoom);                    /* clamped to the source's range (+1 over-zoom) */
int cp0_map_get_zoom(lv_obj_t *map);
void cp0_map_get_zoom_range(lv_obj_t *map, int *zmin, int *zmax); /* zmax includes the over-zoom level */
/* Display scale: 0 = automatic (the panel density), else snapped to 1, 1.5 or 2. */
void cp0_map_set_scale(lv_obj_t *map, float scale);
float cp0_map_get_scale(lv_obj_t *map);                            /* the scale in use: 1, 1.5 or 2 */
/* Moves the map content by (dx, dy) px, as a finger drag does (dx > 0: the map moves right). */
void cp0_map_pan_px(lv_obj_t *map, int dx, int dy);
void cp0_map_set_units(lv_obj_t *map, cp0_map_units_t units);     /* the scale bar; metric by default */

/* The position dot, its translucent accuracy circle and, above CP0_MAP_HEADING_MIN_MPS, a heading arrow; NULL hides
 * them. A fix older than CP0_MAP_FIX_STALE_MS hides itself. */
void cp0_map_set_position(lv_obj_t *map, const cp0_map_fix_t *fix);
/* Centres the map on the fix (the Home key and the locate button do this). 0, or CP0_MAP_E_NOT_FOUND (no fresh fix). */
int cp0_map_center_on_position(lv_obj_t *map);

/* Overlay layers (at most CP0_MAP_LAYERS_MAX per widget, drawn in creation order over the tiles, under the position
 * dot and the controls). They belong to the widget and go with it. name: for the app (copied, may be NULL). */
cp0_map_layer_t *cp0_map_add_markers(lv_obj_t *map, const char *name);
/* Replaces the markers (the first CP0_MAP_MARKERS_MAX are kept); returns the count kept or a negative error. */
int cp0_map_markers_set(cp0_map_layer_t *layer, const cp0_map_marker_t *markers, int n);
void cp0_map_markers_select(cp0_map_layer_t *layer, int index);   /* -1: none; the selected label always shows */
void cp0_map_markers_label_zoom(cp0_map_layer_t *layer, int zoom); /* labels from this zoom (CP0_MAP_LABEL_ZOOM) */
cp0_map_layer_t *cp0_map_add_track(lv_obj_t *map, const char *name);
/* Adds a point. 0, or CP0_MAP_E_FULL (CP0_MAP_TRACK_POINTS_MAX reached), CP0_MAP_E_NOMEM. */
int cp0_map_track_append(cp0_map_layer_t *layer, double lat, double lon);
void cp0_map_track_clear(cp0_map_layer_t *layer);
void cp0_map_layer_show(cp0_map_layer_t *layer, int on);

/* Tile events (loaded, no tile, archive missing, errors), on the LVGL thread. One callback per widget; NULL removes. */
void cp0_map_on_tile(lv_obj_t *map, cp0_map_tile_cb cb, void *user);

/* A key for the map: key_code an evdev KEY_* (input_keys.h), mods KBD_MOD_* (keyboard_input.h). 1 when it used it. */
int cp0_map_handle_key(lv_obj_t *map, uint32_t key_code, uint32_t mods);

/* Blocks the LVGL thread until the tile-load thread has finished the current view (or timeout_ms passed), then
 * collects the results as the timer would. For screenshots and tests only. 0, or CP0_MAP_E_CANCELLED on timeout. */
int cp0_map_wait_idle(lv_obj_t *map, int timeout_ms);

typedef enum {
    CP0_MAP_CONTROL_ZOOM_IN = 1,
    CP0_MAP_CONTROL_ZOOM_OUT = 2,
    CP0_MAP_CONTROL_LOCATE = 3,
} cp0_map_control_t;

/* The touch square of one of the widget's controls, in screen coordinates (scripted tests tap its centre).
 * 0, or CP0_MAP_E_NOT_FOUND when it is not shown (the locate button without a fresh fix). */
int cp0_map_control_area(lv_obj_t *map, cp0_map_control_t control, lv_area_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_WIDGET_H */
