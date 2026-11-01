/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > System packages: the scan (see settings_apt_scan.hpp).
 */

#include "settings_apt_scan.hpp"

#include "settings_apt_platform.hpp"

#include "sample_log.h"

#include <chrono>
#include <thread>
#include <utility>

namespace launcher_apt {

namespace {

using Clock = std::chrono::steady_clock;

long long ms_since(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

bool cancel_requested()
{
    return apt_cancel_flag().load();
}

} // namespace

std::atomic<bool> &apt_cancel_flag()
{
    static std::atomic<bool> flag{false};
    return flag;
}

const char *scan_stage_text(int stage)
{
    switch (stage) {
    case 1: return "Reading the package lists";
    case 2: return "Finding the packages that can be upgraded";
    case 3: return "Sorting them";
    case 4: return "Checking that the upgrade is safe";
    default: return "";
    }
}

Overview scan(const std::function<void(int)> &stage)
{
    Overview overview;
    overview.scanned = true;
    overview.lists_time = platform::lists_time();
    // first: an interrupted dpkg also makes apt-get -s fail ("dpkg was interrupted"), and Repair is the way out
    overview.dpkg_unfinished = platform::dpkg_unfinished();

    std::string text;
    if (!platform::simulate(false, text)) {
        overview.failed = true;
        return overview;
    }
    const Simulation upgrade = parse_simulation(text);
    if (!upgrade.valid) {
        overview.failed = true; /* apt said something else: no lists, a broken dpkg, a lock */
        return overview;
    }
    if (stage) stage(2);
    Simulation dist;
    if (!upgrade.kept_back.empty() && platform::simulate(true, text)) dist = parse_simulation(text);

    Board board;
    for (const std::string &name : platform::library_owners()) board.libraries.insert(name);
    for (const std::string &name : platform::anchor_depends()) board.libraries.insert(name); /* P4: what install.sh named */
    for (const std::string &name : parse_installed_packages(platform::installed_record())) board.apps.insert(name);
    for (const std::string &name : platform::app_owners()) board.apps.insert(name);
    board.holds = platform::holds();

    if (stage) stage(3);
    overview.report = classify(upgrade, dist, board);
    overview.detection = platform::detection();
    overview.detection.holds = board.holds.size();
    return overview;
}

Planned make_plans(const Overview &overview, const std::function<void(int)> &progress)
{
    ClosureHooks hooks;
    hooks.simulate = [](const std::vector<std::string> &names) { return platform::simulate_install(names); };
    hooks.records = [](const std::vector<std::string> &names) { return platform::candidate_records(names); };
    hooks.progress = progress;
    hooks.cancelled = cancel_requested;
    Planned planned = plan_upgrade(overview.report, hooks);
    for (Plan *plan : {&planned.plan, &planned.plan_libraries}) {
        if (!plan->valid || plan->count() == 0 || cancel_requested()) continue;
        const std::vector<std::string> names = plan->names();
        apply_sizes(*plan, platform::print_uris(names), platform::candidate_records(names), platform::installed_sizes(names));
    }
    return planned;
}

Scanner &Scanner::instance()
{
    static Scanner scanner;
    return scanner;
}

void Scanner::start()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (snap_.running) {
            again_ = true; /* the lists changed while it read them: once more when it is done */
            return;
        }
        snap_.running = true;
        snap_.stage = 1;
        snap_.checks = 0;
        apt_cancel_flag() = false;
    }
    std::thread(&Scanner::work).detach();
}

void Scanner::cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snap_.running) return;
    again_ = false;
    apt_cancel_flag() = true; /* the running program is stopped by the platform; work() drops the pass */
}

ScanSnapshot Scanner::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snap_;
}

void Scanner::set_stage(int stage)
{
    std::lock_guard<std::mutex> lock(mutex_);
    snap_.stage = stage;
}

void Scanner::work()
{
    Scanner &self = instance();
    for (;;) {
        const bool finished = pass(self);
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (!finished || apt_cancel_flag().load()) {
            // never a half result: back to the last complete pass (or nothing)
            const std::uint64_t cancelled = self.snap_.cancelled + 1;
            self.snap_ = self.complete_;
            self.snap_.cancelled = cancelled;
            self.snap_.planning = false;
            self.snap_.stage = 0;
            apt_cancel_flag() = false;
            SLOGI("[UPDATE] package scan cancelled");
            if (self.again_) { /* a new scan was asked after the cancel (Packages entered again): it runs now */
                self.again_ = false;
                self.snap_.running = true;
                self.snap_.stage = 1;
                self.snap_.checks = 0;
                continue;
            }
            self.snap_.running = false;
            return;
        }
        if (!self.again_) {
            self.snap_.running = false;
            self.snap_.stage = 0;
            self.complete_ = self.snap_;
            return;
        }
        self.again_ = false;
        self.snap_.stage = 1;
        self.snap_.checks = 0;
    }
}

bool Scanner::pass(Scanner &self)
{
    // one log line per stage: its name and how long it took (to measure the Zero 2 W)
    static const char *const names[] = {"", "scan.read", "scan.list", "scan.sort", "scan.plan"};
    Clock::time_point t = Clock::now();
    int current = 1;
    self.set_stage(1);
    Overview overview = scan([&](int next) {
        SLOGI("[UPDATE] phase %s took %lld ms", names[current], ms_since(t));
        t = Clock::now();
        current = next;
        self.set_stage(next);
    });
    if (cancel_requested()) return false;
    SLOGI("[UPDATE] phase %s took %lld ms", names[current], ms_since(t));
    const bool plannable = overview.report.valid && overview.report.safe.size() + overview.report.libraries.size() > 0;
    {
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.snap_.overview = overview;
        ++self.snap_.done;
        self.snap_.planning = plannable;
        self.snap_.plan = Plan();
        self.snap_.plan_libraries = Plan();
        if (!plannable) {
            self.snap_.plan.ready = self.snap_.plan_libraries.ready = true;
            ++self.snap_.planned;
            return true;
        }
        self.snap_.stage = 4;
        self.snap_.checks = 0;
    }
    // the list shows already: the closure follows (each check a simulation, a few seconds on the Zero 2 W)
    t = Clock::now();
    Planned planned = make_plans(overview, [&self](int checks) {
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.snap_.checks = checks;
    });
    if (cancel_requested()) return false;
    SLOGI("[UPDATE] phase scan.plan took %lld ms (%d simulations; plans of %d and %d packages)", ms_since(t),
          self.snapshot().checks, planned.plan.count(), planned.plan_libraries.count());
    std::lock_guard<std::mutex> lock(self.mutex_);
    self.snap_.overview.report = std::move(planned.report); /* the moved packages in their new section */
    self.snap_.plan = std::move(planned.plan);
    self.snap_.plan_libraries = std::move(planned.plan_libraries);
    self.snap_.planning = false;
    ++self.snap_.planned;
    return true;
}

} // namespace launcher_apt
