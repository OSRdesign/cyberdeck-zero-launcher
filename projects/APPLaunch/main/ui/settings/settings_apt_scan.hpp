/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > System packages (report 047 P3): the scan. One worker thread reads the board (the plan of
 * `apt-get -s upgrade`, and of `dist-upgrade` only when `upgrade` keeps packages back, the launcher's libraries, the Store's
 * apps, the holds, the automatic-update tools, the lists' age) and classifies it (settings_apt_model.hpp). It runs
 * when the Updates page opens and after a refresh of the lists: never in the background (D12), never as root.
 * The page polls snapshot() from its timer, like the release check.
 */
#pragma once

#include "settings_apt_model.hpp"

#include <cstdint>
#include <mutex>

namespace launcher_apt {

struct ScanSnapshot {
    bool running = false;
    std::uint64_t done = 0; /* the number of scans that finished: a change means a new overview */
    Overview overview;
};

class Scanner {
public:
    static Scanner &instance();
    /* Starts a scan unless one runs (a detached thread: it may outlive the page). */
    void start();
    ScanSnapshot snapshot() const;

private:
    Scanner() = default;
    static void work();
    mutable std::mutex mutex_;
    ScanSnapshot snap_;
};

/* One scan, synchronously (the worker's body; the tests and the scenes call it directly). */
Overview scan();

} // namespace launcher_apt
