/*
 * SPDX-License-Identifier: MIT
 *
 * The SSH indicator's launcher side without a service (owner decisions 2026-10-30, study "SSH session indicator in
 * the status strip"):
 *
 *   Sampler   the launcher's one cp0_ssh_watch (cp0_ssh_watch.h), owned by one thread: the status feed's (and the
 *             render harness's scene thread). Settings > System > SSH indicator Off: nothing is read (no /proc read,
 *             no netlink dump) and the connections are forgotten, so a session is confirmed again (two samples) when
 *             the setting comes back On.
 *   shown()   what the launcher's own strip draws from the feed's last sample: nothing while Off, the glyph steady (no
 *             blink, no redraw behind it) while the lock screen is up.
 *   parse_mode()  the config value of the setting: a missing or unknown value reads as On (the default).
 *
 * Plain C++, no LVGL, no service call (tests/test_ssh_indicator_model.cpp). Peer addresses never leave the watch here:
 * only the count and the activity flag.
 */
#pragma once

#include "cp0_ssh_watch.h"

#include <cstdint>
#include <string>

namespace ssh_indicator {

constexpr int kOff = 0;
constexpr int kOn = 1;                               /* the default: no config key */
constexpr const char *kConfigKey = "ssh_indicator"; /* written only when the user changes it */

struct Snapshot {
    int count = 0;  /* confirmed incoming SSH connections: status.state ssh= */
    int active = 0; /* bytes moved on one since the previous sample: ssh_act= (0 when count is 0) */
};

class Sampler {
public:
    /* root: the watch's fake tree (cp0_ssh_watch_options_t.root); NULL = $APPLAUNCH_STATUS_SYSFS_ROOT or the real
     * files. No I/O here. */
    explicit Sampler(const char *root = nullptr);

    /* One sample at now_ms (a monotonic clock, cp0_ssh_monotonic_ms()), designed every 2 s. enabled false: {0, 0}
     * without reading anything. */
    Snapshot sample(bool enabled, int64_t now_ms);

private:
    void reset();

    std::string root_;
    bool has_root_ = false;
    bool sampling_ = false;
    cp0_ssh_watch_t watch_{};
};

/* The strip's view of the feed's sample: {0, 0} when !enabled; active 0 while locked. */
Snapshot shown(const Snapshot &feed, bool enabled, bool locked);

/* GetInt's reply text -> kOff or kOn ("0" is Off, "1" On; anything else, or nothing, the default On). */
int parse_mode(const std::string &text);

} // namespace ssh_indicator
