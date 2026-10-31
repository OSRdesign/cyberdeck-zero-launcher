/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates: the system side of the check (see settings_update_platform.hpp).
 */

#include "settings_update_platform.hpp"

#include "apps_backend.hpp"
#include "apps_status_model.hpp"
#include "settings_update_install_model.hpp"

#include "cp0_app_contract.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iterator>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
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

// ------------------------------------------------------------------ P2: the download and the root helper's traces

namespace {

constexpr int kDownloadMaxTimeS = 900;  /* report 047 section 5.4: the Store's value */
constexpr int kChecksumMaxTimeS = 60;
constexpr const char *kStatusPath = kHelperStatusPath;

/* curl ... -w "\nhttp_code=%{http_code}\n" as the user, its own process group; the output (small: -s) read until it
 * exits, `cancel` checked every 200 ms (SIGTERM to the group, SIGKILL 5 s later). */
DownloadResult run_curl(const std::vector<std::string> &args, const std::atomic<bool> *cancel, int timeout_s)
{
    DownloadResult result;
    int pipe_fd[2];
    if (::pipe2(pipe_fd, O_CLOEXEC) != 0) return result;
    std::vector<char *> argv;
    static char curl_name[] = "curl";
    argv.push_back(curl_name);
    for (const std::string &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return result;
    }
    if (pid == 0) {
        ::setpgid(0, 0);
        ::dup2(pipe_fd[1], STDOUT_FILENO);
        ::dup2(pipe_fd[1], STDERR_FILENO);
        const int null_fd = ::open("/dev/null", O_RDONLY);
        if (null_fd >= 0) ::dup2(null_fd, STDIN_FILENO);
        ::execvp("curl", argv.data());
        ::_exit(127);
    }
    ::close(pipe_fd[1]);
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    bool timed_out = false;
    bool cancelled = false;
    char buffer[1024];
    for (;;) {
        if (cancel && cancel->load()) {
            cancelled = true;
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            break;
        }
        pollfd p{pipe_fd[0], POLLIN, 0};
        const int ready = ::poll(&p, 1, 200);
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        const ssize_t got = ::read(pipe_fd[0], buffer, sizeof(buffer));
        if (got <= 0) break;
        if (out.size() < 16384) out.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(pipe_fd[0]);
    if (cancelled || timed_out) ::kill(-pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 100; ++i) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0) {
            status = -1;
            break;
        }
        if (i == 50) ::kill(-pid, SIGKILL);
        ::usleep(50 * 1000);
    }
    result.curl_rc = cancelled ? kCancelled : timed_out ? -2 : (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    result.http_status = http_code_of(out);
    return result;
}

} // namespace

std::string download_dir()
{
    return cache_dir();
}

DownloadResult download(const std::string &url, const std::string &path, const std::string &user_agent,
                        const std::atomic<bool> &cancel)
{
    if (!write_file_atomic(path + ".probe", std::string())) { /* creates the folder */
        DownloadResult failed;
        failed.curl_rc = 23;
        return failed;
    }
    ::unlink((path + ".probe").c_str());
    return run_curl({"-fsSL", "-C", "-", "--max-time", std::to_string(kDownloadMaxTimeS), "-A", user_agent, "-o",
                     path, "-w", "\nhttp_code=%{http_code}\n", url},
                    &cancel, kDownloadMaxTimeS + 30);
}

FetchResult fetch_text(const std::string &url, const std::string &user_agent)
{
    FetchResult fetched;
    const std::string path = cache_dir() + "/fetch.text." + std::to_string(static_cast<long long>(::getpid()));
    if (!write_file_atomic(path, std::string())) {
        fetched.curl_rc = 23;
        return fetched;
    }
    const DownloadResult result =
        run_curl({"-fsSL", "--max-time", std::to_string(kChecksumMaxTimeS), "-A", user_agent, "-o", path, "-w",
                  "\nhttp_code=%{http_code}\n", url},
                 nullptr, kChecksumMaxTimeS + 30);
    fetched.curl_rc = result.curl_rc;
    fetched.http_status = result.http_status;
    read_file(path, fetched.body);
    ::unlink(path.c_str());
    return fetched;
}

long long file_size(const std::string &path)
{
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return static_cast<long long>(st.st_size);
}

bool remove_file(const std::string &path)
{
    return ::unlink(path.c_str()) == 0 || errno == ENOENT;
}

bool rename_file(const std::string &from, const std::string &to)
{
    return ::rename(from.c_str(), to.c_str()) == 0;
}

std::vector<std::string> list_dir(const std::string &dir)
{
    std::vector<std::string> names;
    DIR *d = ::opendir(dir.c_str());
    if (!d) return names;
    while (dirent *entry = ::readdir(d)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") names.push_back(name);
    }
    ::closedir(d);
    return names;
}

std::string sha256_of(const std::string &path)
{
    const apps_backend::Result result = apps_backend::run_program("sha256sum", {"--", path}, 120);
    if (result.rc != 0) return std::string();
    const std::string hash = result.out.substr(0, result.out.find_first_of(" \t\n"));
    return valid_sha256(hash) ? hash : std::string();
}

long long free_bytes(const std::string &dir)
{
    std::string path = dir;
    while (!path.empty()) {
        struct statvfs vfs {};
        if (::statvfs(path.c_str(), &vfs) == 0)
            return static_cast<long long>(vfs.f_bavail) * static_cast<long long>(vfs.f_frsize);
        const std::size_t slash = path.rfind('/');
        if (slash == std::string::npos) break;
        path = slash == 0 ? (path == "/" ? std::string() : std::string("/")) : path.substr(0, slash);
    }
    return -1;
}

int battery(bool &charging)
{
    charging = false;
    cp0_app_status_t status {};
    if (cp0_app_status_read(nullptr, &status) != 0) return -1; /* no feed: nothing known, never refused */
    charging = status.battery_charging != 0 || std::string(status.battery_state) == "Full";
    return status.battery;
}

std::string user_name()
{
    const passwd *entry = ::getpwuid(::getuid());
    return entry && entry->pw_name ? entry->pw_name : std::string();
}

bool helper_installed()
{
    return ::access(kHelperPath, X_OK) == 0;
}

std::string helper_status()
{
    std::string text;
    read_file(kStatusPath, text);
    return text;
}

std::string boot_id()
{
    std::string text;
    read_file("/proc/sys/kernel/random/boot_id", text);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

bool process_alive(long long pid)
{
    if (pid <= 0) return false;
    struct stat st {};
    return ::stat(("/proc/" + std::to_string(pid)).c_str(), &st) == 0;
}

long long self_pid()
{
    return static_cast<long long>(::getpid());
}

std::string read_log(const std::string &path, std::size_t max_bytes)
{
    if (path.empty() || path[0] != '/' || path.find("..") != std::string::npos) return std::string();
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const std::streamoff start = size > static_cast<std::streamoff>(max_bytes) ? size - static_cast<std::streamoff>(max_bytes) : 0;
    in.seekg(start);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (start > 0) {
        const std::size_t newline = text.find('\n');
        if (newline != std::string::npos) text.erase(0, newline + 1); /* from a whole line */
    }
    return text;
}

} // namespace platform
} // namespace launcher_update
