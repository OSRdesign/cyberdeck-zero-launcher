/* SPDX-License-Identifier: MIT
 *
 * Render harness: the few platform entry points the linked launcher code still references after
 * --gc-sections, whose device implementations need hardware or system services. Each one answers
 * like the Pi Zero 2 W deck with nothing attached (no battery, no Ethernet, no sudo).
 */
#include "harness.h"

#include "app_registry.h"
#include "cp0_lvgl_app.h"
#include "cp0_timedate_client.hpp"

#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>

/* ------------------------------------------------------------------------- cp0_lvgl C API */

extern "C" const char *cp0_file_path_c(const char *file)
{
    static std::unordered_map<std::string, std::string> cache;
    const std::string key = file ? file : "";
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, harness::resolve_resource(key)).first;
    return it->second.c_str();
}

/* SettingsSystem::api_time_str: "HH:MM" of the local time (the scene's clock here). */
extern "C" void cp0_time_str(char *buf, int buf_size)
{
    if (!buf || buf_size <= 0) return;
    const time_t now = static_cast<time_t>(harness_wall_clock());
    std::tm local{};
    localtime_r(&now, &local);
    std::strftime(buf, static_cast<size_t>(buf_size), "%H:%M", &local);
}

extern "C" cp0_battery_info_t cp0_battery_read(void)
{
    return cp0_battery_info_t{}; /* valid = 0: no battery gauge on the Pi */
}

extern "C" int cp0_backlight_max(void)
{
    return harness::fixtures().backlight_max;
}

extern "C" int cp0_backlight_write(int val)
{
    if (val < 0) return -1;
    harness::fixtures().backlight = val;
    return val;
}

extern "C" int cp0_sudo_run_argv_async_ex(const char *const *, cp0_sudo_callback_thread_t, cp0_sudo_output_cb_t,
                                          cp0_sudo_complete_cb_t, void *, int, int, uint64_t *)
{
    std::fprintf(stderr, "[harness] sudo request refused (no sudo in the harness)\n");
    return -1;
}

int cp0_timedate_set_ntp(int)
{
    return -1;
}

int cp0_timedate_set_time(const char *)
{
    return -1;
}

/* ------------------------------------------------------------------- launcher app registry */

/* launcher_app_registry_entries() lives in builtin_app_registry.cpp, which instantiates every page
 * class (terminal, SSH, games...). This is a copy of its descriptor table for the Pi build
 * (APPLAUNCH_HW_PIZERO2W, native Calculator); keep it in sync with BUILTIN_APPS. */
const AppDescriptor *launcher_app_registry_entries(std::size_t *count)
{
    static const AppDescriptor entries[] = {
        {"Settings", "setting_100.png", "app_Setting", false, true},
        {"Store", "store_100.png", "app_Store", false, true},
        {"CLI", "cli_100.png", "app_CLI", false, true},
        {"Python", "python_100.png", "app_Python", true, false},
        {"SSH", "ssh_100.png", "app_SSH", true, false},
        {"IP Panel", "ip_panel_100.png", "app_IP_Panel", true, false},
        {"Calculator", "math_100.png", "app_Math", true, false},
        {"Snake", "game_100.png", "app_Game", true, false},
        {"Tank", "tank_100.png", "app_Tank", true, false},
    };
    if (count) *count = sizeof(entries) / sizeof(entries[0]);
    return entries;
}
