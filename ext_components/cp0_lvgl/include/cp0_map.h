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
 *   cp0_map_store.h    the shared store: imports (MBTiles, PMTiles, z/x/y folders converted into one MBTiles),
 *                      records, tile lookup across archives
 *   cp0_map_overlay.h  the plain data of the widget's overlays: position fix, markers, layer limits, tile events
 *
 * The LVGL widget is cp0_map_widget.h (include it separately; it needs LVGL). Online sources, the viewing cache and
 * its cap are task 3.
 * Third-party code inside: stb_image 2.30 (public domain or MIT, Sean Barrett), built STB_IMAGE_STATIC.
 */
#ifndef CP0_MAP_H
#define CP0_MAP_H

#include "cp0_map_common.h"
#include "cp0_map_geo.h"
#include "cp0_map_tile.h"
#include "cp0_map_archive.h"
#include "cp0_map_store.h"
#include "cp0_map_overlay.h"

#endif /* CP0_MAP_H */
