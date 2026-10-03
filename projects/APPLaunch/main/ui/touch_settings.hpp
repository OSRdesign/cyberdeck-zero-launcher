/*
 * Per-app touch gesture choice for the Raspberry Pi Zero 2W port (Settings > Touch).
 *
 * 0 = Off            touch does nothing inside the app (the toolbar still works)
 * 1 = Swipe          a swipe is the arrow key for its direction, a tap is Enter
 * 2 = Swipe + Fire   same, but a tap is Space (games where Space fires)
 *
 * Stored in the launcher config as "touch_<app name, lower-case alphanumerics>". Apps without a stored
 * value use a default (see default_choice()).
 */
#pragma once

#include <string>

namespace touch_settings {

constexpr int kOff = 0;
constexpr int kSwipe = 1;
constexpr int kSwipeFire = 2;

/* Built-in defaults: the games that were validated with swipe control. */
int default_choice(const std::string &app_name);

/* The stored choice for the app, or its default. */
int choice(const std::string &app_name);

/* Persist the choice. Returns false when the configuration could not be saved. */
bool set_choice(const std::string &app_name, int value);

} // namespace touch_settings
