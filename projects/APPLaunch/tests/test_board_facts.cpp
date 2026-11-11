// SPDX-License-Identifier: MIT
//
// board.state's facts (report 035) on fake trees and a fake pactl: the meminfo parser, the sink-name guess, the verbs
// of a board, and collect() for the cases the owner's boards show (a Bluetooth speaker on a levels backlight and a
// battery; no sound, no battery, no backlight; a profile hint that says the sound is not set up; the sound server not
// asked).
#include "board_facts.hpp"

#include "cp0_board_state.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

namespace {

std::string g_root;

void put(const std::string &path, const std::string &text)
{
    std::string dir;
    for (size_t i = 1; i < path.size(); ++i)
        if (path[i] == '/') {
            dir = path.substr(0, i);
            ::mkdir(dir.c_str(), 0755);
        }
    std::FILE *f = std::fopen(path.c_str(), "w");
    CHECK(f);
    std::fputs(text.c_str(), f);
    std::fclose(f);
}

void put_script(const std::string &path, const std::string &body)
{
    put(path, "#!/bin/sh\n" + body);
    CHECK(::chmod(path.c_str(), 0755) == 0);
}

void env(const char *name, const std::string &value)
{
    ::setenv(name, value.c_str(), 1);
}

} // namespace

int main()
{
    // pure parts
    CHECK(board_facts::meminfo_mb("MemTotal:        1843200 kB\nMemFree: 5 kB\n") == 1800);
    CHECK(board_facts::meminfo_mb("MemFree: 5 kB\n") == -1 && board_facts::meminfo_mb("MemTotal: x kB\n") == -1);
    CHECK(board_facts::meminfo_mb(nullptr) == -1);

    int guess = -1;
    CHECK(board_facts::server_sink_kind("", guess) == "" && guess == 0);
    CHECK(board_facts::server_sink_kind("0\tauto_null\tmodule-null-sink.c\ts16le 2ch 44100Hz\tSUSPENDED\n", guess) == "");
    CHECK(board_facts::server_sink_kind("1\talsa_output.hdmi-stereo\tm\tf\tIDLE\n", guess) == "");
    CHECK(board_facts::server_sink_kind("1\talsa_output.platform-soc_sound.stereo-fallback\tm\tf\tIDLE\n", guess) == "server" &&
          guess == 0);
    CHECK(board_facts::server_sink_kind("0\talsa_output.hdmi\tm\tf\tIDLE\n3\tbluez_output.AA_BB.1\tm\tf\tIDLE\n", guess) ==
              "bluetooth" &&
          guess == 1);
    CHECK(board_facts::server_sink_kind("3\tbluez_sink.AA_BB.a2dp_sink\tm\tf\tIDLE\n", guess) == "bluetooth" && guess == 1);

    // Bluetooth audio facts: the sound stack from `pactl info` (the HackBerry's real text), the adapter UUIDs, the CIS flag
    CHECK(board_facts::bt_audio_stack("Server String: /run/user/1000/pulse/native\nServer Name: PulseAudio (on PipeWire 1.4.2)\n"
                                      "Server Version: 17.0.0\n",
                                      false, true) == "pipewire");
    CHECK(board_facts::bt_audio_stack("Server Name: pulseaudio\nServer Version: 16.1\n", false, false) == "pulseaudio");
    CHECK(board_facts::bt_audio_stack("Server Name: pulseaudio\n", true, true) == "pulseaudio"); // a running server wins
    CHECK(board_facts::bt_audio_stack("", true, false) == "bluealsa");
    CHECK(board_facts::bt_audio_stack(nullptr, true, true) == "bluealsa");
    CHECK(board_facts::bt_audio_stack("", false, true) == "pipewire"); // no pulse server, pipewire runs (no pactl)
    CHECK(board_facts::bt_audio_stack("", false, false) == "none");
    CHECK(board_facts::bt_audio_stack("Default Sink: bluez_output.x\n", false, false) == "none"); // other lines do not count
    const char *show_hackberry =
        "Controller 2C:CF:67:E7:8D:EC (public)\n\tName: hackberry\n"
        "\tUUID: A/V Remote Control        (0000110e-0000-1000-8000-00805f9b34fb)\n"
        "\tUUID: Handsfree Audio Gateway   (0000111f-0000-1000-8000-00805f9b34fb)\n"
        "\tUUID: Audio Sink                (0000110b-0000-1000-8000-00805f9b34fb)\n\tRoles: central\n";
    CHECK(board_facts::bt_adapter_has_hfp(show_hackberry));
    CHECK(!board_facts::bt_adapter_has_hfp("Controller X\n\tUUID: Handsfree (0000111e-0000-1000-8000-00805f9b34fb)\n"));
    CHECK(!board_facts::bt_adapter_has_hfp("\tName: 0000111f-0000-1000-8000-00805f9b34fb\n")); // only UUID lines count
    CHECK(!board_facts::bt_adapter_has_hfp(nullptr) && !board_facts::bt_adapter_has_hfp(""));
    const char *btmgmt_hackberry =
        "Index list with 1 item\nhci0:\tPrimary controller\n"
        "\tsupported settings: powered connectable discoverable bondable ssp br/edr le advertising secure-conn privacy\n"
        "\tcurrent settings: powered bondable ssp br/edr le secure-conn\n";
    CHECK(!board_facts::bt_controller_has_cis(btmgmt_hackberry));
    CHECK(board_facts::bt_controller_has_cis("\tsupported settings: powered le cis-central cis-peripheral iso-broadcaster\n"));
    CHECK(!board_facts::bt_controller_has_cis("\tcurrent settings: powered cis-central\n")); // supported settings only
    CHECK(!board_facts::bt_controller_has_cis(nullptr) && !board_facts::bt_controller_has_cis(""));

    cp0_app_board_t b;
    std::memset(&b, 0, sizeof(b));
    std::strcpy(b.backlight, "levels");
    b.battery = 1;
    b.sound = 1;
    CHECK(board_facts::requests_for(b) == "hello,caps,toast,beep,volume,brightness,dim");
    b.battery = 0;
    CHECK(board_facts::requests_for(b) == "hello,caps,toast,beep,volume,brightness"); // no dim without a battery
    b.sound = 0;
    std::strcpy(b.backlight, "onoff");
    b.battery = 1;
    CHECK(board_facts::requests_for(b) == "hello,caps,toast,beep,brightness"); // beep answers "err nosound"
    std::strcpy(b.backlight, "none");
    b.sound = -1;
    CHECK(board_facts::requests_for(b) == "hello,caps,toast,beep");

    // collect() on fake trees
    char tmpl[] = "/tmp/applaunch-facts-XXXXXX";
    CHECK(::mkdtemp(tmpl));
    g_root = tmpl;
    const std::string sys = g_root + "/power_supply", bl = g_root + "/backlight", asound = g_root + "/asound";
    ::mkdir(asound.c_str(), 0755);
    put(g_root + "/meminfo", "MemTotal:        2048000 kB\n");
    env("APPLAUNCH_MEMINFO", g_root + "/meminfo");
    env("APPLAUNCH_POWER_SUPPLY_ROOT", sys);
    env("APPLAUNCH_BACKLIGHT_ROOT", bl);
    env("APPLAUNCH_ASOUND_ROOT", asound);
    ::unsetenv("APPLAUNCH_BACKLIGHT");
    ::unsetenv("APPLAUNCH_GAUGE");
    ::unsetenv("APPLAUNCH_HINT_BATTERY");
    ::unsetenv("APPLAUNCH_HINT_BACKLIGHT");
    ::unsetenv("APPLAUNCH_HINT_SOUND");
    env("APPLAUNCH_BOARD", "hackberry-cm5");
    put(sys + "/BAT0/type", "Battery\n");
    put(sys + "/BAT0/capacity", "55\n");
    put(sys + "/BAT0/status", "Discharging\n");
    put(bl + "/intel_backlight/max_brightness", "937\n");
    put(bl + "/intel_backlight/brightness", "400\n");
    put_script(g_root + "/pactl",
               "echo '0\tauto_null\tmodule-null-sink.c\ts16le 2ch 44100Hz\tSUSPENDED'\n"
               "echo '5\tbluez_output.AA_BB_CC.1\tmodule-bluez5-device.c\ts16le 2ch 44100Hz\tIDLE'\n");
    env("APPLAUNCH_PACTL", g_root + "/pactl");
    // fake /proc (no bluealsa, a pipewire), fake bluetoothctl and btmgmt
    put(g_root + "/proc/10/comm", "systemd\n");
    put(g_root + "/proc/11/comm", "pipewire\n");
    env("APPLAUNCH_PROC_ROOT", g_root + "/proc");
    put_script(g_root + "/bluetoothctl", std::string("cat <<'X'\n") + show_hackberry + "X\n");
    put_script(g_root + "/btmgmt", std::string("cat <<'X'\n") + btmgmt_hackberry + "X\n");
    env("APPLAUNCH_BLUETOOTHCTL", g_root + "/bluetoothctl");
    env("APPLAUNCH_BTMGMT", g_root + "/btmgmt");
    env("APPLAUNCH_AMIXER", g_root + "/missing-amixer");

    cp0_app_board_t f;
    std::memset(&f, 0, sizeof(f));
    f.pid = 77;
    f.gen = 5;
    board_facts::collect(f, "0.8.6", true);
    CHECK(f.pid == 77 && f.gen == 5); // the caller's, untouched
    CHECK(std::string(f.launcher) == "0.8.6" && std::string(f.board) == "hackberry-cm5");
    CHECK(f.ram_mb == 2000 && f.arch[0] != '\0');
    CHECK(f.battery == 1 && std::string(f.battery_source) == "power_supply:BAT0");
    CHECK(std::string(f.backlight) == "levels" && f.backlight_steps == 10);
    CHECK(f.sound == 1 && std::string(f.sound_kind) == "bluetooth" && f.sound_guess == 1);
    CHECK(std::string(f.requests) == "hello,caps,toast,beep,volume,brightness,dim");
    char text[1536];
    CHECK(cp0_board_state_format(&f, text, sizeof(text)) > 0 && std::strstr(text, "sound_guess=1\n"));
    // the Bluetooth audio facts of the fake board (no pulse server text, a pipewire process, an adapter with 111f, no CIS)
    CHECK(std::string(f.bt_audio) == "pipewire" && f.bt_hfp == 1 && f.bt_le_audio == 0);
    CHECK(std::strstr(text, "bt_audio=pipewire\nbt_hfp=1\nbt_le_audio=0\n"));
    cp0_app_board_t round;
    CHECK(cp0_app_board_parse(text, std::strlen(text), &round) == 0);
    CHECK(std::string(round.bt_audio) == "pipewire" && round.bt_hfp == 1 && round.bt_le_audio == 0);
    // no adapter tools: hfp and le_audio are 0 (probed, not offered), the stack falls back to the processes
    env("APPLAUNCH_BLUETOOTHCTL", g_root + "/missing-bluetoothctl");
    env("APPLAUNCH_BTMGMT", g_root + "/missing-btmgmt");
    put(g_root + "/proc/11/comm", "dbus-daemon\n");
    put(g_root + "/proc/12/comm", "bluealsad\n");
    board_facts::collect(f, "0.8.6", true);
    CHECK(std::string(f.bt_audio) == "bluealsa" && f.bt_hfp == 0 && f.bt_le_audio == 0);
    ::unlink((g_root + "/proc/12/comm").c_str());
    board_facts::collect(f, "0.8.6", true);
    CHECK(std::string(f.bt_audio) == "none");
    env("APPLAUNCH_BLUETOOTHCTL", g_root + "/bluetoothctl");
    env("APPLAUNCH_BTMGMT", g_root + "/btmgmt");
    put(g_root + "/proc/11/comm", "pipewire\n");

    // the server not asked: the sound is unknown (absent), not "none"; the verbs leave volume out
    board_facts::collect(f, "0.8.6", false);
    CHECK(f.sound == -1 && f.sound_kind[0] == '\0' && f.sound_guess == -1);
    CHECK(std::string(f.requests) == "hello,caps,toast,beep,brightness,dim");
    CHECK(cp0_board_state_format(&f, text, sizeof(text)) > 0 && !std::strstr(text, "sound="));
    // the Bluetooth audio facts are probed with the sound server: not asked, absent keys (not 0)
    CHECK(f.bt_audio[0] == '\0' && f.bt_hfp == -1 && f.bt_le_audio == -1 && !std::strstr(text, "bt_"));

    // a server with no usable sink: sound 0; with the profile's hint: not set up, and the hint text
    put_script(g_root + "/pactl", "echo '0\tauto_null\tm\tf\tSUSPENDED'\n");
    board_facts::collect(f, "0.8.6", true);
    CHECK(f.sound == 0 && f.sound_kind[0] == '\0');
    env("APPLAUNCH_HINT_SOUND", "bluetooth:XWF-M18");
    board_facts::collect(f, "0.8.6", true);
    CHECK(f.sound == 2 && std::string(f.sound_note) == "pair XWF-M18 in Bluetooth");

    // no pactl at all, no battery, no backlight (the deck-like case), the gauge as the battery source
    env("APPLAUNCH_PACTL", g_root + "/missing-pactl");
    ::unsetenv("APPLAUNCH_HINT_SOUND");
    ::unsetenv("APPLAUNCH_BOARD");
    env("APPLAUNCH_POWER_SUPPLY_ROOT", g_root + "/no-power");
    env("APPLAUNCH_BACKLIGHT_ROOT", g_root + "/no-backlight");
    board_facts::collect(f, "0.8.6", true);
    CHECK(f.board[0] == '\0' && f.battery == 0 && f.battery_source[0] == '\0');
    CHECK(f.sound == 0 && std::string(f.backlight) == "none" && f.backlight_steps == 0);
    CHECK(std::string(f.requests) == "hello,caps,toast,beep");
    env("APPLAUNCH_HINT_BATTERY", "button cell");
    board_facts::collect(f, "0.8.6", true);
    CHECK(f.battery == 2 && std::string(f.battery_note) == "button cell");

    // an on/off backlight (the Pi 5 HyperPixel)
    ::unsetenv("APPLAUNCH_HINT_BATTERY");
    put(g_root + "/bl2/backlight/max_brightness", "1\n");
    put(g_root + "/bl2/backlight/brightness", "1\n");
    env("APPLAUNCH_BACKLIGHT_ROOT", g_root + "/bl2");
    board_facts::collect(f, "0.8.6", true);
    CHECK(std::string(f.backlight) == "onoff" && f.backlight_steps == 2);

    const std::string cmd = "rm -rf '" + g_root + "'";
    CHECK(std::system(cmd.c_str()) == 0);
    std::puts("test_board_facts: PASS");
    return 0;
}
