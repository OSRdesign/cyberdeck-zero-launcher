/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates P2 (report 047 section 5.4): the download of a newer release, as the user, before anything asks
 * for a password, so the confirmation can say "downloaded and checked". One download at a time, on a detached worker
 * thread (the Settings > Apps pattern: it outlives the page); the page polls snapshot() on the LVGL thread.
 *
 *   1. the folder: $XDG_CACHE_HOME/cyberdeckzero/update (never /tmp, a RAM disk on the 512 MB boards); older downloads
 *      in it are deleted first
 *   2. curl -fsSL -C - --max-time 900 into <bundle>.part: a stopped download goes on where it stopped; a .part the server
 *      cannot resume (HTTP 416) starts again once; progress = the .part's size against the release's asset size;
 *      cancel() stops curl (no root is involved yet), the .part stays for the next time
 *   3. the .part renamed when it has the asset's size; a bundle already downloaded whole is only checked again
 *   4. the check: its sha256 (sha256sum) against the release's .sha256 asset and GitHub's digest (D5: both when both
 *      exist, at least one); a mismatch deletes the file
 *
 * Nothing runs as root here (settings_update_install.hpp starts the root helper).
 */
#pragma once

#include "settings_update_install_model.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace launcher_update {

enum class DownloadPhase { Idle, Downloading, Verifying, Ready, Failed, Cancelled };

struct DownloadSnapshot {
    DownloadPhase phase = DownloadPhase::Idle;
    Download download;            /* what is (or was last) downloaded */
    long long done = 0;           /* bytes on disk so far */
    std::string file;             /* Ready: the checked bundle */
    std::string sha256;           /* Ready: its hash */
    std::string error;            /* Failed: why, in words */
    std::uint64_t generation = 0; /* increases with every phase change */
};

class Downloader {
public:
    static Downloader &instance();

    DownloadSnapshot snapshot();
    /* Starts the download (a worker thread); false while one runs. */
    bool start(const Download &download, const std::string &user_agent);
    /* Stops a running download (its .part stays). */
    void cancel();
    /* Back to Idle (the confirmation's Cancel): a checked bundle stays on disk, the next Install checks it again. */
    void reset();
    /* The downloaded bundles are not needed any more (the update is done): deleted, back to Idle. */
    void discard_files();

private:
    Downloader() = default;
    void run(Download download, std::string user_agent);
    void set_phase(DownloadPhase phase, const std::string &error = std::string());

    std::mutex mutex_;
    DownloadSnapshot snap_;
    bool running_ = false;
    std::atomic<bool> cancel_{false};
};

} // namespace launcher_update
