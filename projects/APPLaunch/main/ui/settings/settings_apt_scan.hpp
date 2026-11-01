/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > System packages (report 047 P3): the scan. One worker thread reads the board (the plan of
 * `apt-get -s upgrade`, and of `dist-upgrade` only when `upgrade` keeps packages back, the launcher's libraries, the Store's
 * apps, the holds, the automatic-update tools, the lists' age) and classifies it (settings_apt_model.hpp). It runs
 * when the Updates page opens and after a refresh of the lists: never in the background (D12), never as root.
 * The page polls snapshot() from its timer, like the release check.
 *
 * P4: the anchor package's Depends join the launcher libraries, dpkg --audit says whether a repair is needed, and once the
 * overview is out (the list shows) the same worker plans the upgrade (settings_apt_upgrade_model.hpp, plan_upgrade): the
 * closure apt really performs, without and with the launcher libraries, the moved packages in their new section, the
 * download and disk sizes. Four stages, published for the waiting box (settings_wait_model.hpp): 1 reading the package
 * lists, 2 finding the packages that can be upgraded, 3 sorting them, 4 checking that the upgrade is safe (each closure
 * simulation counts). Each stage's duration is logged once: "[UPDATE] phase scan.<stage> took <ms> ms".
 *
 * cancel(): the program running now is stopped (SIGTERM to its process group, then SIGKILL) and the pass is dropped: the
 * snapshot goes back to the last complete pass (or to "not scanned"): never a half result.
 */
#pragma once

#include "settings_apt_model.hpp"
#include "settings_apt_upgrade_model.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace launcher_apt {

constexpr int kScanStages = 4;
/* "Reading the package lists", "Finding the packages that can be upgraded", "Sorting them", "Checking that the upgrade is
 * safe"; "" for 0. */
const char *scan_stage_text(int stage);

struct ScanSnapshot {
    bool running = false;
    std::uint64_t done = 0; /* the number of scans that finished: a change means a new overview */
    Overview overview;
    bool planning = false;  /* (P4) the plans of the last overview are being made */
    std::uint64_t planned = 0; /* the number of plannings that finished */
    Plan plan;              /* the default plan: the launcher libraries left out */
    Plan plan_libraries;    /* the same with the launcher libraries (== plan when there are none) */
    int stage = 0;          /* 1..kScanStages while it works, 0 idle */
    int checks = 0;         /* simulations of the closure so far (stage 4) */
    std::uint64_t cancelled = 0; /* the number of passes cancelled */
};

class Scanner {
public:
    static Scanner &instance();
    /* Starts a scan unless one runs (a detached thread: it may outlive the page); while one runs, it runs once more. */
    void start();
    /* Stops the scan that runs (its program killed) and drops it; nothing when none runs. */
    void cancel();
    ScanSnapshot snapshot() const;

private:
    Scanner() = default;
    static void work();
    static bool pass(Scanner &self); /* one scan and its plans; false: cancelled */
    void set_stage(int stage);
    mutable std::mutex mutex_;
    ScanSnapshot snap_;
    ScanSnapshot complete_;  /* the last pass that ended with its plans */
    bool again_ = false;     /* start() came while a scan ran */
};

/* The scanner's cancel request: the platform calls stop their program while it is set (settings_apt_platform.hpp). */
std::atomic<bool> &apt_cancel_flag();

/* One scan, synchronously (the worker's body; the tests and the scenes call it directly); stage(2), stage(3) when it gets
 * there. */
Overview scan(const std::function<void(int)> &stage = std::function<void(int)>());
/* The plans of an overview (P4): the closure, then the sizes of both plans. Synchronous: the worker calls it. */
Planned make_plans(const Overview &overview, const std::function<void(int)> &progress);

} // namespace launcher_apt
