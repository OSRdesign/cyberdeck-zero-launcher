/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > System packages: the system side of the scan (see settings_apt_platform.hpp).
 */

#include "settings_apt_platform.hpp"

#include "apps_backend.hpp"
#include "settings_apt_scan.hpp"
#include "settings_apt_upgrade_model.hpp"
#include "settings_update_model.hpp"
#include "settings_update_platform.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#include <cerrno>
#include <chrono>
#include <csignal>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

namespace launcher_apt {
namespace platform {

namespace {

constexpr int kSimulateTimeoutS = 180; /* the Zero 2 W reads a Pi 5-sized plan in some seconds; never wait longer */
constexpr int kQuickTimeoutS = 30;
constexpr std::size_t kPathsPerQuery = 50;

apps_backend::Result run_cancellable(const std::string &binary, const std::vector<std::string> &args, int timeout_s);

long long mtime_of(const std::string &path)
{
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 ? static_cast<long long>(info.st_mtime) : 0;
}

bool ends_with(const std::string &text, const char *suffix)
{
    const std::string s = suffix;
    return text.size() >= s.size() && text.compare(text.size() - s.size(), s.size(), s) == 0;
}

} // namespace

bool simulate(bool dist_upgrade, std::string &output)
{
    // -s: a simulation, as the user and without a lock; no network. Not -y (nothing to ask), no progress meter in a pipe.
    const apps_backend::Result result =
        run_cancellable("apt-get", {"-s", dist_upgrade ? "dist-upgrade" : "upgrade"}, kSimulateTimeoutS);
    output = result.out;
    return result.rc >= 0 && result.rc != 127;
}

std::vector<std::string> library_owners()
{
    std::vector<std::string> paths;
    const std::string app = kAppFolder;
    for (const std::string &binary : {app + "/bin/cyberdeckzero", app + "/bin/M5CardputerZero-AppStore",
                                      app + "/bin/cyberdeckzero-ctl", app + "/lib/libapplaunch_vfb.so"}) {
        if (::access(binary.c_str(), R_OK) != 0) continue;
        const apps_backend::Result ldd = apps_backend::run_program("ldd", {binary}, kQuickTimeoutS);
        for (const std::string &path : ldd_paths(ldd.out)) {
            char resolved[PATH_MAX];
            const std::string real = ::realpath(path.c_str(), resolved) ? std::string(resolved) : path;
            const std::string known = dpkg_path(real);
            if (std::find(paths.begin(), paths.end(), known) == paths.end()) paths.push_back(known);
        }
    }
    std::vector<std::string> owners;
    for (std::size_t at = 0; at < paths.size(); at += kPathsPerQuery) {
        std::vector<std::string> args = {"-S"};
        for (std::size_t i = at; i < paths.size() && i < at + kPathsPerQuery; ++i) args.push_back(paths[i]);
        const apps_backend::Result found = apps_backend::run_program("dpkg-query", args, kQuickTimeoutS);
        for (const std::string &name : parse_owners(found.out))
            if (std::find(owners.begin(), owners.end(), name) == owners.end()) owners.push_back(name);
    }
    return owners;
}

std::string installed_record()
{
    std::string text;
    launcher_update::read_file(apps_backend::state_dir() + "/installed.json", text);
    return text;
}

std::vector<std::string> app_owners()
{
    return parse_owners(apps_backend::run_program("dpkg-query", {"-S", kAppFolder}, kQuickTimeoutS).out);
}

std::vector<std::string> holds()
{
    std::vector<std::string> names;
    std::istringstream in(apps_backend::run_program("apt-mark", {"showhold"}, kQuickTimeoutS).out);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line.find(' ') == std::string::npos && line.compare(0, 2, "E:") != 0) names.push_back(line);
    }
    return names;
}

Detection detection()
{
    Detection d;
    const std::string status = apps_backend::run_program(
        "dpkg-query", {"-W", "-f=${Package} ${db:Status-Abbrev}\\n", "unattended-upgrades", "packagekit", "snapd"},
        kQuickTimeoutS).out;
    d.unattended_installed = package_installed(status, "unattended-upgrades");
    d.packagekit = package_installed(status, "packagekit");
    d.snapd = package_installed(status, "snapd");
    if (d.unattended_installed) {
        d.unattended_periodic =
            periodic_value(apps_backend::run_program("apt-config", {"dump"}, kQuickTimeoutS).out, "APT::Periodic::Unattended-Upgrade");
        const std::string enabled =
            apps_backend::run_program("systemctl", {"is-enabled", "unattended-upgrades.service"}, kQuickTimeoutS).out;
        d.unattended_enabled = enabled.compare(0, 7, "enabled") == 0;
    }
    return d;
}

long long lists_time()
{
    // apt-get update leaves an InRelease file that did not change as it was (its mtime is the archive's): the success
    // stamp Debian's apt writes after every update is the better figure; the files are the fallback and a floor
    long long newest = mtime_of("/var/lib/apt/periodic/update-success-stamp");
    if (DIR *dir = ::opendir("/var/lib/apt/lists")) {
        while (const dirent *entry = ::readdir(dir)) {
            const std::string name = entry->d_name;
            if (ends_with(name, "_InRelease") || ends_with(name, "_Release"))
                newest = std::max(newest, mtime_of("/var/lib/apt/lists/" + name));
        }
        ::closedir(dir);
    }
    return newest;
}

std::string refresh_status()
{
    std::string text;
    launcher_update::read_file(kRefreshStatusPath, text);
    return text;
}

long long now()
{
    return launcher_update::platform::now();
}

// ------------------------------------------------------------------ P4

namespace {

/* the user's runtime folder of the launcher ($XDG_RUNTIME_DIR/applaunch, where board.state lives), else the cache */
std::string runtime_dir()
{
    const char *xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] == '/') {
        const std::string dir = std::string(xdg) + "/applaunch";
        struct stat info {};
        if (::stat(dir.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) return dir;
    }
    return std::string();
}

/* the helper's install command, as arguments of apt-get: `first` ("-s", or "--print-uris" "-qq") then the rest */
std::vector<std::string> install_args(std::vector<std::string> args, const std::vector<std::string> &names)
{
    for (const char *a : {"install", "--only-upgrade", "--no-remove", "--no-install-recommends"}) args.push_back(a);
    args.insert(args.end(), names.begin(), names.end());
    return args;
}

/* run_program (apps_backend.cpp) that the scanner can stop: the program in its own process group, LC_ALL=C, its output
 * collected; when apt_cancel_flag() is set (Esc on Packages, the page left) or the time is up, SIGTERM to the group, then
 * SIGKILL a second later. rc: the exit status, -1 failed to start or stopped, 127 not installed. */
apps_backend::Result run_cancellable(const std::string &binary, const std::vector<std::string> &args, int timeout_s)
{
    apps_backend::Result result;
    if (apt_cancel_flag().load()) return result;
    int pipe_fd[2];
    if (::pipe2(pipe_fd, O_CLOEXEC) != 0) return result;
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(binary.c_str()));
    for (const std::string &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> env_store;
    for (char **entry = environ; entry && *entry; ++entry) {
        const std::string item = *entry;
        if (item.rfind("LC_ALL=", 0) != 0 && item.rfind("LANGUAGE=", 0) != 0) env_store.push_back(item);
    }
    env_store.push_back("LC_ALL=C");
    std::vector<char *> envp;
    for (std::string &item : env_store) envp.push_back(item.data());
    envp.push_back(nullptr);
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
        ::execvpe(binary.c_str(), argv.data(), envp.data());
        ::_exit(127);
    }
    ::close(pipe_fd[1]);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    bool stopped = false;
    char buffer[4096];
    for (;;) {
        if (apt_cancel_flag().load() || std::chrono::steady_clock::now() >= deadline) {
            stopped = true;
            break;
        }
        pollfd p{pipe_fd[0], POLLIN, 0};
        const int ready = ::poll(&p, 1, 100);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) continue;
        const ssize_t n = ::read(pipe_fd[0], buffer, sizeof buffer);
        if (n <= 0) break;
        if (result.out.size() < (8u << 20)) result.out.append(buffer, static_cast<std::size_t>(n));
    }
    if (stopped) {
        ::kill(-pid, SIGTERM);
        for (int i = 0; i < 10 && ::waitpid(pid, nullptr, WNOHANG) == 0; ++i) ::usleep(100 * 1000);
        ::kill(-pid, SIGKILL);
    }
    ::close(pipe_fd[0]);
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0 || stopped) return result;
    result.rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

long long free_of(const std::string &path, dev_t &device)
{
    struct stat info {};
    struct statvfs fs {};
    if (::stat(path.c_str(), &info) != 0 || ::statvfs(path.c_str(), &fs) != 0) return -1;
    device = info.st_dev;
    return static_cast<long long>(fs.f_bavail) * static_cast<long long>(fs.f_frsize);
}

std::string first_line(const std::string &path)
{
    std::string text;
    launcher_update::read_file(path, text);
    const std::size_t end = text.find('\n');
    return end == std::string::npos ? text : text.substr(0, end);
}

} // namespace

std::string simulate_install(const std::vector<std::string> &names)
{
    if (names.empty()) return std::string();
    // the helper's second simulation, word for word (as the user: apt turns locking off for a simulation)
    return run_cancellable("apt-get", install_args({"-s"}, names), kSimulateTimeoutS).out;
}

std::string print_uris(const std::vector<std::string> &names)
{
    if (names.empty()) return std::string();
    return run_cancellable("apt-get", install_args({"--print-uris", "-qq"}, names), kSimulateTimeoutS).out;
}

std::string candidate_records(const std::vector<std::string> &names)
{
    if (names.empty()) return std::string();
    std::vector<std::string> args = {"show", "--no-all-versions"};
    args.insert(args.end(), names.begin(), names.end());
    return run_cancellable("apt-cache", args, kSimulateTimeoutS).out;
}

bool slow_board()
{
    // the 512 MB boards (Zero 2 W, Pi 3A+): every apt run takes many seconds there
    std::string text;
    launcher_update::read_file("/proc/meminfo", text);
    const std::size_t at = text.find("MemTotal:");
    if (at == std::string::npos) return false;
    return std::atoll(text.c_str() + at + 9) < 1024LL * 1024; /* kB */
}

std::string installed_sizes(const std::vector<std::string> &names)
{
    if (names.empty()) return std::string();
    std::vector<std::string> args = {"-W", "-f=${Installed-Size}\\n"};
    args.insert(args.end(), names.begin(), names.end());
    return apps_backend::run_program("dpkg-query", args, kQuickTimeoutS).out;
}

std::vector<std::string> anchor_depends()
{
    const apps_backend::Result r =
        apps_backend::run_program("dpkg-query", {"-W", "-f=${Depends}", kAnchorPackage}, kQuickTimeoutS);
    if (r.rc != 0 || r.out.find("no packages found") != std::string::npos) return {};
    return parse_depends(r.out);
}

bool dpkg_unfinished()
{
    const apps_backend::Result r = apps_backend::run_program("dpkg", {"--audit"}, kQuickTimeoutS);
    if (r.rc < 0 || r.rc == 127) return false;
    for (char c : r.out)
        if (!std::isspace(static_cast<unsigned char>(c))) return true;
    return false;
}

bool under_voltage()
{
    // the rpi_volt hwmon's alarm (the kernel's view of the firmware's under-voltage bit)
    if (DIR *dir = ::opendir("/sys/class/hwmon")) {
        while (const dirent *entry = ::readdir(dir)) {
            const std::string base = std::string("/sys/class/hwmon/") + entry->d_name;
            if (entry->d_name[0] == '.' || first_line(base + "/name") != "rpi_volt") continue;
            const bool alarm = first_line(base + "/in0_lcrit_alarm") == "1";
            ::closedir(dir);
            return alarm;
        }
        ::closedir(dir);
    }
    const std::string sysfs = first_line("/sys/devices/platform/soc/soc:firmware/get_throttled");
    if (!sysfs.empty()) return parse_throttled(sysfs).under_now;
    if (::access("/usr/bin/vcgencmd", X_OK) != 0) return false; /* a PC: nothing to ask */
    return parse_throttled(apps_backend::run_program("vcgencmd", {"get_throttled"}, kQuickTimeoutS).out).under_now;
}

long long upgrade_free()
{
    dev_t cache_dev = 0, usr_dev = 0;
    const long long cache = free_of("/var/cache/apt/archives", cache_dev);
    const long long usr = free_of("/usr", usr_dev);
    if (cache < 0) return usr;
    if (usr < 0 || cache_dev == usr_dev) return cache;
    return std::min(cache, usr);
}

std::string upgrade_status()
{
    std::string text;
    launcher_update::read_file(kUpgradeStatusPath, text);
    return text;
}

bool request_cancel()
{
    const std::string dir = runtime_dir();
    if (dir.empty()) return false;
    std::ofstream out(dir + "/apt-cancel", std::ios::trunc);
    out << "cancel\n";
    return static_cast<bool>(out);
}

} // namespace platform
} // namespace launcher_apt
