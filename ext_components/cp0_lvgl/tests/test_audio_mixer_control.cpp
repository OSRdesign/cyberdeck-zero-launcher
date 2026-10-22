/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_audio_mixer_control.hpp"

#include "cp0_capabilities.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string g_dir;

void put(const std::string &name, const std::string &text)
{
    std::ofstream(g_dir + "/" + name) << text;
}

void put_script(const std::string &name, const std::string &body)
{
    put(name, "#!/bin/sh\n" + body);
    assert(chmod((g_dir + "/" + name).c_str(), 0755) == 0);
}

std::string slurp(const std::string &name)
{
    std::ifstream in(g_dir + "/" + name);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool exists(const std::string &name)
{
    return access((g_dir + "/" + name).c_str(), F_OK) == 0;
}

/* Report 030 decision C3: pactl when a server answers, else the local card's playback control through amixer -M. */
void test_backend_chain()
{
    char base[] = "/tmp/cp0-mixer-XXXXXX";
    assert(mkdtemp(base));
    g_dir = base;
    setenv("FAKE_DIR", base, 1);
    put_script("pactl",
               "[ -f \"$FAKE_DIR/pactl-down\" ] && exit 1\n"
               "case \"$1\" in\n"
               "  get-sink-volume) printf 'Volume: front-left: 45875 /  70%% / -9.29 dB,   front-right: 45875 /  70%% / -9.29 dB\\n'; exit 0 ;;\n"
               "  set-sink-volume) echo \"$2 $3\" > \"$FAKE_DIR/pactl-set\"; exit 0 ;;\n"
               "  get-sink-mute) echo 'Mute: no'; exit 0 ;;\n"
               "  set-sink-mute) echo \"$3\" > \"$FAKE_DIR/pactl-mute\"; exit 0 ;;\n"
               "esac\n"
               "exit 1\n");
    put_script("amixer",
               "[ $# -eq 2 ] && { cat \"$FAKE_DIR/controls.txt\"; exit 0; }\n"
               "for a in \"$@\"; do case $a in\n"
               "  sget) cat \"$FAKE_DIR/sget.txt\"; exit 0 ;;\n"
               "  sset) echo \"$*\" > \"$FAKE_DIR/sset\"; exit 0 ;;\n"
               "esac; done\n"
               "exit 1\n");
    setenv(CP0_CAPS_PACTL_ENV, (g_dir + "/pactl").c_str(), 1);
    setenv(CP0_CAPS_AMIXER_ENV, (g_dir + "/amixer").c_str(), 1);
    assert(mkdir((g_dir + "/asound").c_str(), 0755) == 0);
    setenv(CP0_CAPS_ASOUND_ROOT_ENV, (g_dir + "/asound").c_str(), 1);

    Cp0AudioMixerControl mixer;
    /* a server answers (the tablet with PulseAudio, a Bluetooth speaker): pactl */
    assert(mixer.read_volume() == 70);
    assert(mixer.write_volume(40) == 40 && slurp("pactl-set") == "@DEFAULT_SINK@ 40%\n");
    assert(mixer.write_volume(140) == 100 && slurp("pactl-set") == "@DEFAULT_SINK@ 100%\n");
    assert(mixer.read_mute() == 0);
    assert(mixer.toggle_mute() == 0 && slurp("pactl-mute") == "toggle\n");

    /* no server, no local card (the deck, the Pi 5): nothing works */
    put("pactl-down", "");
    assert(mixer.read_volume() == -1);
    assert(mixer.write_volume(40) == -1);
    assert(mixer.read_mute() == -1);
    assert(mixer.toggle_mute() == -1);
    assert(!exists("sset"));

    /* no server, the tablet's HDA card: amixer -c 0 -M on Master */
    put("asound/cards", " 0 [PCH            ]: HDA-Intel - HDA Intel PCH\n");
    assert(mkdir((g_dir + "/asound/card0").c_str(), 0755) == 0);
    assert(mkdir((g_dir + "/asound/card0/pcm0p").c_str(), 0755) == 0);
    put("controls.txt", "Simple mixer control 'Master',0\n  Capabilities: pvolume pswitch\n");
    put("sget.txt", "Simple mixer control 'Master',0\n  Capabilities: pvolume pswitch\n  Playback channels: Mono\n"
                    "  Limits: Playback 0 - 87\n  Mono: Playback 70 [81%] [-12.75dB] [on]\n");
    assert(mixer.read_volume() == 81);
    assert(mixer.write_volume(40) == 40 && slurp("sset") == "-c 0 -M sset Master 40%\n");
    assert(mixer.read_mute() == 0);
    put("sget.txt", "  Mono: Playback 70 [81%] [-12.75dB] [off]\n");
    assert(mixer.toggle_mute() == 1 && slurp("sset") == "-c 0 sset Master toggle\n");

    /* the server is back: pactl again */
    std::remove((g_dir + "/pactl-down").c_str());
    assert(mixer.read_volume() == 70);

    const std::string clean = "rm -rf '" + g_dir + "'";
    assert(std::system(clean.c_str()) == 0);
    unsetenv(CP0_CAPS_PACTL_ENV);
    unsetenv(CP0_CAPS_AMIXER_ENV);
    unsetenv(CP0_CAPS_ASOUND_ROOT_ENV);
}

} // namespace

int main()
{
    int percent = -1;
    assert(!Cp0AudioMixerControl::parse_alsa_volume(nullptr, percent) && percent == -1);
    assert(!Cp0AudioMixerControl::parse_alsa_volume("  Limits: Playback 0 - 87\n", percent) && percent == -1);
    assert(!Cp0AudioMixerControl::parse_alsa_volume("[-26.25dB] [on] [101%]", percent) && percent == -1);
    assert(Cp0AudioMixerControl::parse_alsa_volume("  Front Left: Playback 52 [60%] [-26.25dB] [on]\n"
                                                   "  Front Right: Playback 52 [61%] [-26.25dB] [on]\n", percent) &&
           percent == 60);
    assert(Cp0AudioMixerControl::parse_alsa_volume("  Mono: Playback 0 [0%] [-87.00dB] [off]", percent) && percent == 0);
    assert(Cp0AudioMixerControl::parse_alsa_mute(nullptr) == -1);
    assert(Cp0AudioMixerControl::parse_alsa_mute("  Mono: Playback 52 [60%]") == -1);
    assert(Cp0AudioMixerControl::parse_alsa_mute("  Mono: Playback 52 [60%] [on]") == 0);
    assert(Cp0AudioMixerControl::parse_alsa_mute("  Mono: Playback 52 [60%] [off]") == 1);
    assert(Cp0AudioMixerControl::parse_alsa_mute("Front Left: [off]\nFront Right: [on]") == 1);
    test_backend_chain();

    int value = 77;
    assert(!Cp0AudioMixerControl::parse_volume_line(nullptr, value) && value == 77);
    assert(!Cp0AudioMixerControl::parse_volume_line("", value) && value == 77);
    assert(!Cp0AudioMixerControl::parse_volume_line("Volume: junk%", value) && value == 77);
    assert(!Cp0AudioMixerControl::parse_volume_line("Volume: -1%", value) && value == 77);
    assert(!Cp0AudioMixerControl::parse_volume_line("Volume: junk42%", value) && value == 77);
    assert(!Cp0AudioMixerControl::parse_volume_line("Volume: 101%", value) && value == 77);
    assert(!Cp0AudioMixerControl::parse_volume_line("Volume: 999999999999999999999%", value) && value == 77);
    assert(Cp0AudioMixerControl::parse_volume_line("Volume: 0%", value) && value == 0);
    assert(Cp0AudioMixerControl::parse_volume_line("Volume: 100%", value) && value == 100);
    assert(Cp0AudioMixerControl::parse_volume_line("Volume: 42% / 42%", value) && value == 42);
}
