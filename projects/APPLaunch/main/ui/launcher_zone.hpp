/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's side of the PDA writing zone (report 041, phase S3). The band itself (the key strip, the soft buttons,
 * the letters and digits boxes, the recogniser) lives in the display backend (cp0_display_zone_active(),
 * cp0_zone_ui.h); here are the parts that are the launcher's:
 *   - the soft buttons Home and Apps: the home grid (a page that is open closes, as Esc held does) and the multitasking
 *     switcher (opened, or closed when it is up; nothing when multitasking is off);
 *   - the input rule (launcher_osk): while the launcher is in front the zone is the text input (input.state vkb=zone,
 *     never the fallback keyboard); a fullscreen app in front (run turned, without the zone) gets the usual rule;
 *   - the screensaver draws the band black.
 * Nothing happens without the zone (every board but the PDA profile) or outside the Pi display backend. LVGL thread.
 */
#pragma once

#include <functional>

namespace launcher_zone {

/* The writing zone exists (APPLAUNCH_PDA=1 on a portrait panel, the dpi-scaled backend). */
bool active();

/* Once (Launch::bind_ui): what the Home soft button does (the launcher's home, a page closing as Esc held). */
void bind(std::function<void()> home);

/* A fullscreen app takes the panel (true) or gives it back (false): the on-screen keyboard rule follows. */
void set_app_front(bool front);
bool app_front();

/* The screensaver is up: the band is drawn black. */
void set_blank(bool on);

/* After an app drew over the whole panel without the display being suspended (the blocking fullscreen path). */
void repaint();

/* The key strip closes (a screen that does not leave its row free comes up: the multitasking switcher). */
void close_strip();

} // namespace launcher_zone
