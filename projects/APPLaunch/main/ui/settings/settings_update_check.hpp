/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates (report 047 P1): the one update check of the launcher, shared by the Settings page (a check when
 * it opens, Check now) and the daily background check (launcher_update.hpp). The work runs on a detached worker thread
 * (the Settings > Apps pattern: it outlives the page); the page and the background timer read snapshot() on the LVGL
 * thread. The state file and the cached answer are read once, at the first snapshot().
 *
 * A check: no default route -> Offline; an https address before network time -> NoClock; else curl (ETag of the cached
 * answer), the outcome mapped (settings_update_model.hpp), a 200 saved as the cached answer, a 304 read from it; the
 * state file written with the result. Nothing is downloaded but the release list; nothing runs as root.
 */
#pragma once

#include "settings_update_model.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace launcher_update {

struct Snapshot {
    bool running = false;    /* a check is on its way */
    std::uint64_t done = 0;  /* checks finished since the launcher started */
    Outcome outcome = Outcome::None; /* of the last check finished since the start (None: none yet) */
    int http_status = 0;     /* its HTTP status */
    Build build;
    State state;
    bool state_existed = false; /* the state file was there at the start (take_start_notice) */
    std::vector<Release> releases; /* the cached answer */
    Offer offer;             /* choose(releases, build) */
};

class Checker {
public:
    static Checker &instance();

    Snapshot snapshot();
    /* Starts a check; false when one is already running. */
    bool start();
    /* Changes the state (the toasts' bookkeeping) and writes it when it changed (or the file did not exist). */
    void update_state(const std::function<void(State &)> &change);

private:
    Checker() = default;
    void load_locked();
    void run();

    std::mutex mutex_;
    bool loaded_ = false;
    Snapshot snap_;
};

} // namespace launcher_update
