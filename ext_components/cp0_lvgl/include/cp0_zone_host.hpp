/*
 * SPDX-License-Identifier: MIT
 *
 * The PDA writing zone's host side (report 044): when the band is this process's text input, for the launcher and for
 * an app built on cp0_lvgl alike (the launcher passes APPLAUNCH_PDA=1 to an app it runs upright, whose own display
 * backend then builds the band). The rule is the pure core's (cp0_zone.h, cp0_zone_host_*); this binds it to the
 * on-screen keyboard service (cp0::ui::osk::set_zone, set_close) and to the band (cp0_zone_ui.h: the strip's Keyb key,
 * its shown / hidden listener):
 *   - where a text field would get the on-screen keyboard (it asked, the host's keyboard policy allows it, its screen is
 *     the one shown), the band shows instead while band_input();
 *   - the strip's Keyb puts the standard keyboard in the band's place, for the following fields too, until that
 *     keyboard's close cross (there only while the user's choice is Handwriting + keyboard);
 *   - the host is told (Hooks::changed) whenever the band shows or hides and whenever Keyb or the cross switch, to
 *     publish its input rule again (the launcher: input.state vkb=).
 * Nothing happens without the band (cp0_display_zone_active()) or on the SDL simulator. LVGL thread.
 */
#pragma once

#include <functional>

namespace cp0 {
namespace ui {
namespace zone_host {

struct Hooks {
    /* The user chose Handwriting + keyboard: the launcher's Settings > On-screen keyboard; in an app, input.state hand=1
     * (cp0_app_input_t.hand). Required (NULL: never). */
    std::function<bool()> hand;
    /* The host lets the band be the text input now (the launcher: not while a fullscreen app is in front). NULL: always. */
    std::function<bool()> gate;
    /* The band showed or hid, or Keyb / the cross switched between the band and the keyboard. NULL: nothing. */
    std::function<void()> changed;
};

/* The band exists in this process (the dpi-scaled backend with APPLAUNCH_PDA=1 on a portrait canvas). */
bool active();
/* The band is shown now. */
bool shown();

/* Once, after the host installed its keyboard policy (cp0::ui::osk::set_policy): the band shows where the keyboard would
 * while band_input(); the keyboard gets its close cross while hooks.hand(). Without the band only the hooks are kept. */
void bind(Hooks hooks);
/* hooks.hand's answer may have changed: Keyb is forgotten without it, the cross and the band follow on the next cycle. */
void hand_changed();
/* hooks.gate's answer changed: the band or the keyboard follows on the next cycle, hooks.changed is called. */
void gate_changed();

/* The band is the text input now (cp0_zone_host_band_input). */
bool band_input();
/* The strip's Keyb chose the standard keyboard instead of the band (until its cross). */
bool keyboard_instead();

} // namespace zone_host
} // namespace ui
} // namespace cp0
