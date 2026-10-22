/*
 * SPDX-License-Identifier: MIT
 *
 * The board's capabilities as the launcher uses them (report 030): Settings' rows (settings_capabilities_model.hpp) and
 * the volume and brightness keys (decision C11: they do nothing, and show nothing, without the hardware). LVGL thread
 * only.
 */
#pragma once

#include "cp0_capabilities.h"

namespace settings_caps {

/* The capabilities as Settings opens: battery, backlight and the local sound output, a sound server's sink only when
 * there is no local output (it may take up to CP0_CAPS_SERVER_TIMEOUT_MS). */
cp0_caps_t probe_for_settings();

/* The volume keys: the board has a sound output (a local card or a server's sink, a Bluetooth speaker that connects
 * included). Looked up again at most every 10 s. */
bool sound_for_keys();

/* The brightness keys: the board has a backlight to drive (cp0_backlight_profile: not "none"). */
bool backlight_for_keys();

/* The default (simulator) profile keeps the keys as they always were: both answer true there. The launcher's interface
 * sounds (decision C12) are the build profile's APPLAUNCH_SYSTEM_SOUNDS (settings_hw_profile.hpp; off on our boards). */

} // namespace settings_caps
