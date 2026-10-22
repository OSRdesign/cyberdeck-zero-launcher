/*
 * SPDX-License-Identifier: MIT
 *
 * The board's capabilities as the launcher uses them (see settings_capabilities.hpp).
 */
#include "settings_capabilities.hpp"

#include "cp0_backlight_profile.h"

#include <chrono>

namespace settings_caps {

cp0_caps_t probe_for_settings()
{
    cp0_caps_t caps;
    cp0_caps_probe(&caps, CP0_CAPS_SERVER_TIMEOUT_MS);
    return caps;
}

bool sound_for_keys()
{
#if !defined(APPLAUNCH_HW_PIZERO2W)
    return true; /* the default (simulator) profile: the keys work as they always did */
#else
    using Clock = std::chrono::steady_clock;
    static bool known = false;
    static bool present = false;
    static Clock::time_point checked;
    const Clock::time_point now = Clock::now();
    if (!known || now - checked > std::chrono::seconds(10)) {
        cp0_caps_t caps;
        cp0_caps_probe(&caps, CP0_CAPS_SERVER_TIMEOUT_MS);
        present = caps.sound == CP0_CAP_PRESENT;
        known = true;
        checked = now;
    }
    return present;
#endif
}

bool backlight_for_keys()
{
#if !defined(APPLAUNCH_HW_PIZERO2W)
    return true;
#else
    return cp0_backlight_profile_kind() != CP0_BACKLIGHT_KIND_NONE;
#endif
}

} // namespace settings_caps
