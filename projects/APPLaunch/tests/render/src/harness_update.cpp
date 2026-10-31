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
 */

#include "harness.h"

#include "settings/settings_update_platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>

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

} // namespace platform
} // namespace launcher_update
