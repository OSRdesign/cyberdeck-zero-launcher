/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "../app_registry.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace settings_t12b::launcher {

struct AppEntry
{
    std::string label;
    std::string config_key;
    bool enabled = false;
};

// Settings and CLI never get a toggle. The Store is not listed here: its descriptor decides (configurable where the
// hardware profile sets APPLAUNCH_LAUNCHER_STORE_TOGGLE, always on and not configurable on the stock CardputerZero).
inline bool is_protected_launcher_app(std::string_view config_key)
{
    return config_key == "app_Setting" ||
           config_key == "app_CLI";
}

// The bundled Store is the stock one, unchanged: Settings > Launcher credits it in a read-only row right under its toggle
// (only where the toggle exists, i.e. the Store row is listed). Same text as the Licenses page.
inline constexpr const char *kStoreCredit = "CardputerZero Store by M5Stack";

inline bool store_credit_follows(std::string_view config_key)
{
    return config_key == "app_Store";
}

inline bool is_launcher_settings_origin(LauncherAppOrigin origin)
{
    return origin == LauncherAppOrigin::Builtin ||
           origin == LauncherAppOrigin::Preinstalled;
}

template <typename Descriptor, typename EnabledPredicate>
std::vector<AppEntry> configurable_entries(const Descriptor *entries,
                                           std::size_t count,
                                           EnabledPredicate enabled)
{
    std::vector<AppEntry> result;
    if (!entries || count == 0) return result;

    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const Descriptor &entry = entries[index];
        if (!entry.configurable || !entry.label || !entry.label[0] ||
            !entry.config_key || !entry.config_key[0] ||
            !is_launcher_settings_origin(entry.origin) ||
            is_protected_launcher_app(entry.config_key))
            continue;
        result.push_back({entry.label, entry.config_key, enabled(entry)});
    }
    return result;
}

} // namespace settings_t12b::launcher
