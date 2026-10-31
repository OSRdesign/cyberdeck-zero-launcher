/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates: the update check (see settings_update_check.hpp).
 */

#include "settings_update_check.hpp"

#include "settings_update_platform.hpp"

#include <thread>
#include <utility>

namespace launcher_update {

Checker &Checker::instance()
{
    static Checker *checker = new Checker(); /* never destroyed: a detached worker may still use it at exit */
    return *checker;
}

void Checker::load_locked()
{
    if (loaded_) return;
    loaded_ = true;
    snap_.build = platform::installed_build();
    snap_.state_existed = read_state(platform::state_path(), snap_.state);
    std::string cached;
    if (read_file(platform::cache_path(), cached) && parse_releases(cached, snap_.releases))
        snap_.offer = choose(snap_.releases, snap_.build);
    else
        snap_.releases.clear();
}

Snapshot Checker::snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    load_locked();
    return snap_;
}

bool Checker::start()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        load_locked();
        if (snap_.running) return false;
        snap_.running = true;
    }
    try {
        std::thread([this] { run(); }).detach();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        snap_.running = false;
        return false;
    }
    return true;
}

void Checker::update_state(const std::function<void(State &)> &change)
{
    std::lock_guard<std::mutex> lock(mutex_);
    load_locked();
    const State before = snap_.state;
    change(snap_.state);
    if (!(snap_.state == before) || !snap_.state_existed) {
        if (write_state(platform::state_path(), snap_.state)) snap_.state_existed = true;
    }
}

void Checker::run()
{
    Build build;
    std::string etag;
    bool have_cache = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        build = snap_.build;
        etag = snap_.state.etag;
        have_cache = !snap_.releases.empty();
    }
    const std::string url = platform::releases_url();
    Outcome outcome = Outcome::Failed;
    Headers headers;
    int http_status = 0;
    std::vector<Release> releases;
    bool fresh = false; /* releases came with this answer */

    if (!platform::network_up()) {
        outcome = Outcome::Offline;
    } else if (!platform::clock_synced(url)) {
        outcome = Outcome::NoClock;
    } else {
        const platform::FetchResult fetched =
            platform::fetch(url, have_cache ? etag : std::string(), "cyberdeckzero-launcher/" + build.version);
        headers = parse_headers(fetched.headers);
        http_status = fetched.http_status;
        outcome = classify(fetched.curl_rc, fetched.http_status, headers);
        if (outcome == Outcome::Ok && fetched.http_status != 304) {
            if (parse_releases(fetched.body, releases)) {
                write_file_atomic(platform::cache_path(), fetched.body);
                fresh = true;
            } else {
                outcome = Outcome::BadAnswer;
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (fresh) snap_.releases = std::move(releases);
    Offer offer;
    if (outcome == Outcome::Ok) {
        offer = choose(snap_.releases, build);
        snap_.offer = offer;
    }
    record_check(snap_.state, outcome, outcome == Outcome::Ok ? &offer : nullptr, headers, platform::now());
    write_state(platform::state_path(), snap_.state);
    snap_.outcome = outcome;
    snap_.http_status = http_status;
    snap_.running = false;
    ++snap_.done;
}

} // namespace launcher_update
