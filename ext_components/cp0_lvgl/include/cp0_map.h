/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map.h: the map engine of cp0_lvgl (Deck Atlas, Mesh Hop; report 054 section 3.7, report 055), the part without
 * LVGL. Compiled into an app only when its configuration sets CONFIG_CP0_LVGL_MAP=y (Kconfig, default n): the
 * launcher and the apps that draw no map carry none of it. Interface reference: docs/cp0_lvgl.en.md, "Map engine".
 *
 *   cp0_map_common.h   result codes and cp0_map_strerror(), tile formats, boxes, byte buffers, progress
 *   cp0_map_geo.h      Web Mercator, tile maths, TMS rows, boxes, the scale bar, the display scale (1x / 1.5x / 2x)
 *   cp0_map_tile.h     PNG / JPEG decode (stb_image), the 2x and 1.5x pixel scalers, the RAM tile cache
 *   cp0_map_archive.h  one MBTiles (SQLite, loaded at run time) or PMTiles v3 (pread) archive, "archive missing"
 *   cp0_map_source.h   online tile sources: the policy record, URL rules (HTTPS, never the OSMF servers), User-Agent
 *   cp0_map_store.h    the shared store: imports (MBTiles, PMTiles, z/x/y folders converted into one MBTiles),
 *                      records, tile lookup across archives, online sources (keys, switches), the persistent viewing
 *                      cache, its cap and eviction, statistics
 *   cp0_map_overlay.h  the plain data of the widget's overlays: position fix, markers, layer limits, tile events
 *   cp0_map_fetch.h    the fetch pool's clients: the visible tiles a viewer lacks, fetch events
 *
 * The LVGL widget is cp0_map_widget.h (include it separately; it needs LVGL).
 *
 * 2026-11-07 (report 054 3.7.3, additive; CP0_MAP_API_VERSION stays 1): a converted folder's bounds / minzoom /
 * maxzoom cover ALL its zooms and its record keeps a tile rectangle per zoom, so the lookup across all archives
 * decides per zoom (archives imported before keep the declared-bounds rule); remove and import drop the RAM-cached
 * tiles of the "all archives" view too; cp0_map_source_info_t gains tiles, imported, source_path and
 * has_zoom_extents; the widget gains cp0_map_on_view_change() (cp0_map_widget.h).
 * Third-party code inside: stb_image 2.30 (public domain or MIT, Sean Barrett), built STB_IMAGE_STATIC. Loaded at run
 * time when present: SQLite (libsqlite3.so.0, public domain), libcurl (libcurl.so.4, curl licence, MIT-style).
 */
#ifndef CP0_MAP_H
#define CP0_MAP_H

#include "cp0_map_common.h"
#include "cp0_map_geo.h"
#include "cp0_map_tile.h"
#include "cp0_map_archive.h"
#include "cp0_map_source.h"
#include "cp0_map_store.h"
#include "cp0_map_overlay.h"
#include "cp0_map_fetch.h"

#endif /* CP0_MAP_H */
