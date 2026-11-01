/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > System packages, phase P3 of report 047 (apt, LIST ONLY): the rules, with no LVGL, no thread and
 * no process. Unit-tested on the P0 board captures in projects/APPLaunch/tests/test_settings_apt_model.cpp
 * (tests/fixtures/apt/<board>/).
 *
 *   parse_simulation   the text of `apt-get -s upgrade` (and `-s dist-upgrade`, only to see what `upgrade` leaves
 *                      behind): Inst lines (old and new version), Remv lines, the "kept back" list, the summary line
 *   parse_owners       the output of `dpkg-query -S` ("libc6:arm64: /usr/lib/...", "a, b, c: /usr/share/APPLaunch")
 *                      as package names
 *   ldd_paths          the libraries `ldd` names, as the paths dpkg knows them (realpath, then /lib/ -> /usr/lib/: dpkg
 *                      on Debian 13 records the merged-usr paths, P0 check 14)
 *   parse_installed_packages   the Store backend's installed.json: the "package" of every installed app
 *   classify           the plan against the board: SAFE (the default set), LAUNCHER LIBRARIES (D14: the ELF closure of
 *                      the launcher's binaries, curl and ca-certificates, the session services), BOOT CHAIN (D15:
 *                      kernel, firmware, bootloader, EEPROM, initramfs: terminal only), TERMINAL (kept back by apt or
 *                      removed: never done here), and the STORE apps left out of the list altogether: a package whose
 *                      name is an app the Store installed, or that owns files under /usr/share/APPLaunch (Debian's own
 *                      "2048" is not an update of the Store's "2048"). Names come from the board, never from a list
 *                      of app names in the launcher.
 *   detection          unattended-upgrades on / off, PackageKit, snapd, holds: shown, never changed (D18)
 *   texts              the page's rows, values and the terminal-command page, so the rules are tested here
 *
 * What the page says about the lists' age: apt-get update does not touch an InRelease file that did not change, so the
 * age is the newest of /var/lib/apt/periodic/update-success-stamp (Debian's apt writes it after each successful update)
 * and the InRelease / Release files (the platform layer reads both).
 */
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace launcher_apt {

constexpr const char *kAppFolder = "/usr/share/APPLaunch";
/* "Lists are N days old" from this age on (D12: a list from old lists says little). */
constexpr long long kStaleDays = 7;

// ------------------------------------------------------------------ the simulation

struct Change {
    std::string name;    /* as apt prints it ("libc6", "libc6:armhf") */
    std::string from;    /* the installed version ("" for a new package) */
    std::string to;      /* the candidate ("" for a removal) */
};

struct Simulation {
    bool valid = false;               /* it ended with a plan (a summary line, or at least one Inst / Remv line) */
    std::vector<Change> upgrades;     /* Inst with an installed version */
    std::vector<Change> installs;     /* Inst of a package that is not installed (new) */
    std::vector<Change> removals;     /* Remv */
    std::vector<std::string> kept_back;
    std::vector<std::string> phased;  /* (P4) "deferred due to phasing" (Ubuntu's phased updates): not now, never an error */
    int summary_upgraded = -1;        /* the summary line's counts, -1 when it is absent */
    int summary_new = -1;
    int summary_remove = -1;
    int summary_kept = -1;
};

Simulation parse_simulation(const std::string &text);

/* "libc6:arm64" -> "libc6" */
std::string base_name(const std::string &name);

// ------------------------------------------------------------------ owners, libraries, apps

std::vector<std::string> parse_owners(const std::string &dpkg_query_output);
/* The paths in an `ldd` output ("libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x...)"), not the loader line. */
std::vector<std::string> ldd_paths(const std::string &ldd_output);
/* A realpath() result as dpkg lists it: "/lib/x" -> "/usr/lib/x". */
std::string dpkg_path(const std::string &real_path);
std::vector<std::string> parse_installed_packages(const std::string &installed_json);

bool is_boot_chain(const std::string &name);
/* The static part of the launcher's library set: what the binaries need on any board (libc6, libstdc++6, libinput10,
 * ...), curl and ca-certificates (the launcher runs curl), and the session services (systemd, udev, dbus, polkit, sudo,
 * NetworkManager, wpa_supplicant, bluez, the display managers: report 047 class N). The board's own ldd closure is added
 * to it by classify(). */
bool is_static_library(const std::string &name);

struct Board {
    std::set<std::string> libraries; /* dpkg owners of the ELF closure of the launcher's binaries (ldd) */
    std::set<std::string> apps;      /* the Store's installed packages (installed.json) and the owners of /usr/share/APPLaunch */
    std::vector<std::string> holds;  /* apt-mark showhold */
};

// ------------------------------------------------------------------ the classified plan

struct Package {
    std::string name;
    std::string from;
    std::string to;
    std::string note; /* "kept back" / "will be removed" / "new package" for the terminal and boot sections */
};

struct Report {
    bool valid = false;               /* a plan was read (false: apt-get -s failed or said nothing) */
    std::vector<Package> safe;
    std::vector<Package> libraries;
    std::vector<Package> boot;
    std::vector<Package> terminal;    /* kept back by apt (not boot chain) and removals */
    std::vector<std::string> store;   /* package names left out: the Store's own apps */
    std::vector<Package> store_versions; /* (P4) the Store apps `upgrade` would upgrade, with their versions */
    std::vector<std::string> phased;  /* (P4) upgrades apt defers by phasing: offered later by apt itself, never in a plan */
    bool needs_dist_upgrade = false;  /* something in boot / terminal is only in `dist-upgrade` */
    int shown() const;                /* the packages the page lists */
};

/* upgrade: `apt-get -s upgrade`; dist: `apt-get -s dist-upgrade`, only read when `upgrade` kept packages back (it
 * may be invalid or empty otherwise). */
Report classify(const Simulation &upgrade, const Simulation &dist, const Board &board);

// ------------------------------------------------------------------ detection (D18)

struct Detection {
    bool unattended_installed = false;
    bool unattended_enabled = false;  /* its service is enabled */
    std::string unattended_periodic;  /* APT::Periodic::Unattended-Upgrade ("" absent, "0" off, "7"...) */
    bool packagekit = false;
    bool snapd = false;
    std::size_t holds = 0;
};

/* "on": installed and switched on (the periodic setting is not 0 / the service enabled); "off": installed but not
 * switched on; "not installed". */
std::string automatic_updates(const Detection &detection);

// ------------------------------------------------------------------ what the page shows

enum class Tone { Normal, Ok, Warn, Muted };

struct Value {
    std::string text;
    Tone tone = Tone::Normal;
};

/* What the scan found; the page's rows and the Updates page's row read it. */
struct Overview {
    bool scanned = false;     /* a scan has run */
    bool failed = false;      /* apt-get -s gave no plan */
    long long lists_time = 0; /* the newest list / success stamp (epoch seconds), 0: none (never refreshed) */
    Report report;
    Detection detection;
    bool dpkg_unfinished = false; /* (P4) dpkg --audit printed something: Repair */
};

/* The "System packages" row of the Updates page: amber "N updates" (what the page lists in its three package
 * sections: safe, libraries, boot), green "Up to date", grey "Lists are N days old", grey "not checked". */
Value row_value(const Overview &overview, long long now);
/* Whole days since `lists_time`, 0 when it is in the future. */
long long lists_age_days(long long lists_time, long long now);

// ------------------------------------------------------------------ the refresh of the lists (Refresh package lists)

constexpr const char *kRefreshUnit = "cyberdeckzero-apt";
constexpr const char *kRefreshStatusPath = "/var/lib/cyberdeckzero-update/apt-status";
constexpr const char *kHelperPath = "/usr/share/APPLaunch/bin/cyberdeckzero-update";

/* The ONE command the shared sudo prompt runs: systemd-run --unit=cyberdeckzero-apt --collect --quiet <helper> apt-refresh
 * --user NAME. The helper runs `apt-get update` (Status-Fd progress) in that transient system unit, outside the launcher's
 * cgroup, and writes kRefreshStatusPath. Empty for a bad user name or helper path. */
std::vector<std::string> refresh_argv(const std::string &user, const std::string &helper = kHelperPath);

struct RefreshStatus {
    bool present = false;
    std::string state;  /* running, done, failed */
    std::string text;   /* what apt says it is doing ("Retrieving file 2 of 4") */
    std::string reason; /* why it failed (apt's last E: line) */
    std::string boot;
    int percent = -1;   /* 0..100, -1 unknown */
    long long pid = 0, started = 0, updated = 0;
};
/* key=value lines (a v=1 file); unknown keys ignored; present = a v=1 file with a state. */
RefreshStatus parse_refresh_status(const std::string &text);

enum class RefreshPhase { None, Running, Done, Failed, Interrupted };
/* alive: the helper's pid is a live process of this boot. A "running" status whose helper is gone is Interrupted. */
RefreshPhase refresh_phase(const RefreshStatus &status, bool alive);
/* The bar's value (0..100) of a running refresh; 0 while apt has not said yet. */
int refresh_percent(const RefreshStatus &status);
/* The Refresh row's value while it runs: "37 %", "starting" before the first figure. */
std::string refresh_value(const RefreshStatus &status);
/* The status line after a failed or interrupted refresh ("Refresh failed: ..."), "" otherwise. */
std::string refresh_failure(const RefreshStatus &status, RefreshPhase phase);
/* What stops a refresh before the password is asked, as a status line ("" nothing): the update helper missing, no
 * network, a launcher update or a refresh running. */
std::string refresh_blocker(bool helper_installed, bool network, bool update_running, bool refresh_running);

/* Facts from commands' output. */
bool package_installed(const std::string &dpkg_query_output, const std::string &name);  /* "name ii " lines */
std::string periodic_value(const std::string &apt_config_dump, const std::string &key); /* 'APT::Periodic::X "7";' -> 7 */

/* The page's package rows. */
struct PackageRow {
    std::string label;
    std::string value;                /* the full text: "2.41-12 > 2.41-12+deb13u1" */
    std::vector<std::string> values;  /* the same, then shorter ones for a narrow row (longest first; values[0] == value):
                                         "2.41-12 > 2.41-12+deb13u1", "> 2.41-12+deb13u1", "...12+deb13u1" */
};

std::string section_title(const char *name, std::size_t count);
PackageRow package_row(const Package &package);
/* The terminal-command page's text: what to run and why, for the boot chain and the packages apt keeps back. */
std::string terminal_text(const Report &report);
/* The grey line under the status line: later release, until then the terminal. */
const char *info_line();

} // namespace launcher_apt
