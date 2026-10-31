/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates outside Settings: the start notice and the daily background check (see launcher_update.hpp).
 */

#include "launcher_update.hpp"

#include "launcher_toast.h"
#include "native_ui.hpp"
#include "settings/settings_update_check.hpp"
#include "settings/settings_update_platform.hpp"

#include "lvgl/lvgl.h"
#include "sample_log.h"

#include <cstdint>
#include <string>

namespace launcher_update {

namespace {

constexpr uint32_t kFirstTickMs = 2 * 60 * 1000; /* not in the busy first minutes of a boot */
constexpr uint32_t kTickMs = 10 * 60 * 1000;
constexpr uint32_t kToastMs = 6000;              /* two lines to read */

bool s_installed = false;
bool s_waiting = false;            /* a background check of ours is on its way */
std::uint64_t s_started_done = 0;  /* Snapshot::done when it started */
bool s_toast_pending = false;      /* it found a release: the toast waits for the home grid */
std::string s_start_notice;        /* "Updated to ...": shown once the home grid is on screen */

void toast(const std::string &text)
{
    if (!text.empty()) launcher_toast().show_for(text.c_str(), kToastMs);
}

/* Our check ended: a release found waits for the home grid to be announced (once per version). True while a toast
 * still waits. */
bool settle()
{
    Checker &checker = Checker::instance();
    if (s_waiting) {
        const Snapshot snap = checker.snapshot();
        if (snap.running || snap.done <= s_started_done) return false;
        s_waiting = false;
        s_toast_pending = snap.outcome == Outcome::Ok;
    }
    if (!s_toast_pending) return false;
    if (!native_ui::home_in_front()) return true;
    s_toast_pending = false;
    std::string text;
    checker.update_state([&text](State &state) { text = take_available_toast(state); });
    toast(text);
    return false;
}

/* Every 10 min: start the daily check when it is due and the board is ready for it. */
void tick(lv_timer_t *timer)
{
    lv_timer_set_period(timer, kTickMs);
    if (settle() || s_waiting) return;
    Checker &checker = Checker::instance();
    const Snapshot snap = checker.snapshot();
    if (snap.running || !background_due(snap.state, platform::now())) return;
    if (!native_ui::home_in_front() || !platform::network_up()) return; /* the clock: the check's own rule */
    if (checker.start()) {
        s_waiting = true;
        s_started_done = snap.done;
        SLOGI("[UPDATE] daily check started");
    }
}

/* Every 2 s while our check runs or its toast waits: it is shown soon after the check ends, not 10 min later. */
void poll(lv_timer_t *)
{
    if (s_waiting || s_toast_pending) settle();
}

} // namespace

void install()
{
    if (s_installed) return;
    s_installed = true;
    Checker &checker = Checker::instance();
    const Snapshot snap = checker.snapshot();
    std::string text;
    checker.update_state([&](State &state) { text = take_start_notice(state, snap.state_existed, snap.build.version); });
    if (!text.empty()) SLOGI("[UPDATE] %s", text.c_str());
    s_start_notice = text;
    lv_timer_create(tick, kFirstTickMs, nullptr);
    lv_timer_create(poll, 2000, nullptr);
}

void show_start_notice()
{
    if (s_start_notice.empty()) return;
    toast(s_start_notice);
    s_start_notice.clear();
}

} // namespace launcher_update
