/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's side of the PDA writing zone (report 041, phase S3; report 042: on demand, no soft buttons). The band
 * itself (the key strip, the letters and digits boxes, the recogniser) lives in the display backend
 * (cp0_display_zone_active(), cp0_zone_ui.h); here are the parts that are the launcher's:
 *   - the input rule (launcher_osk): with Settings > On-screen keyboard on Handwriting + keyboard (the PDA profile's
 *     default) and the launcher in front, the band is the text input instead of the fallback keyboard: it shows where
 *     the keyboard would (a text field asks and the keyboard's policy allows it), input.state says vkb=zone while it
 *     shows; the strip's Keyb key puts the standard keyboard in its place (for the following fields too, vkb= the
 *     usual rule) until the keyboard's close cross brings the band back. Auto and On never show the band (the
 *     standard keyboard as on every other board); a fullscreen app in front (run turned, without the band) gets the
 *     usual rule;
 *   - a page may keep the band shown (Strokes training: set_pinned);
 *   - the screensaver draws the band black.
 * Nothing happens without the zone (every board but the PDA profile) or outside the Pi display backend. LVGL thread.
 * The rule itself (when the band is the text input, Keyb and the cross) is cp0_lvgl's (cp0_zone_host.hpp, report 044),
 * shared with the apps the launcher runs upright with their own band; this binds it to the launcher's setting.
 */
#pragma once

namespace launcher_zone {

/* The writing zone exists (APPLAUNCH_PDA=1 on a portrait panel, the dpi-scaled backend). */
bool active();
/* The band is shown now. */
bool shown();

/* Once (Launch::bind_ui): the band shows instead of the fallback keyboard (cp0::ui::osk::set_zone) and the input rule
 * is published again whenever it is shown or hidden. */
void bind();

/* Settings > On-screen keyboard changed (launcher_osk::set_mode): the band, the Keyb choice and the cross follow. */
void mode_changed();
/* The strip's Keyb chose the keyboard instead of the band (until its cross). */
bool keyboard_instead();

/* A fullscreen app takes the panel (true) or gives it back (false): the on-screen keyboard rule follows. */
void set_app_front(bool front);
bool app_front();

/* A page keeps the band shown while it is up (Strokes training). */
void set_pinned(bool on);

/* The screensaver is up: the band is drawn black. */
void set_blank(bool on);

/* After an app drew over the whole panel without the display being suspended (the blocking fullscreen path). */
void repaint();

/* A screen came up over the page whose field wants input (the multitasking switcher): the band follows on the next
 * cycle (hidden while that screen is in front). */
void screen_changed();

} // namespace launcher_zone
