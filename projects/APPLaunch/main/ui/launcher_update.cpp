/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates outside Settings: the start notice and the daily background check (see launcher_update.hpp).
 */

#include "launcher_update.hpp"

#include "launcher_toast.h"
#include "native_ui.hpp"
#include "settings/settings_session_model.hpp"
#include "settings/settings_update_check.hpp"
#include "settings/settings_update_download.hpp"
#include "settings/settings_update_install_model.hpp"
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
std::string s_start_notice;        /* "Updated to ..." / "Update to ... failed": shown once the home grid is on screen */
bool s_restart_asked = false;      /* P2: the update helper asked this launcher to restart (exit status 76) */
std::string s_version;             /* the running launcher's version */

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

/* The root update helper's status (P2): read by every launcher. */
HelperPhase helper(HelperStatus &status)
{
    status = parse_helper_status(platform::helper_status());
    const bool alive = status.present && status.boot == platform::boot_id() && platform::process_alive(status.pid);
    return helper_phase(status, alive);
}

/* P2, every 2 s: on a desktop board the helper asks the running launcher to exit with status 76 once the new files are
 * in place (restart=launcher, this version, this pid); its unit starts the new launcher without giving the desktop
 * back. A rollback that happened while this launcher kept running is announced like the start notice. */
void watch_helper()
{
    HelperStatus status;
    const HelperPhase phase = helper(status);
    if (phase == HelperPhase::None) return;
    const std::string &version = s_version;
    if (!s_restart_asked && restart_requested(status, phase == HelperPhase::Running, version, platform::self_pid())) {
        s_restart_asked = true;
        SLOGI("[UPDATE] the update helper installed %s: restarting the launcher (exit status 76)", status.to.c_str());
        launcher_session::request_restart();
        return;
    }
    if (phase == HelperPhase::RolledBack) {
        std::string text;
        Checker::instance().update_state([&](State &state) {
            text = take_rollback_notice(state, status, phase, version);
        });
        if (!text.empty()) {
            SLOGI("[UPDATE] %s", text.c_str());
            s_start_notice = text;
        }
    }
    if (!s_start_notice.empty() && native_ui::home_in_front()) {
        toast(s_start_notice);
        s_start_notice.clear();
    }
}

/* Every 2 s while our check runs or its toast waits: it is shown soon after the check ends, not 10 min later. */
void poll(lv_timer_t *)
{
    if (s_waiting || s_toast_pending) settle();
    watch_helper();
}

} // namespace

void install()
{
    if (s_installed) return;
    s_installed = true;
    Checker &checker = Checker::instance();
    const Snapshot snap = checker.snapshot();
    s_version = snap.build.version;
    std::string text;
    checker.update_state([&](State &state) { text = take_start_notice(state, snap.state_existed, snap.build.version); });
    // P2: the root helper's last run: a rollback to this version is announced once; an update done to this version
    // needs its downloaded bundle no more
    HelperStatus status;
    const HelperPhase phase = helper(status);
    if (phase == HelperPhase::RolledBack) {
        std::string rollback;
        checker.update_state([&](State &state) { rollback = take_rollback_notice(state, status, phase, snap.build.version); });
        if (!rollback.empty()) text = rollback;
    }
    if (phase == HelperPhase::Done && status.to == snap.build.version) Downloader::instance().discard_files();
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
