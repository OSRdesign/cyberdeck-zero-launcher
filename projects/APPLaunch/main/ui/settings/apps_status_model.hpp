/*
 * Settings > Apps: the pure logic behind the progress and the messages (no LVGL, no threads, no I/O), so it can be
 * unit tested: per-source sync progress, why a sync failed, why an install or a remove failed, free space check,
 * the "update all" queue.
 */

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace apps_status {

/* ---- per-source sync ---------------------------------------------------------------------------------- */

enum class SyncState { Waiting, Running, Done, Failed };

struct SyncEntry {
    std::string url;
    std::string name;
    SyncState state = SyncState::Waiting;
    std::string reason;        // short reason of a failure ("HTTP 404", "Bad JSON", "No network" ...)
};

class SyncProgress {
public:
    /* (url, name) of every source about to be synced; resets the previous run. */
    void reset(const std::vector<std::pair<std::string, std::string>> &sources);
    void start(std::size_t index);
    void succeed(std::size_t index);
    void fail(std::size_t index, const std::string &reason);

    bool active() const { return running_ >= 0; }
    bool empty() const { return entries_.empty(); }
    std::size_t total() const { return entries_.size(); }
    std::size_t finished() const;      // done + failed
    std::size_t failed() const;
    const std::vector<SyncEntry> &entries() const { return entries_; }
    const SyncEntry *find(const std::string &url) const;

    /* One short line for the status area: "Syncing 2/3 owner/repo", "Synced 3 sources", "Synced 2 of 3 sources",
     * "Sync failed". */
    std::string status() const;

    /* Colour of the final line once the run is over: 0 all good (green), 1 some failed (amber), 2 all failed (red). */
    int severity() const;

private:
    std::vector<SyncEntry> entries_;
    int running_ = -1;
};

/* What the probe of a failed registry download saw. */
struct SyncProbe {
    bool network_up = true;    // a default route exists
    int http_code = 0;         // 0 when unknown (no answer)
    int curl_rc = 0;           // exit code of the probe (6/7/28...: no connection)
};

/* What the backend really did for one source. `--edit-registry` exits 0 even when it could not fetch the source
 * (it keeps the cached list and says so in the "REGISTRY UPDATED" line), so the exit code alone proves nothing. */
struct SyncVerdict {
    bool ok = false;
    std::string backend_text;      // why it failed, as far as the backend says (may be empty)
};

/* Status word of the "REGISTRY<TAB>UPDATED<TAB>old<TAB>new<TAB>status<TAB>apps<TAB>name" line ("ok", "cached",
 * "error"), empty when the output has no such line. */
std::string update_registry_status(const std::string &edit_registry_output);

/* Decides from the exit code, the output of `--edit-registry` and the source's own record from `--registries`
 * (`registry_status` / `registry_error`, empty when unknown). Success needs a positive "ok". */
SyncVerdict decide_sync(int rc, const std::string &edit_registry_output, const std::string &registry_status,
                        const std::string &registry_error);

/* Short human reason of a failed registry sync. `backend_text` is the backend's ERROR text (or the Source's
 * stored error). The registry format has no checksum of its own, so a damaged registry shows as "Bad JSON". */
std::string sync_failure_reason(const std::string &backend_text, const SyncProbe &probe);

/* Turns the result of the curl probe (`curl -sS -L -o /dev/null -w %{http_code}`: exit code of the process, -2 for
 * our own timeout, and its output) into a SyncProbe. `network_up` is left at its default (true). */
SyncProbe curl_probe(int run_rc, const std::string &output);

/* True when /proc/net/route (the text) has a default route. */
bool has_default_route(const std::string &proc_net_route);

/* ---- install, upgrade, remove ------------------------------------------------------------------------- */

struct Failure {
    std::string headline;      // short: fits the status area (about 28 characters)
    std::string detail;        // the first line of the real error, for the message panel
};

/* `output` is what the backend or the privileged helper printed (dpkg's own lines included), `stage_prepare`
 * is true when the failure came while preparing (download, MD5), false for the privileged step. */
Failure package_failure(const std::string &output, int exit_code, bool network_up, bool stage_prepare);

/* The status area (about 30 characters wide): the step, then the queue position and the seconds. When it is too long
 * the step is cut with "~", never the counters: "Downloading Wi-Fi S~ (2/2) 14s". `queue_total` below 2 means no
 * queue; `seconds` below 0 means no counter. */
std::string stage_line(const std::string &text, std::size_t queue_position, std::size_t queue_total, long seconds,
                       std::size_t width = 30);

/* The privileged helper prints "PROGRESS<TAB>stage<TAB>..<TAB>message": text to show for the latest such line,
 * empty when there is none. */
std::string progress_text(const std::string &helper_output);

/* Empty when there is room; else a message such as "Not enough space: need 12 MB, 3 MB free". `package_bytes`
 * is the download size (0 when unknown: no check); an install needs about three times that (the .deb, the
 * unpacked files and a rollback copy). */
std::string space_problem(unsigned long long package_bytes, unsigned long long free_bytes);

/* Download size field of the catalogue ("1454518", "1.4 MB" or "online"): bytes, 0 when unknown. */
unsigned long long parse_size_bytes(const std::string &text);

/* "12 MB", "340 KB". */
std::string format_bytes(unsigned long long bytes);

/* ---- update all --------------------------------------------------------------------------------------- */

/* Ids still to update, one at a time (each needs the sudo password). */
class UpdateQueue {
public:
    void reset(const std::vector<std::pair<std::string, std::string>> &id_and_title);
    bool empty() const { return next_ >= items_.size(); }
    bool active() const { return !items_.empty(); }
    std::size_t total() const { return items_.size(); }
    std::size_t position() const { return next_; }   // how many were taken: 1-based index of the current one
    /* The next (id, title); the queue advances. */
    std::pair<std::string, std::string> take();
    /* A failed update stops the queue: nothing else is attempted. */
    void abort() { items_.clear(); next_ = 0; }
    void clear() { abort(); }

private:
    std::vector<std::pair<std::string, std::string>> items_;
    std::size_t next_ = 0;
};

} // namespace apps_status
