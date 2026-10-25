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
#include "cp0_zone_ui.h"

namespace launcher_zone {
namespace {

bool s_app_front = false;

void on_shown(int)
{
    launcher_osk::refresh(); /* vkb=zone only while the band shows */
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

bool writing()
{
    return active() && cp0_zone_ui_vis() == CP0_ZONE_VIS_WRITE;
}

void bind()
{
    if (!active()) return;
    cp0_zone_ui_set_vis_listener(on_shown);
    /* the band is the text input while the launcher is in front: it shows where the keyboard would */
    cp0::ui::osk::set_zone([] { return active() && !s_app_front; },
                           [](bool show, cp0::ui::TextType type, bool multiline, const void *field) {
                               if (show) /* a password field opens the band on the keyboard (report 042) */
                                   cp0_zone_ui_set_field(type == cp0::ui::TextType::Password ? CP0_ZONE_FIELD_PASSWORD
                                                         : type == cp0::ui::TextType::Number ? CP0_ZONE_FIELD_NUMBER
                                                                                             : CP0_ZONE_FIELD_TEXT,
                                                         multiline ? 1 : 0, field);
                               cp0_zone_ui_set_text_wanted(show ? 1 : 0);
                           });
}

void set_app_front(bool front)
{
    if (front == s_app_front) return;
    s_app_front = front;
    if (!active()) return;
    cp0::ui::osk::reevaluate();
    launcher_osk::refresh();
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
bool writing() { return false; }
void bind() {}
void set_app_front(bool) {}
bool app_front() { return false; }
void set_pinned(bool) {}
void set_blank(bool) {}
void repaint() {}
void screen_changed() {}

} // namespace launcher_zone

#endif
