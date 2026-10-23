/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_lvgl_app_runner.hpp"
#include "sample_log.h"
#include "ui/input_presence.hpp"
#include "ui/launcher_media_controls.h"
#include "ui/settings/settings_session_model.hpp"
#include "ui/status_feed.hpp"
#include "ui/ui.h"
#include "ui/ui_screensaver.h"

#include <cstdlib>
#include <string>
#include <utility>

#if CONFIG_BACKWARD_CPP_ENABLED
#define BACKWARD_HAS_DW 1
#include "backward.hpp"
#include "backward.h"
#endif

int main(void)
{
    Cp0LvglRunOptions options;
    options.after_resource_init = []() {
        // Once per launcher start (never on a wake from the screensaver or a return from an app): the saved
        // brightness, or full when nothing is saved and the board came up below half (systemd-backlight brings
        // back the level the last session ended with). No backlight, or an on/off one: left as the board set it.
        const int restored = launcher_media_controls::restore_startup_backlight(cp0_backlight_profile_kind());
        if (restored > 0)
            SLOGI("[BOOT] restored working backlight brightness");
        else if (restored < 0)
            SLOGE("[BOOT] failed to restore working backlight brightness");
    };
    options.setup = []() {
        SLOGI("[BOOT] cp0_lvgl initialized");
        launcher_ui::init();
        ui_screensaver_init();
        // The Wi-Fi, Bluetooth and battery values the home grid shows, published for the status strip of apps.
        status_feed::start();
        return true;
    };
    options.teardown = []() {
        status_feed::stop();
        launcher_ui::deinit();
    };
    // Settings > System > Quit to desktop (desktop boards) ends the main loop: teardown, then exit status 0, and
    // applaunch-desktop.service gives the screen back to the desktop (settings_session_model.hpp).
    launcher_session::set_wake(&cp0_lvgl_wake);
    options.should_quit = []() { return launcher_session::quit_requested(); };
    // Every keyboard (USB, Bluetooth) types in the launcher; its presence is published for apps. Before
    // cp0_lvgl_run(): the keyboard thread starts inside it.
    input_presence::configure();
    input_presence::start();
    const int rc = cp0_lvgl_run(std::move(options));
    input_presence::stop();
    return rc;
}
