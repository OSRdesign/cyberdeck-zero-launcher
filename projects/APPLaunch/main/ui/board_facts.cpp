/*
 * SPDX-License-Identifier: MIT
 *
 * The board's facts for board.state (see board_facts.hpp).
 */
#include "board_facts.hpp"

#include "cp0_power_supply.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__)
#include <sys/utsname.h>
#endif

namespace board_facts {
namespace {

void copy(char *to, size_t size, const char *from)
{
    std::snprintf(to, size, "%s", from ? from : "");
}

bool icontains(const std::string &text, const char *needle)
{
    std::string lower;
    for (char c : text) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find(needle) != std::string::npos;
}

std::string read_meminfo()
{
    const char *path = std::getenv("APPLAUNCH_MEMINFO"); /* test hook */
    std::FILE *f = std::fopen(path && path[0] ? path : "/proc/meminfo", "r");
    if (!f) return {};
    char text[2048];
    const size_t n = std::fread(text, 1, sizeof(text) - 1, f);
    std::fclose(f);
    text[n] = '\0';
    return text;
}

} // namespace

int meminfo_mb(const char *text)
{
    if (!text) return -1;
    const char *p = std::strstr(text, "MemTotal:");
    if (!p) return -1;
    p += 9;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p < '0' || *p > '9') return -1;
    const long long kb = std::atoll(p);
    return static_cast<int>(kb / 1024);
}

std::string server_sink_kind(const char *short_sinks, int &guess)
{
    guess = 0;
    if (!short_sinks) return {};
    std::string text = short_sinks;
    std::string kind;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const size_t tab2 = line.find('\t', tab + 1);
        const std::string name = line.substr(tab + 1, tab2 == std::string::npos ? std::string::npos : tab2 - tab - 1);
        if (name.empty() || name == "auto_null" || icontains(name, "hdmi")) continue;
        if (name.compare(0, 12, "bluez_output") == 0 || name.compare(0, 10, "bluez_sink") == 0) {
            guess = 1;
            return "bluetooth";
        }
        kind = "server";
    }
    return kind;
}

std::string requests_for(const cp0_app_board_t &b)
{
    std::string verbs = "hello,caps,toast,beep";
    if (b.sound == 1) verbs += ",volume";
    if (std::strcmp(b.backlight, "none") != 0 && b.backlight[0]) verbs += ",brightness";
    if (b.battery == 1 && std::strcmp(b.backlight, "levels") == 0) verbs += ",dim";
    return verbs;
}

void collect(cp0_app_board_t &out, const char *launcher_version, bool ask_server)
{
    const int pid = out.pid;
    const unsigned gen = out.gen;
    std::memset(&out, 0, sizeof(out));
    out.pid = pid;
    out.gen = gen;
    out.ram_mb = -1;
    out.battery = -1;
    out.backlight_steps = -1;
    out.sound = -1;
    out.sound_guess = -1;
    copy(out.launcher, sizeof(out.launcher), launcher_version);
    const char *board = std::getenv("APPLAUNCH_BOARD");
    copy(out.board, sizeof(out.board), board);
#if defined(__linux__)
    struct utsname u;
    if (uname(&u) == 0) copy(out.arch, sizeof(out.arch), u.machine);
#endif
    out.ram_mb = meminfo_mb(read_meminfo().c_str());

    cp0_caps_t caps;
    cp0_caps_probe(&caps, -1); /* card and sysfs only: the server is asked below, once */
    bool sink = false;
    if (caps.sound != CP0_CAP_PRESENT && ask_server) {
        const char *const argv[] = {cp0_caps_pactl(), "list", "short", "sinks", nullptr};
        char listing[4096];
        if (cp0_caps_run(argv, listing, sizeof(listing), CP0_CAPS_SERVER_TIMEOUT_MS) == 0) {
            int guess = 0;
            const std::string kind = server_sink_kind(listing, guess);
            if (!kind.empty()) {
                sink = true;
                copy(out.sound_kind, sizeof(out.sound_kind), kind.c_str());
                out.sound_guess = guess;
            }
        }
    }

    out.battery = caps.battery == CP0_CAP_PRESENT ? 1 : caps.battery == CP0_CAP_NOT_SET_UP ? 2 : 0;
    cp0_power_battery_t battery;
    if (caps.battery == CP0_CAP_PRESENT && cp0_power_supply_battery(&battery)) {
        if (std::strcmp(battery.name, "max17048") == 0)
            copy(out.battery_source, sizeof(out.battery_source), "gauge:max17048");
        else
            std::snprintf(out.battery_source, sizeof(out.battery_source), "power_supply:%s", battery.name);
    }
    copy(out.battery_note, sizeof(out.battery_note), caps.note_battery);

    switch (caps.bl.kind) {
    case CP0_BL_LEVELS:
        copy(out.backlight, sizeof(out.backlight), "levels");
        out.backlight_steps = 10;
        break;
    case CP0_BL_ONOFF:
        copy(out.backlight, sizeof(out.backlight), "onoff");
        out.backlight_steps = 2;
        break;
    default:
        copy(out.backlight, sizeof(out.backlight), "none");
        out.backlight_steps = 0;
        break;
    }
    copy(out.backlight_note, sizeof(out.backlight_note), caps.note_backlight);

    if (caps.sound == CP0_CAP_PRESENT) {
        out.sound = 1;
        copy(out.sound_kind, sizeof(out.sound_kind), "alsa");
        out.sound_guess = 0;
    } else if (sink) {
        out.sound = 1;
    } else if (caps.sound == CP0_CAP_NOT_SET_UP) {
        out.sound = 2;
        out.sound_kind[0] = '\0';
        out.sound_guess = -1;
    } else if (ask_server || caps.sound_server >= 0) {
        out.sound = 0;
        out.sound_kind[0] = '\0';
        out.sound_guess = -1;
    } else {
        out.sound = -1; /* not asked: unknown, so the key is absent */
    }
    copy(out.sound_note, sizeof(out.sound_note), caps.note_sound);
    copy(out.requests, sizeof(out.requests), requests_for(out).c_str());
}

} // namespace board_facts
