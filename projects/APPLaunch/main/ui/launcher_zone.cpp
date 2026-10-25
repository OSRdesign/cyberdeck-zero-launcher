/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's side of the PDA writing zone (see launcher_zone.hpp).
 */

#include "launcher_zone.hpp"

#include "launcher_osk.hpp"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "cp0_display.h"
#include "cp0_zone_ui.h"
#include "multitask/multitask_ui.hpp"
#include "sample_log.h"
#include "ui_screensaver.h"

#include <utility>

namespace launcher_zone {
namespace {

std::function<void()> s_home;
bool s_app_front = false;

void on_action(cp0_zone_action_t action)
{
    if (ui_screensaver_is_active()) return; /* the screensaver is up: its touch only wakes it */
    switch (action) {
    case CP0_ZONE_ACTION_HOME:
        SLOGI("[zone] Home");
        if (multitask_ui::switcher_home()) return; /* the switcher's own Home */
        if (s_home) s_home();
        break;
    case CP0_ZONE_ACTION_APPS:
        if (!multitask_ui::toggle_switcher("Apps")) SLOGI("[zone] Apps: multitasking is off");
        break;
    }
}

} // namespace

bool active()
{
    return cp0_display_available() && cp0_display_zone_active();
}

void bind(std::function<void()> home)
{
    s_home = std::move(home);
    if (active()) cp0_zone_ui_set_action_cb(on_action);
}

void set_app_front(bool front)
{
    if (front == s_app_front) return;
    s_app_front = front;
    if (active()) launcher_osk::refresh();
}

bool app_front()
{
    return s_app_front;
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

} // namespace launcher_zone

#else // SDL simulator and non-Linux builds: no writing zone

namespace launcher_zone {

bool active() { return false; }
void bind(std::function<void()>) {}
void set_app_front(bool) {}
bool app_front() { return false; }
void set_blank(bool) {}
void repaint() {}

} // namespace launcher_zone

#endif
