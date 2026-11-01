// SPDX-License-Identifier: MIT
//
// Settings > Updates > System packages (report 047 P3, apt list only): the simulation parser and the classifier on the
// real captures of the six boards (P0 of 2026-10-31: deck, Pi 3A+, Pi 4 B, Pi 5, HackBerry, FZ-M1 tablet; the files of
// tests/fixtures/apt/<board>/), the Store exclusion (Debian's own "2048" on the deck against the Store's 2048), the
// Pi 5's `upgrade` against `dist-upgrade`, the ldd and dpkg parsers, installed.json, detection and the page's texts.
//
//   test_settings_apt_model FIXTURE_DIR      (prints the table of the classes per board)

#include "settings_apt_model.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace launcher_apt;

namespace {

std::string g_fixtures;

std::string load(const std::string &path, bool must = true)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (!must) return std::string();
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::abort();
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string fx(const std::string &board, const char *name, bool must = true)
{
    return load(g_fixtures + "/" + board + "/" + name, must);
}

Board board_of(const std::string &name)
{
    Board board;
    for (const std::string &n : parse_owners(fx(name, "closure.txt").empty() ? "" : [&] {
             std::string out;
             std::stringstream lines(fx(name, "closure.txt"));
             std::string line;
             while (std::getline(lines, line)) out += line + ": /usr/lib/x\n"; /* the fixture holds the owners' names */
             return out;
         }()))
        board.libraries.insert(n);
    for (const std::string &n : parse_installed_packages(fx(name, "installed.json"))) board.apps.insert(n);
    std::stringstream owners(fx(name, "owners.txt"));
    std::string line;
    while (std::getline(owners, line))
        if (!line.empty()) board.apps.insert(line);
    return board;
}

Report report_of(const std::string &name)
{
    return classify(parse_simulation(fx(name, "upgrade.txt")), parse_simulation(fx(name, "dist-upgrade.txt")),
                    board_of(name));
}

bool contains(const std::vector<Package> &list, const std::string &name)
{
    return std::any_of(list.begin(), list.end(), [&](const Package &p) { return p.name == name; });
}

const Package *find(const std::vector<Package> &list, const std::string &name)
{
    for (const Package &p : list)
        if (p.name == name) return &p;
    return nullptr;
}

struct Expect {
    const char *board;
    int upgraded, safe, libraries, boot, terminal, store;
    bool needs_dist;
};

// the counts of every board's capture (checked by hand against the apt output when they were written)
const Expect kExpect[] = {
    // board, Inst lines of `upgrade`, safe, libraries, boot, terminal, Store apps left out, needs dist-upgrade
    {"deck", 1, 0, 0, 0, 0, 1, false},       /* Debian's 2048 against the Store's 2048: nothing left to list */
    {"pi3a-dsi", 24, 19, 4, 1, 0, 0, false},
    {"pi4b", 21, 16, 4, 1, 0, 0, false},
    {"pi5", 244, 234, 8, 16, 18, 0, true},   /* `upgrade` keeps 23 back; `dist-upgrade` adds 34 new and removes pcmanfm */
    {"hackberry", 40, 38, 1, 1, 0, 0, false},
    {"fzm1", 9, 7, 2, 0, 0, 0, false},
};

void parser_cases()
{
    // an Inst with an installed version, a new package, one with the trailing "[]", a Remv, the kept-back list
    const Simulation s = parse_simulation(
        "Reading package lists...\n"
        "The following packages have been kept back:\n  linux-image-rpi-2712 libfoo1\n  xyz\n"
        "The following packages will be upgraded:\n  curl\n"
        "2 upgraded, 1 newly installed, 1 to remove and 3 not upgraded.\n"
        "Inst curl [8.14.1-2+deb13u5] (8.14.1-2+deb13u7 Debian-Security:13/stable-security [arm64]) []\n"
        "Inst libnew1 (1.0-1 Debian:13/stable [arm64])\n"
        "Inst libc6:armhf [2.41-12] (2.41-12+deb13u1 Debian:13/stable [armhf])\n"
        "Remv pcmanfm [1.4.0-1+rpt9] []\n"
        "Conf curl (8.14.1-2+deb13u7 Debian-Security:13/stable-security [arm64])\r\n");
    assert(s.valid);
    assert(s.upgrades.size() == 2 && s.installs.size() == 1 && s.removals.size() == 1);
    assert(s.upgrades[0].name == "curl" && s.upgrades[0].from == "8.14.1-2+deb13u5" && s.upgrades[0].to == "8.14.1-2+deb13u7");
    assert(s.upgrades[1].name == "libc6:armhf" && base_name(s.upgrades[1].name) == "libc6");
    assert(s.installs[0].name == "libnew1" && s.installs[0].from.empty() && s.installs[0].to == "1.0-1");
    assert(s.removals[0].name == "pcmanfm" && s.removals[0].from == "1.4.0-1+rpt9" && s.removals[0].to.empty());
    assert((s.kept_back == std::vector<std::string>{"linux-image-rpi-2712", "libfoo1", "xyz"}));
    assert(s.summary_upgraded == 2 && s.summary_new == 1 && s.summary_remove == 1 && s.summary_kept == 3);

    // nothing to do: a valid plan with no change
    const Simulation none = parse_simulation("Reading package lists...\n0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded.\n");
    assert(none.valid && none.upgrades.empty());
    // an error (no lists, a lock) is no plan
    assert(!parse_simulation("E: Could not open lock file /var/lib/dpkg/lock-frontend - open (13: Permission denied)\n").valid);
    assert(!parse_simulation("").valid);
    // one kept-back package: apt's singular
    assert(parse_simulation("The following package has been kept back:\n  zzz\n").kept_back.size() == 1);
}

void owner_cases()
{
    // dpkg-query -S: arch suffixes, several owners of one file, diversions and errors skipped, no duplicates
    const std::vector<std::string> owners = parse_owners(
        "libc6:arm64: /usr/lib/aarch64-linux-gnu/libc.so.6\n"
        "libstdc++6:arm64: /usr/lib/aarch64-linux-gnu/libstdc++.so.6\n"
        "serial-console, markdown-notes, mine-field: /usr/share/APPLaunch\n"
        "libc6:arm64: /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1\n"
        "diversion by dpkg from: /bin/x\n"
        "dpkg-query: no path found matching pattern /opt/x\n");
    assert((owners == std::vector<std::string>{"libc6", "libstdc++6", "serial-console", "markdown-notes", "mine-field"}));

    const std::vector<std::string> paths = ldd_paths(
        "\tlinux-vdso.so.1 (0x0000ffff9a2f0000)\n"
        "\tlibinput.so.10 => /lib/aarch64-linux-gnu/libinput.so.10 (0x0000ffff99c40000)\n"
        "\tlibc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x0000ffff99a90000)\n"
        "\tlibmissing.so.1 => not found\n"
        "\t/lib/ld-linux-aarch64.so.1 (0x0000ffff9a2f4000)\n");
    assert((paths == std::vector<std::string>{"/lib/aarch64-linux-gnu/libinput.so.10", "/lib/aarch64-linux-gnu/libc.so.6"}));
    // P0 check 14: dpkg knows /usr/lib on Debian 13
    assert(dpkg_path("/lib/aarch64-linux-gnu/libc.so.6") == "/usr/lib/aarch64-linux-gnu/libc.so.6");
    assert(dpkg_path("/usr/lib/x86_64-linux-gnu/libc.so.6") == "/usr/lib/x86_64-linux-gnu/libc.so.6");

    // the Store's record: the "package" of each app, whatever the other fields hold; nothing else counts
    const std::vector<std::string> apps = parse_installed_packages(
        "{\"apps\": [ {\"id\":\"a\", \"package\" : \"2048\"}, {\"package\":\"net-toolbox\",\"title\":\"package\"},"
        " {\"package\":\"Bad Name\"}, {\"package\":\"2048\"} ]}");
    assert((apps == std::vector<std::string>{"2048", "net-toolbox"}));
    assert(parse_installed_packages("").empty() && parse_installed_packages("not json").empty());
}

void class_cases()
{
    // boot chain
    for (const char *n : {"linux-image-rpi-v8", "linux-image-6.18.50+rpt-rpi-2712", "raspi-firmware", "rpi-eeprom", "firmware-brcm80211",
                          "initramfs-tools", "linux-headers-rpi-v8", "armbian-firmware", "armbian-bsp-cli-x", "u-boot-rpi", "grub-efi-amd64", "shim-signed",
                          "intel-microcode", "linux-modules-7.0.0-1-generic", "bluez-firmware"})
        assert(is_boot_chain(n));
    for (const char *n : {"curl", "libc6", "linux-libc-dev", "raspi-config", "armbian-config", "u-boot-tools", "bluez", "libraspberrypi-bin", "tzdata"})
        assert(!is_boot_chain(n));
    // the static library set: the closure's names with and without t64, tools and the session
    for (const char *n : {"libc6", "libstdc++6", "libglib2.0-0t64", "libglib2.0-0", "libinput10", "libfreetype6", "libssl3t64",
                          "curl", "libcurl4t64", "ca-certificates", "systemd", "udev", "dbus", "polkitd", "sudo",
                          "network-manager", "wpasupplicant", "bluez", "lightdm", "libmtdev1t64"})
        assert(is_static_library(n));
    for (const char *n : {"tzdata", "raspi-config", "libcurl3t64-gnutls", "vim", "libglib2.0-data", "2048", "bluez-firmware"})
        assert(!is_static_library(n));

    // a small plan through every class, with a Store app of the same name as a Debian package
    Board board;
    board.libraries = {"libbrotli1", "libweird3"};
    board.apps = {"2048"};
    const Simulation up = parse_simulation(
        "The following packages have been kept back:\n  linux-image-rpi-2712 pcmanfm 2048\n"
        "Inst tzdata [2026c-0] (2026e-0 Debian:13 [all])\n"
        "Inst libweird3 [1.0] (1.1 Debian:13 [arm64])\n"
        "Inst libc6 [2.41-12] (2.41-12+deb13u1 Debian:13 [arm64])\n"
        "Inst rpi-eeprom [28.33-1] (28.34-1 Raspberry Pi Foundation:stable [all])\n"
        "Inst 2048 [0.1.0] (1.0.3-1 Debian:13.7/stable [arm64])\n");
    const Simulation dist = parse_simulation(
        "Inst linux-image-rpi-2712 [1:6.18.50-1] (1:6.18.60-1 Raspberry Pi Foundation:stable [arm64])\n"
        "Inst linux-image-6.18.60+rpt-rpi-2712 (1:6.18.60-1 Raspberry Pi Foundation:stable [arm64])\n"
        "Inst libnewthing (1.0 Debian:13 [arm64])\n"
        "Remv pcmanfm [1.4.0-1+rpt9] []\n"
        "Inst pcmanfm-pi (1.11 Raspberry Pi Foundation:stable [arm64]) []\n");
    const Report r = classify(up, dist, board);
    assert(r.valid);
    assert(r.safe.size() == 1 && r.safe[0].name == "tzdata" && r.safe[0].from == "2026c-0" && r.safe[0].to == "2026e-0");
    assert(r.libraries.size() == 2 && r.libraries[0].name == "libc6" && r.libraries[1].name == "libweird3"); /* sorted */
    assert(r.boot.size() == 3 && contains(r.boot, "rpi-eeprom") && contains(r.boot, "linux-image-rpi-2712") &&
           contains(r.boot, "linux-image-6.18.60+rpt-rpi-2712"));
    assert(find(r.boot, "linux-image-rpi-2712")->note == "kept back" && find(r.boot, "linux-image-rpi-2712")->to == "1:6.18.60-1");
    assert(find(r.boot, "linux-image-6.18.60+rpt-rpi-2712")->note == "new package");
    assert(r.terminal.size() == 1 && find(r.terminal, "pcmanfm")->note == "will be removed"); /* kept back and removed: one row */
    assert(r.needs_dist_upgrade);
    // the Store app is left out in every place it can appear (an upgrade, kept back) and is never in a section
    assert(r.store == std::vector<std::string>{"2048"});
    for (const auto *list : {&r.safe, &r.libraries, &r.boot, &r.terminal}) assert(!contains(*list, "2048"));

    // no kept-back package: `dist-upgrade` is not read (a stray removal in it changes nothing)
    const Simulation plain = parse_simulation("Inst tzdata [1] (2 Debian:13 [all])\n");
    const Report p = classify(plain, dist, Board());
    assert(p.safe.size() == 1 && p.terminal.empty() && p.boot.empty() && !p.needs_dist_upgrade);
    // no plan at all
    assert(!classify(Simulation(), dist, Board()).valid);
    // the board's closure widens the library set beyond the static list
    Board closure;
    closure.libraries = {"libweird3"};
    assert(classify(parse_simulation("Inst libweird3 [1] (2 D [arm64])\nInst libother [1] (2 D [arm64])\n"), Simulation(), closure).libraries.size() == 1);
}

void detection_cases()
{
    Detection d;
    assert(automatic_updates(d) == "not installed");
    d.unattended_periodic = "7"; /* the FZ-M1 tablet: the setting, no package */
    assert(automatic_updates(d) == "not installed");
    d.unattended_installed = true;
    assert(automatic_updates(d) == "on");
    d.unattended_periodic = "0";
    assert(automatic_updates(d) == "off");
    d.unattended_enabled = true;
    assert(automatic_updates(d) == "on");
    d.unattended_enabled = false;
    d.unattended_periodic = "";
    assert(automatic_updates(d) == "off");
}

void refresh_cases()
{
    // the one root command: systemd-run into a transient unit, the helper's apt-refresh, the user name checked
    const std::vector<std::string> argv = refresh_argv("pi");
    assert(argv.size() == 9 && argv[0] == "/usr/bin/systemd-run" && argv[1] == "--unit=cyberdeckzero-apt" &&
           argv[5] == "/usr/share/APPLaunch/bin/cyberdeckzero-update" && argv[6] == "apt-refresh" && argv[7] == "--user" &&
           argv[8] == "pi");
    assert(refresh_argv("").empty() && refresh_argv("-x").empty() && refresh_argv("a b").empty() && refresh_argv("a;rm").empty());
    assert(refresh_argv("pi", "relative/helper").empty() && refresh_argv("pi", "").empty());
    assert(!refresh_argv("o.livier_1-x").empty());

    // the helper's status file
    const RefreshStatus s = parse_refresh_status("v=1\nstate=running\npercent=37\ntext=Retrieving file 2 of 4\npid=812\nboot=b1\n"
                                                 "started=100\nupdated=105\nreason=\nunknown=x\n");
    assert(s.present && s.state == "running" && s.percent == 37 && s.text == "Retrieving file 2 of 4" && s.pid == 812 &&
           s.started == 100 && s.updated == 105 && s.boot == "b1");
    assert(!parse_refresh_status("state=running\n").present && !parse_refresh_status("v=1\n").present && !parse_refresh_status("").present);
    assert(parse_refresh_status("v=1\nstate=running\npercent=250\n").percent == 100);
    assert(parse_refresh_status("v=1\nstate=running\npercent=abc\n").percent == 0 && parse_refresh_status("v=1\nstate=running\npercent=\n").percent == -1);
    assert(refresh_phase(s, true) == RefreshPhase::Running && refresh_phase(s, false) == RefreshPhase::Interrupted);
    assert(refresh_phase(parse_refresh_status("v=1\nstate=done\n"), false) == RefreshPhase::Done);
    assert(refresh_phase(parse_refresh_status("v=1\nstate=failed\n"), false) == RefreshPhase::Failed);
    assert(refresh_phase(RefreshStatus(), true) == RefreshPhase::None);
    assert(refresh_percent(s) == 37 && refresh_value(s) == "37 %");
    const RefreshStatus fresh = parse_refresh_status("v=1\nstate=running\n");
    assert(refresh_percent(fresh) == 0 && refresh_value(fresh) == "starting");
    const RefreshStatus failed = parse_refresh_status("v=1\nstate=failed\nreason=E: Failed to fetch http://deb.debian.org/ Temporary failure resolving\n");
    assert(refresh_failure(failed, RefreshPhase::Failed) == "Refresh failed: E: Failed to fetch http://deb.debian.org/ Temporary failure resolving");
    assert(refresh_failure(parse_refresh_status("v=1\nstate=failed\n"), RefreshPhase::Failed).find("journalctl") != std::string::npos);
    assert(refresh_failure(s, RefreshPhase::Interrupted).find("interrupted") != std::string::npos);
    assert(refresh_failure(s, RefreshPhase::Running).empty() && refresh_failure(s, RefreshPhase::Done).empty());

    // what stops a refresh before the password
    assert(refresh_blocker(true, true, false, false).empty());
    assert(refresh_blocker(false, true, false, false).find("update helper") != std::string::npos && refresh_blocker(false, true, false, false).find("sudo apt-get update") != std::string::npos);
    assert(refresh_blocker(true, false, false, false) == "No network");
    assert(refresh_blocker(true, true, true, false) == "A launcher update is running");
    assert(refresh_blocker(true, true, false, true) == "A refresh is already running");

    // facts from dpkg-query -W, apt-config dump
    const std::string dpkg = "dpkg-query: no packages found matching snapd\nunattended-upgrades ii \npackagekit rc \n";
    assert(package_installed(dpkg, "unattended-upgrades") && !package_installed(dpkg, "packagekit") && !package_installed(dpkg, "snapd"));
    const std::string dump = "APT::Periodic \"\";\nAPT::Periodic::Enable \"0\";\nAPT::Periodic::Unattended-Upgrade \"7\";\n";
    assert(periodic_value(dump, "APT::Periodic::Unattended-Upgrade") == "7");
    assert(periodic_value(dump, "APT::Periodic::Enable") == "0" && periodic_value(dump, "APT::Periodic::Nope").empty());
}

void text_cases()
{
    const long long now = 1000000000;
    const long long day = 24 * 60 * 60;
    Overview o;
    assert(row_value(o, now).text.empty()); /* no scan yet */
    o.scanned = true;
    o.failed = true;
    assert(row_value(o, now).text == "Check failed" && row_value(o, now).tone == Tone::Muted);
    o.failed = false;
    o.report.valid = true;
    assert(row_value(o, now).text == "not checked"); /* lists never refreshed */
    o.lists_time = now - 2 * day;
    assert(row_value(o, now).text == "Up to date" && row_value(o, now).tone == Tone::Ok);
    o.report.safe.push_back({"curl", "1", "2", ""});
    assert(row_value(o, now).text == "1 update" && row_value(o, now).tone == Tone::Warn);
    o.report.boot.push_back({"rpi-eeprom", "1", "2", ""});
    o.report.terminal.push_back({"pcmanfm", "", "", "will be removed"});
    assert(row_value(o, now).text == "3 updates");
    o.lists_time = now - 9 * day; /* D12: old lists say little */
    assert(row_value(o, now).text == "Lists are 9 days old" && row_value(o, now).tone == Tone::Muted);
    o.lists_time = now - 7 * day; /* exactly a week: still counted */
    assert(row_value(o, now).text == "3 updates");
    assert(lists_age_days(now + 5, now) == 0 && lists_age_days(0, now) == 0 && lists_age_days(now - 3 * day - 5, now) == 3);

    assert(section_title("Safe to upgrade", 31) == "Safe to upgrade (31)");
    assert(package_row({"libc6", "2.41-12", "2.41-12+deb13u1", ""}).value == "2.41-12 > 2.41-12+deb13u1");
    assert(package_row({"libc6", "2.41-12", "2.41-12+deb13u1", "kept back"}).value == "2.41-12 > 2.41-12+deb13u1");
    assert(package_row({"linux-image-x", "", "6.1", "new package"}).value == "new 6.1");
    assert(package_row({"pcmanfm", "1.4", "", "will be removed"}).value == "will be removed");
    assert(package_row({"foo", "", "", "kept back"}).value == "kept back");
    // a narrow row: the longer texts first, the end of the version (what differs) last
    const PackageRow wide = package_row({"libcamera0.7", "0.7.2+rpt20260817-1", "0.7.2+rpt20261009-1", ""});
    assert(wide.values.size() == 3 && wide.values[0] == wide.value && wide.values[1] == "> 0.7.2+rpt20261009-1" &&
           wide.values[2] == "...+rpt20261009-1");
    assert(package_row({"curl", "1", "2", ""}).values[2] == "2"); /* a short version stays whole */
    assert(package_row({"libnew", "", "6.18.60+rpt-rpi-2712", "new package"}).values.size() == 2);

    Report r;
    r.valid = true;
    r.boot.push_back({"rpi-eeprom", "28.33-1", "28.34-1", ""});
    std::string text = terminal_text(r);
    assert(text.find("sudo apt-get upgrade") != std::string::npos && text.find("dist-upgrade") == std::string::npos);
    assert(text.find("rpi-eeprom") != std::string::npos && text.find("power cut") != std::string::npos);
    r.needs_dist_upgrade = true;
    r.terminal.push_back({"pcmanfm", "1.4", "", "will be removed"});
    r.store.push_back("2048");
    text = terminal_text(r);
    assert(text.find("sudo apt-get dist-upgrade") != std::string::npos && text.find("pcmanfm  (will be removed)") != std::string::npos);
    assert(text.find("Debian's own 2048") != std::string::npos && text.find("Settings > Apps") != std::string::npos);
    assert(std::string(info_line()).find("sudo apt-get upgrade") != std::string::npos && std::string(info_line()).size() <= 40);
}

void board_cases()
{
    std::printf("%-10s %9s %5s %9s %5s %9s %6s %s\n", "board", "upgraded", "safe", "libraries", "boot", "terminal", "store",
                "needs dist-upgrade");
    for (const Expect &e : kExpect) {
        const Simulation up = parse_simulation(fx(e.board, "upgrade.txt"));
        const Report r = report_of(e.board);
        std::printf("%-10s %9d %5zu %9zu %5zu %9zu %6zu %s\n", e.board, static_cast<int>(up.upgrades.size()), r.safe.size(),
                    r.libraries.size(), r.boot.size(), r.terminal.size(), r.store.size(), r.needs_dist_upgrade ? "yes" : "no");
        assert(r.valid);
        // the parser read every Inst line and the summary line agrees
        assert(static_cast<int>(up.upgrades.size()) == e.upgraded);
        assert(up.summary_upgraded == e.upgraded);
        assert(static_cast<int>(r.safe.size()) == e.safe && static_cast<int>(r.libraries.size()) == e.libraries &&
               static_cast<int>(r.boot.size()) == e.boot && static_cast<int>(r.terminal.size()) == e.terminal &&
               static_cast<int>(r.store.size()) == e.store && r.needs_dist_upgrade == e.needs_dist);
    }

    // the Pi 5: `upgrade` upgrades 244 and keeps 23 back; `dist-upgrade` upgrades those too (267), adds 34 new packages and
    // removes pcmanfm. The page lists the 244 as they are and sends the 23 and the removal to the terminal.
    {
        const Simulation up = parse_simulation(fx("pi5", "upgrade.txt"));
        const Simulation dist = parse_simulation(fx("pi5", "dist-upgrade.txt"));
        assert(up.summary_kept == 23 && up.kept_back.size() == 23 && up.removals.empty() && up.installs.empty());
        assert(dist.summary_upgraded == 267 && dist.summary_new == 34 && dist.summary_remove == 1 && dist.removals.size() == 1);
        assert(dist.upgrades.size() == 267 && dist.installs.size() == 34 && dist.removals[0].name == "pcmanfm");
        const Report r = report_of("pi5");
        assert(contains(r.terminal, "pcmanfm") && find(r.terminal, "pcmanfm")->note == "will be removed");
        assert(contains(r.terminal, "vlc") && find(r.terminal, "vlc")->note == "kept back" && !find(r.terminal, "vlc")->to.empty());
        assert(contains(r.boot, "linux-image-rpi-2712") && contains(r.boot, "linux-image-6.18.50+rpt-rpi-2712"));
        // without dist-upgrade's text the kept-back packages are still sent to the terminal (no versions, no removal)
        const Report alone = classify(up, Simulation(), board_of("pi5"));
        assert(!alone.terminal.empty() && !alone.boot.empty() && !contains(alone.terminal, "pcmanfm"));
        assert(find(alone.terminal, "vlc") && find(alone.terminal, "vlc")->to.empty());
    }
    // the deck: Debian's 2048 [0.1.0 -> 1.0.3-1] is the Store app's name: left out, the system is up to date
    {
        const Report r = report_of("deck");
        assert(r.store.size() == 1 && r.store[0] == "2048" && r.shown() == 0);
        Overview o;
        o.scanned = true;
        o.report = r;
        o.lists_time = 1000000;
        assert(row_value(o, 1000000 + 3600).text == "Up to date");
        // the same capture on a board with no 2048 installed lists it
        const Report other = classify(parse_simulation(fx("deck", "upgrade.txt")), Simulation(), Board());
        assert(other.store.empty() && contains(other.safe, "2048"));
        // an installed.json that names it is enough, and so is dpkg's owner list of /usr/share/APPLaunch
        Board by_owner;
        for (const std::string &n : parse_owners("2048, net-toolbox: /usr/share/APPLaunch\n")) by_owner.apps.insert(n);
        assert(classify(parse_simulation(fx("deck", "upgrade.txt")), Simulation(), by_owner).store.size() == 1);
    }
}

} // namespace

int main(int argc, char **argv)
{
    g_fixtures = argc > 1 ? argv[1] : "fixtures/apt";
    parser_cases();
    owner_cases();
    class_cases();
    detection_cases();
    refresh_cases();
    text_cases();
    board_cases();
    std::printf("settings_apt_model: all passed\n");
    return 0;
}
