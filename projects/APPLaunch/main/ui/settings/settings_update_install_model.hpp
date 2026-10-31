/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates, phase P2 of report 047 (the launcher downloads and installs a newer release, with an automatic
 * rollback): the rules, with no LVGL, no thread and no process. Unit-tested in
 * projects/APPLaunch/tests/test_settings_update_install_model.cpp.
 *
 *   download     the offered release's bundle for this architecture, its .sha256 asset and GitHub's digest; the hash
 *                check (the file's own hash against both: at least one must exist, every one there must agree; D5)
 *   guards       free space before the download (300 MB, or four times a bigger bundle: the tarball, root's copy, the
 *                unpacked bundle and the backup; report 047 section 5.4), a battery board under 50 % that is not
 *                charging (D16)
 *   root command the ONE command the shared sudo prompt runs: systemd-run --unit=cyberdeckzero-update --collect --quiet
 *                /usr/share/APPLaunch/bin/cyberdeckzero-update apply ... (the work runs in a transient system unit,
 *                never in the launcher's cgroup, section 5.5); every value checked here (the helper checks them again)
 *   status       the helper's status file /var/lib/cyberdeckzero-update/status (pizero2w/cyberdeckzero-update): what
 *                phase it is in, whether the running launcher must exit with status 76 (desktop boards), the progress
 *                page's steps, the result line and the rollback toast (once per helper run)
 */
#pragma once

#include "settings_update_model.hpp"

#include <string>
#include <vector>

namespace launcher_update {

constexpr const char *kHelperPath = "/usr/share/APPLaunch/bin/cyberdeckzero-update";
constexpr const char *kHelperUnit = "cyberdeckzero-update";
constexpr const char *kHelperStatusPath = "/var/lib/cyberdeckzero-update/status";
constexpr long long kMinFreeBytes = 300LL * 1024 * 1024;

// ------------------------------------------------------------------ the download

struct Download {
    std::string version;      /* "0.8.13" */
    std::string name;         /* "cyberdeckzero-launcher-0.8.13-arm64.tar.gz" */
    std::string url;          /* browser_download_url */
    long long size = 0;
    std::string digest;       /* GitHub's sha256 of the bundle (hex), "" when it gives none */
    std::string checksum_url; /* the .sha256 asset, "" when the release has none */
};

/* The bundle of release `version` for `arch` in the list, with its checksum asset and digest. False when the release or
 * its bundle is missing, or the release is not one to install (a draft, a pre-release, no plain X.Y.Z tag). */
bool download_of(const std::vector<Release> &releases, const std::string &version, const std::string &arch,
                 Download &out);
/* "X.Y.Z" with digits only (what the helper accepts). */
bool plain_version(const std::string &text);
/* 64 lower-case hex digits. */
bool valid_sha256(const std::string &text);
/* "sha256:<hex>" (GitHub's digest field) -> the hex in lower case, "" for anything else. */
std::string digest_hex(const std::string &digest);
/* The .sha256 asset ("<hash> <name>", one or two spaces, or "<hash>" alone): the hash in lower case when the name is
 * absent or is `bundle_name` (a leading '*' of sha256sum's binary mode accepted); "" otherwise. */
std::string parse_checksum_file(const std::string &text, const std::string &bundle_name);

enum class HashCheck {
    Ok,          /* the file's hash equals every published one */
    Mismatch,    /* it differs from one of them: a broken download, a captive portal's page */
    Disagree,    /* the .sha256 asset and GitHub's digest differ from each other */
    NoReference, /* nothing published to check against */
};
HashCheck check_hash(const std::string &computed, const std::string &published, const std::string &api_digest);

// ------------------------------------------------------------------ guards before the download

/* The free space an update needs: 300 MB, or four times a bundle bigger than 75 MB. */
long long space_needed(long long bundle_size);
/* "" when free_bytes (< 0: unknown) is enough, else "Not enough space: need 300 MB, 120 MB free". */
std::string space_problem(long long bundle_size, long long free_bytes);
/* D16: battery_percent < 0 is a board without a battery (never refused). */
bool battery_too_low(int battery_percent, bool charging);
/* "57 MB" (whole MB, at least "1 MB" for anything above 0). */
std::string megabytes(long long bytes);
/* "23 of 57 MB" (done clamped to total; total 0: "23 MB"). */
std::string progress_text(long long done, long long total);
/* A failed download in words: curl's exit status (-2: our own time limit) and the HTTP status. */
std::string download_failure_text(int curl_rc, int http_status);

// ------------------------------------------------------------------ the root command

struct ApplyRequest {
    std::string version;   /* the release to install */
    std::string from;      /* the running launcher */
    std::string sha256;    /* the checked hash of the file */
    std::string file;      /* the downloaded bundle */
    std::string user;      /* the launcher's user */
    std::string cache_dir; /* the folder the file must be in */
    std::string helper = kHelperPath;
};
/* systemd-run --unit=cyberdeckzero-update --collect --quiet <helper> apply --version V --from-version F --sha256 H
 * --file PATH --user NAME; empty when a value is not acceptable: versions that are not X.Y.Z, a hash that is not 64 hex
 * digits, a file outside cache_dir (or with "..", or not absolute), a user name with anything but letters, digits,
 * '.', '_', '-' (or starting with '-'), an empty helper path. */
std::vector<std::string> apply_argv(const ApplyRequest &request);
/* systemd-run ... <helper> resume --user NAME; empty for a bad user name. */
std::vector<std::string> resume_argv(const std::string &user, const std::string &helper = kHelperPath);

// ------------------------------------------------------------------ the helper's status file

struct HelperStatus {
    bool present = false;
    std::string state;   /* running, done, refused, rolled-back, failed */
    std::string step;    /* check, plan, backup, install, restart, health, rollback */
    std::string from, to, variant, user, boot, restart, log, reason;
    long long pid = 0, launcher_pid = 0, started = 0, updated = 0;
    bool backup = false;
};
/* key=value lines (a v=1 file); unknown keys ignored; present = a v=1 file with a state. */
HelperStatus parse_helper_status(const std::string &text);

enum class HelperPhase { None, Running, Done, Refused, RolledBack, Failed, Interrupted };
/* alive: the helper's pid is a live process of this boot (its boot= is the current boot id). A "running" status whose
 * helper is gone is Interrupted (a power cut, a kill). */
HelperPhase helper_phase(const HelperStatus &status, bool alive);

/* Desktop boards (report 047 5.6 step 8b): the helper asks the running launcher to exit with status 76 (its unit starts
 * it again without giving the desktop back): restart=launcher, the helper alive, this launcher is the "from" version
 * and the pid the helper named. */
bool restart_requested(const HelperStatus &status, bool alive, const std::string &running_version, long long my_pid);

/* The progress page: four steps with their state. */
enum class StepMark { Pending, Current, Done };
struct StepRow {
    std::string label; /* "Checking the download", "Backup of 0.8.12", "Installing 0.8.13", "Restart and check" */
    std::string value; /* "done", the current step's seconds ("45 s"), "" */
    StepMark mark = StepMark::Pending;
};
std::vector<StepRow> progress_rows(const HelperStatus &status, long long now);
/* The progress page's status line: what happens now, and "do not switch off". */
std::string progress_caption(const HelperStatus &status);

/* The progress bars (0..100). The download's: the bytes on disk against the asset's size, rounded, 100 only when the
 * file is whole (0 when the size is unknown). The
 * Updating page's: the run's steps, never a guessed percentage inside a step (Installing keeps its seconds counter):
 * checking the download 25, backup 50, installing 75, restart and check 100 (a rollback too: the run's last step). */
int download_percent(long long done, long long total);
int step_percent(const HelperStatus &status);

/* A finished or interrupted update of a release newer than the running launcher (status.to above running), in words
 * for the Launcher level's status line: "Update to 0.8.13 failed: rolled back to 0.8.12", "0.8.13 not installed:
 * <reason>", "Update to 0.8.13 interrupted: Repair puts 0.8.12 back", ...; "" for none (and for a done update: the
 * start toast of P1 says it). */
std::string result_text(const HelperStatus &status, HelperPhase phase, const std::string &running_version);
/* The short value of the "Last update" row: "rolled back", "not installed", "failed", "interrupted"; "" for none. */
std::string result_value(const HelperStatus &status, HelperPhase phase, const std::string &running_version);
/* The same as one status line (about 40 characters, the reason is in Show log): "Update to 0.8.13 failed: 0.8.12 put
 * back", "0.8.13 not installed: see Show log", "Update to 0.8.13 failed: see Show log", "Update to 0.8.13 interrupted:
 * Repair it"; "" for none. */
std::string result_caption(const HelperStatus &status, HelperPhase phase, const std::string &running_version);

/* What stops an install before the download starts, as one status line ("" nothing): the helper missing, an update
 * running or interrupted, a battery under 50 % not charging (D16), not enough free space (bundle_size against
 * free_bytes, -1 unknown), no network. */
struct InstallGuards {
    bool helper_installed = true;
    HelperPhase helper = HelperPhase::None;
    int battery_percent = -1; /* -1: no battery */
    bool charging = false;
    long long bundle_size = 0;
    long long free_bytes = -1;
    bool network = true;
};
std::string install_blocker(const InstallGuards &guards);

/* The toast after a rollback, once per helper run (state.rollback_seen keeps its start time): "Update to 0.8.13
 * failed\nRolled back to 0.8.12", when the running launcher is the version rolled back to. "" otherwise. */
std::string take_rollback_notice(State &state, const HelperStatus &status, HelperPhase phase,
                                 const std::string &running_version);

/* The downloaded bundles of `dir` (cyberdeckzero-launcher-*.tar.gz and .part) other than `keep`: the names to delete
 * (an earlier download, a release already installed). */
std::vector<std::string> stale_downloads(const std::vector<std::string> &names, const std::string &keep);

} // namespace launcher_update
