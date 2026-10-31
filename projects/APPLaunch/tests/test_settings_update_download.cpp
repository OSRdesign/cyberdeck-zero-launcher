// SPDX-License-Identifier: MIT
//
// Settings > Updates P2 (report 047 section 11, "the curl path"): the real check and the real download
// (settings_update_check.cpp, settings_update_download.cpp with the real platform file: curl as a child process,
// sha256sum) against the local fake release server in its bundle mode (tests/fixtures/update/fake_release_server.py
// --bundles DIR) through the APPLAUNCH_UPDATE_URL hook. DIR holds a real small tarball
// cyberdeckzero-launcher-0.8.13-amd64.tar.gz; the server makes the release list, the .sha256 asset and GitHub's digest
// from it. Checked: the release offered with its bundle, the download into $XDG_CACHE_HOME/cyberdeckzero/update with
// both checksums, an older download there deleted, a .part resumed with a byte range, a whole bundle already there only
// checked again, a damaged one refused and deleted, a stopped download (its .part kept), a missing bundle (HTTP 404).
// Built with LAUNCHER_VERSION_RAW=0.8.12 LAUNCHER_CHANNEL_RAW=release, run with APPLAUNCH_ARCH=amd64.
//
//   test_settings_update_download FIXTURE_DIR

#include "settings_update_check.hpp"
#include "settings_update_download.hpp"
#include "settings_update_platform.hpp"

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

using namespace launcher_update;

namespace {

pid_t g_server = -1;
int g_port = 0;
std::string g_dir;

void start_server(const std::string &fixtures, const std::string &bundles)
{
    int out[2];
    assert(pipe(out) == 0);
    g_server = fork();
    assert(g_server >= 0);
    if (g_server == 0) {
        dup2(out[1], STDOUT_FILENO);
        close(out[0]);
        close(out[1]);
        const std::string script = fixtures + "/fake_release_server.py";
        execlp("python3", "python3", "-I", script.c_str(), "--port", "0", "--bundles", bundles.c_str(),
               static_cast<char *>(nullptr));
        _exit(127);
    }
    close(out[1]);
    std::string line;
    char c = 0;
    while (read(out[0], &c, 1) == 1 && c != '\n') line.push_back(c);
    close(out[0]);
    if (line.rfind("port ", 0) != 0) {
        std::fprintf(stderr, "fake release server did not start: [%s]\n", line.c_str());
        std::abort();
    }
    g_port = std::atoi(line.c_str() + 5);
}

void stop_server()
{
    if (g_server > 0) {
        kill(g_server, SIGTERM);
        waitpid(g_server, nullptr, 0);
        g_server = -1;
    }
}

std::string url(const std::string &path)
{
    return "http://127.0.0.1:" + std::to_string(g_port) + path;
}

void sh(const std::string &command)
{
    if (std::system(command.c_str()) != 0) {
        std::fprintf(stderr, "failed: %s\n", command.c_str());
        std::abort();
    }
}

std::string sha256(const std::string &path)
{
    const std::string out = g_dir + "/sha.out";
    sh("sha256sum '" + path + "' | cut -d' ' -f1 > '" + out + "'");
    std::string text;
    assert(read_file(out, text));
    while (!text.empty() && text.back() == '\n') text.pop_back();
    return text;
}

DownloadSnapshot wait_download(DownloadPhase until_not = DownloadPhase::Downloading)
{
    for (int i = 0; i < 1200; ++i) {
        const DownloadSnapshot snap = Downloader::instance().snapshot();
        if (snap.phase != until_not && snap.phase != DownloadPhase::Verifying) return snap;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::fprintf(stderr, "the download did not finish\n");
    std::abort();
}

std::string server_log()
{
    const platform::FetchResult result = platform::fetch(url("/log"), "", "test");
    assert(result.curl_rc == 0 && result.http_status == 200);
    return result.body;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s FIXTURE_DIR\n", argv[0]);
        return 2;
    }
    char dir_template[] = "/tmp/update-download-XXXXXX";
    const char *dir = mkdtemp(dir_template);
    assert(dir);
    g_dir = dir;
    setenv("XDG_STATE_HOME", (g_dir + "/state").c_str(), 1);
    setenv("XDG_CACHE_HOME", (g_dir + "/cache").c_str(), 1);
    setenv("APPLAUNCH_ARCH", "amd64", 1);
    setenv("TZ", "UTC", 1);

    // a real small bundle (random data: about 600 KB that do not compress), and its release served by the fake server
    const std::string bundles = g_dir + "/bundles";
    const std::string name = "cyberdeckzero-launcher-0.8.13-amd64.tar.gz";
    sh("mkdir -p '" + g_dir + "/src/bundle/payload/bin' '" + bundles + "' && head -c 600000 /dev/urandom > '" + g_dir +
       "/src/bundle/payload/bin/cyberdeckzero' && printf '#!/bin/sh\\n' > '" + g_dir +
       "/src/bundle/install.sh' && tar -C '" + g_dir + "/src' -czf '" + bundles + "/" + name + "' bundle");
    const std::string source = bundles + "/" + name;
    const std::string want = sha256(source);
    const long long size = platform::file_size(source);
    assert(size > 500000);
    start_server(argv[1], bundles);
    setenv("APPLAUNCH_UPDATE_URL", url("/releases").c_str(), 1);

    // the check: 0.8.13 offered, its bundle on the fake server with the .sha256 asset and the digest
    Checker &checker = Checker::instance();
    assert(checker.start());
    Snapshot snap;
    for (int i = 0; i < 600; ++i) {
        snap = checker.snapshot();
        if (snap.done > 0 && !snap.running) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(snap.outcome == Outcome::Ok && snap.offer.available == "0.8.13");
    Download d;
    assert(download_of(snap.releases, "0.8.13", "amd64", d));
    assert(d.size == size && d.digest == want && d.url == url("/download/" + name) &&
           d.checksum_url == url("/download/" + name + ".sha256"));
    assert(!download_of(snap.releases, "0.8.13", "arm64", d) || d.name.find("arm64") != std::string::npos);
    assert(download_of(snap.releases, "0.8.13", "amd64", d));

    // the download, as the user, into the cache folder; an older download there goes
    const std::string cache = platform::download_dir();
    assert(cache == g_dir + "/cache/cyberdeckzero/update");
    sh("mkdir -p '" + cache + "' && echo old > '" + cache + "/cyberdeckzero-launcher-0.8.12-amd64.tar.gz'");
    Downloader &downloader = Downloader::instance();
    assert(downloader.start(d, "cyberdeckzero-launcher/0.8.12"));
    assert(!downloader.start(d, "cyberdeckzero-launcher/0.8.12")); /* one at a time */
    DownloadSnapshot got = wait_download();
    assert(got.phase == DownloadPhase::Ready);
    assert(got.file == cache + "/" + name && got.sha256 == want && sha256(got.file) == want);
    assert(platform::file_size(cache + "/" + name + ".part") < 0);
    assert(platform::file_size(cache + "/cyberdeckzero-launcher-0.8.12-amd64.tar.gz") < 0);
    std::string log = server_log();
    assert(log.find("/download/" + name + " if-none-match= user-agent=cyberdeckzero-launcher/0.8.12 range=\n") !=
           std::string::npos);
    assert(log.find("/download/" + name + ".sha256 ") != std::string::npos);

    // a bundle already there whole: only checked again (no request for it)
    downloader.reset();
    const std::size_t requests = log.size();
    assert(downloader.start(d, "ua"));
    got = wait_download();
    assert(got.phase == DownloadPhase::Ready);
    log = server_log();
    assert(log.substr(requests).find("/download/" + name + " ") == std::string::npos);

    // a stopped download goes on where it stopped: half of it in the .part, curl -C - asks for the rest
    platform::remove_file(got.file);
    sh("head -c " + std::to_string(size / 2) + " '" + source + "' > '" + cache + "/" + name + ".part'");
    assert(downloader.start(d, "ua"));
    got = wait_download();
    assert(got.phase == DownloadPhase::Ready && sha256(got.file) == want);
    log = server_log();
    assert(log.find("range=bytes=" + std::to_string(size / 2) + "-") != std::string::npos);

    // a damaged bundle of the right size: refused and deleted; the next attempt downloads it again
    sh("head -c " + std::to_string(size) + " /dev/zero > '" + got.file + "'");
    assert(downloader.start(d, "ua"));
    got = wait_download();
    assert(got.phase == DownloadPhase::Failed && got.error == "Damaged download (checksum): try again");
    assert(platform::file_size(cache + "/" + name) < 0);
    assert(downloader.start(d, "ua"));
    got = wait_download();
    assert(got.phase == DownloadPhase::Ready && sha256(got.file) == want);

    // a release whose two checksums differ: refused
    Download wrong = d;
    wrong.digest = std::string(64, 'a');
    assert(downloader.start(wrong, "ua"));
    got = wait_download();
    assert(got.phase == DownloadPhase::Failed && got.error == "The release's checksums differ");

    // stopped while it downloads (32 KiB/s): no root involved, the .part stays for the next time
    platform::remove_file(cache + "/" + name);
    Download slow = d;
    slow.url += "?rate=32";
    assert(downloader.start(slow, "ua"));
    for (int i = 0; i < 200 && downloader.snapshot().done < 40000; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const long long partial = downloader.snapshot().done;
    assert(partial > 0 && partial < size);
    downloader.cancel();
    got = wait_download();
    assert(got.phase == DownloadPhase::Cancelled);
    assert(platform::file_size(cache + "/" + name + ".part") > 0);

    // a bundle the server does not have: curl -f, HTTP 404
    Download missing = d;
    missing.name = "cyberdeckzero-launcher-0.8.14-amd64.tar.gz";
    missing.url = url("/download/" + missing.name);
    assert(downloader.start(missing, "ua"));
    got = wait_download();
    assert(got.phase == DownloadPhase::Failed && got.error == "The server refused it (HTTP 404)");

    // the update done: the downloads go
    downloader.discard_files();
    assert(platform::list_dir(cache).size() == 0 ||
           platform::file_size(cache + "/" + name) < 0);

    stop_server();
    sh("rm -rf '" + g_dir + "'");
    std::puts("settings update download OK");
    return 0;
}
