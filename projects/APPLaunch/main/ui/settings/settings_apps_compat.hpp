/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps: which catalogue apps fit this board's screen (pure: no LVGL, no I/O, no thread;
 * tests/test_settings_apps_compat.cpp).
 *
 * A registry entry may carry `min_size` ("WxH", the app's X-MinSize: the smallest logical LANDSCAPE screen) and
 * `portrait_min_size` ("WxH", its X-Portrait). Only min_size filters, against the landscape canvas the launcher gives
 * an app (owner decision: orientation hides nothing; a landscape-only app runs turned on a portrait board, as
 * desktop_fit() judges an installed one): the screen as it is, or turned (h x w) on a portrait screen (h > w), so a
 * 480x800 board is judged as 800x480. portrait_min_size is information only (the app page's Screen row).
 * No min_size, an unparsable value (same syntax as X-MinSize: "WxH", each 1..65535) or an unknown screen (w or h <= 0):
 * compatible.
 */
#pragma once

#include <string>

namespace settings_apps {

/* Whether an app with these registry fields can run on a logical screen of screen_w x screen_h px. */
bool app_fits_screen(const std::string &min_size, int screen_w, int screen_h);

/* A "WxH" value the launcher can read (empty is "absent", not malformed). */
bool size_value_valid(const std::string &value);

/* The app page's two read-only rows (Screen, Minimum size), from the registry / .desktop fields; both empty when the app
 * states nothing readable. Screen: "Landscape" (min_size), "Landscape and portrait" (portrait_min_size too);
 * Minimum size: min_size as "WxH" (the landscape minimum, also for a portrait-capable app). */
struct ScreenFacts {
    std::string screen;
    std::string minimum;
};
ScreenFacts screen_facts(const std::string &min_size, const std::string &portrait_min_size);

} // namespace settings_apps
