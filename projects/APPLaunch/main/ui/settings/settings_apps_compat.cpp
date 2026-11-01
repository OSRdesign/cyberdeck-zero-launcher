/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps: which catalogue apps fit this board's screen (see settings_apps_compat.hpp).
 */
#include "settings_apps_compat.hpp"

#include "../desktop_entry.h"

namespace settings_apps {

bool size_value_valid(const std::string &value)
{
    int w = 0;
    int h = 0;
    return parse_desktop_min_size(value, w, h);
}

bool app_fits_screen(const std::string &min_size, int screen_w, int screen_h)
{
    if (screen_w <= 0 || screen_h <= 0) return true;
    int min_w = 0;
    int min_h = 0;
    if (min_size.empty() || !parse_desktop_min_size(min_size, min_w, min_h)) return true;
    /* a portrait screen runs the app turned: X-MinSize against the landscape canvas (h x w), as desktop_fit() */
    if (desktop_portrait_screen(screen_w, screen_h)) return desktop_min_size_fits(min_w, min_h, true, screen_h, screen_w);
    return desktop_min_size_fits(min_w, min_h, true, screen_w, screen_h);
}

ScreenFacts screen_facts(const std::string &min_size, const std::string &portrait_min_size)
{
    ScreenFacts facts;
    int w = 0;
    int h = 0;
    const bool has_min = !min_size.empty() && parse_desktop_min_size(min_size, w, h);
    const bool has_portrait = !portrait_min_size.empty() && size_value_valid(portrait_min_size);
    if (has_min) facts.minimum = std::to_string(w) + "x" + std::to_string(h);
    if (has_min || has_portrait) facts.screen = has_portrait ? "Landscape and portrait" : "Landscape";
    return facts;
}

} // namespace settings_apps
