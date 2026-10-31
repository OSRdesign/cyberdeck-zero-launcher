/* SPDX-License-Identifier: MIT
 *
 * Render harness: Settings > Updates' system side (report 047 P1) from the scene's fixtures, instead of
 * settings_update_platform.cpp (the Makefile leaves that file out): no curl, no timedatectl, never the user's own state
 * or cache folders. The check itself (settings_update_check.cpp: its worker thread, the outcome map, the state file,
 * the cached answer) and the page are the launcher's real code.
 *
 *   installed_build  the scene's "update build VERSION CHANNEL [ARCH]" (default 0.8.12 release arm64)
 *   network_up       "update net on|off" (default off: no other scene ever checks)
 *   clock_synced     "update clock on|off" (default on)
 *   fetch            "update answer ...": a file of projects/APPLaunch/tests/fixtures/update (200 with ok.headers),
 *                    pending (the worker waits until the scene gives another answer: "Checking"), ratelimit (403 with
 *                    rate-limit.headers and its body), error (500), dns (curl 6), notjson (200, a captive portal page)
 *   state / cache    out/<WxH>/runtime/update/{update.state,releases.json}: emptied at the first use in a run, then the
 *                    scene's "update state KEY VALUE" lines and "update cache FILE" written there
 *   shipped_notes    the repository's docs/dev/release-notes-<version>.md (what the bundle installs)
 *
 * P2 (the download and the root helper), never a real file, process or curl:
 *   download         "update download done|fail|pending|PERCENT": done ends at once (curl 0), fail as a 404 (curl 22),
 *                    pending and a percent hold the worker until the scene changes it (or the page stops it); the
 *                    .part's size is the percent of the asset's (file_size)
 *   fetch_text / sha256_of   "update checksum ok|bad": the .sha256 asset names the fixture's digest; the file's own hash
 *                    is that digest (ok) or another one (bad: "damaged download")
 *   helper_status    "update helper KEY VALUE" lines (v=1 and boot= added; "now-N" values: the scene clock), none
 *                    without them; process_alive "update helper-alive on|off"; helper_installed "update
 *                    helper-installed on|off"; read_log "update log FILE" (a file of the fixture folder)
 *   battery, free_bytes   "update battery PERCENT|none [charging]", "update space MB"
 */

#include "harness.h"

#include "settings/settings_update_platform.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

namespace launcher_update {
namespace platform {

namespace {

std::string update_dir()
{
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    return std::string(runtime && runtime[0] ? runtime : "/tmp/harness-runtime") + "/update";
}

std::string fixture_dir()
{
    return harness::fixtures().fixture_root + "/../../fixtures/update";
}

std::string fixture(const std::string &name)
{
    std::string text;
    if (name.empty() || name.find('/') != std::string::npos || !read_file(fixture_dir() + "/" + name, text))
        std::fprintf(stderr, "[harness] update: no fixture %s\n", name.c_str());
    return text;
}

/* The first use in this run: an empty folder, then the scene's seeds. */
void prepare()
{
    static std::once_flag once;
    std::call_once(once, [] {
        const std::string dir = update_dir();
        ::unlink((dir + "/update.state").c_str());
        ::unlink((dir + "/releases.json").c_str());
        harness::Fixtures &fx = harness::fixtures();
        std::lock_guard<std::mutex> lock(fx.update_mutex);
        if (!fx.update_seed_state.empty()) {
            std::string text;
            const long long now = static_cast<long long>(std::time(nullptr));
            for (const auto &kv : fx.update_seed_state) {
                std::string value = kv.second;
                if (value.compare(0, 3, "now") == 0) value = std::to_string(now + std::atoll(value.c_str() + 3));
                text += kv.first + "=" + value + "\n";
            }
            write_file_atomic(dir + "/update.state", text);
        }
        if (!fx.update_seed_cache.empty()) write_file_atomic(dir + "/releases.json", fixture(fx.update_seed_cache));
    });
}

} // namespace

Build installed_build()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    return Build{fx.update_version, fx.update_channel, fx.update_arch};
}

std::string releases_url()
{
    return kDefaultReleasesUrl; /* never fetched: fetch() answers from the fixtures */
}

bool network_up()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    return fx.update_network;
}

bool clock_synced(const std::string &)
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    return fx.update_clock;
}

FetchResult fetch(const std::string &, const std::string &, const std::string &)
{
    harness::Fixtures &fx = harness::fixtures();
    std::string answer;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(fx.update_mutex);
            answer = fx.update_answer;
        }
        if (answer != "pending") break;
        ::usleep(2000); /* the scene gives the answer later ("Checking") */
    }
    FetchResult result;
    result.curl_rc = 0;
    if (answer == "dns") {
        result.curl_rc = 6;
    } else if (answer == "ratelimit") {
        result.http_status = 403;
        result.headers = fixture("rate-limit.headers");
        result.body = fixture("rate-limit.json");
    } else if (answer == "error") {
        result.http_status = 500;
        result.body = "{\"message\":\"Server Error\"}";
    } else if (answer == "notjson") {
        result.http_status = 200;
        result.body = "<html>captive portal</html>";
    } else {
        result.http_status = 200;
        result.headers = fixture("ok.headers");
        result.body = fixture(answer);
    }
    return result;
}

std::string state_path()
{
    prepare();
    return update_dir() + "/update.state";
}

std::string cache_path()
{
    prepare();
    return update_dir() + "/releases.json";
}

std::string shipped_notes(const std::string &version)
{
    std::string text;
    read_file(harness::fixtures().resource_root + "/../../../docs/dev/release-notes-" + version + ".md", text);
    return text;
}

long long now()
{
    return static_cast<long long>(std::time(nullptr)); /* the scene's clock */
}

// ------------------------------------------------------------------ P2

namespace {

std::string fixture_value(std::string value)
{
    if (value.compare(0, 3, "now") == 0)
        value = std::to_string(static_cast<long long>(std::time(nullptr)) + std::atoll(value.c_str() + 3));
    return value;
}

bool ends_with(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

std::string download_dir()
{
    return update_dir() + "/downloads";
}

DownloadResult download(const std::string &, const std::string &, const std::string &, const std::atomic<bool> &cancel)
{
    harness::Fixtures &fx = harness::fixtures();
    for (;;) {
        std::string state;
        {
            std::lock_guard<std::mutex> lock(fx.update_mutex);
            state = fx.update_download;
        }
        DownloadResult result;
        if (cancel.load()) {
            result.curl_rc = kCancelled;
            return result;
        }
        if (state == "done") {
            result.curl_rc = 0;
            result.http_status = 200;
            return result;
        }
        if (state == "fail") {
            result.curl_rc = 22;
            result.http_status = 404;
            return result;
        }
        ::usleep(2000); /* pending or a percent: the scene finishes it later */
    }
}

FetchResult fetch_text(const std::string &url, const std::string &)
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    std::string name = url.substr(url.rfind('/') + 1);
    if (ends_with(name, ".sha256")) name.erase(name.size() - 7);
    FetchResult result;
    result.curl_rc = 0;
    result.http_status = 200;
    result.body = fx.update_digest + " " + name + "\n";
    return result;
}

long long file_size(const std::string &path)
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    if (ends_with(path, ".part")) {
        if (fx.update_download == "done") return fx.update_asset_size;
        const int percent = std::atoi(fx.update_download.c_str());
        return fx.update_asset_size * percent / 100;
    }
    return fx.update_download == "done" ? fx.update_asset_size : -1;
}

bool remove_file(const std::string &)
{
    return true;
}

bool rename_file(const std::string &, const std::string &)
{
    return true;
}

std::vector<std::string> list_dir(const std::string &)
{
    return {};
}

std::string sha256_of(const std::string &)
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    return fx.update_checksum == "ok" ? fx.update_digest : std::string(64, '0');
}

long long free_bytes(const std::string &)
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    return fx.update_space_mb * 1024LL * 1024LL;
}

int battery(bool &charging)
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    charging = fx.update_charging;
    return fx.update_battery;
}

std::string user_name()
{
    return "pi";
}

bool helper_installed()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    return fx.update_helper_installed;
}

std::string helper_status()
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    if (fx.update_helper.empty()) return std::string();
    std::string text = "v=1\nboot=harness-boot\npid=4000\n";
    for (const auto &kv : fx.update_helper) text += kv.first + "=" + fixture_value(kv.second) + "\n";
    return text;
}

std::string boot_id()
{
    return "harness-boot";
}

bool process_alive(long long pid)
{
    harness::Fixtures &fx = harness::fixtures();
    std::lock_guard<std::mutex> lock(fx.update_mutex);
    return pid > 0 && fx.update_helper_alive;
}

long long self_pid()
{
    return 4242;
}

std::string read_log(const std::string &, std::size_t)
{
    std::string name;
    {
        harness::Fixtures &fx = harness::fixtures();
        std::lock_guard<std::mutex> lock(fx.update_mutex);
        name = fx.update_log;
    }
    return name.empty() ? std::string() : fixture(name);
}

} // namespace platform
} // namespace launcher_update
