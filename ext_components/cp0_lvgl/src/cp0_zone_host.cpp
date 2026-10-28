/*
 * SPDX-License-Identifier: MIT
 *
 * The PDA writing zone's host side (see cp0_zone_host.hpp). Moved from the launcher's launcher_zone.cpp (report 044) so
 * that an app built on cp0_lvgl decides like the launcher.
 */
#include "cp0_zone_host.hpp"

#include <utility>

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "cp0_display.h"
#include "cp0_ui_widgets.hpp"
#include "cp0_zone.h"
#include "cp0_zone_ui.h"

namespace cp0 {
namespace ui {
namespace zone_host {
namespace {

Hooks s_hooks;
cp0_zone_host_t s_state = {0};

bool hand()
{
    return s_hooks.hand && s_hooks.hand();
}

bool gate()
{
    return !s_hooks.gate || s_hooks.gate();
}

void changed()
{
    if (s_hooks.changed) s_hooks.changed();
}

/* the band or the keyboard follows on the next cycle, the host publishes its input rule again */
void follow()
{
    osk::reevaluate();
    changed();
}

void on_shown(int)
{
    changed(); /* the launcher: vkb=zone only while the band shows */
}

/* the strip's Keyb: the standard keyboard for this field and the next ones */
void on_keyb()
{
    if (cp0_zone_host_keyb(&s_state, hand() ? 1 : 0)) follow();
}

/* the keyboard's cross: the band again */
void on_close()
{
    if (cp0_zone_host_close(&s_state)) follow();
}

/* the keyboard has its cross only where the band is the first input (Handwriting + keyboard) */
void apply_close()
{
    if (hand()) osk::set_close(on_close);
    else osk::set_close(nullptr);
}

} // namespace

bool active()
{
    return cp0_display_available() && cp0_display_zone_active();
}

bool shown()
{
    return active() && cp0_zone_ui_shown() != 0;
}

void bind(Hooks hooks)
{
    s_hooks = std::move(hooks);
    if (!active()) return;
    cp0_zone_ui_set_vis_listener(on_shown);
    cp0_zone_ui_set_keyb_cb(on_keyb);
    /* the band is the text input (band_input): it shows where the keyboard would */
    osk::set_zone([] { return band_input(); }, [](bool show) { cp0_zone_ui_set_text_wanted(show ? 1 : 0); });
    apply_close();
}

void hand_changed()
{
    if (!active()) return;
    (void)cp0_zone_host_hand_changed(&s_state, hand() ? 1 : 0);
    apply_close();
    follow();
}

void gate_changed()
{
    if (!active()) return;
    follow();
}

bool band_input()
{
    return cp0_zone_host_band_input(&s_state, active() ? 1 : 0, gate() ? 1 : 0, hand() ? 1 : 0) != 0;
}

bool keyboard_instead()
{
    return cp0_zone_host_keyboard_instead(&s_state, active() ? 1 : 0, hand() ? 1 : 0) != 0;
}

} // namespace zone_host
} // namespace ui
} // namespace cp0

#else // SDL simulator and non-Linux builds: no writing zone

namespace cp0 {
namespace ui {
namespace zone_host {

bool active() { return false; }
bool shown() { return false; }
void bind(Hooks) {}
void hand_changed() {}
void gate_changed() {}
bool band_input() { return false; }
bool keyboard_instead() { return false; }

} // namespace zone_host
} // namespace ui
} // namespace cp0

#endif
