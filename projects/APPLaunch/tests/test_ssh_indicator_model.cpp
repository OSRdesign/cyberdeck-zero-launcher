// SPDX-License-Identifier: MIT
//
// The SSH indicator's launcher side (model/ssh_indicator_model.hpp, 2026-10-30), on a fake /proc tree: the feed's
// samples with the setting On (a session confirmed by the second sample, traffic, the end of the session) and Off
// (nothing, even with a session in the tree; the connections forgotten, so On confirms them again), what the launcher's
// own strip shows (nothing while Off, no blink while locked), and the config value of the setting (absent = On).
#include "model/ssh_indicator_model.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                             \
        }                                                                             \
    } while (0)

namespace {

std::string g_root;

void write_tcp(int sessions, bool traffic)
{
    const std::string dir = g_root + "/proc/net";
    std::string text = "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
                       "   0: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 100 1\n";
    for (int i = 0; i < sessions; ++i) {
        char line[160];
        std::snprintf(line, sizeof(line),
                      "%4d: 1444A8C0:0016 0C44A8C0:%04X 01 %08X:00000000 02:000AFC7B 00000000     0        0 %d 4\n",
                      i + 1, 50000 + i, traffic ? 0x24 : 0, 200 + i);
        text += line;
    }
    for (const std::string &name : {std::string("tcp"), std::string("tcp6")}) {
        FILE *f = std::fopen((dir + "/" + name).c_str(), "w");
        CHECK(f != nullptr);
        std::fputs(name == "tcp" ? text.c_str() : "", f);
        std::fclose(f);
    }
}

void test_sampler()
{
    ssh_indicator::Sampler sampler(g_root.c_str());
    int64_t now = 1000;
    auto next = [&](bool enabled) {
        now += 2000;
        return sampler.sample(enabled, now);
    };

    // On: nothing, then a session seen once (not yet), then confirmed
    write_tcp(0, false);
    ssh_indicator::Snapshot s = next(true);
    CHECK(s.count == 0 && s.active == 0);
    write_tcp(1, false);
    s = next(true);
    CHECK(s.count == 0 && s.active == 0);
    s = next(true);
    CHECK(s.count == 1 && s.active == 0);
    // traffic (the queue fallback under a fake root), two sessions, then idle again
    write_tcp(1, true);
    s = next(true);
    CHECK(s.count == 1 && s.active == 1);
    write_tcp(2, true);
    s = next(true);
    CHECK(s.count == 1 && s.active == 1); // the second one is not confirmed yet
    s = next(true);
    CHECK(s.count == 2 && s.active == 1);
    write_tcp(2, false);
    s = next(true);
    CHECK(s.count == 2 && s.active == 0);

    // Off: nothing, whatever the tree holds
    write_tcp(2, true);
    s = next(false);
    CHECK(s.count == 0 && s.active == 0);
    s = next(false);
    CHECK(s.count == 0 && s.active == 0);
    // On again: the sessions are confirmed again (forgotten while Off), not shown at the first sample
    s = next(true);
    CHECK(s.count == 0 && s.active == 0);
    s = next(true);
    CHECK(s.count == 2 && s.active == 1);

    // the session ends
    write_tcp(0, false);
    s = next(true);
    CHECK(s.count == 0 && s.active == 0);
}

void test_shown()
{
    ssh_indicator::Snapshot feed;
    feed.count = 2;
    feed.active = 1;
    ssh_indicator::Snapshot s = ssh_indicator::shown(feed, true, false);
    CHECK(s.count == 2 && s.active == 1);
    s = ssh_indicator::shown(feed, true, true); // locked: steady, no blink behind the lock screen
    CHECK(s.count == 2 && s.active == 0);
    s = ssh_indicator::shown(feed, false, false); // Off: nothing, even before the feed's next sample
    CHECK(s.count == 0 && s.active == 0);
    feed.count = 0;
    s = ssh_indicator::shown(feed, true, false); // no session: no activity either
    CHECK(s.count == 0 && s.active == 0);
}

void test_mode()
{
    CHECK(ssh_indicator::parse_mode("0") == ssh_indicator::kOff);
    CHECK(ssh_indicator::parse_mode("1") == ssh_indicator::kOn);
    CHECK(ssh_indicator::parse_mode("") == ssh_indicator::kOn); // no reply: the default
    CHECK(ssh_indicator::parse_mode("7") == ssh_indicator::kOn);
    CHECK(std::string(ssh_indicator::kConfigKey) == "ssh_indicator");
}

} // namespace

int main()
{
    char root[] = "/tmp/applaunch-ssh-indicator-XXXXXX";
    CHECK(mkdtemp(root) != nullptr);
    g_root = root;
    CHECK(mkdir((g_root + "/proc").c_str(), 0755) == 0 && mkdir((g_root + "/proc/net").c_str(), 0755) == 0);
    test_sampler();
    test_shown();
    test_mode();
    CHECK(std::system(("rm -rf '" + g_root + "'").c_str()) == 0);
    std::puts("test_ssh_indicator_model: PASS");
    return 0;
}
