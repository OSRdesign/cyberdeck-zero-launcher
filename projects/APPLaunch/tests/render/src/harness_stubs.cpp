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
#include "cp0_timezone.hpp"
#include "input_presence.hpp"
#include "native_screensaver.hpp"
#include "settings/settings_hw_profile.hpp"

#include "lvgl/lvgl.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <mutex>
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

/* `env HARNESS_SUDO ok`: the request succeeds a cycle later as if the password was right and the command exited 0
 * (Settings > Updates' systemd-run: nothing runs; the scene then gives the helper's status file). */
namespace {
struct SudoOk {
    cp0_sudo_complete_cb_t complete;
    void *user;
};
void sudo_ok_cb(void *data)
{
    auto *ok = static_cast<SudoOk *>(data);
    if (ok && ok->complete) ok->complete(CP0_SUDO_RESULT_SUCCESS, 0, ok->user);
    delete ok;
}
} // namespace

/* Settings > Apps' privileged step (settings_apps_jobs.cpp), Settings > Updates' root step. Refused, unless the scene
 * sets `env HARNESS_SUDO prompt`: then the responsive password prompt opens as on the board
 * (harness::sudo_prompt_request); Cancel / Esc ends the request as cancelled, nothing ever runs. `env HARNESS_SUDO ok`:
 * it succeeds without a prompt (sudo_ok_cb). */
extern "C" int cp0_sudo_run_argv_async_ex(const char *const *, cp0_sudo_callback_thread_t, cp0_sudo_output_cb_t,
                                          cp0_sudo_complete_cb_t complete, void *user, int, int, uint64_t *request_id)
{
    const char *mode = std::getenv("HARNESS_SUDO");
    if (mode && std::strcmp(mode, "ok") == 0 && complete) {
        if (lv_async_call(sudo_ok_cb, new SudoOk{complete, user}) != LV_RESULT_OK) return -1;
        if (request_id) *request_id = 1;
        return 0;
    }
    if (mode && std::strcmp(mode, "prompt") == 0 && complete &&
        harness::sudo_prompt_request([complete, user] { complete(CP0_SUDO_RESULT_CANCELLED, -ECANCELED, user); })) {
        if (request_id) *request_id = 1;
        return 0;
    }
    std::fprintf(stderr, "[harness] sudo request refused (no sudo in the harness)\n");
    return -1;
}

/* The Network Time toggle (a worker thread): always works; NtpGet answers the new state. */
int cp0_timedate_set_ntp(int enabled)
{
    harness::fixtures().ntp_on = enabled != 0;
    return 0;
}

/* ------------------------------------------------------------------ Date & Time > Time zone (fixtures, harness.h) */

int cp0_timedate_get_timezone(char *zone, int size)
{
    if (!zone || size <= 0) return -22;
    harness::Fixtures &f = harness::fixtures();
    std::lock_guard<std::mutex> lock(f.tz_mutex);
    if (f.timezone.empty()) return -1;
    if (f.timezone.size() + 1 > static_cast<std::size_t>(size)) return -34;
    std::memcpy(zone, f.timezone.c_str(), f.timezone.size() + 1);
    return 0;
}

/* a worker thread: the scene's "tzset ok|fail|denied" */
int cp0_timedate_set_timezone(const char *zone)
{
    if (!zone || !cp0::tz::valid_name(zone)) return -22;
    harness::Fixtures &f = harness::fixtures();
    std::lock_guard<std::mutex> lock(f.tz_mutex);
    if (f.timezone_set_result == 0) f.timezone = zone;
    return f.timezone_set_result;
}

/* a worker thread: fixtures/timezones.txt */
char *cp0_timedate_list_timezones(void)
{
    {
        harness::Fixtures &f = harness::fixtures();
        std::lock_guard<std::mutex> lock(f.tz_mutex);
        if (!f.timezone_list) return nullptr; /* "tzlist off" */
    }
    std::ifstream file(harness::fixtures().fixture_root + "/timezones.txt", std::ios::binary);
    if (!file) return nullptr;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return strdup(text.c_str());
}

/* the LVGL thread, after a change: the process's local time follows the fixture's zone (the harness runs with TZ set,
 * UTC by default; the host's zoneinfo files give the offsets) */
void cp0_timedate_zone_changed(void)
{
    harness::Fixtures &f = harness::fixtures();
    std::string zone;
    {
        std::lock_guard<std::mutex> lock(f.tz_mutex);
        zone = f.timezone;
    }
    setenv("TZ", zone.c_str(), 1);
    tzset();
}

/* ------------------------------------------------------------------ on-screen keyboard policy inputs (launcher_osk) */

/* input_presence.cpp reads /proc and inotify on the device: here the scene says how many keyboards are present
 * ("keyboards N", default none) and none ever went away. */
input_presence::KeyboardSnapshot input_presence::keyboards()
{
    input_presence::KeyboardSnapshot snapshot;
    snapshot.known = true;
    snapshot.keyboards = harness::fixtures().keyboards; /* scene command "keyboards N" */
    snapshot.now_s = 1000;
    return snapshot;
}

/* input.state is not published by the harness (no presence watcher); the rule launcher_osk publishes is kept for the
 * scene command expect_vkb (report 041: zone while the writing zone is the text input) */
void input_presence::set_vkb(cp0_app_vkb_t vkb)
{
    harness::fixtures().vkb = cp0_app_vkb_name(vkb);
}
void input_presence::set_pointer_nav(bool) {}
/* input.state hand= (report 044), kept for the scene command expect_hand */
void input_presence::set_hand(bool on)
{
    harness::fixtures().hand = on ? 1 : 0;
}

/* ui_screensaver.cpp (the stock lock screen) is not linked: on the native port it answers the clock screensaver's
 * state (native_screensaver.cpp, linked here), so this does too */
extern "C" int ui_screensaver_is_active(void)
{
    return native_screensaver::active() ? 1 : 0;
}

/* Date & Time > Set Manually > Save? (a worker thread): the scene's "timeset ok|fail". A set time is what LocalTime
 * answers afterwards; the wall clock (the status strip's) stays the scene's. */
int cp0_timedate_set_time(const char *timestamp)
{
    harness::Fixtures &f = harness::fixtures();
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!f.time_settable || !timestamp ||
        std::sscanf(timestamp, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
        return -1;
    std::tm t{};
    t.tm_year = year - 1900, t.tm_mon = month - 1, t.tm_mday = day, t.tm_hour = hour, t.tm_min = minute,
    t.tm_sec = second;
    f.time_set_epoch.store(static_cast<int64_t>(timegm(&t)));
    return 0;
}

/* ------------------------------------------------------------------- launcher app registry */

/* launcher_app_registry_entries() lives in builtin_app_registry.cpp, which instantiates every page
 * class (terminal, SSH, games...). This is a copy of its descriptor table for the Pi build
 * (APPLAUNCH_HW_PIZERO2W, native Calculator); keep it in sync with BUILTIN_APPS. */
const AppDescriptor *launcher_app_registry_entries(std::size_t *count)
{
    static const AppDescriptor entries[] = {
        {"Settings", "setting_100.png", "app_Setting", false, true},
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
