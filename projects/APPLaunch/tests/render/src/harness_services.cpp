/* SPDX-License-Identifier: MIT
 *
 * Render harness: stubbed services and the fake wall clock.
 *
 * On the device the cp0_signal_* callback lists (defined by the real commount.cpp, linked here)
 * get their handlers from the cp0_lvgl service implementations (D-Bus, nmcli, sysfs...). Here each
 * list gets one synchronous handler that answers from harness::fixtures() in the documented wire
 * formats (docs/cp0_lvgl.en.md). An unknown command answers -1 and is logged once, so a scene that
 * needs more fixtures says so on stderr.
 */
#include "harness.h"

#include "cp0_network_api_contract.hpp"
#include "hal_lvgl_bsp.h"
#include "cp0_lvgl_app.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>

#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

/* --------------------------------------------------------------------------- fake wall clock */

namespace {
int64_t s_wall = 1791656000; /* overwritten by every scene (clock command) */
}

extern "C" void harness_set_wall_clock(int64_t epoch_seconds) { s_wall = epoch_seconds; }
extern "C" int64_t harness_wall_clock(void) { return s_wall; }

/* Interposed libc entry points: every time() / gettimeofday() / clock_gettime(CLOCK_REALTIME)
 * made by the launcher code reads the scene's clock (status bar, screensaver, Settings). The
 * monotonic clocks stay real (threads, timeouts); LVGL's tick is faked separately. */
extern "C" time_t time(time_t *out)
{
    const time_t value = static_cast<time_t>(s_wall);
    if (out) *out = value;
    return value;
}

extern "C" int gettimeofday(struct timeval *tv, void *)
{
    if (tv) {
        tv->tv_sec = static_cast<time_t>(s_wall);
        tv->tv_usec = 0;
    }
    return 0;
}

extern "C" int clock_gettime(clockid_t clock, struct timespec *ts)
{
    if ((clock == CLOCK_REALTIME || clock == CLOCK_REALTIME_COARSE) && ts) {
        ts->tv_sec = static_cast<time_t>(s_wall);
        ts->tv_nsec = 0;
        return 0;
    }
    return static_cast<int>(syscall(SYS_clock_gettime, clock, ts));
}

/* ------------------------------------------------------------------------------- fixtures */

namespace harness {

Fixtures &fixtures()
{
    static Fixtures f;
    return f;
}

namespace {

using Reply = std::function<void(int, std::string)>;
using Args = std::list<std::string>;

std::map<std::string, std::string> &config()
{
    static std::map<std::string, std::string> values;
    return values;
}

void unknown(const char *service, const Args &args, const Reply &reply)
{
    static std::set<std::string> logged;
    const std::string command = args.empty() ? std::string("(none)") : args.front();
    if (logged.insert(std::string(service) + ":" + command).second)
        std::fprintf(stderr, "[harness] %s %s: no fixture, answering -1\n", service, command.c_str());
    if (reply) reply(-1, "harness: no fixture");
}

std::string arg(const Args &args, std::size_t index)
{
    if (index >= args.size()) return {};
    auto it = args.begin();
    std::advance(it, static_cast<long>(index));
    return *it;
}

bool starts_with(const std::string &value, const std::string &prefix)
{
    return value.compare(0, prefix.size(), prefix) == 0;
}

std::string lower_ext(const std::string &path)
{
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot + 1);
    for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

} // namespace

/* Same rules as Cp0Filesystem::resolve_path (cp0_lvgl_filesystem.cpp) with /usr/share/APPLaunch
 * replaced by the repository's resource tree. Images are LVGL paths ("A:" + absolute path, the
 * harness LVGL config has an empty LV_FS_POSIX_PATH), fonts and audio absolute paths. */
std::string resolve_resource(const std::string &file)
{
    const std::string &root = fixtures().resource_root;
    if (file.empty()) return "";
    if (file == "applications") return root + "/applications";
    if (file == "home_dir") return "/tmp";
    if (file.size() >= 2 && std::isalpha(static_cast<unsigned char>(file[0])) && file[1] == ':') return file;
    std::string rel = file;
    if (starts_with(rel, "/usr/share/APPLaunch/")) rel = rel.substr(std::strlen("/usr/share/APPLaunch/"));
    else if (starts_with(rel, "APPLaunch/")) rel = rel.substr(std::strlen("APPLaunch/"));
    const std::string ext = lower_ext(file);
    if (rel.find("..") != std::string::npos && !ext.empty()) return "";
    if (ext == "png" || ext == "gif" || ext == "jpg" || ext == "jpeg" || ext == "svg") {
        if (!rel.empty() && rel.front() == '/') return "A:" + rel;
        if (starts_with(rel, "share/images/")) return "A:" + root + "/" + rel;
        return "A:" + root + "/share/images/" + rel;
    }
    if (ext == "ttf" || ext == "otf") {
        if (!rel.empty() && rel.front() == '/') return rel;
        if (starts_with(rel, "share/font/")) rel = rel.substr(std::strlen("share/font/"));
        return root + "/share/font/" + rel;
    }
    if (ext == "wav" || ext == "mp3" || ext == "ogg") {
        if (!rel.empty() && rel.front() == '/') return rel;
        if (starts_with(rel, "share/audio/")) rel = rel.substr(std::strlen("share/audio/"));
        return root + "/share/audio/" + rel;
    }
    return file;
}

void config_set(const std::string &key, const std::string &value)
{
    config()[key] = value;
}

void install_services()
{
    cp0_signal_config_api.append([](Args args, Reply reply) {
        const std::string command = arg(args, 0);
        if (command == "GetInt" || command == "GetStr") {
            const auto found = config().find(arg(args, 1));
            if (reply) reply(0, found != config().end() ? found->second : arg(args, 2));
        } else if (command == "SetInt" || command == "SetStr") {
            config()[arg(args, 1)] = arg(args, 2);
            if (reply) reply(0, "");
        } else if (command == "SetManyAndSave") {
            for (std::size_t i = 1; i + 1 < args.size(); i += 2) config()[arg(args, i)] = arg(args, i + 1);
            if (reply) reply(0, "");
        } else if (command == "Save" || command == "Init") {
            if (reply) reply(0, "");
        } else {
            unknown("config", args, reply);
        }
    });

    cp0_signal_filesystem_api.append([](Args args, Reply reply) {
        const std::string command = arg(args, 0);
        if (command == "Path" && args.size() == 2) {
            if (reply) reply(0, resolve_resource(arg(args, 1)));
        } else if (command == "Exists") {
            if (reply) reply(0, access(arg(args, 1).c_str(), R_OK) == 0 ? "1" : "0");
        } else {
            unknown("filesystem", args, reply);
        }
    });

    cp0_signal_wifi_api.append([](Args args, Reply reply) {
        const Fixtures &f = fixtures();
        const std::string command = arg(args, 0);
        if (command == "Status") {
            cp0_wifi_status_t status{};
            status.connected = f.wifi_connected ? 1 : 0;
            std::snprintf(status.ssid, sizeof(status.ssid), "%s", f.wifi_connected ? f.wifi_ssid.c_str() : "");
            std::snprintf(status.ip, sizeof(status.ip), "%s", f.wifi_connected ? f.wifi_ip.c_str() : "");
            status.signal = f.wifi_connected ? f.wifi_signal : 0;
            if (reply) reply(0, cp0::network::encode_status_payload(status));
        } else if (command == "RadioEnabled") {
            if (reply) reply(f.wifi_radio ? 1 : 0, "");
        } else {
            unknown("wifi", args, reply);
        }
    });

    cp0_signal_bt_api.append([](Args args, Reply reply) {
        const Fixtures &f = fixtures();
        const std::string command = arg(args, 0);
        if (command == "BtStatus") {
            if (reply) reply(0, std::string(f.bt_powered ? "1" : "0") + "\t00:11:22:33:44:55\t0\tzero7");
        } else if (command == "BtConnectedList") {
            if (f.bt_connected) {
                if (reply) reply(1, "AA:BB:CC:DD:EE:FF\t-50\t1\t1\t1\tM4 keyboard\n");
            } else if (reply) {
                reply(0, "");
            }
        } else {
            unknown("bt", args, reply);
        }
    });

    cp0_signal_settings_api.append([](Args args, Reply reply) {
        Fixtures &f = fixtures();
        const std::string command = arg(args, 0);
        if (command == "BacklightMax") {
            if (reply) reply(0, std::to_string(f.backlight_max));
        } else if (command == "BacklightRead") {
            if (reply) reply(0, std::to_string(f.backlight));
        } else if (command == "BacklightWrite") {
            f.backlight = std::atoi(arg(args, 1).c_str());
            if (reply) reply(0, std::to_string(f.backlight));
        } else if (command == "GpioGet") {
            if (reply) reply(0, "1");
        } else if (command == "GpioSet") {
            if (reply) reply(0, "");
        } else {
            unknown("settings", args, reply);
        }
    });

    auto time_service = [](Args args, Reply reply) {
        const std::string command = arg(args, 0);
        if (command == "LocalTime") {
            const time_t now = static_cast<time_t>(harness_wall_clock());
            std::tm local{};
            localtime_r(&now, &local);
            char text[64];
            std::snprintf(text, sizeof(text), "%d,%d,%d,%d,%d,%d", local.tm_year + 1900, local.tm_mon + 1,
                          local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
            if (reply) reply(0, text);
        } else if (command == "NtpGet") {
            if (reply) reply(0, "1");
        } else if (command == "AccountInfoRead") {
            if (reply) reply(0, "osrde\nzero7");
        } else {
            unknown("osinfo/timedate", args, reply);
        }
    };
    cp0_signal_osinfo_api.append(time_service);
    cp0_signal_timedate_api.append(time_service);

    cp0_signal_audio_api.append([](Args args, Reply reply) {
        // System sounds and volume: accepted, nothing to play on the harness.
        if (reply) reply(0, arg(args, 0) == "VolumeRead" ? "50" : "");
    });
    cp0_signal_process_api.append([](Args args, Reply reply) { unknown("process", args, reply); });
}

} // namespace harness
