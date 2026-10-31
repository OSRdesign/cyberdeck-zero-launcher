/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates (report 047 P1): what the check needs from the system, in one file the render harness replaces
 * with fixtures (tests/render/src/harness_update.cpp; the Makefile leaves this file's .cpp out). Worker threads only
 * for fetch() and clock_synced(): they run programs.
 *
 *   installed_build  the running launcher: LAUNCHER_VERSION_RAW, LAUNCHER_CHANNEL_RAW, the architecture
 *                    (apps_backend::system_arch(), APPLAUNCH_ARCH in tests)
 *   releases_url     GitHub's release list of the launcher repository; APPLAUNCH_UPDATE_URL replaces it (a test hook:
 *                    a local fake release server for the PC tests and the boards, e.g. http://192.168.1.10:8047/releases
 *                    served by projects/APPLaunch/tests/fixtures/update/fake_release_server.py)
 *   network_up       a default route (/proc/net/route, the Settings > Apps rule)
 *   clock_synced     for an https address: timedated's NTPSynchronized (timedatectl), so a board without a clock
 *                    (the Pi boards) does not see GitHub's certificate as "not yet valid"; true when it cannot tell and
 *                    for a plain http address
 *   fetch            curl as the user: -sS -L --max-time 20, the User-Agent cyberdeckzero-launcher/<version>, GitHub's
 *                    Accept header, If-None-Match with the ETag of the cached answer; the headers and the body through
 *                    files in the cache folder; never --fail, so a 403 keeps its headers (the rate limit)
 *   state_path       $XDG_STATE_HOME (~/.local/state)/cyberdeckzero/update.state
 *   cache_path       $XDG_CACHE_HOME (~/.cache)/cyberdeckzero/update/releases.json: the last answer, so the notes of
 *                    the skipped versions stay readable offline after the update (report 047 section 6.4)
 *   shipped_notes    the running version's notes as the bundle installs them:
 *                    /usr/share/doc/cyberdeckzero-launcher/release-notes-<version>.md (pizero2w/build.sh, 5.9)
 *   now              the wall clock, epoch seconds
 *
 * Phase P2 (the download and the install, settings_update_download.hpp, settings_update_install.hpp):
 *   download_dir     $XDG_CACHE_HOME (~/.cache)/cyberdeckzero/update: the bundle is downloaded here, as the user, never
 *                    to /tmp (a RAM disk on the 512 MB boards; report 047 section 5.4)
 *   download         curl -fsSL -C - --max-time 900 (resumes a .part file), cancellable: SIGTERM to its process group,
 *                    then SIGKILL; the HTTP status of the last response
 *   fetch_text       a small file (the .sha256 asset): curl -fsSL --max-time 60
 *   file_size, remove_file, rename_file, list_dir   the download folder
 *   sha256_of        sha256sum of a file (coreutils: every board has it; no hash code in the launcher)
 *   free_bytes       statvfs of the nearest existing folder (-1 unknown)
 *   battery          the launcher's own status feed (status.state): percent (-1: no battery) and charging (D16)
 *   user_name        the launcher's user (getpwuid)
 *   helper_installed /usr/share/APPLaunch/bin/cyberdeckzero-update is there and executable
 *   helper_status    /var/lib/cyberdeckzero-update/status, "" when absent
 *   boot_id, process_alive, self_pid   whether the helper that wrote the status still runs, in this boot
 *   read_log         the end of the helper's log (world-readable, /var/log/cyberdeckzero/update-<date>.log)
 */
#pragma once

#include "settings_update_model.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace launcher_update {
namespace platform {

struct FetchResult {
    int curl_rc = -1;    /* curl's exit status; 127 not installed, -2 our own timeout */
    int http_status = 0; /* the last response's status, 0 none */
    std::string headers; /* curl -D: every response's headers (redirects included) */
    std::string body;
};

Build installed_build();
std::string releases_url();
bool network_up();
bool clock_synced(const std::string &url);
FetchResult fetch(const std::string &url, const std::string &etag, const std::string &user_agent);
std::string state_path();
std::string cache_path();
std::string shipped_notes(const std::string &version);
long long now();

/* P2 */
constexpr int kCancelled = -3; /* download(): stopped by cancel */
struct DownloadResult {
    int curl_rc = -1;    /* curl's exit status; 127 not installed, -2 our own timeout, kCancelled */
    int http_status = 0; /* the last response's status, 0 none */
};
std::string download_dir();
DownloadResult download(const std::string &url, const std::string &path, const std::string &user_agent,
                        const std::atomic<bool> &cancel);
FetchResult fetch_text(const std::string &url, const std::string &user_agent);
long long file_size(const std::string &path); /* -1: none */
bool remove_file(const std::string &path);
bool rename_file(const std::string &from, const std::string &to);
std::vector<std::string> list_dir(const std::string &dir);
std::string sha256_of(const std::string &path); /* "" on failure */
long long free_bytes(const std::string &dir);
int battery(bool &charging);
std::string user_name();
bool helper_installed();
std::string helper_status();
std::string boot_id();
bool process_alive(long long pid);
long long self_pid();
std::string read_log(const std::string &path, std::size_t max_bytes);

} // namespace platform
} // namespace launcher_update
