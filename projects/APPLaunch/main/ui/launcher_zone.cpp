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
bool s_keyb = false; /* the strip's Keyb put the on-screen keyboard in the band's place, until the keyboard's cross */

bool hand_mode()
{
    return launcher_osk::mode() == launcher_osk::kHand;
}

/* The band is the text input: Settings > On-screen keyboard is Handwriting + keyboard (the only value with the band,
 * report 042), the launcher is in front and the keyboard was not chosen instead (Keyb). */
bool band_input()
{
    return active() && !s_app_front && hand_mode() && !s_keyb;
}

/* the band or the keyboard follows on the next cycle, the input rule is published again */
void follow()
{
    cp0::ui::osk::reevaluate();
    launcher_osk::refresh();
}

void on_shown(int)
{
    launcher_osk::refresh(); /* vkb=zone only while the band shows */
}

/* the strip's Keyb: the standard keyboard for this field and the next ones */
void on_keyb()
{
    if (!hand_mode() || s_keyb) return;
    s_keyb = true;
    follow();
}

/* the keyboard's cross: the band again */
void on_close()
{
    if (!s_keyb) return;
    s_keyb = false;
    follow();
}

/* the keyboard has its cross only where the band is the first input (Handwriting + keyboard) */
void apply_close()
{
    if (hand_mode()) cp0::ui::osk::set_close(on_close);
    else cp0::ui::osk::set_close(nullptr);
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

void bind()
{
    if (!active()) return;
    cp0_zone_ui_set_vis_listener(on_shown);
    cp0_zone_ui_set_keyb_cb(on_keyb);
    /* the band is the text input (band_input): it shows where the keyboard would */
    cp0::ui::osk::set_zone([] { return band_input(); }, [](bool show) { cp0_zone_ui_set_text_wanted(show ? 1 : 0); });
    apply_close();
}

void mode_changed()
{
    if (!active()) return;
    if (!hand_mode()) s_keyb = false;
    apply_close();
    follow();
}

bool keyboard_instead()
{
    return active() && hand_mode() && s_keyb;
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
