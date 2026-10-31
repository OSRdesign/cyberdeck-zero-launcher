/*
 * SPDX-License-Identifier: MIT
 *
 * Home tiles that a board profile starts hidden: APPLAUNCH_HIDE_TILES in /etc/applaunch/board.conf, a comma list of
 * tile config keys (e.g. app_Game; a key that names no tile is ignored).
 * Only the default changes: Settings > Launcher still lists the tile, and the user's own choice, once saved, wins.
 */
#pragma once

#include <string_view>

namespace tile_default_policy {

// true when key is one of the comma-separated entries of list (spaces around an entry ignored)
inline bool hidden_by_default(std::string_view key, const char *list)
{
    if (key.empty() || !list) return false;
    std::string_view rest(list);
    while (!rest.empty()) {
        const auto comma = rest.find(',');
        std::string_view entry = rest.substr(0, comma);
        rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
        while (!entry.empty() && (entry.front() == ' ' || entry.front() == '\t')) entry.remove_prefix(1);
        while (!entry.empty() && (entry.back() == ' ' || entry.back() == '\t')) entry.remove_suffix(1);
        if (entry == key) return true;
    }
    return false;
}

// the config default of a configurable tile: 1 shown, 0 hidden by the board profile
inline int default_enabled(std::string_view key, const char *list)
{
    return hidden_by_default(key, list) ? 0 : 1;
}

} // namespace tile_default_policy
