/*
 * SPDX-License-Identifier: MIT
 *
 * Volume, brightness, beeps, toasts and idle dimming for apps (see app_media.hpp).
 */
#include "app_media.hpp"

#include <deque>
#include <mutex>

namespace app_media {
namespace {

std::mutex g_toast_mutex;
std::deque<std::string> g_toasts;

} // namespace

void toast_post(const std::string &text)
{
    std::lock_guard<std::mutex> lock(g_toast_mutex);
    if (g_toasts.size() >= 4) g_toasts.pop_front();
    g_toasts.push_back(text);
}

bool toast_take(std::string &text)
{
    std::lock_guard<std::mutex> lock(g_toast_mutex);
    if (g_toasts.empty()) return false;
    text = std::move(g_toasts.front());
    g_toasts.pop_front();
    return true;
}

} // namespace app_media

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "cp0_backlight_profile.h"
#include "cp0_capabilities.h"
#include "cp0_power_supply.h"
#include "hal_lvgl_bsp.h"
#include "launcher_media_controls.h"
#include "launcher_toast.h"
#include "lvgl/lvgl.h"
#include "model/global_hint_policy.hpp"
#include "model/screensaver_runtime_contract.hpp"
#include "model/setup_value_policy.hpp"
#include "sample_log.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>

namespace app_media {
namespace {

using Clock = std::chrono::steady_clock;

std::mutex g_ops; /* every level read and write */
std::atomic<unsigned> g_generation{0};

/* ---- capabilities */
std::mutex g_cap_mutex;
bool g_cap_known = false;
bool g_cap_sound = false;
Clock::time_point g_cap_at;

/* ---- the worker */
enum class KeyAction { VolumeUp, VolumeDown, Mute, BrightnessUp, BrightnessDown };

std::mutex g_mutex; /* guards the queue and the dim state below */
std::condition_variable g_wake;
std::thread g_worker;
bool g_stopping = false;
std::deque<KeyAction> g_keys;

std::atomic<bool> g_app_front{false};
std::atomic<bool> g_raw_session{false};
std::atomic<bool> g_dim_allowed{false};
std::atomic<int> g_dark_s{0};
std::atomic<long long> g_activity_ms{0};

bool g_dimmed = false;
int g_dim_raw = -1;
long long g_dim_at_ms = 0;

long long now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

void bump()
{
    g_generation.fetch_add(1);
}

void undim_locked()
{
    if (!g_dimmed) return;
    g_dimmed = false;
    const int raw = g_dim_raw;
    g_dim_raw = -1;
    try {
        launcher_media_controls::restore_backlight(raw);
    } catch (...) {
    }
    bump();
}

void run_key(KeyAction action)
{
    try {
        std::lock_guard<std::mutex> ops(g_ops);
        switch (action) {
        case KeyAction::BrightnessUp:
        case KeyAction::BrightnessDown:
            if (!backlight_present()) return;
            (void)launcher_media_controls::adjust_brightness(action == KeyAction::BrightnessUp
                                                                 ? setup_values::kBrightnessStepPercent
                                                                 : -setup_values::kBrightnessStepPercent);
            break;
        case KeyAction::VolumeUp:
        case KeyAction::VolumeDown:
            if (!sound_present()) return;
            (void)launcher_media_controls::adjust_volume(action == KeyAction::VolumeUp
                                                             ? launcher_media_controls::VOLUME_STEP_PERCENT
                                                             : -launcher_media_controls::VOLUME_STEP_PERCENT);
            break;
        case KeyAction::Mute:
            if (!sound_present()) return;
            (void)launcher_media_controls::toggle_mute();
            break;
        }
        bump();
    } catch (...) {
    }
}

void tick_dim()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const long long now = now_ms();
    const bool active = g_app_front.load() && g_raw_session.load() && g_dim_allowed.load();
    if (g_dimmed) {
        if (!active || g_activity_ms.load() > g_dim_at_ms) undim_locked();
        return;
    }
    const int dark = g_dark_s.load();
    if (!active || dark <= 0 || now - g_activity_ms.load() < static_cast<long long>(dark) * 1000) return;
    try {
        std::lock_guard<std::mutex> ops(g_ops);
        const int raw = launcher_media_controls::dim_backlight(launcher_media_controls::idle_dim_percent()); /* as the clock screensaver */
        if (raw > 0) {
            g_dimmed = true;
            g_dim_raw = raw;
            g_dim_at_ms = now;
            SLOGI("[DIM] app idle: backlight dimmed");
        }
    } catch (...) {
    }
}

void work()
{
    for (;;) {
        std::deque<KeyAction> batch;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            if (!g_stopping && g_keys.empty()) g_wake.wait_for(lock, std::chrono::seconds(1));
            if (g_stopping) return;
            batch.swap(g_keys);
            /* any key or touch since the dim: the light comes back before the key is acted on */
            if (g_dimmed && g_activity_ms.load() > g_dim_at_ms) undim_locked();
        }
        for (KeyAction a : batch) run_key(a);
        tick_dim();
    }
}

} // namespace

/* ---------------------------------------------------------------------------------------------------- lifecycle */

void start()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_worker.joinable()) return;
    g_stopping = false;
    g_worker = std::thread(work);
}

void stop()
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_worker.joinable()) return;
        g_stopping = true;
    }
    g_wake.notify_all();
    g_worker.join();
    std::lock_guard<std::mutex> lock(g_mutex);
    undim_locked();
}

/* ---------------------------------------------------------------------------------------------------- the board */

bool sound_present()
{
#if !defined(APPLAUNCH_HW_PIZERO2W)
    return true; /* the default (simulator) profile: as the keys always were */
#else
    std::lock_guard<std::mutex> lock(g_cap_mutex);
    const Clock::time_point now = Clock::now();
    if (!g_cap_known || now - g_cap_at > std::chrono::seconds(10)) {
        cp0_caps_t caps;
        cp0_caps_probe(&caps, CP0_CAPS_SERVER_TIMEOUT_MS);
        g_cap_sound = caps.sound == CP0_CAP_PRESENT;
        g_cap_known = true;
        g_cap_at = now;
    }
    return g_cap_sound;
#endif
}

bool backlight_present()
{
#if !defined(APPLAUNCH_HW_PIZERO2W)
    return true;
#else
    return cp0_backlight_profile_kind() != CP0_BACKLIGHT_KIND_NONE;
#endif
}

const char *backlight_kind()
{
    if (!backlight_present()) return "none";
    return cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_GPIO_ONOFF ? "onoff" : "levels";
}

bool dim_eligible()
{
    if (std::string(backlight_kind()) != "levels") return false;
    cp0_power_battery_t battery;
    return cp0_power_supply_battery(&battery) != 0;
}

/* ---------------------------------------------------------------------------------------------------- levels */

int volume_get(bool &muted)
{
    muted = false;
    if (!sound_present()) return -1;
    std::lock_guard<std::mutex> ops(g_ops);
    muted = launcher_media_controls::read_mute_state();
    return launcher_media_controls::read_volume_percent();
}

int volume_set(int percent)
{
    if (!sound_present()) return -1;
    std::lock_guard<std::mutex> ops(g_ops);
    const int now = launcher_media_controls::set_volume_percent(percent);
    bump();
    return now;
}

int volume_step(int direction)
{
    if (!sound_present()) return -1;
    std::lock_guard<std::mutex> ops(g_ops);
    const int now = launcher_media_controls::adjust_volume(direction > 0 ? launcher_media_controls::VOLUME_STEP_PERCENT
                                                                         : -launcher_media_controls::VOLUME_STEP_PERCENT);
    bump();
    return now;
}

int volume_mute(bool muted)
{
    if (!sound_present()) return -1;
    std::lock_guard<std::mutex> ops(g_ops);
    (void)launcher_media_controls::set_mute_state(muted);
    bump();
    return launcher_media_controls::read_volume_percent();
}

int brightness_get()
{
    if (!backlight_present()) return -1;
    std::lock_guard<std::mutex> ops(g_ops);
    return launcher_media_controls::read_brightness_percent();
}

int brightness_set(int percent)
{
    if (std::string(backlight_kind()) != "levels") return -1;
    std::lock_guard<std::mutex> ops(g_ops);
    const int now = launcher_media_controls::set_brightness_percent(percent);
    bump();
    return now;
}

Levels read_levels()
{
    Levels levels;
    bool muted = false;
    levels.volume = volume_get(muted);
    levels.mute = muted ? 1 : 0;
    levels.brightness_pct = brightness_get();
    return levels;
}

unsigned levels_generation()
{
    return g_generation.load();
}

/* ---------------------------------------------------------------------------------------------------- beeps */

int beep(const std::string &name)
{
    const char *asset = name == "notify" ? "Ding2.wav" : name == "done" ? "enter.wav" : name == "warn" ? "blocked.mp3" : nullptr;
    if (!asset) return 2;
    if (!sound_present()) return 1;
    try {
        static std::once_flag registered;
        std::call_once(registered, [] {
            cp0_signal_audio_api({"RegisterSystemSounds", "Ding2.wav", "enter.wav", "blocked.mp3"}, nullptr);
        });
        cp0_signal_system_play(asset);
    } catch (...) {
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------------------- toasts */

namespace {

void toast_pump(lv_timer_t *)
{
    if (g_app_front.load()) return;
    static uint32_t shown_at = 0;
    static bool shown = false;
    if (shown && lv_tick_elaps(shown_at) < 2500) return; /* the previous one stays a moment */
    std::string text;
    if (!toast_take(text)) return;
    launcher_toast().show(text.c_str());
    shown_at = lv_tick_get();
    shown = true;
}

} // namespace

void toast_install_pump()
{
    static lv_timer_t *timer = nullptr;
    if (!timer) timer = lv_timer_create(toast_pump, 400, nullptr);
}

/* ---------------------------------------------------------------------------------------------------- the app in front */

void set_app_front(bool front)
{
    g_app_front.store(front);
    g_activity_ms.store(now_ms());
    if (!front) {
        g_dim_allowed.store(false); /* a permission is for the app that gave it */
        std::lock_guard<std::mutex> lock(g_mutex);
        undim_locked();
    }
    g_wake.notify_all();
}

void raw_key(int code, int value)
{
    if (!g_app_front.load()) return;
    g_activity_ms.store(now_ms());
    const GlobalHintAction action = GlobalHintPolicy().action_for(
        {static_cast<uint32_t>(code), nullptr, value == 1, value == 2, false, false, false});
    KeyAction key;
    switch (action) {
    case GlobalHintAction::VOLUME_UP: key = KeyAction::VolumeUp; break;
    case GlobalHintAction::VOLUME_DOWN: key = KeyAction::VolumeDown; break;
    case GlobalHintAction::TOGGLE_MUTE: key = KeyAction::Mute; break;
    case GlobalHintAction::BRIGHTNESS_UP: key = KeyAction::BrightnessUp; break;
    case GlobalHintAction::BRIGHTNESS_DOWN: key = KeyAction::BrightnessDown; break;
    default:
        if (g_dimmed) g_wake.notify_all();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_keys.size() < 8) g_keys.push_back(key);
    }
    g_wake.notify_all();
}

void raw_touch()
{
    g_activity_ms.store(now_ms());
    if (g_dimmed) g_wake.notify_all();
}

void raw_session(bool on)
{
    g_raw_session.store(on);
    g_activity_ms.store(now_ms());
    g_wake.notify_all();
}

bool set_dim_allowed(bool allow)
{
    if (!allow) {
        g_dim_allowed.store(false);
        g_wake.notify_all();
        return true;
    }
    if (!dim_eligible()) return false;
    int seconds = 30;
    try {
        bool ok = false;
        std::string response;
        cp0_signal_config_api({"GetInt", "dark_time", "30"}, [&](int code, std::string data) {
            ok = code == 0;
            response = std::move(data);
        });
        seconds = screensaver_timeout_from_config(ok, response);
    } catch (...) {
    }
    g_dark_s.store(seconds);
    g_activity_ms.store(now_ms());
    g_dim_allowed.store(true);
    g_wake.notify_all();
    return true;
}

} // namespace app_media

#else /* SDL simulator and non-Linux builds: nothing to drive */

namespace app_media {

void start() {}
void stop() {}
bool sound_present() { return false; }
bool backlight_present() { return false; }
const char *backlight_kind() { return "none"; }
bool dim_eligible() { return false; }
int volume_get(bool &muted) { muted = false; return -1; }
int volume_set(int) { return -1; }
int volume_step(int) { return -1; }
int volume_mute(bool) { return -1; }
int brightness_get() { return -1; }
int brightness_set(int) { return -1; }
Levels read_levels() { return Levels(); }
unsigned levels_generation() { return 0; }
int beep(const std::string &) { return 1; }
void toast_install_pump() {}
void set_app_front(bool) {}
void raw_key(int, int) {}
void raw_touch() {}
void raw_session(bool) {}
bool set_dim_allowed(bool) { return false; }

} // namespace app_media

#endif

/* The raw readers' hooks (weak no-ops in the supervisor and in the external app runner of cp0_lvgl). */
extern "C" void cp0_external_app_session(int on)
{
    app_media::raw_session(on != 0);
}

extern "C" void cp0_external_app_key(int code, int value)
{
    app_media::raw_key(code, value);
}

extern "C" void cp0_external_app_touch(void)
{
    app_media::raw_touch();
}
