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
 */
#pragma once

#include "settings_update_model.hpp"

#include <string>

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

} // namespace platform
} // namespace launcher_update
