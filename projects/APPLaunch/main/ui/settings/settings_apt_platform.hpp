/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > System packages (report 047 P3): what the apt scan needs from the system, in one file the render
 * harness replaces with the P0 captures (tests/render/src/harness_apt.cpp; the Makefile leaves settings_apt_platform.cpp
 * out). Worker threads: they run programs. Nothing here changes the system: apt-get -s is a simulation, the rest reads.
 *
 *   simulate       `apt-get -s upgrade` or `apt-get -s dist-upgrade` as the user (a simulation by a user needs no lock and
 *                  no root: apt-get(8)), LC_ALL=C; false when the program could not run
 *   library_owners the dpkg owners of the libraries the launcher's binaries load: `ldd` of /usr/share/APPLaunch/bin/*
 *                  and lib/libapplaunch_vfb.so, realpath, /lib/ -> /usr/lib/ (dpkg on Debian 13 lists the merged-usr
 *                  paths), then `dpkg-query -S` (P0 check 14)
 *   installed_record   the Store backend's installed.json (apps_backend::state_dir())
 *   app_owners     `dpkg-query -S /usr/share/APPLaunch`: every package that owns files there (the apps)
 *   holds          `apt-mark showhold`
 *   detection      dpkg-query -W for unattended-upgrades / packagekit / snapd, apt-config's Unattended-Upgrade setting,
 *                  the unit's enabled state
 *   lists_time     the newest of /var/lib/apt/periodic/update-success-stamp and the InRelease / Release files of
 *                  /var/lib/apt/lists (0: none)
 *   refresh_status the helper's /var/lib/cyberdeckzero-update/apt-status ("" when absent)
 *   now            the wall clock (launcher_update::platform::now)
 * The helper's presence, the network, the boot id and a pid's life come from settings_update_platform.hpp.
 */
#pragma once

#include "settings_apt_model.hpp"

#include <string>
#include <vector>

namespace launcher_apt {
namespace platform {

bool simulate(bool dist_upgrade, std::string &output);
std::vector<std::string> library_owners();
std::string installed_record();
std::vector<std::string> app_owners();
std::vector<std::string> holds();
Detection detection();
long long lists_time();
std::string refresh_status();
long long now();

} // namespace platform
} // namespace launcher_apt
