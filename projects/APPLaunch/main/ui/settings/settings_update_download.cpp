/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates P2: the download (see settings_update_download.hpp).
 */

#include "settings_update_download.hpp"

#include "settings_update_platform.hpp"

#include <thread>
#include <utility>

namespace launcher_update {

Downloader &Downloader::instance()
{
    static Downloader *downloader = new Downloader(); /* never destroyed: a detached worker may still use it at exit */
    return *downloader;
}

DownloadSnapshot Downloader::snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (snap_.phase == DownloadPhase::Downloading && !snap_.download.name.empty()) {
        const long long size = platform::file_size(platform::download_dir() + "/" + snap_.download.name + ".part");
        if (size >= 0) snap_.done = size;
    }
    return snap_;
}

bool Downloader::start(const Download &download, const std::string &user_agent)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) return false;
        running_ = true;
        cancel_ = false;
        snap_.download = download;
        snap_.done = 0;
        snap_.file.clear();
        snap_.sha256.clear();
        snap_.error.clear();
        snap_.phase = DownloadPhase::Downloading;
        ++snap_.generation;
    }
    try {
        std::thread([this, download, user_agent] { run(download, user_agent); }).detach();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        snap_.phase = DownloadPhase::Failed;
        snap_.error = "Could not start the download";
        ++snap_.generation;
        return false;
    }
    return true;
}

void Downloader::cancel()
{
    cancel_ = true;
}

void Downloader::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    snap_.phase = DownloadPhase::Idle;
    snap_.error.clear();
    ++snap_.generation;
}

void Downloader::discard_files()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    const std::string dir = platform::download_dir();
    for (const std::string &name : stale_downloads(platform::list_dir(dir), std::string()))
        platform::remove_file(dir + "/" + name);
    snap_ = DownloadSnapshot{DownloadPhase::Idle, {}, 0, {}, {}, {}, snap_.generation + 1};
}

void Downloader::set_phase(DownloadPhase phase, const std::string &error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    snap_.phase = phase;
    snap_.error = error;
    if (phase != DownloadPhase::Downloading && phase != DownloadPhase::Verifying) running_ = false;
    ++snap_.generation;
}

void Downloader::run(Download d, std::string user_agent)
{
    const std::string dir = platform::download_dir();
    const std::string bundle = dir + "/" + d.name;
    const std::string part = bundle + ".part";
    for (const std::string &name : stale_downloads(platform::list_dir(dir), d.name)) platform::remove_file(dir + "/" + name);

    // 1. the bundle: already whole, or curl into the .part (resumed), then renamed
    bool whole = d.size > 0 && platform::file_size(bundle) == d.size;
    std::string error;
    if (!whole) {
        platform::remove_file(bundle);
        for (int attempt = 0; attempt < 2 && !whole && error.empty(); ++attempt) {
            const platform::DownloadResult result = platform::download(d.url, part, user_agent, cancel_);
            if (result.curl_rc == platform::kCancelled || cancel_) {
                set_phase(DownloadPhase::Cancelled);
                return;
            }
            const long long have = platform::file_size(part);
            const bool unsatisfiable = result.http_status == 416 || result.curl_rc == 33;
            if (have >= 0 && have == d.size && (result.curl_rc == 0 || unsatisfiable)) {
                whole = platform::rename_file(part, bundle);
                if (!whole) error = "Cannot keep the download: disk full?";
            } else if (result.curl_rc == 0 && d.size <= 0 && have > 0) {
                whole = platform::rename_file(part, bundle); /* no size published: curl's word */
            } else if (unsatisfiable || (result.curl_rc == 0 && have > d.size)) {
                platform::remove_file(part); /* a .part the server cannot resume: once more from the start */
            } else if (result.curl_rc == 0) {
                error = "The download is incomplete: try again";
            } else {
                error = download_failure_text(result.curl_rc, result.http_status);
            }
        }
        if (!whole && error.empty()) error = "The download failed: try again";
    }
    if (!whole) {
        set_phase(DownloadPhase::Failed, error);
        return;
    }

    // 2. the check: the .sha256 asset and GitHub's digest
    set_phase(DownloadPhase::Verifying);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = true;
        snap_.done = d.size;
    }
    std::string published;
    if (!d.checksum_url.empty()) {
        const platform::FetchResult fetched = platform::fetch_text(d.checksum_url, user_agent);
        if (fetched.curl_rc == 0) published = parse_checksum_file(fetched.body, d.name);
    }
    if (cancel_) {
        set_phase(DownloadPhase::Cancelled);
        return;
    }
    const std::string computed = platform::sha256_of(bundle);
    switch (check_hash(computed, published, d.digest)) {
    case HashCheck::Ok: {
        std::lock_guard<std::mutex> lock(mutex_);
        snap_.file = bundle;
        snap_.sha256 = computed;
        snap_.phase = DownloadPhase::Ready;
        snap_.error.clear();
        running_ = false;
        ++snap_.generation;
        return;
    }
    case HashCheck::Mismatch:
        platform::remove_file(bundle);
        set_phase(DownloadPhase::Failed, "Damaged download (checksum): try again");
        return;
    case HashCheck::Disagree:
        platform::remove_file(bundle);
        set_phase(DownloadPhase::Failed, "The release's checksums differ");
        return;
    case HashCheck::NoReference:
        set_phase(DownloadPhase::Failed, "No checksum for " + d.version + ": try later");
        return;
    }
}

} // namespace launcher_update
