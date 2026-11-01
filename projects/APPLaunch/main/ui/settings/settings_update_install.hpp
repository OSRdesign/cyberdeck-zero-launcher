/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates P2 (report 047 sections 5.5, 5.6): the one root step of the launcher. After the confirmation the
 * shared sudo prompt (cp0_sudo_*: the handwriting band in PDA mode, the fallback keyboard elsewhere) runs ONE short
 * command:
 *     /usr/bin/systemd-run --unit=cyberdeckzero-update --collect --quiet --description=... \
 *         /usr/share/APPLaunch/bin/cyberdeckzero-update apply --version V --from-version F --sha256 H --file PATH --user U
 * systemd-run returns as soon as the transient system unit runs: the helper works outside the launcher's cgroup, so the
 * launcher's own restart (which the helper causes), a crash or the hold-Esc watchdog never cuts it half way. Its argv is
 * built and checked by apply_argv() (settings_update_install_model.hpp); the helper checks every value again.
 * "Repair" after an interruption runs `... cyberdeckzero-update resume --user U` the same way.
 *
 * LVGL thread only (the sudo service calls back on it). The progress of the root work is the helper's status file
 * (platform::helper_status()), which every launcher reads: the page shows it on both sides of the restart.
 */
#pragma once

#include "settings_apt_model.hpp"
#include "settings_update_install_model.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace launcher_update {

struct RootSnapshot {
    bool asking = false;       /* the password prompt is up, or systemd-run runs */
    std::string error;         /* the last attempt's failure in words ("Wrong password", ...), "" none */
    std::uint64_t started = 0; /* increases each time the helper's unit was started */
    std::uint64_t apt_started = 0; /* the same for a package-list refresh (P3: systemd-run ... apt-refresh) */
    std::uint64_t upgrade_started = 0; /* the same for a package upgrade or repair (P4: ... apt-upgrade / apt-repair) */
};

/* A root step is asking for its password or starting its unit (any kind): Settings > Apps waits (report 047 P4, the
 * shared busy flag). Any thread. */
bool root_job_asking();

class RootJob {
public:
    static RootJob &instance();

    RootSnapshot snapshot() const { return snap_; }
    /* The sudo prompt, then systemd-run ... apply. False (error set) when argv is not acceptable or the prompt cannot
     * open, or while one is asked already. */
    bool apply(const ApplyRequest &request);
    /* The same for ... resume --user USER. */
    bool resume(const std::string &user);
    /* P3: the same prompt, then systemd-run ... cyberdeckzero-update apt-refresh --user USER (apt-get update in a
     * transient unit: apt_started counts the starts). */
    bool refresh_lists(const std::string &user);
    /* P4: the same prompt, then systemd-run --unit=cyberdeckzero-apt ... cyberdeckzero-update apt-upgrade --user USER
     * [--libraries] -- PACKAGE... (the plan the confirmation page showed), or ... apt-repair --user USER; upgrade_started
     * counts the starts of both. */
    bool upgrade_packages(const std::string &user, const std::vector<std::string> &packages, bool libraries);
    bool repair_packages(const std::string &user);
    void clear_error() { snap_.error.clear(); }

private:
    enum class Kind { Update, Refresh, Upgrade };
    RootJob() = default;
    bool run(const std::vector<std::string> &argv, Kind kind = Kind::Update);
    static void output_cb(const char *data, std::size_t size, void *user);
    static void complete_cb(int result, int exit_code, void *user);

    RootSnapshot snap_;
    std::string output_;
    Kind kind_ = Kind::Update; /* what the request asked is for */
};

/* The sudo service's result (cp0_sudo_result_t) and systemd-run's output in words; apt: a package job (the unit
 * cyberdeckzero-apt) rather than the launcher's update. */
std::string root_failure_text(int sudo_result, int exit_code, const std::string &output, bool apt = false);

} // namespace launcher_update
