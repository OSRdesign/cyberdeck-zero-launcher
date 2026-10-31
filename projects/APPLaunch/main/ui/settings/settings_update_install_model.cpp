/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates P2: the rules of the download and the install (see settings_update_install_model.hpp).
 */

#include "settings_update_install_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace launcher_update {

namespace {

constexpr long long kMiB = 1024LL * 1024;

bool is_hex64(const std::string &text)
{
    if (text.size() != 64) return false;
    for (char c : text)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

std::string lower(std::string text)
{
    for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string trim(const std::string &text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

bool plain_name(const std::string &name)
{
    if (name.empty() || name[0] == '-') return false;
    for (char c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') return false;
    return true;
}

bool starts_with(const std::string &text, const std::string &prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

long long number_of(const std::string &value)
{
    if (value.empty() || value.size() > 18) return 0;
    for (char c : value)
        if (c < '0' || c > '9') return 0;
    return std::atoll(value.c_str());
}

std::vector<std::string> systemd_run(const std::string &helper)
{
    return {"/usr/bin/systemd-run", std::string("--unit=") + kHelperUnit, "--collect", "--quiet",
            "--description=CyberdeckZero launcher update", helper};
}

/* the newer release the status is about, above the running launcher */
bool about_newer(const HelperStatus &status, const std::string &running)
{
    return !status.to.empty() && compare_versions(status.to, running) > 0;
}

} // namespace

// ------------------------------------------------------------------ the download

bool plain_version(const std::string &text)
{
    Version version;
    if (!parse_version(text, version) || !version.pre.empty()) return false;
    for (char c : text)
        if (c != '.' && (c < '0' || c > '9')) return false;
    return true;
}

bool valid_sha256(const std::string &text)
{
    return is_hex64(text) && lower(text) == text;
}

std::string digest_hex(const std::string &digest)
{
    const std::string prefix = "sha256:";
    if (!starts_with(digest, prefix)) return std::string();
    const std::string hex = lower(digest.substr(prefix.size()));
    return is_hex64(hex) ? hex : std::string();
}

std::string parse_checksum_file(const std::string &text, const std::string &bundle_name)
{
    std::istringstream in(trim(text));
    std::string line;
    std::getline(in, line);
    line = trim(line);
    const std::size_t space = line.find_first_of(" \t");
    const std::string hash = lower(line.substr(0, space));
    if (!is_hex64(hash)) return std::string();
    if (space == std::string::npos) return hash;
    std::string name = trim(line.substr(space));
    if (!name.empty() && name[0] == '*') name.erase(0, 1);
    const std::size_t slash = name.rfind('/');
    if (slash != std::string::npos) name.erase(0, slash + 1);
    return name.empty() || name == bundle_name ? hash : std::string();
}

bool download_of(const std::vector<Release> &releases, const std::string &version, const std::string &arch,
                 Download &out)
{
    for (const Release &release : releases) {
        if (release.version != version || !eligible(release)) continue;
        const Asset *asset = find_asset(release, arch);
        if (!asset || asset->url.empty()) return false;
        Download download;
        download.version = version;
        download.name = asset->name;
        download.url = asset->url;
        download.size = asset->size;
        download.digest = digest_hex(asset->digest);
        for (const Asset &other : release.assets)
            if (other.name == asset->name + ".sha256") download.checksum_url = other.url;
        out = download;
        return true;
    }
    return false;
}

HashCheck check_hash(const std::string &computed, const std::string &published, const std::string &api_digest)
{
    const std::string file = lower(computed);
    const bool have_published = is_hex64(published);
    const bool have_digest = is_hex64(api_digest);
    if (!have_published && !have_digest) return HashCheck::NoReference;
    if (have_published && have_digest && lower(published) != lower(api_digest)) return HashCheck::Disagree;
    const std::string reference = lower(have_published ? published : api_digest);
    return is_hex64(file) && file == reference ? HashCheck::Ok : HashCheck::Mismatch;
}

// ------------------------------------------------------------------ guards

long long space_needed(long long bundle_size)
{
    return std::max(kMinFreeBytes, 4 * std::max(0LL, bundle_size));
}

std::string space_problem(long long bundle_size, long long free_bytes)
{
    const long long need = space_needed(bundle_size);
    if (free_bytes < 0 || free_bytes >= need) return std::string();
    return "Not enough space: need " + megabytes(need) + ", " + megabytes(free_bytes) + " free";
}

bool battery_too_low(int battery_percent, bool charging)
{
    return battery_percent >= 0 && battery_percent < 50 && !charging;
}

std::string megabytes(long long bytes)
{
    if (bytes <= 0) return "0 MB";
    return std::to_string(std::max(1LL, (bytes + kMiB / 2) / kMiB)) + " MB";
}

std::string progress_text(long long done, long long total)
{
    done = std::max(0LL, done);
    if (total <= 0) return std::to_string(done / kMiB) + " MB";
    done = std::min(done, total);
    return std::to_string(done / kMiB) + " of " + megabytes(total);
}

std::string download_failure_text(int curl_rc, int http_status)
{
    /* one status line at 480 px: about 40 characters; a retry goes on where the download stopped (curl -C -) */
    switch (curl_rc) {
    case 6: return "No network name for the server (DNS)";
    case 7: return "Cannot reach the server";
    case 28:
    case -2: return "The download took too long: try again";
    case 22: return "The server refused it (HTTP " + std::to_string(http_status) + ")";
    case 23: return "Cannot write the download: disk full?";
    case 35:
    case 51:
    case 58:
    case 60:
    case 77:
    case 83: return "Certificate refused: is the clock right?";
    case 56:
    case 18: return "The connection broke: try again";
    case 127: return "curl is not installed";
    default: break;
    }
    return "The download failed (curl " + std::to_string(curl_rc) + ")";
}

// ------------------------------------------------------------------ the root command

std::vector<std::string> apply_argv(const ApplyRequest &r)
{
    if (!plain_version(r.version) || !plain_version(r.from) || !valid_sha256(r.sha256) || !plain_name(r.user) ||
        r.helper.empty() || r.helper[0] != '/')
        return {};
    if (r.cache_dir.empty() || r.cache_dir[0] != '/' || r.file.find("..") != std::string::npos ||
        r.file.find('\n') != std::string::npos || !starts_with(r.file, r.cache_dir + "/") ||
        r.file.size() <= r.cache_dir.size() + 1)
        return {};
    std::vector<std::string> argv = systemd_run(r.helper);
    argv.insert(argv.end(), {"apply", "--version", r.version, "--from-version", r.from, "--sha256", r.sha256, "--file",
                             r.file, "--user", r.user});
    return argv;
}

std::vector<std::string> resume_argv(const std::string &user, const std::string &helper)
{
    if (!plain_name(user) || helper.empty() || helper[0] != '/') return {};
    std::vector<std::string> argv = systemd_run(helper);
    argv.insert(argv.end(), {"resume", "--user", user});
    return argv;
}

// ------------------------------------------------------------------ the helper's status file

HelperStatus parse_helper_status(const std::string &text)
{
    HelperStatus status;
    bool v1 = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t equal = line.find('=');
        if (equal == std::string::npos || equal == 0) continue;
        const std::string key = line.substr(0, equal);
        const std::string value = line.substr(equal + 1);
        if (key == "v") v1 = value == "1";
        else if (key == "state") status.state = value;
        else if (key == "step") status.step = value;
        else if (key == "from") status.from = value;
        else if (key == "to") status.to = value;
        else if (key == "variant") status.variant = value;
        else if (key == "user") status.user = value;
        else if (key == "boot") status.boot = value;
        else if (key == "restart") status.restart = value;
        else if (key == "log") status.log = value;
        else if (key == "reason") status.reason = value;
        else if (key == "pid") status.pid = number_of(value);
        else if (key == "launcher_pid") status.launcher_pid = number_of(value);
        else if (key == "started") status.started = number_of(value);
        else if (key == "updated") status.updated = number_of(value);
        else if (key == "backup") status.backup = value == "1";
    }
    status.present = v1 && !status.state.empty();
    return status;
}

HelperPhase helper_phase(const HelperStatus &status, bool alive)
{
    if (!status.present) return HelperPhase::None;
    if (status.state == "running") return alive ? HelperPhase::Running : HelperPhase::Interrupted;
    if (status.state == "done") return HelperPhase::Done;
    if (status.state == "refused") return HelperPhase::Refused;
    if (status.state == "rolled-back") return HelperPhase::RolledBack;
    if (status.state == "failed") return HelperPhase::Failed;
    return HelperPhase::None;
}

bool restart_requested(const HelperStatus &status, bool alive, const std::string &running_version, long long my_pid)
{
    return helper_phase(status, alive) == HelperPhase::Running && status.restart == "launcher" &&
           !running_version.empty() && status.from == running_version && status.launcher_pid > 0 &&
           status.launcher_pid == my_pid;
}

std::vector<StepRow> progress_rows(const HelperStatus &status, long long now)
{
    std::vector<StepRow> rows = {
        {"Checking the download", "", StepMark::Pending},
        {"Backup of " + status.from, "", StepMark::Pending},
        {"Installing " + status.to, "", StepMark::Pending},
        {"Restart and check", "", StepMark::Pending},
    };
    int current = 0;
    if (status.step == "backup") current = 1;
    else if (status.step == "install") current = 2;
    else if (status.step == "restart" || status.step == "health") current = 3;
    else if (status.step == "rollback") {
        rows.push_back({"Putting " + status.from + " back", "", StepMark::Pending});
        current = 4;
    }
    const long long seconds = status.updated > 0 && now >= status.updated ? now - status.updated : -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        StepRow &row = rows[static_cast<std::size_t>(i)];
        if (i < current) {
            row.mark = StepMark::Done;
            row.value = "done";
        } else if (i == current) {
            row.mark = StepMark::Current;
            if (seconds >= 0) row.value = std::to_string(seconds) + " s";
        }
    }
    return rows;
}

std::string progress_caption(const HelperStatus &status)
{
    if (status.step == "health") return "Checking that " + status.to + " runs. Do not switch off.";
    if (status.step == "rollback") return "Putting " + status.from + " back. Do not switch off.";
    return "Do not switch off: it restarts by itself"; /* one status line at 480 px */
}

int download_percent(long long done, long long total)
{
    if (total <= 0 || done <= 0) return 0;
    if (done >= total) return 100;
    return static_cast<int>(std::min(99LL, (done * 100 + total / 2) / total)); /* full only when it is whole */
}

int step_percent(const HelperStatus &status)
{
    if (status.step == "backup") return 50;
    if (status.step == "install") return 75;
    if (status.step == "restart" || status.step == "health" || status.step == "rollback") return 100;
    return 25; /* check, plan, or a run that has not written its first step yet */
}

std::string result_text(const HelperStatus &status, HelperPhase phase, const std::string &running)
{
    if (!about_newer(status, running)) return std::string();
    const std::string why = status.reason.empty() ? std::string() : " (" + status.reason + ")";
    switch (phase) {
    case HelperPhase::RolledBack: return "Update to " + status.to + " failed" + why + ": " + status.from + " was put back";
    case HelperPhase::Refused: return status.to + " not installed: " + status.reason;
    case HelperPhase::Failed: return "Update to " + status.to + " failed: " + status.reason;
    case HelperPhase::Interrupted:
        return "The update to " + status.to + " was interrupted: Repair finishes it or puts " + status.from + " back";
    case HelperPhase::None:
    case HelperPhase::Running:
    case HelperPhase::Done: break;
    }
    return std::string();
}

std::string result_value(const HelperStatus &status, HelperPhase phase, const std::string &running)
{
    if (!about_newer(status, running)) return std::string();
    switch (phase) {
    case HelperPhase::RolledBack: return "rolled back";
    case HelperPhase::Refused: return "not installed";
    case HelperPhase::Failed: return "failed";
    case HelperPhase::Interrupted: return "interrupted";
    case HelperPhase::None:
    case HelperPhase::Running:
    case HelperPhase::Done: break;
    }
    return std::string();
}

std::string result_caption(const HelperStatus &status, HelperPhase phase, const std::string &running)
{
    if (!about_newer(status, running)) return std::string();
    switch (phase) {
    case HelperPhase::RolledBack: return "Update to " + status.to + " failed: " + status.from + " put back";
    case HelperPhase::Refused: return status.to + " not installed: see Show log";
    case HelperPhase::Failed: return "Update to " + status.to + " failed: see Show log";
    case HelperPhase::Interrupted: return "Update to " + status.to + " interrupted: Repair it";
    case HelperPhase::None:
    case HelperPhase::Running:
    case HelperPhase::Done: break;
    }
    return std::string();
}

std::string install_blocker(const InstallGuards &g)
{
    if (!g.helper_installed) return "The update helper is missing";
    if (g.helper == HelperPhase::Running) return "An update is already running";
    if (g.helper == HelperPhase::Interrupted) return "Repair the interrupted update first";
    if (battery_too_low(g.battery_percent, g.charging))
        return "Battery " + std::to_string(g.battery_percent) + " %: charge it or plug it in";
    const std::string space = space_problem(g.bundle_size, g.free_bytes);
    if (!space.empty()) return space;
    if (!g.network) return "No network";
    return std::string();
}

std::string take_rollback_notice(State &state, const HelperStatus &status, HelperPhase phase, const std::string &running)
{
    if (phase != HelperPhase::RolledBack || status.started <= 0 || status.started == state.rollback_seen ||
        status.from != running || !about_newer(status, running))
        return std::string();
    state.rollback_seen = status.started;
    return "Update to " + status.to + " failed\nRolled back to " + status.from;
}

std::vector<std::string> stale_downloads(const std::vector<std::string> &names, const std::string &keep)
{
    std::vector<std::string> stale;
    for (const std::string &name : names) {
        if (!starts_with(name, "cyberdeckzero-launcher-")) continue;
        if (!ends_with(name, ".tar.gz") && !ends_with(name, ".tar.gz.part")) continue;
        if (!keep.empty() && (name == keep || name == keep + ".part")) continue;
        stale.push_back(name);
    }
    std::sort(stale.begin(), stale.end());
    return stale;
}

} // namespace launcher_update
