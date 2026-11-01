/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > Packages, P4 of report 047: the state of the root package jobs as the launcher sees it, and the
 * busy flag Settings > Apps and Settings > Updates share (one dpkg, one job at a time, both directions):
 *
 *   read_upgrade      the helper's apt-upgrade-status and its phase (alive: its pid is a live process of this boot)
 *   system_job_busy   why a Settings > Apps package job must wait ("" none): an upgrade or a repair running (its status
 *                     file, the helper alive: also after a launcher restart), a launcher update running, a root step
 *                     asking for its password (settings_update_install.hpp). Any thread (it reads files and an atomic).
 * The other direction: settings_apps::package_job_running() (settings_apps_jobs.hpp), asked before an upgrade starts.
 * Against programs outside the launcher (the Store tile, a terminal, unattended-upgrades) the dpkg lock and the helper's
 * DPkg::Lock::Timeout (120 s, then "another program is installing packages") apply.
 */
#pragma once

#include "settings_apt_upgrade_model.hpp"

#include <string>

namespace launcher_apt {

struct UpgradeRead {
    UpgradeStatus status;
    UpgradePhase phase = UpgradePhase::None;
};
UpgradeRead read_upgrade();

std::string system_job_busy();

} // namespace launcher_apt
