/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > System packages: the scan (see settings_apt_scan.hpp).
 */

#include "settings_apt_scan.hpp"

#include "settings_apt_platform.hpp"

#include <thread>
#include <utility>

namespace launcher_apt {

Overview scan()
{
    Overview overview;
    overview.scanned = true;
    overview.lists_time = platform::lists_time();

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
    Simulation dist;
    if (!upgrade.kept_back.empty() && platform::simulate(true, text)) dist = parse_simulation(text);

    Board board;
    for (const std::string &name : platform::library_owners()) board.libraries.insert(name);
    for (const std::string &name : parse_installed_packages(platform::installed_record())) board.apps.insert(name);
    for (const std::string &name : platform::app_owners()) board.apps.insert(name);
    board.holds = platform::holds();

    overview.report = classify(upgrade, dist, board);
    overview.detection = platform::detection();
    overview.detection.holds = board.holds.size();
    return overview;
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
        if (snap_.running) return;
        snap_.running = true;
    }
    std::thread(&Scanner::work).detach();
}

ScanSnapshot Scanner::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snap_;
}

void Scanner::work()
{
    Overview overview = scan();
    Scanner &self = instance();
    std::lock_guard<std::mutex> lock(self.mutex_);
    self.snap_.overview = std::move(overview);
    ++self.snap_.done;
    self.snap_.running = false;
}

} // namespace launcher_apt
