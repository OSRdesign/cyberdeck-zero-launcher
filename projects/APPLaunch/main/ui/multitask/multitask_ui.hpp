/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b in the launcher (task 015): the flag, the supervisor and its launcher ports (cp0_lvgl framebuffer,
 * input grabs, the launcher's keyboard thread, the evdev keyboards), the bridge timer that carries the supervisor's
 * UI events onto the LVGL thread, and the key filter for Alt+Tab while the launcher is in front. P-d: the switcher
 * screen and the legacy sheet (switcher_view.hpp) built and deleted on those events, the home grid's dots, the
 * first-run hint (canvases of 640x480 and more).
 *
 * On by default on canvases of 640x480 and more (the Pi display backend, Linux); APPLAUNCH_MULTITASK=0 (or
 * false/off/no) turns it off, =1 (true/on/yes) forces it on also below 640x480 (multitask::multitask_on()). When it
 * is off nothing here runs and Launch keeps its blocking fullscreen path unchanged.
 */
#pragma once

#include "lvgl/lvgl.h"
#include "multitask_model.hpp"

#include <functional>
#include <string>

namespace multitask_ui {

struct Hooks {
    /* LVGL thread: an app takes the panel (display_handoff::enter_app_front and the launcher's own state) */
    std::function<void()> app_front;
    /* LVGL thread: the launcher takes the panel back; after_exit = the app in front exited (republish screen.state) */
    std::function<void(bool after_exit)> launcher_front;
    /* LVGL thread (P-d): the home grid, when the switcher or the sheet closes and no other launcher screen was open */
    std::function<void()> show_home;
};

/* Linux, the dpi-scaled display backend (native UI) and multitask::multitask_on(APPLAUNCH_MULTITASK, the logical
 * canvas). Read once. */
bool enabled();
/* LVGL thread, once (Launch::bind_ui): the supervisor thread, the bridge timer, the key filter. */
bool start(Hooks hooks);
/* start() succeeded: full-screen apps go through the supervisor. */
bool active();
/* Every resident app is closed (SIGCONT, SIGTERM, SIGKILL 3 s later), the thread ends, the filter is restored. */
void stop();
/* The timer display_handoff::enter_app_front() must keep running. */
lv_timer_t *bridge_timer();
/* Is the app with this Exec line resident (a tap resumes it)? Any thread. */
bool is_resident(const std::string &key);
/* P-d, LVGL thread: the switcher screen or the legacy sheet is the launcher's screen now (the launcher must not show
 * its grid over it when it takes the panel back); the sheet is (the lock screen stays off: only its box is drawn). */
bool screen_up();
bool sheet_up();
/* Start, or resume, a fullscreen app. `key` is its .desktop Exec line (one instance per key), `command` the sh -c
 * line with the app contract's exports in front. Returns at once; the hooks run when the panel changes hands. */
void launch(const std::string &name, const std::string &key, const std::string &command, bool keep_root,
            multitask::Background background);

} // namespace multitask_ui
