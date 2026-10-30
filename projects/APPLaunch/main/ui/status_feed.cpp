/*
 * The launcher's status feed for apps (see status_feed.hpp).
 */

#include "status_feed.hpp"

#include <cstdlib>
#include <cstring>

namespace status_feed {

bool differs(const cp0_app_status_t &a, const cp0_app_status_t &b)
{
    return a.wifi != b.wifi || a.wifi_pct != b.wifi_pct || a.eth != b.eth || a.bt != b.bt || a.battery != b.battery ||
           std::strcmp(a.battery_state, b.battery_state) != 0 || a.volume != b.volume || a.mute != b.mute ||
           a.brightness_pct != b.brightness_pct || a.ssh != b.ssh || a.ssh_act != b.ssh_act;
}

} // namespace status_feed

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "app_media.hpp"
#include "cp0_lvgl_app.h"
#include "cp0_ssh_watch.h"
#include "launcher_ssh.hpp"
#include "cp0_power_supply.h"
#include "cp0_statusbar.h"
#include "cp0_status_state.h"
#include "cp0_ui_status_strip.h"
#include "hal_lvgl_bsp.h"
#include "sample_log.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <unistd.h>

namespace {

constexpr auto kPeriod = std::chrono::seconds(2);
constexpr auto kBtTimeout = std::chrono::milliseconds(1500);
constexpr auto kLevelsPeriod = std::chrono::seconds(10); // volume and brightness run programs / sysfs: not every 2 s

struct Reply {
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    int code = -1;
    std::string data;
};

// One BlueZ request, waited for here (the thread is ours). The callback may come late or from another thread, so it
// owns the reply through a shared_ptr.
bool bt_request(const char *command, int &code, std::string &data)
{
    auto reply = std::make_shared<Reply>();
    cp0_signal_bt_api({command}, [reply](int c, std::string d) {
        std::lock_guard<std::mutex> lock(reply->mutex);
        reply->code = c;
        reply->data = std::move(d);
        reply->done = true;
        reply->ready.notify_all();
    });
    std::unique_lock<std::mutex> lock(reply->mutex);
    if (!reply->ready.wait_for(lock, kBtTimeout, [&] { return reply->done; })) return false;
    code = reply->code;
    data = reply->data;
    return true;
}

// Exactly what native_ui.cpp's fill_status shows: Powered, and any connected device.
cp0_app_bt_t read_bluetooth()
{
    int code = -1;
    std::string data;
    const bool powered = bt_request("BtStatus", code, data) && code == 0 && !data.empty() && data[0] == '1';
    if (!powered) return CP0_APP_BT_OFF;
    const bool connected = bt_request("BtConnectedList", code, data) && code >= 0 && data.find(':') != std::string::npos;
    return connected ? CP0_APP_BT_CONNECTED : CP0_APP_BT_ON;
}

// Volume, mute and brightness (report 035): read again when the launcher changed one (a key, a request) or every
// kLevelsPeriod (the user may run pactl); in between the last reading stands.
void sample_levels(cp0_app_status_t &now)
{
    static app_media::Levels levels;
    static unsigned seen = ~0u;
    static std::chrono::steady_clock::time_point read_at;
    const auto clock_now = std::chrono::steady_clock::now();
    const unsigned generation = app_media::levels_generation();
    if (seen == ~0u || generation != seen || clock_now - read_at >= kLevelsPeriod) {
        levels = app_media::read_levels();
        seen = generation;
        read_at = clock_now;
    }
    now.volume = levels.volume;
    now.mute = levels.mute;
    now.brightness_pct = levels.brightness_pct;
}

// The SSH indicator (2026-10-30): this thread owns the launcher's one watch (not thread-safe); nothing is read while
// Settings > System > SSH indicator is Off. The sample goes to the launcher's own strip (launcher_ssh::snapshot) and
// into the file: the count and the activity flag only, never a peer address.
void sample_ssh(cp0_app_status_t &now, ssh_indicator::Sampler &ssh)
{
    const ssh_indicator::Snapshot s = ssh.sample(launcher_ssh::enabled(), cp0_ssh_monotonic_ms());
    launcher_ssh::publish(s);
    now.ssh = s.count;
    now.ssh_act = s.active;
}

void sample(cp0_app_status_t &now, ssh_indicator::Sampler &ssh)
{
    sample_ssh(now, ssh);
    cp0_wifi_status_t wifi{};
    const bool read_ok = cp0_wifi_status_read(&wifi) == 0;
    if (!read_ok) wifi = cp0_wifi_status_t{};
    const bool connected = wifi.connected;
    cp0_statusbar_state_t strip{};
    cp0_ui_status_set_wifi(&strip, connected ? 1 : 0, connected ? wifi.signal : 0);
    now.wifi = strip.wifi_up;
    now.wifi_pct = strip.wifi_pct;
    now.eth = wifi.ethernet ? 1 : 0;
    now.bt = read_bluetooth();
    cp0_power_battery_t battery{};
    if (cp0_power_supply_battery(&battery) && battery.percent >= 0) {
        now.battery = battery.percent > 100 ? 100 : battery.percent;
        std::snprintf(now.battery_state, sizeof(now.battery_state), "%s", cp0_power_supply_state_name(battery.state));
        now.battery_charging = battery.state == CP0_BATTERY_CHARGING;
    } else {
        now.battery = -1;
        now.battery_state[0] = '\0';
        now.battery_charging = 0;
    }
    sample_levels(now);
}

struct Feed {
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
};

Feed &feed()
{
    static Feed instance;
    return instance;
}

void run()
{
    char path[512];
    if (cp0_status_state_path(std::getenv("XDG_RUNTIME_DIR"), path, sizeof(path)) != 0) {
        SLOGW("[STATUS] XDG_RUNTIME_DIR is too long: status feed not published");
        return;
    }
    // A restarted launcher continues gen from the file of its previous run.
    cp0_app_status_t last;
    cp0_app_status_load(path, &last);
    unsigned gen = last.gen;
    cp0_app_status_t written;
    std::memset(&written, 0, sizeof(written));
    bool have_written = false;
    bool write_failed = false;
    Feed &f = feed();
    ssh_indicator::Sampler ssh;
    for (;;) {
        cp0_app_status_t now;
        std::memset(&now, 0, sizeof(now));
        sample(now, ssh);
        if (!have_written || status_feed::differs(now, written)) {
            now.pid = static_cast<int>(::getpid());
            now.gen = ++gen;
            if (cp0_status_state_write(path, &now) != 0) {
                if (!write_failed) SLOGW("[STATUS] cannot write %s: %s", path, std::strerror(errno));
                write_failed = true;
            } else {
                write_failed = false;
                written = now;
                have_written = true;
            }
        }
        std::unique_lock<std::mutex> lock(f.mutex);
        if (f.wake.wait_for(lock, kPeriod, [&] { return f.stopping; })) return;
    }
}

} // namespace

namespace status_feed {

void start()
{
    Feed &f = feed();
    std::lock_guard<std::mutex> lock(f.mutex);
    if (f.worker.joinable()) return;
    f.stopping = false;
    f.worker = std::thread(run);
}

void stop()
{
    Feed &f = feed();
    {
        std::lock_guard<std::mutex> lock(f.mutex);
        if (!f.worker.joinable()) return;
        f.stopping = true;
    }
    f.wake.notify_all();
    f.worker.join();
}

} // namespace status_feed

#else

namespace status_feed {

void start() {}
void stop() {}

} // namespace status_feed

#endif
