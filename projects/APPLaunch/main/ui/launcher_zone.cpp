/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's side of the PDA writing zone (see launcher_zone.hpp).
 */

#include "launcher_zone.hpp"

#include "launcher_osk.hpp"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "cp0_display.h"
#include "cp0_ui_widgets.hpp"
#include "cp0_zone_host.hpp"
#include "cp0_zone_ui.h"

namespace launcher_zone {
namespace {

bool s_app_front = false;

} // namespace

bool active()
{
    return cp0::ui::zone_host::active();
}

bool shown()
{
    return cp0::ui::zone_host::shown();
}

void bind()
{
    /* The band is the text input (report 042; the rule shared with the apps since report 044, cp0_zone_host.hpp):
     * Settings > On-screen keyboard on Handwriting + keyboard, the launcher in front, Keyb not chosen. The input rule is
     * published again whenever the band shows or hides (vkb=zone only while it shows). */
    cp0::ui::zone_host::bind({[] { return launcher_osk::mode() == launcher_osk::kHand; },
                              [] { return !s_app_front; },
                              [] { launcher_osk::refresh(); }});
}

void mode_changed()
{
    cp0::ui::zone_host::hand_changed();
}

bool keyboard_instead()
{
    return cp0::ui::zone_host::keyboard_instead();
}

void set_app_front(bool front)
{
    if (front == s_app_front) return;
    s_app_front = front;
    cp0::ui::zone_host::gate_changed();
}

bool app_front()
{
    return s_app_front;
}

void set_pinned(bool on)
{
    if (active()) cp0_zone_ui_set_pinned(on ? 1 : 0);
}

void set_blank(bool on)
{
    if (active()) cp0_zone_ui_set_blank(on ? 1 : 0);
}

void repaint()
{
    if (!active()) return;
    cp0_display_zone_invalidate();
    cp0_zone_ui_reset_input(); /* a stroke or a held strip key from before the app is not finished now */
}

void screen_changed()
{
    if (active()) cp0::ui::osk::reevaluate();
}

} // namespace launcher_zone

#else // SDL simulator and non-Linux builds: no writing zone

namespace launcher_zone {

bool active() { return false; }
bool shown() { return false; }
void bind() {}
void mode_changed() {}
bool keyboard_instead() { return false; }
void set_app_front(bool) {}
bool app_front() { return false; }
void set_pinned(bool) {}
void set_blank(bool) {}
void repaint() {}
void screen_changed() {}

} // namespace launcher_zone

#endif
