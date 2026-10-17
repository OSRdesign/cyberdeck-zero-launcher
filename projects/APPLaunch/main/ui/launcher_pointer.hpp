/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's side of pointer navigation (cp0_pointer_nav.h): the Settings choice and where it goes.
 *
 *   Settings > System > Pointer navigation: Off (the default) / On; config key "pointer_nav" (0 Off, 1 On) through the
 *   config service, like the on-screen keyboard's "osk_mode" (launcher_osk.hpp).
 *
 * The value goes to the launcher's own keyboard backend (cp0_pointer_nav_set_enabled: its pointer reader opens or
 * closes the pointer devices) and to the apps (input.state pointer=on|off through input_presence::set_pointer_nav,
 * gen + 1), so every app that links cp0_lvgl and reads every keyboard follows it. APPLAUNCH_POINTER_NAV=1|0 in the
 * environment overrides the setting for testing (it is read by the reader of each process, the launcher's and the
 * apps', which inherit it).
 *
 * LVGL thread only.
 */
#pragma once

namespace launcher_pointer {

constexpr int kOff = 0;
constexpr int kOn = 1;

/* Settings > Pointer navigation: kOff or kOn (read once from the config, then cached). */
int mode();
/* Persist the choice (SetInt + Save) and apply it. False when it could not be saved (the cached value stays). */
bool set_mode(int mode);

/* Apply the stored choice once (later calls do nothing). */
void install();

} // namespace launcher_pointer
