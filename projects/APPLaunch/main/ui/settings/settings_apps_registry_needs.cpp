/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps: the screen fields of the cached registries (see settings_apps_registry_needs.hpp).
 */
#include "settings_apps_registry_needs.hpp"

#include "settings_apps_compat.hpp"

#include "../desktop_entry.h"

#include "cJSON.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

namespace settings_apps {
namespace {

std::string text_of(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring ? std::string(item->valuestring) : std::string();
}

std::string app_key(const cJSON *entry)
{
    for (const char *name : {"uuid", "share_code", "id", "title"}) {
        const std::string value = text_of(entry, name);
        if (!value.empty()) return value;
    }
    return {};
}

/* the backend's desktop_path(): app.applaunch.desktop_entry, else applications/<slug>.desktop (share_code or title,
 * lower case, white space as "-") */
std::string desktop_of(const cJSON *entry)
{
    const cJSON *app = cJSON_GetObjectItemCaseSensitive(entry, "app");
    const cJSON *applaunch = cJSON_IsObject(app) ? cJSON_GetObjectItemCaseSensitive(app, "applaunch") : nullptr;
    if (cJSON_IsObject(applaunch)) {
        const std::string named = text_of(applaunch, "desktop_entry");
        if (!named.empty()) return named;
    }
    std::string slug = text_of(entry, "share_code");
    if (slug.empty()) slug = text_of(entry, "title");
    if (slug.empty()) return {};
    std::transform(slug.begin(), slug.end(), slug.begin(),
                   [](unsigned char c) { return std::isspace(c) ? '-' : std::tolower(c); });
    return "applications/" + slug + ".desktop";
}

std::string size_text(int w, int h)
{
    return w > 0 && h > 0 ? std::to_string(w) + "x" + std::to_string(h) : std::string();
}

void note_malformed(const std::string &key, const char *field, const std::string &value)
{
    static std::set<std::string> logged;
    if (!logged.insert(key + "/" + field).second) return;
    std::fprintf(stderr, "apps compat: %s: %s \"%s\" is not WxH, taken as absent\n", key.c_str(), field, value.c_str());
}

} // namespace

std::map<std::string, ScreenNeeds> parse_registry_needs(const std::string &registry_json)
{
    std::map<std::string, ScreenNeeds> needs;
    cJSON *root = cJSON_ParseWithLength(registry_json.data(), registry_json.size());
    if (!root) return needs;
    const cJSON *index = cJSON_GetObjectItemCaseSensitive(root, "index");
    const cJSON *apps = cJSON_GetObjectItemCaseSensitive(index ? index : root, "apps");
    if (cJSON_IsArray(apps)) {
        const cJSON *entry = nullptr;
        cJSON_ArrayForEach(entry, apps)
        {
            if (!cJSON_IsObject(entry)) continue;
            const std::string key = app_key(entry);
            if (key.empty()) continue;
            ScreenNeeds need;
            need.min_size = text_of(entry, "min_size");
            need.portrait_min_size = text_of(entry, "portrait_min_size");
            if (need.portrait_min_size.empty()) need.portrait_min_size = text_of(entry, "portrait"); /* a bool: "" */
            need.desktop = desktop_of(entry);
            if (!need.min_size.empty() && !size_value_valid(need.min_size))
                note_malformed(key, "min_size", need.min_size);
            if (!need.portrait_min_size.empty() && !size_value_valid(need.portrait_min_size))
                note_malformed(key, "portrait_min_size", need.portrait_min_size);
            needs[key] = need; /* every entry: an installed app's facts come from its .desktop */
        }
    }
    cJSON_Delete(root);
    return needs;
}

bool parse_desktop_needs(const std::string &desktop_contents, ScreenNeeds &needs)
{
    const std::optional<DesktopEntry> entry = parse_desktop_entry(desktop_contents);
    if (!entry) return false;
    needs.min_size = size_text(entry->min_w, entry->min_h);
    needs.portrait_min_size = size_text(entry->portrait_w, entry->portrait_h);
    return true;
}

bool read_desktop_needs(const std::string &desktop_path, ScreenNeeds &needs)
{
    if (desktop_path.empty() || desktop_path[0] == '/' || desktop_path.find("..") != std::string::npos) return false;
    const char *env = std::getenv("M5APPSTORE_APP_ROOT");
    const std::string root = env && *env ? env : "/usr/share/APPLaunch";
    std::ifstream file(root + "/" + desktop_path, std::ios::binary);
    if (!file) return false;
    std::ostringstream text;
    text << file.rdbuf();
    return parse_desktop_needs(text.str(), needs);
}

std::map<std::string, ScreenNeeds> read_cache_needs(const std::string &cache_dir_in)
{
    std::map<std::string, ScreenNeeds> needs;
    std::string dir = cache_dir_in;
    if (dir.empty()) {
        const char *env = std::getenv("M5APPSTORE_CACHE_DIR");
        if (env && *env) {
            dir = env;
        } else {
            const char *home = std::getenv("HOME");
            if (!home || !*home) return needs;
            dir = std::string(home) + "/.cache/cardputerzero-appstore";
        }
    }
    DIR *handle = opendir(dir.c_str());
    if (!handle) return needs;
    while (const dirent *item = readdir(handle)) {
        const std::string name = item->d_name;
        if (name.size() < 15 || name.compare(0, 9, "registry-") != 0 || name.compare(name.size() - 5, 5, ".json") != 0)
            continue;
        std::ifstream file(dir + "/" + name, std::ios::binary);
        if (!file) continue;
        std::ostringstream text;
        text << file.rdbuf();
        for (auto &entry : parse_registry_needs(text.str())) needs.insert(std::move(entry));
    }
    closedir(handle);
    return needs;
}

} // namespace settings_apps
