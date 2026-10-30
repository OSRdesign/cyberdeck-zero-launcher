/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's side of the home grid's user order (see home_order_launcher.hpp).
 */

#include "home_order_launcher.hpp"

#include "app_registry.h"
#include "hal_lvgl_bsp.h"
#include "launch.h"
#include "model/home_order.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

home_order::ConfigCall config_call()
{
    return [](std::list<std::string> arguments, home_order::Reply reply) {
        cp0_signal_config_api(std::move(arguments), std::move(reply));
    };
}

} // namespace

void launcher_home_order_apply(std::list<app> &apps)
{
    const std::vector<std::uint32_t> saved = home_order::load(config_call());
    if (saved.empty() || apps.size() < 2) return;

    std::vector<std::list<app>::iterator> tiles;
    std::vector<std::uint32_t> keys;
    tiles.reserve(apps.size());
    keys.reserve(apps.size());
    for (auto it = apps.begin(); it != apps.end(); ++it) {
        tiles.push_back(it);
        keys.push_back(home_order::key_of(it->Id));
    }
    std::list<app> ordered;
    for (const std::size_t index : home_order::arrange(keys, saved)) ordered.splice(ordered.end(), apps, tiles[index]);
    ordered.splice(ordered.end(), apps); /* nothing is left; kept so no tile can ever be dropped */
    apps = std::move(ordered);
}

bool launcher_home_order_save(const std::list<app> &apps)
{
    std::vector<std::string> registered;
    std::size_t count = 0;
    const AppDescriptor *entries = launcher_app_registry_entries(&count);
    for (std::size_t index = 0; entries && index < count; ++index)
        if (entries[index].config_key && entries[index].config_key[0])
            registered.emplace_back(entries[index].config_key);
    std::vector<std::string> visible;
    for (const app &item : apps) visible.push_back(item.Id);
    const bool saved = home_order::save(config_call(), registered, visible);
    if (!saved) std::fprintf(stderr, "home order: cannot save (config store full?)\n");
    return saved;
}

bool launcher_home_order_stored()
{
    return !home_order::load(config_call()).empty();
}

bool launcher_home_order_reset()
{
    const bool cleared = home_order::reset(config_call());
    if (!cleared) std::fprintf(stderr, "home order: cannot reset (config store refused the write)\n");
    return cleared;
}
