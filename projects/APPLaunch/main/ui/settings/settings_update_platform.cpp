/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates: the system side of the check (see settings_update_platform.hpp).
 */

#include "settings_update_platform.hpp"

#include "apps_backend.hpp"
#include "apps_status_model.hpp"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

#include <pwd.h>
#include <unistd.h>

#define UPDATE_STRINGIFY_IMPL(value) #value
#define UPDATE_STRINGIFY(value) UPDATE_STRINGIFY_IMPL(value)

namespace launcher_update {
namespace platform {

namespace {

constexpr int kCurlMaxTimeS = 20;     /* report 047 section 5.1 */
constexpr int kProcessTimeoutS = 30;  /* curl's own limit plus its start */
constexpr const char *kDocDir = "/usr/share/doc/cyberdeckzero-launcher";

std::string home_dir()
{
    const char *home = std::getenv("HOME");
    if (home && home[0]) return home;
    const passwd *entry = ::getpwuid(::getuid());
    return entry && entry->pw_dir ? entry->pw_dir : "/tmp";
}

std::string xdg_dir(const char *variable, const char *fallback)
{
    const char *value = std::getenv(variable);
    if (value && value[0] == '/') return value;
    return home_dir() + "/" + fallback;
}

std::string cache_dir()
{
    return xdg_dir("XDG_CACHE_HOME", ".cache") + "/cyberdeckzero/update";
}

/* "...\nhttp_code=304\n" (curl -w) -> 304; 0 when absent. */
int http_code_of(const std::string &output)
{
    const std::string key = "http_code=";
    const std::size_t at = output.rfind(key);
    if (at == std::string::npos) return 0;
    int code = 0;
    for (std::size_t i = at + key.size(); i < output.size() && output[i] >= '0' && output[i] <= '9'; ++i)
        code = code * 10 + (output[i] - '0');
    return code;
}

} // namespace

Build installed_build()
{
    Build build;
#ifdef LAUNCHER_VERSION_RAW
    build.version = UPDATE_STRINGIFY(LAUNCHER_VERSION_RAW);
#else
    build.version = "0.0.0-dev";
#endif
#ifdef LAUNCHER_CHANNEL_RAW
    build.channel = UPDATE_STRINGIFY(LAUNCHER_CHANNEL_RAW);
#else
    build.channel = "development";
#endif
    build.arch = apps_backend::system_arch();
    return build;
}

std::string releases_url()
{
    // APPLAUNCH_UPDATE_URL: a test hook (report 047 section 11): a local fake release server instead of GitHub, for the
    // PC tests and the boards (tests/fixtures/update/fake_release_server.py). Any URL curl takes.
    const char *forced = std::getenv("APPLAUNCH_UPDATE_URL");
    if (forced && forced[0]) return forced;
    return kDefaultReleasesUrl;
}

bool network_up()
{
    std::ifstream route("/proc/net/route");
    std::stringstream text;
    text << route.rdbuf();
    return apps_status::has_default_route(text.str());
}

bool clock_synced(const std::string &url)
{
    if (url.compare(0, 6, "https:") != 0) return true; /* no certificate to check */
    const apps_backend::Result result =
        apps_backend::run_program("timedatectl", {"show", "-p", "NTPSynchronized", "--value"}, 10);
    if (result.rc != 0) return true; /* no timedated (a PC without systemd): let curl try */
    return result.out.find("yes") != std::string::npos;
}

FetchResult fetch(const std::string &url, const std::string &etag, const std::string &user_agent)
{
    FetchResult fetched;
    const std::string dir = cache_dir();
    const std::string suffix = "." + std::to_string(static_cast<long long>(::getpid()));
    const std::string headers_path = dir + "/fetch.headers" + suffix;
    const std::string body_path = dir + "/fetch.body" + suffix;
    if (!write_file_atomic(headers_path, std::string())) { /* creates the folder */
        fetched.curl_rc = 23; /* curl's "write error" */
        return fetched;
    }
    std::vector<std::string> args = {"-sS", "-L", "--max-time", std::to_string(kCurlMaxTimeS), "-A", user_agent,
                                     "-H", "Accept: application/vnd.github+json"};
    if (!etag.empty()) {
        args.push_back("-H");
        args.push_back("If-None-Match: " + etag);
    }
    args.insert(args.end(), {"-D", headers_path, "-o", body_path, "-w", "\nhttp_code=%{http_code}\n", url});
    const apps_backend::Result result = apps_backend::run_program("curl", args, kProcessTimeoutS);
    fetched.curl_rc = result.rc;
    fetched.http_status = http_code_of(result.out);
    read_file(headers_path, fetched.headers);
    read_file(body_path, fetched.body);
    ::unlink(headers_path.c_str());
    ::unlink(body_path.c_str());
    return fetched;
}

std::string state_path()
{
    return xdg_dir("XDG_STATE_HOME", ".local/state") + "/cyberdeckzero/update.state";
}

std::string cache_path()
{
    return cache_dir() + "/releases.json";
}

std::string shipped_notes(const std::string &version)
{
    std::string text;
    if (version.empty() || version.find('/') != std::string::npos) return text;
    read_file(std::string(kDocDir) + "/release-notes-" + version + ".md", text);
    return text;
}

long long now()
{
    return static_cast<long long>(std::time(nullptr));
}

} // namespace platform
} // namespace launcher_update
