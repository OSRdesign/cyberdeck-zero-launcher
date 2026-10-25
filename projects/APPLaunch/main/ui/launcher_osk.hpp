/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's side of the fallback on-screen keyboard (task 014 P2b, F1 rules of task 012): the policy the shell
 * widget (cp0::ui::osk, cp0_ui_widgets.hpp) asks before it shows, and the Settings choice.
 *
 * The keyboard shows for a focused native text field (TextField) only when
 *   - no physical keyboard is present (the presence watcher, input_presence.hpp; a Bluetooth keyboard that went away
 *     counts as present for 10 minutes: it sleeps; any keyboard for a 2 s grace),
 *   - Settings > System > On-screen keyboard is Auto (the default; config key "osk_mode": 0 Auto, 1 Off),
 *   - the lock screen is not up and the launcher owns the screen (no desktop session),
 * with APPLAUNCH_OSK=force|auto|off in the environment overriding for development (force: shown even with a physical
 * keyboard, e.g. the HackBerry's built-in one, and in the render harness). It is never summoned by the user.
 * The PDA profile's writing zone (report 041, 042, launcher_zone.hpp): while it is the text input (the launcher in
 * front) its band shows instead of the keyboard, by these same rules, and input.state says vkb=zone while it shows.
 * Compat apps and compat pages keep today's behaviour (no keyboard): this covers native pages only.
 *
 * LVGL thread only.
 */
#pragma once

namespace launcher_osk {

constexpr int kAuto = 0;
constexpr int kOff = 1;

/* Settings > On-screen keyboard: kAuto or kOff (read once from the config, then cached). */
int mode();
/* Persist the choice (SetInt + Save). False when it could not be saved (the cached value stays). */
bool set_mode(int mode);

/* Give the shell keyboard its policy (once; later calls do nothing). */
void install();

/* Page change, app start: the keyboard goes at once. */
void dismiss();

/* The inputs of vkb= changed (the PDA writing zone: an app came in front or left, launcher_zone.hpp): publish again. */
void refresh();

} // namespace launcher_osk
