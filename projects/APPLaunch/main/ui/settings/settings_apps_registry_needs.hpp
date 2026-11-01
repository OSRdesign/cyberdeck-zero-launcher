/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps: the screen fields of the catalogue's registry entries. The Store backend's --summary does not carry
 * them, so the launcher reads the registries the backend caches (read only): <cache dir>/registry-*.json, each a record
 * {"url", "status", "index": {"apps": [entry...]}}; the cache dir is M5APPSTORE_CACHE_DIR or
 * ~/.cache/cardputerzero-appstore (what the backend uses). An entry's id is the backend's app key: the first of uuid,
 * share_code, id, title that is not empty.
 */
#pragma once

#include <map>
#include <string>

namespace settings_apps {

struct ScreenNeeds {
    std::string min_size;          /* "WxH" as the registry states it; empty: none */
    std::string portrait_min_size; /* "WxH"; empty: none (a boolean `portrait` flag carries no size and is ignored) */
    std::string desktop;           /* the entry's .desktop under the app root (the backend's desktop_path rule) */
};

/* The needs of every entry of one cached registry that states any, by app key. Anything unreadable gives nothing.
 * Both shapes are accepted: `portrait_min_size` as a string, and `portrait` as a string or a boolean. A value that is
 * not a readable "WxH" is kept (the filter takes it as absent); its warning goes to stderr once per app and field. */
std::map<std::string, ScreenNeeds> parse_registry_needs(const std::string &registry_json);

/* The same two facts of an installed app's .desktop (X-MinSize, X-Portrait=WxH) through the launcher's own parser, as
 * "WxH" strings (empty: key absent or unreadable). False when the text is not a launcher entry. */
bool parse_desktop_needs(const std::string &desktop_contents, ScreenNeeds &needs);
/* The file of a ScreenNeeds::desktop path under the app root (M5APPSTORE_APP_ROOT or /usr/share/APPLaunch). */
bool read_desktop_needs(const std::string &desktop_path, ScreenNeeds &needs);

/* Every registry-*.json of the cache folder merged (empty argument: the default folder). */
std::map<std::string, ScreenNeeds> read_cache_needs(const std::string &cache_dir = std::string());

} // namespace settings_apps
