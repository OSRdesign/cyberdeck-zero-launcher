/*
 * SPDX-License-Identifier: MIT
 *
 * Which Settings renderer runs (see settings_native_mode.hpp).
 */

#include "settings_native_mode.hpp"

#include "../native_ui.hpp"

#include <cstdlib>
#include <cstring>

#define SETTINGS_UI_STRINGIFY_VALUE(value) #value
#define SETTINGS_UI_STRINGIFY(value) SETTINGS_UI_STRINGIFY_VALUE(value)

namespace settings_ui {

bool native_selected()
{
    const char *value = std::getenv("APPLAUNCH_SETTINGS_UI");
    return value && std::strcmp(value, "native") == 0 && native_ui::enabled();
}

bool dev_pages()
{
    const char *dev = std::getenv("APPLAUNCH_DEV");
    if (dev && dev[0]) return std::strcmp(dev, "0") != 0;
#ifdef LAUNCHER_CHANNEL_RAW
    return std::strcmp(SETTINGS_UI_STRINGIFY(LAUNCHER_CHANNEL_RAW), "development") == 0;
#else
    return true; /* no channel: a developer's own build */
#endif
}

} // namespace settings_ui
