/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's pointer navigation setting (see launcher_pointer.hpp).
 */

#include "launcher_pointer.hpp"

#include "input_presence.hpp"

#include "hal_lvgl_bsp.h"
#include "sample_log.h"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)
#include "cp0_pointer_nav.h"
#endif

#include <charconv>
#include <list>
#include <string>
#include <utility>

namespace launcher_pointer {
namespace {

constexpr const char *kConfigKey = "pointer_nav";

int s_mode = -1; /* -1: not read yet */
bool s_installed = false;

bool call_config(std::list<std::string> arguments, std::string *data = nullptr)
{
    bool ok = false;
    try {
        cp0_signal_config_api(std::move(arguments), [&](int code, std::string reply) {
            ok = code == 0;
            if (data) *data = std::move(reply);
        });
    } catch (...) {
        return false;
    }
    return ok;
}

void apply()
{
    const bool on = mode() == kOn;
#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)
    cp0_pointer_nav_set_enabled(on ? 1 : 0); /* the launcher's own pointer reader */
#endif
    input_presence::set_pointer_nav(on); /* the apps: input.state pointer= */
    SLOGI("[PTR] Settings > Pointer navigation: %s", on ? "On" : "Off");
}

} // namespace

int mode()
{
    if (s_mode >= 0) return s_mode;
    std::string text;
    int value = kOff;
    if (call_config({"GetInt", kConfigKey, "0"}, &text)) {
        int parsed = kOff;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec == std::errc{} && (parsed == kOff || parsed == kOn)) value = parsed;
    }
    s_mode = value;
    return s_mode;
}

bool set_mode(int value)
{
    if (value != kOff && value != kOn) return false;
    if (!call_config({"SetInt", kConfigKey, std::to_string(value)}) || !call_config({"Save"})) return false;
    s_mode = value;
    if (s_installed) apply();
    return true;
}

void install()
{
    if (s_installed) return;
    s_installed = true;
    apply();
}

} // namespace launcher_pointer
