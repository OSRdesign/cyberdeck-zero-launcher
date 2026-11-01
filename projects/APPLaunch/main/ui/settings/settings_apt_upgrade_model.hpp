/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > Packages, phase P4 of report 047 (the launcher upgrades the safe set of system packages): the
 * rules, with no LVGL, no thread and no process. Unit-tested in projects/APPLaunch/tests/test_settings_apt_upgrade_model.cpp
 * (on the P0 board captures of tests/fixtures/apt/<board>/, like P3).
 *
 *   plan       what "Upgrade N packages" installs: the CLOSURE apt really performs. The helper runs
 *              `apt-get install --only-upgrade --no-remove --no-install-recommends LIST` and refuses unless its simulation
 *              upgrades exactly LIST; apt pulls more than the list (a dependency, or since apt 2.7 the other installed
 *              packages of the same source: installing libcurl3t64-gnutls also upgrades curl and libcurl4t64). So the
 *              launcher simulates the very same command (as the user: no lock) on the safe set and iterates to a fixpoint
 *              (close_plan): a package whose upgrade pulls in a launcher library moves to "Launcher libraries" (the switch
 *              includes it with them), one that pulls in anything never upgraded here (kernel, firmware, a Store app, a
 *              held, kept-back or phased package, a new package, a removal) moves to "Needs the terminal", until the
 *              simulation of the plan upgrades exactly the plan: the helper's own rule (closure_problem), so the two
 *              cannot disagree on the same apt state. The plan with the libraries is closed the same way.
 *   sizes      the download (`apt-get --print-uris`: the size field of each line), what the installed packages grow by
 *              (Installed-Size of the candidates from `apt-cache show`, against dpkg's installed sizes), and the disk the
 *              run needs: download + growth + a margin (64 MB, or the largest package: dpkg keeps its two copies for a
 *              moment). The helper computes the same before it downloads.
 *   guards     what refuses an upgrade before the password: the helper missing, another package job (Settings > Apps, a
 *              launcher update, a refresh), an interrupted run to repair first, lists that say little (D12), under-voltage
 *              now, a battery under 50 % not charging (D16), the space, no network
 *   power      the firmware's throttled bits (vcgencmd get_throttled, /sys/.../get_throttled): bit 0 = under-voltage now
 *   root       the ONE command the shared sudo prompt runs: systemd-run --unit=cyberdeckzero-apt --collect --quiet <helper>
 *              apt-upgrade --user NAME [--libraries] -- PACKAGE... (or apt-repair --user NAME): every name checked here,
 *              and again by the helper
 *   status     the helper's /var/lib/cyberdeckzero-update/apt-upgrade-status: the progress page's steps and bar
 *              ("Downloading 12 of 40", "Installing 12 of 40"), whether Cancel still works (the download only), the result
 *              page's text, and whether a repair is needed (a run that stopped while dpkg worked, or dpkg --audit)
 *   texts      the Packages page's action row, the confirmation page's text block, the switch row, the warning line
 */
#pragma once

#include "settings_apt_model.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace launcher_apt {

constexpr const char *kUpgradeStatusPath = "/var/lib/cyberdeckzero-update/apt-upgrade-status";
constexpr const char *kAnchorPackage = "cyberdeckzero-runtime";
constexpr long long kMarginBytes = 64LL * 1024 * 1024;
constexpr int kBatteryFloor = 50;
constexpr std::size_t kMaxPackages = 3000;
constexpr int kMaxSimulations = 600; /* a plan's closure: whole-plan simulations and one per suspect package */

// ------------------------------------------------------------------ the plan

struct Plan {
    bool ready = false;           /* computed (after the scan, in the background) */
    bool valid = false;           /* the closure converged: its simulation upgrades exactly its packages */
    bool libraries = false;       /* planned with the launcher libraries */
    std::string error;            /* why there is no plan: apt's E: line, or what apt pulls in that nothing explains */
    std::vector<Package> packages; /* what the run installs (name, installed version, new version), by name */
    int waiting_libraries = 0;    /* safe packages moved out because their upgrade pulls in a launcher library */
    int waiting_terminal = 0;     /* packages moved to the terminal (they pull in something never upgraded here) */
    long long download = -1;      /* bytes, -1 unknown */
    long long growth = 0;         /* bytes the installed packages grow by (never below 0) */
    long long largest = 0;        /* the largest candidate's installed size, bytes */

    int count() const { return static_cast<int>(packages.size()); }
    std::vector<std::string> names() const;
    long long need() const;       /* disk_needed(download, growth, largest) */
};

// ------------------------------------------------------------------ the closure

/* What `apt-get -s install --only-upgrade --no-remove --no-install-recommends NAMES` prints: the helper's second
 * simulation, the same command (as the user it needs no lock). */
using SimulateInstall = std::function<std::string(const std::vector<std::string> &names)>;
/* What `apt-cache show --no-all-versions NAMES` prints (the candidates' records: Source, Depends, Installed-Size). */
using CandidateRecords = std::function<std::string(const std::vector<std::string> &names)>;

struct Record {
    std::string source;               /* the source package ("" in the record: the same as the package) */
    std::vector<std::string> depends; /* Depends and Pre-Depends, names only */
};
std::map<std::string, Record> parse_records(const std::string &apt_cache_show);

/* THE rule, the helper's (pizero2w/cyberdeckzero-update, cmd_apt_upgrade) and the launcher's: the simulation of exactly
 * `names` upgrades exactly them (base names): no other package, none missing, no removal, no new package. "" when it
 * holds, else the first difference in words. */
std::string closure_problem(const Simulation &sim, const std::vector<std::string> &names);

/* What a package outside the plan is, when a plan's package pulls it in. */
enum class Outside { Library, Never };
struct Pull {
    std::string name;               /* the package taken out of the plan */
    std::vector<std::string> needs; /* what its upgrade pulls in that the plan cannot have ("" known: none, apt refused) */
    bool terminal = false;          /* something never upgraded here: it goes to "Needs the terminal" */
};
struct Closure {
    bool valid = false;
    std::string error;
    std::vector<std::string> names;  /* the closed plan, sorted */
    std::vector<Change> changes;     /* its upgrades as the last simulation printed them (versions) */
    std::vector<Pull> pulls;         /* taken out, in the order found */
    int simulations = 0;
};
struct ClosureHooks {
    SimulateInstall simulate;
    CandidateRecords records;                 /* may be empty: then every package is checked by its own simulation */
    std::function<void(int simulations)> progress; /* after each simulation (the waiting box's checks) */
    std::function<bool()> cancelled;          /* true: stop at once (the result is not used) */
    int max_simulations = kMaxSimulations;
};
/* The fixpoint: simulate `start`; while apt also upgrades a package outside the plan (or removes one, or installs a new
 * one, or leaves a listed one), find the plan's packages that pull it in (the suspects first: the same source as what was
 * pulled, or a Depends on it or on another suspect; each checked by its own simulation; every package when no suspect
 * explains it) and take them out (Pull.terminal when a pulled package is Never for `kind`); simulate again. Converges:
 * the plan only shrinks. Invalid when a whole-plan difference is explained by no single package (error says what). */
Closure close_plan(const std::vector<std::string> &start, const std::function<Outside(const std::string &)> &kind,
                   const ClosureHooks &hooks);

/* The two plans and the report as the Packages page shows them once the closure is known: the safe plan (closed against
 * the launcher libraries), the plan with the libraries (closed against everything else), and the report with the moved
 * packages in their new section (a package pulling a library: "Launcher libraries"; one pulling the boot chain, a Store
 * app, a held / kept-back / phased / new package: "Needs the terminal", its note "needs X"). Sizes are not filled. */
struct Planned {
    Report report;
    Plan plan;
    Plan plan_libraries;
};
Planned plan_upgrade(const Report &report, const ClosureHooks &hooks);

/* `apt-get --print-uris -qq install ...`: the sum of the size fields ("'URI' file SIZE hash"); -1 for no line at all
 * while `expected` > 0 (apt said nothing usable: unknown), 0 for none expected. */
long long parse_uris_bytes(const std::string &text, int expected);
/* `apt-cache show --no-all-versions ...`: the sum and the largest of the Installed-Size fields (KiB) of its records. */
void parse_candidate_sizes(const std::string &text, long long &sum_kib, long long &largest_kib);
/* `dpkg-query -W -f='${Installed-Size}\n' ...`: the sum of the numbers (KiB). */
long long parse_installed_kib(const std::string &text);
/* Fills download, growth and largest from the three outputs. */
void apply_sizes(Plan &plan, const std::string &uris, const std::string &candidates, const std::string &installed);
/* download (unknown counts as 0) + growth + max(64 MB, largest). */
long long disk_needed(long long download, long long growth, long long largest);
/* "a, b (>= 1), c | d" (a Depends field) -> a, b, c, d (no versions, no architecture). */
std::vector<std::string> parse_depends(const std::string &field);

// ------------------------------------------------------------------ power

struct Throttle {
    bool known = false;
    bool under_now = false;    /* bit 0 */
    bool under_before = false; /* bit 16: since the boot */
};
/* "throttled=0x50005", "0x50005" or "50005" (the firmware's hex). */
Throttle parse_throttled(const std::string &text);

// ------------------------------------------------------------------ guards

struct UpgradeGuards {
    bool helper_installed = true;
    std::string busy;           /* another package job, in words ("Settings > Apps is installing an app"), "" none */
    bool repair_needed = false; /* an interrupted run, or dpkg --audit */
    int packages = 0;           /* the plan's count */
    long long lists_time = 0;   /* the lists' age (P3), 0 never refreshed */
    long long now = 0;
    bool under_voltage = false;
    int battery_percent = -1;   /* -1: no battery */
    bool charging = false;
    long long need_bytes = 0;
    long long free_bytes = -1;  /* -1 unknown */
    bool network = true;
};
/* What stops an upgrade before the password, as one status line ("" nothing), most fundamental first. */
std::string upgrade_blocker(const UpgradeGuards &guards);
/* The same for a repair (no lists, no space, no network needed). */
std::string repair_blocker(const UpgradeGuards &guards);
bool battery_low(int battery_percent, bool charging);
/* "41 MB" (whole MB, at least "1 MB" above 0). */
std::string megabytes_text(long long bytes);

// ------------------------------------------------------------------ the root command

/* A Debian package name as the helper accepts it ("libc6", "libc6:armhf"). */
bool valid_package(const std::string &name);
/* systemd-run --unit=cyberdeckzero-apt --collect --quiet --description=... <helper> apt-upgrade --user NAME [--libraries]
 * -- PACKAGE...; empty for a bad user name, no package, too many, a name that is not one, a boot-chain package (D15). */
std::vector<std::string> upgrade_argv(const std::string &user, const std::vector<std::string> &packages, bool libraries,
                                      const std::string &helper = kHelperPath);
/* ... <helper> apt-repair --user NAME; empty for a bad user name. */
std::vector<std::string> repair_argv(const std::string &user, const std::string &helper = kHelperPath);

// ------------------------------------------------------------------ the helper's status file

struct UpgradeStatus {
    bool present = false;
    std::string job;    /* upgrade, repair */
    std::string state;  /* running, done, failed, refused, cancelled */
    std::string phase;  /* check, download, install, clean, repair */
    int done = -1, total = -1, percent = -1;
    std::string text, package, reason, reboot_why, log, boot, user;
    bool reboot = false, libraries = false;
    long long download = -1, need = -1, pid = 0, started = 0, updated = 0;
};
UpgradeStatus parse_upgrade_status(const std::string &text);

enum class UpgradePhase { None, Running, Done, Failed, Refused, Cancelled, Interrupted };
/* alive: the helper's pid is a live process of this boot. A running status whose helper is gone is Interrupted when it
 * stopped while dpkg worked (install, repair); one that stopped while checking or downloading changed nothing (None). */
UpgradePhase upgrade_phase(const UpgradeStatus &status, bool alive);
/* The Packages page offers "Repair the installation": an interrupted run, or dpkg reports unfinished packages. */
bool repair_needed(UpgradePhase phase, bool dpkg_unfinished);
/* Cancel (the row, Esc) works while the download runs, never while installing. */
bool cancellable(const UpgradeStatus &status, UpgradePhase phase);

/* The progress page's status line: "Downloading 12 of 40", "Installing 12 of 40", "Checking the plan", "Cleaning up",
 * "Finishing the interrupted installation"; "Cancelling the download" once asked. */
std::string progress_caption(const UpgradeStatus &status, bool cancel_asked);
/* The bar (0..100): the download's and the install's percent; -1 (no bar) for the repair, whose length is unknown. */
int progress_percent(const UpgradeStatus &status);

enum class StepState { Pending, Current, Done };
struct StepLine {
    std::string label; /* "Check the plan", "Download", "Install", "Clean up"; the repair: "Finish the installation" */
    std::string value; /* "12 of 40", "done", the seconds of a step without a count */
    StepState state = StepState::Pending;
};
std::vector<StepLine> progress_steps(const UpgradeStatus &status, long long now);
/* The steps' last line while installing: dpkg must not be stopped. */
const char *do_not_switch_off();

/* The result page's text: what happened in words (upgraded, upgraded and a restart advised, cancelled, not started and
 * why, failed and why, repaired), then for a failure the end of the log (log_tail: its last lines). */
std::string result_text(const UpgradeStatus &status, UpgradePhase phase, const std::string &log_tail);
/* The last `lines` lines of a text. */
std::string last_lines(const std::string &text, std::size_t lines);

// ------------------------------------------------------------------ the Packages page and the confirmation page

/* "Upgrade 40 packages" / "Upgrade 1 package"; "Upgrade packages" when only the launcher libraries could be. */
std::string upgrade_label(const Plan &plan);
/* "41 MB", the download (amber on the row); "" while the size is unknown. */
std::string upgrade_value(const Plan &plan);
/* The Packages page's status line once the launcher can upgrade the safe set: "Kernel and kept-back ones: in a terminal"
 * when the report has boot-chain or kept-back packages, "" otherwise (one status line at 480 px). */
std::string packages_line(const Report &report);
/* The switch row's label: "Launcher libraries (4)" (its value Off / On says whether they are included). */
std::string libraries_label(std::size_t libraries);
/* The switch row's value. */
const char *switch_value(bool on);
/* The status line while the switch is on (D14's warning, "Included: ..."; the text block explains it). */
const char *libraries_warning();
/* The status line while it is off: "Launcher libraries are not included". */
const char *libraries_off_line();
/* The confirmation page's read-only text: the count, the download, the disk needed, "keep the board plugged in"
 * (battery_board false) or the battery's 50 % rule, apps keep running until the next restart and a reboot may follow,
 * the install cannot be stopped once it starts and apt cannot undo it; with the libraries on, why that is riskier; the
 * packages left waiting for a launcher library. */
std::string confirm_text(const Plan &plan, bool battery_board, bool libraries_on, std::size_t libraries);

} // namespace launcher_apt
