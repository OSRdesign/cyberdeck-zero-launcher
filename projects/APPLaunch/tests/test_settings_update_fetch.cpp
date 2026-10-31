// SPDX-License-Identifier: MIT
//
// Settings > Updates (report 047 P1, section 11 "the curl path"): the real check (settings_update_check.cpp with the
// real platform file: curl as a child process) against the local fake release server
// (tests/fixtures/update/fake_release_server.py) through the APPLAUNCH_UPDATE_URL hook: a 200 saved as the cached answer
// with its ETag and the state file written, the next check sending If-None-Match and taking the 304 from the cache, the
// User-Agent, a redirect, the 403 rate limit with its reset time, a 500, an answer that is no release list, a closed
// port. State and cache go to a temporary XDG_STATE_HOME / XDG_CACHE_HOME. Built with LAUNCHER_VERSION_RAW=0.8.12
// LAUNCHER_CHANNEL_RAW=release and run with APPLAUNCH_ARCH=amd64.
//
//   test_settings_update_fetch FIXTURE_DIR

#include "settings_update_check.hpp"
#include "settings_update_platform.hpp"

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

using namespace launcher_update;

namespace {

pid_t g_server = -1;
int g_port = 0;

void start_server(const std::string &fixtures)
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
        execlp("python3", "python3", "-I", script.c_str(), "--port", "0", static_cast<char *>(nullptr));
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

std::string url(const char *path)
{
    return "http://127.0.0.1:" + std::to_string(g_port) + path;
}

Snapshot check(const char *path)
{
    setenv("APPLAUNCH_UPDATE_URL", url(path).c_str(), 1);
    Checker &checker = Checker::instance();
    const std::uint64_t before = checker.snapshot().done;
    assert(checker.start());
    assert(!checker.start()); /* one at a time */
    for (int i = 0; i < 600; ++i) {
        const Snapshot snap = checker.snapshot();
        if (snap.done > before && !snap.running) return snap;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::fprintf(stderr, "the check of %s did not finish\n", path);
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
    char dir_template[] = "/tmp/update-fetch-XXXXXX";
    const char *dir = mkdtemp(dir_template);
    assert(dir);
    setenv("XDG_STATE_HOME", (std::string(dir) + "/state").c_str(), 1);
    setenv("XDG_CACHE_HOME", (std::string(dir) + "/cache").c_str(), 1);
    setenv("APPLAUNCH_ARCH", "amd64", 1);
    setenv("TZ", "UTC", 1);
    start_server(argv[1]);

    // the hook
    setenv("APPLAUNCH_UPDATE_URL", "http://example.invalid/x", 1);
    assert(platform::releases_url() == "http://example.invalid/x");
    unsetenv("APPLAUNCH_UPDATE_URL");
    assert(platform::releases_url() == kDefaultReleasesUrl);
    assert(platform::state_path() == std::string(dir) + "/state/cyberdeckzero/update.state");
    assert(platform::cache_path() == std::string(dir) + "/cache/cyberdeckzero/update/releases.json");
    assert(platform::clock_synced("http://127.0.0.1/")); /* no certificate: no clock rule */

    Snapshot snap = Checker::instance().snapshot();
    assert(snap.build.version == "0.8.12" && snap.build.channel == "release" && snap.build.arch == "amd64");
    assert(!snap.state_existed && snap.releases.empty());

    // 200: the list, saved with its ETag; 0.8.13 offered (the draft and the pre-release above it ignored)
    snap = check("/releases");
    assert(snap.outcome == Outcome::Ok && snap.http_status == 200);
    assert(snap.offer.available == "0.8.13" && snap.releases.size() == 7);
    assert(snap.state.result == "ok" && snap.state.available == "0.8.13" && snap.state.etag.rfind("W/\"", 0) == 0);
    State on_disk;
    assert(read_state(platform::state_path(), on_disk) && on_disk == snap.state);
    std::string cached;
    assert(read_file(platform::cache_path(), cached) && cached.size() > 1000);

    // 304: If-None-Match sent, the cached list used
    snap = check("/releases");
    assert(snap.outcome == Outcome::Ok && snap.http_status == 304 && snap.offer.available == "0.8.13");
    std::string log = server_log();
    assert(log.find("/releases if-none-match= user-agent=cyberdeckzero-launcher/0.8.12") != std::string::npos);
    assert(log.find("/releases if-none-match=W/\"") != std::string::npos);

    // a redirect is followed
    snap = check("/moved");
    assert(snap.outcome == Outcome::Ok);

    // 403 with GitHub's rate limit headers: what is known stays
    snap = check("/ratelimit");
    assert(snap.outcome == Outcome::RateLimited && snap.http_status == 403);
    assert(snap.state.rate_reset == 1793480400LL && snap.state.available == "0.8.13" &&
           snap.state.result == "rate-limited");
    assert(outcome_text(snap.outcome, snap.http_status, clock_text(snap.state.rate_reset)) ==
           "GitHub's limit reached: try again after 21:00");
    assert(root_value(snap.state, snap.build).text == "0.8.13 available");

    // 500, and a 200 that is no release list (a captive portal)
    snap = check("/error");
    assert(snap.outcome == Outcome::Http && snap.http_status == 500);
    snap = check("/notjson");
    assert(snap.outcome == Outcome::BadAnswer);
    assert(read_file(platform::cache_path(), cached) && cached.find("captive") == std::string::npos);

    // nothing listens: connect refused
    stop_server();
    snap = check("/releases");
    assert(snap.outcome == Outcome::Connect);
    assert(root_value(snap.state, snap.build).text == "0.8.13 available");

    std::string command = std::string("rm -rf '") + dir + "'";
    assert(std::system(command.c_str()) == 0);
    std::puts("settings update fetch OK");
    return 0;
}
