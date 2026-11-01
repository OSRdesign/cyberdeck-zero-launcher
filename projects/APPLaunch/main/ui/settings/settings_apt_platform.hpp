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
 *
 * P4 (the upgrade, settings_apt_upgrade_model.hpp), as the user and changing nothing either:
 *   simulate_install `apt-get -s install --only-upgrade --no-remove --no-install-recommends NAMES`: the helper's second
 *                  simulation word for word (the closure, settings_apt_upgrade_model.hpp)
 *   print_uris     `apt-get --print-uris -qq install --only-upgrade --no-remove --no-install-recommends NAMES` (no lock:
 *                  apt opens its cache read-only for --print-uris)
 *   candidate_records `apt-cache show --no-all-versions NAMES` (Source, Depends, Installed-Size)
 *   installed_sizes `dpkg-query -W -f='${Installed-Size}\n' NAMES`
 *   slow_board     a 512 MB board (MemTotal under 1 GB: the Zero 2 W, the Pi 3A+): the waiting box says it at once
 * The apt-get and apt-cache runs stop at once when the scanner's cancel flag is set (apt_cancel_flag(),
 * settings_apt_scan.hpp: SIGTERM to the program's process group, then SIGKILL).
 *   anchor_depends the Depends of the anchor package cyberdeckzero-runtime (install.sh): launcher libraries too
 *   dpkg_unfinished `dpkg --audit` prints something (an interrupted dpkg run: Repair)
 *   under_voltage  now: the rpi_volt hwmon's in0_lcrit_alarm, else /sys/devices/platform/soc/soc:firmware/get_throttled,
 *                  else `vcgencmd get_throttled` (bit 0); false when none answers (a PC)
 *   upgrade_free   free bytes for the run: /var/cache/apt/archives, or the smaller of it and /usr when they are on two
 *                  file systems (-1 unknown)
 *   upgrade_status the helper's /var/lib/cyberdeckzero-update/apt-upgrade-status ("" when absent)
 *   request_cancel creates $XDG_RUNTIME_DIR/applaunch/apt-cancel (the helper reads it while it downloads, and only then)
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

/* P4 */
std::string simulate_install(const std::vector<std::string> &names);
std::string print_uris(const std::vector<std::string> &names);
std::string candidate_records(const std::vector<std::string> &names);
bool slow_board();
std::string installed_sizes(const std::vector<std::string> &names);
std::vector<std::string> anchor_depends();
bool dpkg_unfinished();
bool under_voltage();
long long upgrade_free();
std::string upgrade_status();
bool request_cancel();

} // namespace platform
} // namespace launcher_apt
