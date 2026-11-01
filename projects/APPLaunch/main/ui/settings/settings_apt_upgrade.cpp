/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > Packages, P4: the root package jobs' state and the shared busy flag (see settings_apt_upgrade.hpp).
 */

#include "settings_apt_upgrade.hpp"

#include "settings_apt_platform.hpp"
#include "settings_update_install.hpp"
#include "settings_update_install_model.hpp"
#include "settings_update_platform.hpp"

namespace launcher_apt {

UpgradeRead read_upgrade()
{
    UpgradeRead read;
    read.status = parse_upgrade_status(platform::upgrade_status());
    const bool alive = read.status.present && read.status.boot == launcher_update::platform::boot_id() &&
                       launcher_update::platform::process_alive(read.status.pid);
    read.phase = upgrade_phase(read.status, alive);
    return read;
}

std::string system_job_busy()
{
    const UpgradeRead upgrade = read_upgrade();
    if (upgrade.phase == UpgradePhase::Running)
        return upgrade.status.job == "repair" ? "Settings > Updates is repairing the packages"
                                              : "Settings > Updates is upgrading the packages";
    const launcher_update::HelperStatus helper =
        launcher_update::parse_helper_status(launcher_update::platform::helper_status());
    const bool alive = helper.present && helper.boot == launcher_update::platform::boot_id() &&
                       launcher_update::platform::process_alive(helper.pid);
    if (launcher_update::helper_phase(helper, alive) == launcher_update::HelperPhase::Running)
        return "A launcher update is running";
    if (launcher_update::root_job_asking()) return "Settings > Updates is asking for the password";
    return std::string();
}

} // namespace launcher_apt
