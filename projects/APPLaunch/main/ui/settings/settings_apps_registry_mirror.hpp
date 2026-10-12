/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps: the Store's shared source list, kept in step with the backend (pure, no I/O, no LVGL).
 *
 * The Store UI keeps its own copy of the source list in the launcher's config.json (keys "appstore.registry.*", see
 * projects/AppStore/main/interface/shared_registry_store.cpp) and, at every start, writes that copy over the backend's
 * registries.json. A source added only to the backend file (Settings > Apps, install.sh) is therefore wiped the next
 * time the Store opens. The launcher mirrors the backend list into the same keys, in exactly the Store's format, so
 * that the copy the Store imports always holds the sources of Settings > Apps.
 *
 * Format: count, region, active_region, and N.entry = name TAB url TAB enabled(0/1) TAB builtin(0/1) TAB region with
 * the Store's escaping (\\ \t \n \r), at most 16 entries; the unused slots are written empty.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace apps_registry_mirror {

constexpr const char *kPrefix = "appstore.registry.";
constexpr int kMaxEntries = 16;
/* The launcher config service cuts a value at this length (cp0_config_service.hpp kMaxValueLength). */
constexpr std::size_t kMaxValueLength = 255;

/* The project's own source (docs/HOSTING-APPS.md, pizero2w/default-sources.sh). */
constexpr const char *kDefaultSource = "OSRdesign/cyberdeck-zero-apps";
/* The once-per-user decision, a config.json key. */
constexpr const char *kDefaultFlagKey = "apps_default_source";

struct Entry {
    std::string name;
    std::string url;
    bool enabled = true;
    bool builtin = false;
    std::string region;
};

struct Config {
    std::string region = "auto";
    std::string active_region = "default";
    std::vector<Entry> entries;
};

using KeyValues = std::vector<std::pair<std::string, std::string>>;

std::string pack_entry(const Entry &entry);
bool unpack_entry(const std::string &packed, Entry &entry);

/* The backend's --registry-config output (CONFIG and CONFIG_REG records). False when it holds no source. */
bool parse_backend(const std::string &output, Config &config);

/* Every key to write, in the Store's save() order: region, active_region, count, then the 16 slots (unused ones
 * empty). False, nothing to write, when the list is empty (an empty list is never written over a non-empty one) or
 * when no entry fits a config value. An entry whose packed form exceeds kMaxValueLength cannot be stored (the config
 * service would cut it and the Store would drop it): it is left out and counted in `skipped`. */
bool pack(const Config &config, KeyValues &keys, int *skipped = nullptr);

/* The Store's load(): `get` returns a key's value ("" when missing). False when there is no usable list. */
bool parse(const std::function<std::string(const std::string &)> &get, Config &config);

/* Both lists would be written as the same keys. */
bool equivalent(const Config &a, const Config &b);

struct PlanInput {
    bool backend_read = false;     /* the backend list could be read */
    Config backend;
    bool mirror_present = false;   /* a usable mirror is in config.json */
    Config mirror;
    bool flag_set = false;         /* the default source was decided already (kDefaultFlagKey) */
};

struct Plan {
    bool write_mirror = false;     /* push backend -> mirror */
    bool add_default = false;      /* add kDefaultSource to the backend, then set the flag and refresh the mirror */
    bool set_flag = false;         /* a source of the user's is there: only record the decision */
};

/* The backend is the source of truth: the mirror follows it, never the reverse. Nothing at all when the backend list
 * is unknown or empty. */
Plan plan(const PlanInput &input);

} // namespace apps_registry_mirror
