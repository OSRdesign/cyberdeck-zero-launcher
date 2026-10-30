/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * The user's order of the home grid (study 046, phase P1).
 *
 * Without a user order the grid is today's: the built-ins in table order, the .desktop apps in their app_order
 * (model/desktop_app_order.hpp), then the label table sort. Once the user moves a tile (P2), the launcher stores the
 * order of every tile it knows, as the FNV-1a keys of the tiles' ids (the tile's config key: "app_Setting",
 * "app_desktop_<name>_<fnv>"), comma separated hex, in the config keys "home_order", "home_order2" ... "home_order5"
 * (27 keys per chunk, 135 at most): the exact format and helpers of desktop_app_order.
 *
 *   arrange  the tiles shown, in today's order, put in the user's order: known ids first in the user's order, unknown
 *            ids (a new app) after them in today's order. A hash collision never hides a tile (it goes last).
 *   merge    the list to store after a move: hidden and removed ids keep their slots; the visible ids fill the
 *            visible slots in their new order. The first save starts from every registered tile (hidden built-ins
 *            and disabled preinstalled apps included), so a tile enabled later appears at its slot.
 *   save     one SetManyAndSave per save (never SetStr: the store holds 64 entries and a plain set fails silently).
 *            False when the store refuses it (full): the caller says "cannot save".
 *   reset    clears every home_order* key (one SetManyAndSave): today's grid again. app_order is not touched.
 *
 * The store has no delete: a cleared chunk stays as an empty value, read as "no user order".
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <vector>

namespace home_order {

inline constexpr std::size_t kMaxSaved = 135;
inline constexpr std::size_t kChunks = 5;      // config keys "home_order", "home_order2" ... "home_order5"
inline constexpr std::size_t kChunkKeys = 27;  // 27 * 9 characters < 255

/* FNV-1a of a tile id (its config key). */
std::uint32_t key_of(const std::string &tile_id);

std::vector<std::uint32_t> parse(const std::string &text);
std::string format(const std::vector<std::uint32_t> &keys);

/* Config key of chunk `index` (0..kChunks-1). */
std::string chunk_name(std::size_t index);
std::vector<std::string> split_chunks(const std::vector<std::uint32_t> &keys);

/* `tiles`: the keys of the tiles shown, in today's order; `saved`: the user's order. Returns tile indices in grid
 * order; every tile appears exactly once. */
std::vector<std::size_t> arrange(const std::vector<std::uint32_t> &tiles, const std::vector<std::uint32_t> &saved);

/* The list to store. `stored`: the user's order so far (empty on the first save); `registered`: every tile the
 * launcher knows (shown or hidden), in registry order; `visible`: the tiles shown, in their new grid order. */
std::vector<std::uint32_t> merge(const std::vector<std::uint32_t> &stored,
                                 const std::vector<std::uint32_t> &registered,
                                 const std::vector<std::uint32_t> &visible);

/* The config service's call (cp0_signal_config_api in the launcher; a fake in the tests). */
using Reply = std::function<void(int, std::string)>;
using ConfigCall = std::function<void(std::list<std::string>, Reply)>;

/* The stored user order; empty: none (no key, or cleared). */
std::vector<std::uint32_t> load(const ConfigCall &config);

/* Store `list` in one SetManyAndSave (a chunk that exists and is no longer needed is cleared). True when stored or
 * nothing had to be written; false when the store refused it (full) or the call failed. */
bool store(const ConfigCall &config, const std::vector<std::uint32_t> &list);

/* load + merge + store: the save after a move. Ids are the tiles' config keys. */
bool save(const ConfigCall &config, const std::vector<std::string> &registered_ids,
          const std::vector<std::string> &visible_ids);

/* Clear every home_order* key that exists, in one SetManyAndSave. True when cleared or nothing was stored. */
bool reset(const ConfigCall &config);

} // namespace home_order
