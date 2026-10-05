/*
 * Settings > Apps: progress and message logic (see apps_status_model.hpp).
 */

#include "apps_status_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace apps_status {

namespace {

std::string lower(const std::string &text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool contains(const std::string &haystack_lower, const char *needle)
{
    return haystack_lower.find(needle) != std::string::npos;
}

std::string clip(const std::string &text, size_t limit)
{
    if (text.size() <= limit) return text;
    return text.substr(0, limit > 1 ? limit - 1 : 0) + "~";
}

std::string trim(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    return text.substr(begin);
}

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> split_tab(const std::string &line)
{
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == '\t') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(current);
    return fields;
}

// Text of the last "ERROR<TAB>text" record, empty when there is none.
std::string last_error_record(const std::string &output)
{
    std::string found;
    for (const std::string &line : split_lines(output)) {
        if (line.compare(0, 6, "ERROR\t") != 0) continue;
        const auto fields = split_tab(line);
        if (fields.size() > 1 && !fields[1].empty()) found = fields[1];
    }
    return found;
}

// "download failed (curl 6): url" -> 6, -1 when there is no code.
int curl_code(const std::string &text)
{
    const size_t at = text.find("curl ");
    if (at == std::string::npos) return -1;
    size_t i = at + 5;
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return -1;
    return std::atoi(text.c_str() + i);
}

} // namespace

/* ---- sync ---------------------------------------------------------------------------------------------- */

void SyncProgress::reset(const std::vector<std::pair<std::string, std::string>> &sources)
{
    entries_.clear();
    running_ = -1;
    for (const auto &source : sources) {
        SyncEntry entry;
        entry.url = source.first;
        entry.name = source.second.empty() ? source.first : source.second;
        entries_.push_back(std::move(entry));
    }
}

void SyncProgress::start(std::size_t index)
{
    if (index >= entries_.size()) return;
    entries_[index].state = SyncState::Running;
    entries_[index].reason.clear();
    running_ = static_cast<int>(index);
}

void SyncProgress::succeed(std::size_t index)
{
    if (index >= entries_.size()) return;
    entries_[index].state = SyncState::Done;
    if (running_ == static_cast<int>(index)) running_ = -1;
}

void SyncProgress::fail(std::size_t index, const std::string &reason)
{
    if (index >= entries_.size()) return;
    entries_[index].state = SyncState::Failed;
    entries_[index].reason = reason.empty() ? "Sync failed" : reason;
    if (running_ == static_cast<int>(index)) running_ = -1;
}

std::size_t SyncProgress::finished() const
{
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const SyncEntry &e) {
        return e.state == SyncState::Done || e.state == SyncState::Failed;
    }));
}

std::size_t SyncProgress::failed() const
{
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const SyncEntry &e) {
        return e.state == SyncState::Failed;
    }));
}

const SyncEntry *SyncProgress::find(const std::string &url) const
{
    for (const SyncEntry &entry : entries_)
        if (entry.url == url) return &entry;
    return nullptr;
}

std::string SyncProgress::status() const
{
    if (entries_.empty()) return "";
    if (running_ >= 0)
        return "Syncing " + std::to_string(finished() + 1) + "/" + std::to_string(total()) + " " +
               clip(entries_[static_cast<size_t>(running_)].name, 16);
    const size_t bad = failed();
    const size_t good = finished() - bad;
    if (bad == 0) return "Synced " + std::to_string(good) + (good == 1 ? " source" : " sources");
    if (good == 0) return bad == 1 ? "Sync failed" : "Sync failed (" + std::to_string(bad) + ")";
    return "Synced " + std::to_string(good) + " of " + std::to_string(total()) + " sources";
}

int SyncProgress::severity() const
{
    const size_t bad = failed();
    if (bad == 0) return 0;
    return bad >= finished() ? 2 : 1;
}

std::string update_registry_status(const std::string &edit_registry_output)
{
    std::string found;
    for (const std::string &line : split_lines(edit_registry_output)) {
        if (line.compare(0, 17, "REGISTRY\tUPDATED\t") != 0) continue;
        const auto fields = split_tab(line);
        if (fields.size() > 4) found = trim(fields[4]);
    }
    return found;
}

SyncVerdict decide_sync(int rc, const std::string &edit_registry_output, const std::string &registry_status,
                        const std::string &registry_error)
{
    SyncVerdict verdict;
    if (rc != 0) {
        verdict.backend_text = rc == -2 ? "timed out" : last_error_record(edit_registry_output);
        return verdict;
    }
    std::string status = update_registry_status(edit_registry_output);
    if (status.empty()) status = registry_status;
    if (status == "ok") {
        verdict.ok = true;
        return verdict;
    }
    verdict.backend_text = registry_error;     // "cached" or "error": the list could not be fetched
    return verdict;
}

std::string sync_failure_reason(const std::string &backend_text, const SyncProbe &probe)
{
    const std::string text = lower(backend_text);
    if (!probe.network_up) return "No network";
    if (contains(text, "invalid json")) return "Bad JSON";
    if (contains(text, "md5")) return "MD5 mismatch";
    if (probe.http_code >= 400) return "HTTP " + std::to_string(probe.http_code);
    if (probe.curl_rc == 6) return "No network";          // host cannot be resolved
    if (probe.curl_rc == 7) return "Connection refused";
    if (probe.curl_rc == 28) return "Timed out";
    if (probe.curl_rc != 0) return "Connection failed";
    if (contains(text, "cancelled")) return "Cancelled";
    if (contains(text, "timed out")) return "Timed out";
    if (text.empty()) return "Sync failed";
    if (contains(text, "download failed")) return "Download failed";
    return clip(trim(backend_text), 40);
}

SyncProbe curl_probe(int run_rc, const std::string &output)
{
    SyncProbe probe;
    probe.curl_rc = run_rc == 0 ? 0 : (run_rc == -2 ? 28 : (run_rc > 0 ? run_rc : 1));
    if (run_rc == 0 && output.size() >= 3) probe.http_code = std::atoi(output.substr(output.size() - 3).c_str());
    return probe;
}

bool has_default_route(const std::string &proc_net_route)
{
    const auto lines = split_lines(proc_net_route);
    for (size_t i = 1; i < lines.size(); ++i) {       // line 0 is the header
        std::istringstream fields(lines[i]);
        std::string iface, destination, gateway, flags;
        if (!(fields >> iface >> destination >> gateway >> flags)) continue;
        if (destination == "00000000" && (std::strtoul(flags.c_str(), nullptr, 16) & 0x1)) return true;   // RTF_UP
    }
    return false;
}

/* ---- packages ------------------------------------------------------------------------------------------ */

Failure package_failure(const std::string &output, int exit_code, bool network_up, bool stage_prepare)
{
    const std::string text = lower(output);
    const std::string record = last_error_record(output);

    if (contains(text, "no space left") || contains(text, "not enough space") || curl_code(text) == 23 ||
        contains(text, "write error"))
        return {"Not enough space", "The deck is out of disk space. Free some space and try again."};

    if (contains(text, "md5 mismatch") || contains(text, "md5 verification") || contains(text, "sha256 mismatch") ||
        contains(text, "sha mismatch") || contains(text, "checksum"))
        return {"MD5 mismatch", "The package does not match its checksum (damaged or changed download). Sync and try again."};

    if (contains(text, "another package transaction is pending") || contains(text, "pending_conflict")) {
        std::string package;
        for (const std::string &raw : split_lines(output)) {
            if (raw.compare(0, 17, "PENDING_CONFLICT\t") != 0) continue;
            const auto fields = split_tab(raw);
            if (fields.size() > 3) package = fields[3].empty() ? fields[1] : fields[3];
        }
        return {"Unfinished install", "An earlier install" + (package.empty() ? std::string() : " of " + clip(package, 30)) +
                                       " did not finish. Retry that app first, then try again."};
    }

    if (stage_prepare) {
        const int code = curl_code(text);
        if (!network_up || code == 6 || code == 7 || code == 28 || code == 35 || code == 56)
            return {"No network", "The package could not be downloaded: no network connection."};
        if (code == 22) return {"Download failed", "The server refused the download (HTTP error)."};
    }

    // The first dpkg or apt complaint is the useful line. dpkg often only announces the failing package
    // ("dpkg: error processing package x (--install):") and gives the real reason on the next line(s).
    const std::vector<std::string> lines = split_lines(output);
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string line = trim(lines[i]);
        const std::string low = lower(line);
        if (low.compare(0, 6, "dpkg: ") == 0 && (contains(low, "error") || contains(low, "dependency problems") ||
                                                  contains(low, "cannot") || contains(low, "failed"))) {
            std::string detail;
            if (!line.empty() && line.back() == ':') {            // only an announcement: the reason follows
                int reasons = 0;
                for (size_t j = i + 1; j < lines.size() && reasons < 2; ++j) {
                    const std::string next = trim(lines[j]);
                    const std::string next_low = lower(next);
                    if (next.empty() || next.compare(0, 6, "ERROR\t") == 0 || next.compare(0, 9, "PROGRESS\t") == 0 ||
                        next_low.compare(0, 6, "dpkg: ") == 0 || contains(next_low, "errors were encountered"))
                        break;
                    detail += (detail.empty() ? "" : "\n") + clip(next, 95);
                    ++reasons;
                }
            }
            return {"dpkg error", detail.empty() ? clip(line, 200) : clip(detail, 200)};
        }
        if (line.compare(0, 3, "E: ") == 0) return {"apt error", clip(line, 200)};
    }
    if (!record.empty()) {
        if (!stage_prepare) return {"Install failed", clip(record, 200)};
        // "Download failed" only for a download; anything else the backend refused to prepare has its own words.
        if (contains(lower(record), "download") || curl_code(lower(record)) >= 0)
            return {"Download failed", clip(record, 200)};
        return {"Could not prepare", clip(record, 200)};
    }
    return {"Failed", "The operation failed" + (exit_code != 0 ? " (exit " + std::to_string(exit_code) + ")" : std::string())};
}

std::string progress_text(const std::string &helper_output)
{
    std::string text;
    for (const std::string &line : split_lines(helper_output)) {
        if (line.compare(0, 9, "PROGRESS\t") != 0) continue;
        const auto fields = split_tab(line);
        if (fields.size() > 5 && !fields[5].empty()) text = fields[5];
        else if (fields.size() > 1 && !fields[1].empty()) text = fields[1];
    }
    return text;
}

std::string stage_line(const std::string &text, std::size_t queue_position, std::size_t queue_total, long seconds,
                       std::size_t width)
{
    std::string suffix;
    if (queue_total >= 2) suffix += " (" + std::to_string(queue_position) + "/" + std::to_string(queue_total) + ")";
    if (seconds >= 0) suffix += " " + std::to_string(seconds) + "s";
    const std::size_t room = width > suffix.size() + 1 ? width - suffix.size() : 1;
    return clip(text, room) + suffix;
}

unsigned long long parse_size_bytes(const std::string &text)
{
    const std::string value = trim(text);
    if (value.empty() || !std::isdigit(static_cast<unsigned char>(value[0]))) return 0;
    char *end = nullptr;
    const double number = std::strtod(value.c_str(), &end);
    std::string unit = lower(trim(end ? std::string(end) : std::string()));
    double factor = 1;
    if (unit == "kb" || unit == "k" || unit == "kib") factor = 1024.0;
    else if (unit == "mb" || unit == "m" || unit == "mib") factor = 1024.0 * 1024;
    else if (unit == "gb" || unit == "g" || unit == "gib") factor = 1024.0 * 1024 * 1024;
    else if (!unit.empty() && unit != "b") return 0;
    return static_cast<unsigned long long>(number * factor);
}

std::string format_bytes(unsigned long long bytes)
{
    if (bytes >= (1ull << 30)) {
        const unsigned long long tenths = bytes * 10 / (1ull << 30);
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " GB";
    }
    if (bytes >= (1ull << 20)) return std::to_string((bytes + (1ull << 19)) >> 20) + " MB";
    return std::to_string((bytes + 512) >> 10) + " KB";
}

std::string space_problem(unsigned long long package_bytes, unsigned long long free_bytes)
{
    if (package_bytes == 0) return "";
    const unsigned long long need = package_bytes * 3;
    if (free_bytes >= need) return "";
    return "Not enough space: need " + format_bytes(need) + ", " + format_bytes(free_bytes) + " free";
}

/* ---- update all ---------------------------------------------------------------------------------------- */

void UpdateQueue::reset(const std::vector<std::pair<std::string, std::string>> &id_and_title)
{
    items_ = id_and_title;
    next_ = 0;
}

std::pair<std::string, std::string> UpdateQueue::take()
{
    if (next_ >= items_.size()) return {};
    return items_[next_++];
}

} // namespace apps_status
