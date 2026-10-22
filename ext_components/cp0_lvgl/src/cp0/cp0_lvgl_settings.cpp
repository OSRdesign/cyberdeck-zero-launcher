/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "hal_lvgl_bsp.h"
#include "cp0_lvgl_app.h"
#include "cp0_backlight_profile.h"

#include "cp0_lvgl_log.h"
#include "../cp0_settings_policy.hpp"
#include "../cp0_signal_registration.hpp"
#include "../cp0_sync_signal.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <utility>

namespace {

class SettingsSystem
{
public:
    using callback_t = std::function<void(int, std::string)>;
    using arg_t = cp0::settings::Arguments;

    void api_call(arg_t arg, callback_t callback)
    {
        try {
        if (!cp0::settings::valid_arguments(arg)) {
            report(callback, -1, "invalid settings api request");
            return;
        }
        switch (cp0::settings::command_from(arg)) {
        case cp0::settings::Command::BacklightRead: {
            int val = backlight_read();
            report(callback, val < 0 ? -1 : 0, std::to_string(val));
            break;
        }
        case cp0::settings::Command::BacklightMax: {
            int val = backlight_max();
            report(callback, val < 0 ? -1 : 0, std::to_string(val));
            break;
        }
        case cp0::settings::Command::BacklightWrite: {
            int requested = 0;
            if (!cp0::settings::integer_argument(
                    arg, 1, 0, std::numeric_limits<int>::max(), requested)) {
                report(callback, -1, "invalid settings api request");
                break;
            }
            int val = backlight_write(requested);
            report(callback, val < 0 ? -1 : 0, std::to_string(val));
            break;
        }
        case cp0::settings::Command::Log: {
            const std::string topic = cp0::settings::argument_at(arg, 1);
            const std::string message = cp0::settings::argument_at(arg, 2);
            settings_log(topic.c_str(), message.c_str());
            report(callback, 0, "");
            break;
        }
        case cp0::settings::Command::TimeStr: {
            char buf[32] = {};
            time_str(buf, sizeof(buf));
            report(callback, 0, buf);
            break;
        }
        case cp0::settings::Command::Unknown:
        default:
            report(callback, -1, "unknown settings api command");
            break;
        }
        } catch (...) {
            report(callback, -1, "settings api failure");
        }
    }

    static int api_int(const arg_t &arg,
                       int default_value,
                       int minimum,
                       int maximum)
    {
        int result = default_value;
        cp0::signal::invoke_noexcept([&] { cp0_signal_settings_api(arg, [&](int code, std::string data) {
            int parsed = 0;
            if (code == 0 &&
                cp0::settings::parse_integer_response(data, minimum, maximum, parsed))
                result = parsed;
        }); });
        return result;
    }

    static void api_time_str(char *buf, int buf_size)
    {
        if (!buf || buf_size <= 0)
            return;
        buf[0] = '\0';
        cp0::signal::invoke_noexcept([&] { cp0_signal_settings_api({"TimeStr"}, [&](int code, std::string data) {
            if (code == 0)
                copy_string(buf, static_cast<size_t>(buf_size), data);
        }); });
        if (buf[0] == '\0')
            fallback_time_str(buf, buf_size);
    }

private:
    void report(callback_t callback, int code, const std::string &data)
    {
        if (!callback) return;
        try {
            callback(code, data);
        } catch (...) {
        }
    }

    static void copy_string(char *dst, size_t dst_size, const std::string &src)
    {
        if (!dst || dst_size == 0)
            return;
        std::strncpy(dst, src.c_str(), dst_size - 1);
        dst[dst_size - 1] = '\0';
    }

    static void fallback_time_str(char *buf, int buf_size)
    {
        if (!buf || buf_size <= 0)
            return;
        std::time_t now = std::time(nullptr);
        std::tm *t = std::localtime(&now);
        if (!t) {
            buf[0] = '\0';
            return;
        }
        std::snprintf(buf, static_cast<size_t>(buf_size), "%02d:%02d", t->tm_hour, t->tm_min);
    }

    static int read_int_file(const char *path,
                             int default_value,
                             int minimum,
                             int maximum)
    {
        FILE *f = std::fopen(path, "r");
        if (!f)
            return default_value;
        char buffer[64] = {};
        const bool read_succeeded = std::fgets(buffer, sizeof(buffer), f) != nullptr;
        std::fclose(f);
        int value = default_value;
        return read_succeeded && cp0::settings::parse_integer_file_line(
                                     buffer, minimum, maximum, value)
                   ? value
                   : default_value;
    }

    /* Board profile (cp0_backlight_profile.h): a gpio on/off or a sysfs (levels) backlight lives in
     * its own sysfs directory; the default stays today's /sys/class/backlight/backlight. Every
     * brightness client (Settings, hotkeys, screensaver, startup restore) goes through these three calls.
     * No backlight (none, or none detected): every call fails. */
    static bool backlight_onoff()
    {
        return cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_GPIO_ONOFF;
    }

    static bool backlight_none()
    {
        return cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_NONE;
    }

    static std::string backlight_file(const char *name)
    {
        if (cp0_backlight_profile_kind() != CP0_BACKLIGHT_KIND_DEFAULT)
            return std::string(cp0_backlight_profile_dir()) + "/" + name;
        return std::string("/sys/class/backlight/backlight/") + name;
    }

    int backlight_read()
    {
        if (backlight_none())
            return -1;
        return read_int_file(backlight_file("brightness").c_str(), -1, 0,
                             std::numeric_limits<int>::max());
    }

    int backlight_max()
    {
        if (backlight_none())
            return -1;
        return read_int_file(backlight_file("max_brightness").c_str(), backlight_onoff() ? 1 : 100, 1,
                             std::numeric_limits<int>::max());
    }

    int backlight_write(int val)
    {
        if (backlight_none())
            return -1;
        if (val < 0)
            val = 0;
        int mx = backlight_max();
        if (val > mx || (backlight_onoff() && val > 0))
            val = mx; // on/off: any level above zero is "on"
        FILE *f = std::fopen(backlight_file("brightness").c_str(), "w");
        if (!f)
            return -1;
        const int written = std::fprintf(f, "%d", val);
        const int close_result = std::fclose(f);
        return written > 0 && close_result == 0 ? val : -1;
    }

    void settings_log(const char *topic, const char *message)
    {
        cp0_zmq_log(topic && topic[0] ? topic : "settings", message ? message : "");
    }

    void time_str(char *buf, int buf_size) { fallback_time_str(buf, buf_size); }
};

} // namespace

extern "C" void init_settings(void)
{
    cp0_zmq_log_init();
    cp0_zmq_log("settings", "init_settings");
    auto settings = std::make_shared<SettingsSystem>();
    static cp0::SignalRegistration<decltype(cp0_signal_settings_api)> registration;
    registration.replace(cp0_signal_settings_api,
                         [settings](std::list<std::string> arg,
                                    std::function<void(int, std::string)> callback) {
                             settings->api_call(std::move(arg), std::move(callback));
                         });
}

extern "C" int cp0_backlight_read(void)
{
    return SettingsSystem::api_int(
        {"BacklightRead"}, -1, 0, std::numeric_limits<int>::max());
}

extern "C" int cp0_backlight_max(void)
{
    return SettingsSystem::api_int(
        {"BacklightMax"}, 100, 1, std::numeric_limits<int>::max());
}

extern "C" int cp0_backlight_write(int val)
{
    if (val < 0) return -1;
    return SettingsSystem::api_int(
        {"BacklightWrite", std::to_string(val)}, -1, 0,
        std::numeric_limits<int>::max());
}

extern "C" void cp0_time_str(char *buf, int buf_size)
{
    SettingsSystem::api_time_str(buf, buf_size);
}
