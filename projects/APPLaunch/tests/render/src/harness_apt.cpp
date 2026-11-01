/* SPDX-License-Identifier: MIT
 *
 * Render harness: Settings > Updates > System packages' system side (report 047 P3) from the scene's fixtures, instead of
 * settings_apt_platform.cpp (the Makefile leaves that file out): no apt-get, no dpkg, no ldd, never the host's own packages.
 * A fake apt-get / apt-mark / dpkg shim: the scan itself (settings_apt_scan.cpp), the classifier and the page are the
 * launcher's real code.
 *
 *   simulate         `apt-get -s upgrade` / `-s dist-upgrade` of the scene's board: the captures of the P0 checks
 *                    (projects/APPLaunch/tests/fixtures/apt/<board>/upgrade.txt, dist-upgrade.txt)
 *   library_owners   <board>/closure.txt (the dpkg owners of the launcher's ldd closure on that board)
 *   installed_record <board>/installed.json   app_owners <board>/owners.txt (dpkg -S /usr/share/APPLaunch)
 *   holds, detection <board>/detect.txt, "apt holds N", "apt auto on|off"
 *   lists_time       the scene clock minus "apt lists-age DAYS" (never: 0)
 *   refresh_status   "apt refresh KEY VALUE" lines (v=1, boot= and pid= added; "now-N" values: the scene clock)
 *
 * Scene commands (before `home`, or later: the next scan reads them):
 *   apt board NAME|empty|error|none|pending   NAME a fixture folder (deck, pi3a-dsi, pi4b, pi5, hackberry, fzm1); empty: a
 *                    plan with nothing to do; error: apt says "E: ..." (no plan); none: no apt-get at all; pending: the
 *                    simulation waits (the page shows "Reading the package lists...") until the scene gives another board
 *   apt lists-age DAYS|never       apt holds N       apt auto on|off
 *   apt refresh KEY VALUE | apt refresh clear
 */

#include "harness.h"

#include "settings/settings_apt_platform.hpp"
#include "settings/settings_update_model.hpp"

#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace launcher_apt {
namespace platform {

namespace {

std::string fixture_dir()
{
    return harness::fixtures().fixture_root + "/../../fixtures/apt";
}

std::string board_name()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    return fx.apt_board;
}

std::string board_file(const std::string &board, const char *name)
{
    std::string text;
    if (board.empty() || board == "empty" || board == "error" || board == "none" || board == "pending") return text;
    if (!launcher_update::read_file(fixture_dir() + "/" + board + "/" + name, text))
        std::fprintf(stderr, "[harness] apt: no fixture %s/%s\n", board.c_str(), name);
    return text;
}

std::vector<std::string> lines_of(const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

} // namespace

bool simulate(bool dist_upgrade, std::string &output)
{
    std::string board = board_name();
    while (board == "pending") { /* the scene gives the board later: "Reading the package lists..." */
        ::usleep(2000);
        board = board_name();
    }
    if (board == "none") return false; /* no apt-get */
    if (board == "error") {
        output = "E: Could not open lock file /var/lib/dpkg/lock-frontend - open (13: Permission denied)\n";
        return true;
    }
    if (board == "empty") {
        output = "Reading package lists...\nBuilding dependency tree...\nCalculating upgrade...\n"
                 "0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded.\n";
        return true;
    }
    output = board_file(board, dist_upgrade ? "dist-upgrade.txt" : "upgrade.txt");
    return true;
}

std::vector<std::string> library_owners()
{
    return lines_of(board_file(board_name(), "closure.txt"));
}

std::string installed_record()
{
    return board_file(board_name(), "installed.json");
}

std::vector<std::string> app_owners()
{
    return lines_of(board_file(board_name(), "owners.txt"));
}

std::vector<std::string> holds()
{
    harness::Fixtures &fx = harness::fixtures();
    int count;
    {
        std::lock_guard<std::mutex> lock(fx.apt_mutex);
        count = fx.apt_holds;
    }
    std::vector<std::string> names;
    for (int i = 1; i <= count; ++i) names.push_back("held-package-" + std::to_string(i));
    return names;
}

Detection detection()
{
    Detection d;
    for (const std::string &line : lines_of(board_file(board_name(), "detect.txt"))) {
        const std::size_t equal = line.find('=');
        if (equal == std::string::npos) continue;
        const std::string key = line.substr(0, equal), value = line.substr(equal + 1);
        if (key == "unattended_installed") d.unattended_installed = value == "1";
        else if (key == "unattended_enabled") d.unattended_enabled = value == "1";
        else if (key == "unattended_periodic") d.unattended_periodic = value;
        else if (key == "packagekit") d.packagekit = value == "1";
        else if (key == "snapd") d.snapd = value == "1";
    }
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    if (fx.apt_auto == "on") d.unattended_installed = d.unattended_enabled = true;
    else if (fx.apt_auto == "off") d.unattended_installed = true, d.unattended_enabled = false, d.unattended_periodic = "0";
    return d;
}

long long lists_time()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    if (fx.apt_lists_days < 0) return 0;
    return static_cast<long long>(std::time(nullptr)) - fx.apt_lists_days * 24 * 60 * 60 - 3 * 60 * 60;
}

std::string refresh_status()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    if (fx.apt_refresh.empty()) return std::string();
    std::string text = "v=1\nboot=harness-boot\npid=4001\n";
    for (const auto &kv : fx.apt_refresh) {
        std::string value = kv.second;
        if (value.compare(0, 3, "now") == 0)
            value = std::to_string(static_cast<long long>(std::time(nullptr)) + std::atoll(value.c_str() + 3));
        text += kv.first + "=" + value + "\n";
    }
    return text;
}

long long now()
{
    return static_cast<long long>(std::time(nullptr)); /* the scene's clock */
}

} // namespace platform
} // namespace launcher_apt
