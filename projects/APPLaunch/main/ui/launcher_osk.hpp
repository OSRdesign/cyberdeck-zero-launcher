/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's side of the fallback on-screen keyboard (task 014 P2b, F1 rules of task 012): the policy the shell
 * widget (cp0::ui::osk, cp0_ui_widgets.hpp) asks before it shows, and the Settings choice.
 *
 * Settings > System > On-screen keyboard (config key "osk_mode", report 042):
 *   Auto (0, the default except on the PDA profile)  the keyboard shows for a focused native text field (TextField) only
 *       when no physical keyboard is present (the presence watcher, input_presence.hpp; a Bluetooth keyboard that went
 *       away counts as present for 10 minutes: it sleeps; any keyboard for a 2 s grace). On the PDA profile too: the
 *       standard keyboard, no writing zone.
 *   On (2)  the standard keyboard for every focused text field, a physical keyboard present or not (as
 *       APPLAUNCH_OSK=force); never the writing zone.
 *   Handwriting + keyboard (3, the PDA profile only, its default)  the writing zone's band first (launcher_zone.hpp)
 *       by the Auto rule; its strip's Keyb key puts the standard keyboard in its place for every field from then on,
 *       until the keyboard's close cross brings the band back.
 *   1 was Off until report 042: it reads as Auto (and is written again only when the user picks a value), as does 3
 *   on a profile without the writing zone; any other stored value reads as the profile's default.
 * Always: never under the lock screen nor while a desktop session owns the screen, with APPLAUNCH_OSK=force|auto|off
 * in the environment overriding for development (force: shown even with a physical keyboard, e.g. the HackBerry's
 * built-in one, and in the render harness). The keyboard is never summoned by the user (the strip's Keyb aside).
 * input.state says vkb=zone while the band shows as the text input (the launcher in front), else the rule in force.
 * Compat apps and compat pages keep today's behaviour (no keyboard): this covers native pages only.
 *
 * LVGL thread only.
 */
#pragma once

namespace launcher_osk {

constexpr int kAuto = 0;
constexpr int kOn = 2;
constexpr int kHand = 3; /* Handwriting + keyboard: the PDA profile only (launcher_zone::active()) */

/* Settings > On-screen keyboard: kAuto, kOn or kHand (read once from the config, then cached). */
int mode();
/* The values offered on this profile, in the Settings list's order (Auto, On, and Handwriting + keyboard on the PDA
 * profile); returns how many (at most 3). */
int choices(int out[3]);
/* Persist the choice (SetInt + Save). False when it is not offered here or could not be saved (the cached value
 * stays). */
bool set_mode(int mode);

/* Give the shell keyboard its policy (once; later calls do nothing). */
void install();

/* Page change, app start: the keyboard goes at once. */
void dismiss();

/* The inputs of vkb= changed (the PDA writing zone: an app came in front or left, the band was shown or hidden,
 * launcher_zone.hpp): publish again. */
void refresh();

} // namespace launcher_osk
