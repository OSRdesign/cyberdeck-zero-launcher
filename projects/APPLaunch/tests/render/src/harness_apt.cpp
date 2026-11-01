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
 *
 * P4 (the upgrade): `apt-get -s install --only-upgrade NAMES` is the closure of NAMES through the board's pulls.txt ("a: b
 * c": installing a also upgrades b and c, as apt 2.7 does for one source), each upgradable name printed with its versions
 * from the capture; apt-cache records from the board's records.txt (Source, Depends) or made up; --print-uris and
 * dpkg-query sizes fixed per package name; the anchor names nothing more than the closure. The cancel flag of the scanner
 * stops a held stage at once, as the real platform kills its program.
 *   apt hold reading|listing|checking|none   (the scan blocks at that stage: the waiting box's scenes)
 *   apt slow on|off                (a 512 MB board)
 *   apt upgrade KEY VALUE | apt upgrade clear (the helper's apt-upgrade-status: v=1, boot=,
 *                                  pid=4002 added; "now-N" values; its helper alive: "update helper-alive on|off")
 *   apt undervoltage on|off        apt audit on|off (dpkg --audit not empty)      apt space MB (free for the run)
 *   apt expect-cancel yes|no       (fails the scene unless the launcher wrote the cancel flag, or did not)
 */

#include "harness.h"

#include "settings/settings_apt_platform.hpp"
#include "settings/settings_apt_scan.hpp"
#include "settings/settings_apt_upgrade_model.hpp"
#include "settings/settings_update_model.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
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

/* the scan blocks at a held stage until the scene moves on, or the launcher cancels the scan; false: cancelled */
bool hold_at(const char *stage)
{
    for (;;) {
        if (apt_cancel_flag().load()) return false;
        {
            harness::Fixtures &fx = harness::fixtures();
            std::lock_guard<std::mutex> lock(fx.apt_mutex);
            if (fx.apt_hold != stage) return true;
        }
        ::usleep(2000);
    }
}

bool simulate(bool dist_upgrade, std::string &output)
{
    std::string board = board_name();
    while (board == "pending") { /* the scene gives the board later: "Reading the package lists..." */
        if (apt_cancel_flag().load()) return false;
        ::usleep(2000);
        board = board_name();
    }
    if (!dist_upgrade && !hold_at("reading")) return false;
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
    if (!hold_at("listing")) return {};
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

// ------------------------------------------------------------------ P4: the upgrade

namespace {

/* a fixed size per package name: the scenes' figures never move */
long long name_hash(const std::string &name)
{
    long long h = 7;
    for (unsigned char c : name) h = (h * 31 + c) % 1000003;
    return h;
}

} // namespace

/* `apt-get -s install --only-upgrade NAMES`: the closure through the board's pulls.txt */
std::string simulate_install(const std::vector<std::string> &names)
{
    if (!hold_at("checking")) return std::string();
    const std::string board = board_name();
    std::map<std::string, std::pair<std::string, std::string>> versions;
    for (const Change &c : parse_simulation(board_file(board, "upgrade.txt")).upgrades)
        versions[base_name(c.name)] = {c.from, c.to};
    std::map<std::string, std::vector<std::string>> pulls;
    std::string pulls_text;
    launcher_update::read_file(fixture_dir() + "/" + board + "/pulls.txt", pulls_text);
    for (const std::string &line : lines_of(pulls_text)) {
        if (line[0] == '#' || line.find(':') == std::string::npos) continue;
        std::istringstream rest(line.substr(line.find(':') + 1));
        std::string n;
        while (rest >> n) pulls[line.substr(0, line.find(':'))].push_back(n);
    }
    std::vector<std::string> todo(names.begin(), names.end());
    std::set<std::string> all;
    while (!todo.empty()) {
        const std::string n = todo.back();
        todo.pop_back();
        if (!all.insert(n).second) continue;
        const auto it = pulls.find(n);
        if (it != pulls.end()) todo.insert(todo.end(), it->second.begin(), it->second.end());
    }
    std::string out = "NOTE: This is only a simulation!\n";
    for (const std::string &n : all) {
        const auto v = versions.find(n);
        if (v != versions.end())
            out += "Inst " + n + " [" + v->second.first + "] (" + v->second.second + " Debian:13/stable [arm64])\n";
        else if (std::find(names.begin(), names.end(), n) == names.end())
            out += "Inst " + n + " (1.0-1 Debian:13/stable [arm64])\n";
    }
    out += "0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded.\n";
    return out;
}

bool slow_board()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    return fx.apt_slow;
}

std::string print_uris(const std::vector<std::string> &names)
{
    std::string out;
    for (const std::string &n : names)
        out += "'http://deb.example/pool/" + n + "_1_arm64.deb' " + n + "_1_arm64.deb " +
               std::to_string(200000 + name_hash(n) % 1800000) + " SHA256:00\n";
    return out;
}

std::string candidate_records(const std::vector<std::string> &names)
{
    // the board's records.txt (Source, Depends) when it has one, the sizes made up per name
    std::string text;
    launcher_update::read_file(fixture_dir() + "/" + board_name() + "/records.txt", text);
    const std::map<std::string, Record> records = parse_records(text);
    std::string out;
    for (const std::string &n : names) {
        out += "Package: " + n + "\n";
        const auto it = records.find(n);
        if (it != records.end()) {
            if (!it->second.source.empty()) out += "Source: " + it->second.source + "\n";
            if (!it->second.depends.empty()) {
                out += "Depends: ";
                for (std::size_t i = 0; i < it->second.depends.size(); ++i) out += (i ? ", " : "") + it->second.depends[i];
                out += "\n";
            }
        }
        out += "Installed-Size: " + std::to_string(1000 + name_hash(n) % 3000) + "\n\n";
    }
    return out;
}

std::string installed_sizes(const std::vector<std::string> &names)
{
    std::string out;
    for (const std::string &n : names) out += std::to_string(950 + name_hash(n) % 3000) + "\n";
    return out;
}

std::vector<std::string> anchor_depends()
{
    return {}; /* the board's closure is its library set already */
}

bool dpkg_unfinished()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    return fx.apt_audit;
}

bool under_voltage()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    return fx.apt_undervoltage;
}

long long upgrade_free()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    return fx.apt_space_mb * 1024LL * 1024LL;
}

std::string upgrade_status()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    if (fx.apt_upgrade.empty()) return std::string();
    std::string text = "v=1\nboot=harness-boot\npid=4002\n";
    for (const auto &kv : fx.apt_upgrade) {
        std::string value = kv.second;
        if (value.compare(0, 3, "now") == 0)
            value = std::to_string(static_cast<long long>(std::time(nullptr)) + std::atoll(value.c_str() + 3));
        text += kv.first + "=" + value + "\n";
    }
    return text;
}

bool request_cancel()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.apt_mutex);
    fx.apt_cancelled = true;
    return true;
}

} // namespace platform
} // namespace launcher_apt
