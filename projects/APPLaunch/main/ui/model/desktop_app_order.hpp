/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * Stable grid order of the apps found through .desktop files.
 *
 * The file time of a .desktop file is not a stable order: an upgrade or a reinstall replaces the file, and dpkg
 * stamps it with the build time of the package, so an upgraded tile used to jump to the end of the grid. The
 * launcher therefore remembers the order itself. Each .desktop file name is reduced to a 32-bit key; the saved
 * order is the list of keys, stored as comma separated hex in a few config strings (a config value holds at
 * most 255 characters). Candidates are given in their fallback order (oldest file time first):
 *   - a candidate already in the saved order keeps its place (an upgrade keeps the file name, so it stays);
 *   - a candidate not in it (first install, or first run after this feature) goes after the known ones, in the
 *     fallback order. On the first run that reproduces the current grid.
 * Keys of apps that are gone stay in the saved list (bounded), so a removed and reinstalled app returns to its
 * old place.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace desktop_app_order {

inline constexpr std::size_t kMaxSaved = 135;
inline constexpr std::size_t kChunks = 5;       // config keys "app_order", "app_order2" ... "app_order5"
inline constexpr std::size_t kChunkKeys = 27;   // 27 * 9 characters < 255

std::uint32_t key_of(const std::string &desktop_filename);

std::vector<std::uint32_t> parse(const std::string &text);
std::string format(const std::vector<std::uint32_t> &keys);

/* Config key of chunk `index` (0..kChunks-1). */
std::string chunk_name(std::size_t index);
std::vector<std::string> split_chunks(const std::vector<std::uint32_t> &keys);

struct Result {
    std::vector<std::size_t> order;       // candidate indices, in grid order
    std::vector<std::uint32_t> saved;     // the list to store
};

/* `candidates` are the keys in fallback order; `saved` is what was stored last time. */
Result arrange(const std::vector<std::uint32_t> &candidates, const std::vector<std::uint32_t> &saved);

} // namespace desktop_app_order
