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

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <strings.h>

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

/* --------------------------------------------------------------------------- fake BlueZ (task 014 P3c) */

std::recursive_mutex &bt_mutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

/* the device with this address (case-insensitive, as BlueZ), or nullptr; call with bt_mutex() held */
FixtureBtDevice *bt_device(const std::string &address)
{
    for (FixtureBtDevice &device : fixtures().bt_devices)
        if (strcasecmp(device.address.c_str(), address.c_str()) == 0) return &device;
    return nullptr;
}

std::string bt_path(const std::string &address)
{
    std::string path = "/org/bluez/hci0/dev_" + address;
    for (char &c : path)
        if (c == ':') c = '_';
    return path;
}

Reply &bt_display_pairing() /* a "display" pairing waiting for the remote keyboard (bt_key_typed) */
{
    static Reply pending;
    return pending;
}

uint64_t bt_next_agent_id()
{
    static std::atomic<uint64_t> id{1};
    return id++;
}

void bt_set_paired(const std::string &address)
{
    std::lock_guard<std::recursive_mutex> lock(bt_mutex());
    if (FixtureBtDevice *device = bt_device(address)) device->paired = true;
}

/* Pair with an agent request, as BlueZ does: the request goes to the UI (cp0_signal_bt_agent), Pair answers when the
 * UI does. Nobody listening: rejected, as the real backend does without a subscriber. */
void bt_pair_with_agent(const std::string &address, const std::string &pairing, Reply reply)
{
    Fixtures &f = fixtures();
    if (cp0_signal_bt_agent.empty()) {
        if (reply) reply(-1, "Authentication Rejected");
        return;
    }
    if (pairing == "display") {
        {
            std::lock_guard<std::recursive_mutex> lock(bt_mutex());
            bt_display_pairing() = reply;
        }
        cp0_signal_bt_agent(bt_next_agent_id(), "DisplayPasskey", bt_path(address), f.bt_display_code,
                            [](bool, std::string) {});
        return;
    }
    std::string method = "RequestConfirmation", value = f.bt_confirm_code, expected;
    if (pairing == "pin") method = "RequestPinCode", value.clear(), expected = f.bt_pin;
    else if (pairing == "passkey") method = "RequestPasskey", value.clear(), expected = f.bt_passkey;
    cp0_signal_bt_agent(bt_next_agent_id(), method, bt_path(address), value,
                        [address, expected, reply](bool accepted, std::string text) {
                            std::fprintf(stderr, "[harness] bt agent reply accepted=%d text=%s\n", accepted ? 1 : 0,
                                         text.c_str());
                            if (!accepted) {
                                if (reply) reply(-1, "Authentication Rejected");
                                return;
                            }
                            if (!expected.empty() && text != expected) {
                                if (reply) reply(-1, "Authentication Failed");
                                return;
                            }
                            bt_set_paired(address);
                            if (reply) reply(0, "");
                        });
}

} // namespace

void bt_agent_request(const std::string &method, const std::string &address, const std::string &value)
{
    cp0_signal_bt_agent(bt_next_agent_id(), method, bt_path(address), value, [method](bool accepted, std::string text) {
        std::fprintf(stderr, "[harness] bt agent %s reply accepted=%d text=%s\n", method.c_str(), accepted ? 1 : 0,
                     text.c_str());
    });
}

void bt_key_typed()
{
    Reply reply;
    {
        std::lock_guard<std::recursive_mutex> lock(bt_mutex());
        reply = std::move(bt_display_pairing());
        bt_display_pairing() = nullptr;
        for (FixtureBtDevice &device : fixtures().bt_devices)
            if (device.pairing == "display" && !device.paired) device.paired = true;
    }
    if (reply) reply(0, "");
}

namespace {

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

/* --------------------------------------------------------------------------- fake PTY (task 014 P4b) */

namespace {

struct FakePty {
    std::mutex mutex;
    bool open = false;
    bool exited = false;
    std::string output; /* what Read returns next */
    std::string prompt = "$ ";
    std::vector<std::string> log;
};

FakePty &fake_pty()
{
    static FakePty pty;
    return pty;
}

/* the bytes written, readable: text as is, Esc as \e, other control bytes as \xNN */
std::string visible_bytes(const std::string &data)
{
    std::string out;
    for (unsigned char c : data) {
        char hex[8];
        if (c == 0x1b) out += "\\e";
        else if (c < 0x20 || c == 0x7f) {
            std::snprintf(hex, sizeof(hex), "\\x%02x", c);
            out += hex;
        } else out += static_cast<char>(c);
    }
    return out;
}

/* a cooked tty with a shell behind it: what the user sees of a key (call with the mutex held) */
void pty_echo(FakePty &pty, const std::string &data)
{
    if (!data.empty() && data[0] == 0x1b) return; /* Esc and the arrows' sequences: no echo */
    for (unsigned char c : data) {
        if (c == '\r') pty.output += "\r\n" + pty.prompt;
        else if (c == 0x7f) pty.output += "\b \b";
        else if (c == 0x03) pty.output += "^C\r\n" + pty.prompt;
        else if (c == 0x04) { /* ^D on the prompt: the shell says exit and ends */
            pty.output += "exit\r\n";
            pty.exited = true;
            pty.log.push_back("exit");
            return;
        }
        else if (c == '\t') continue; /* completion: nothing to complete here */
        else if (c < 0x20) {
            pty.output += '^';
            pty.output += static_cast<char>(c + '@');
        } else pty.output += static_cast<char>(c);
    }
}

} // namespace

void pty_output(const std::string &text)
{
    std::string bytes;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == 'n' || text[i + 1] == 'e')) {
            bytes += text[i + 1] == 'n' ? "\r\n" : "\x1b";
            ++i;
        } else {
            bytes += text[i];
        }
    }
    FakePty &pty = fake_pty();
    std::lock_guard<std::mutex> lock(pty.mutex);
    pty.output += bytes;
}

void pty_set_prompt(const std::string &prompt)
{
    FakePty &pty = fake_pty();
    std::lock_guard<std::mutex> lock(pty.mutex);
    pty.prompt = prompt;
}

void pty_exit()
{
    FakePty &pty = fake_pty();
    std::lock_guard<std::mutex> lock(pty.mutex);
    pty.exited = true;
    pty.log.push_back("exit");
}

std::vector<std::string> pty_log()
{
    FakePty &pty = fake_pty();
    std::lock_guard<std::mutex> lock(pty.mutex);
    return pty.log;
}

void install_services()
{
    /* The PTY service (cp0_pty_contract: Open COMMAND COLS ROWS ARGV0 ARGS..., Read H SIZE, Write H DATA, Resize H COLS
     * ROWS, CheckChild H, Close H): one fake session at a time, handle "7". */
    cp0_signal_pty_api.append([](Args args, Reply reply) {
        FakePty &pty = fake_pty();
        const std::string command = arg(args, 0);
        std::unique_lock<std::mutex> lock(pty.mutex);
        if (command == "Open") {
            pty.open = true;
            pty.exited = false;
            pty.output.clear();
            pty.log.push_back("open " + arg(args, 1) + " " + arg(args, 2) + "x" + arg(args, 3));
            lock.unlock();
            if (reply) reply(0, "7");
        } else if (command == "Read") {
            if (pty.exited && pty.output.empty()) {
                lock.unlock();
                if (reply) reply(-1, "");
                return;
            }
            const std::size_t size = static_cast<std::size_t>(std::max(1, std::atoi(arg(args, 2).c_str())));
            const std::string data = pty.output.substr(0, size);
            pty.output.erase(0, data.size());
            lock.unlock();
            if (reply) reply(static_cast<int>(data.size()), data);
        } else if (command == "Write") {
            const std::string data = arg(args, 2);
            pty.log.push_back("write " + visible_bytes(data));
            pty_echo(pty, data);
            lock.unlock();
            if (reply) reply(static_cast<int>(data.size()), "");
        } else if (command == "Resize") {
            pty.log.push_back("resize " + arg(args, 2) + "x" + arg(args, 3));
            lock.unlock();
            if (reply) reply(0, "");
        } else if (command == "CheckChild") {
            const bool exited = pty.exited;
            lock.unlock();
            if (reply) reply(exited ? 1 : 0, exited ? "0" : "");
        } else if (command == "Close") {
            pty.open = false;
            pty.log.push_back("close");
            lock.unlock();
            if (reply) reply(0, "");
        } else {
            lock.unlock();
            unknown("pty", args, reply);
        }
    });

    cp0_signal_config_api.append([](Args args, Reply reply) {
        const std::string command = arg(args, 0);
        if (command == "GetInt" || command == "GetStr") {
            const auto found = config().find(arg(args, 1));
            if (reply) reply(0, found != config().end() ? found->second : arg(args, 2));
        } else if (command == "SetInt" || command == "SetStr") { /* "ok" as the real service (cp0_config_service.cpp) */
            config()[arg(args, 1)] = arg(args, 2);
            if (reply) reply(0, "ok");
        } else if (command == "SetManyAndSave") {
            for (std::size_t i = 1; i + 1 < args.size(); i += 2) config()[arg(args, i)] = arg(args, i + 1);
            if (reply) reply(0, "ok");
        } else if (command == "Save" || command == "Init") {
            if (reply) reply(0, "ok");
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
        Fixtures &f = fixtures();
        const std::string command = arg(args, 0);
        const bool connected = f.wifi_connected && f.wifi_radio;
        auto find = [&f](const std::string &ssid) -> FixtureNetwork * {
            for (FixtureNetwork &network : f.wifi_networks)
                if (network.ssid == ssid) return &network;
            return nullptr;
        };
        if (command == "Status") {
            cp0_wifi_status_t status{};
            status.connected = connected ? 1 : 0;
            std::snprintf(status.ssid, sizeof(status.ssid), "%s", connected ? f.wifi_ssid.c_str() : "");
            std::snprintf(status.ip, sizeof(status.ip), "%s", connected ? f.wifi_ip.c_str() : "");
            status.signal = connected ? f.wifi_signal : 0;
            if (reply) reply(0, cp0::network::encode_status_payload(status));
        } else if (command == "RadioEnabled") {
            if (reply) reply(f.wifi_radio ? 1 : 0, "");
        } else if (command == "RadioSetEnabled") { /* "on" / "off" */
            f.wifi_radio = arg(args, 1) == "on" || arg(args, 1) == "1" || arg(args, 1) == "true";
            if (reply) reply(0, "");
        } else if (command == "Scan") { /* the fixture networks, in their order (the page sorts them) */
            if (!f.wifi_radio) {
                if (reply) reply(CP0_WIFI_ERROR_RADIO_OFF, "");
                return;
            }
            std::vector<cp0_wifi_ap_t> aps;
            for (const FixtureNetwork &network : f.wifi_networks) {
                cp0_wifi_ap_t ap{};
                std::snprintf(ap.ssid, sizeof(ap.ssid), "%s", network.ssid.c_str());
                std::snprintf(ap.security, sizeof(ap.security), "%s", network.security.c_str());
                ap.signal = network.signal;
                ap.in_use = connected && network.ssid == f.wifi_ssid ? 1 : 0;
                ap.saved = network.saved ? 1 : 0;
                aps.push_back(ap);
            }
            if (reply)
                reply(static_cast<int>(aps.size()), cp0::network::encode_scan_payload(aps.data(), static_cast<int>(aps.size())));
        } else if (command == "Connect" || command == "ConnectHidden") {
            /* an open or saved network joins without a password; a new secured one only with f.wifi_password;
             * a hidden one only as f.wifi_hidden_ssid */
            const std::string ssid = arg(args, 1);
            const std::string password = arg(args, 2);
            if (!f.wifi_radio) {
                if (reply) reply(CP0_WIFI_ERROR_RADIO_OFF, "");
                return;
            }
            FixtureNetwork *network = find(ssid);
            if (command == "ConnectHidden" && !network && ssid == f.wifi_hidden_ssid) {
                f.wifi_networks.push_back({ssid, 58, "WPA2", false});
                network = &f.wifi_networks.back();
            }
            if (!network) {
                if (reply) reply(CP0_WIFI_ERROR_NOT_FOUND, "");
                return;
            }
            const bool open = network->security.empty();
            if (!open && !network->saved && password != f.wifi_password) {
                if (reply) reply(CP0_WIFI_ERROR_AUTH, "");
                return;
            }
            network->saved = network->saved || !open;
            f.wifi_connected = true; /* the status keeps the scene's signal (wifi PCT): the strip stays comparable */
            f.wifi_ssid = ssid;
            f.wifi_ip = "192.168.1.57";
            if (reply) reply(0, "");
        } else if (command == "ProfileForget") {
            if (FixtureNetwork *network = find(arg(args, 1))) network->saved = false;
            if (reply) reply(0, "");
        } else if (command == "ProfileExists") {
            const FixtureNetwork *network = find(arg(args, 1));
            if (reply) reply(network && network->saved ? 1 : 0, "");
        } else if (command == "ProfileDisconnectActive" || command == "Disconnect") {
            f.wifi_connected = false;
            if (reply) reply(0, "");
        } else {
            unknown("wifi", args, reply);
        }
    });

    /* Bluetooth (task 014 P3c): the BlueZ backend as Settings sees it. Called on the pages' worker threads (the strip
     * calls it on the LVGL thread), so the device fixtures are guarded; the agent is called without the lock (a page
     * may answer at once). BtConnectedList keeps answering from the `bt` scene command only, so the status strip stays
     * what the home grid's header shows. */
    cp0_signal_bt_api.append([](Args args, Reply reply) {
        Fixtures &f = fixtures();
        const std::string command = arg(args, 0);
        std::unique_lock<std::recursive_mutex> lock(bt_mutex());
        if (command == "BtStatus") {
            if (reply) reply(0, std::string(f.bt_powered ? "1" : "0") + "\t00:11:22:33:44:55\t0\t" + f.bt_alias);
        } else if (command == "BtConnectedList") {
            if (f.bt_connected) {
                if (reply) reply(1, "AA:BB:CC:DD:EE:FF\t-50\t1\t1\t1\tM4 keyboard\n");
            } else if (reply) {
                reply(0, "");
            }
        } else if (command == "BtList") {
            if (!f.bt_powered) {
                if (reply) reply(0, "");
                return;
            }
            std::string lines;
            int count = 0;
            for (const FixtureBtDevice &device : f.bt_devices) {
                if (!device.paired && !(device.nearby && f.bt_discovered)) continue;
                lines += device.address + "\t" + std::to_string(device.rssi) + "\t" + (device.connected ? "1" : "0") +
                         "\t" + (device.paired ? "1" : "0") + "\t" + (device.paired ? "1" : "0") + "\t" + device.name +
                         "\n";
                ++count;
            }
            if (reply) reply(count, lines);
        } else if (command == "BtDiscoveryStart") {
            f.bt_discovered = f.bt_powered;
            if (reply) reply(f.bt_powered ? 0 : -1, f.bt_powered ? "" : "Not Ready");
        } else if (command == "BtDiscoveryStop" || command == "BtCancelPairing" || command == "BtReset") {
            if (reply) reply(0, "");
        } else if (command == "BtAlias") {
            f.bt_alias = arg(args, 1);
            if (reply) reply(0, "ok");
        } else if (command == "BtPower") {
            f.bt_powered = arg(args, 1) == "1";
            if (reply) reply(0, "");
        } else if (command == "BtConnect" || command == "BtDisconnect" || command == "BtRemove" ||
                   command == "BtDeviceAlias" || command == "BtPair") {
            FixtureBtDevice *device = bt_device(arg(args, 1));
            if (!device) {
                if (reply) reply(-1, "device not found");
                return;
            }
            if (command == "BtConnect") {
                device->connected = true;
            } else if (command == "BtDisconnect") {
                device->connected = false;
            } else if (command == "BtRemove") {
                device->paired = device->connected = false;
            } else if (command == "BtDeviceAlias") {
                device->name = arg(args, 2);
            } else { /* BtPair: the agent's request first, as BlueZ does */
                const std::string address = device->address;
                const std::string pairing = device->pairing;
                if (pairing.empty()) {
                    device->paired = true;
                } else {
                    lock.unlock();
                    bt_pair_with_agent(address, pairing, reply);
                    return;
                }
            }
            if (reply) reply(0, "");
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
        } else {
            unknown("settings", args, reply);
        }
    });

    auto time_service = [](Args args, Reply reply) {
        const std::string command = arg(args, 0);
        if (command == "LocalTime") {
            const int64_t set_time = fixtures().time_set_epoch.load();
            const time_t now = static_cast<time_t>(set_time >= 0 ? set_time : harness_wall_clock());
            std::tm local{};
            localtime_r(&now, &local);
            char text[64];
            std::snprintf(text, sizeof(text), "%d,%d,%d,%d,%d,%d", local.tm_year + 1900, local.tm_mon + 1,
                          local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
            if (reply) reply(0, text);
        } else if (command == "NtpGet") {
            if (reply) reply(fixtures().ntp_on ? 1 : 0, "1"); /* the code is the state: 1 on, 0 off */
        } else if (command == "AccountInfoRead") {
            if (reply) reply(0, "osrde\nzero7");
        } else if (command == "NetworkDefaultInfoRead") { /* IPv4, gateway, MAC of the default route (fake) */
            const Fixtures &f = fixtures();
            if (reply) reply(0, (f.wifi_connected ? f.wifi_ip : std::string("--")) + "\n192.168.1.1\n02:00:5e:00:53:01");
        } else {
            unknown("osinfo/timedate", args, reply);
        }
    };
    cp0_signal_osinfo_api.append(time_service);
    cp0_signal_timedate_api.append(time_service);

    cp0_signal_audio_api.append([](Args args, Reply reply) {
        // System sounds: accepted, nothing to play on the harness. The volume: the fixture (Settings > Speaker).
        Fixtures &f = fixtures();
        const std::string command = arg(args, 0);
        if (command == "VolumeWrite") f.volume = std::max(0, std::min(100, std::atoi(arg(args, 1).c_str())));
        if (reply) reply(0, command == "VolumeRead" || command == "VolumeWrite" ? std::to_string(f.volume) : "");
    });
    cp0_signal_process_api.append([](Args args, Reply reply) { unknown("process", args, reply); });
}

} // namespace harness
