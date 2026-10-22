/*
 * SPDX-License-Identifier: MIT
 *
 * Report 030 (c-1): the Settings rows of each of our boards, from fake trees of what each board has (the measured
 * facts of report 030 sections 2.2 and 8) through the real detection (cp0_capabilities.c) and the row rules
 * (settings_capabilities_model.cpp). Prints the table: board, Battery, Brightness, Speaker.
 */
#include "settings_capabilities_model.hpp"

#include "cp0_capabilities.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string g_base;
int g_count = 0;

void mkdirs(const std::string &path)
{
    for (std::size_t slash = path.find('/', 1); ; slash = path.find('/', slash + 1)) {
        const std::string part = path.substr(0, slash);
        if (mkdir(part.c_str(), 0755) != 0) assert(errno == EEXIST);
        if (slash == std::string::npos) break;
    }
}

void put(const std::string &dir, const std::string &file, const std::string &text)
{
    mkdirs(dir);
    std::ofstream(dir + "/" + file) << text;
}

void put_script(const std::string &path, const std::string &body)
{
    std::ofstream(path) << "#!/bin/sh\n" << body;
    assert(chmod(path.c_str(), 0755) == 0);
}

/* One board: its own power_supply, backlight and asound trees, the fake programs' answers. */
struct Board {
    std::string root;
    explicit Board(const char *name)
    {
        root = g_base + "/" + std::to_string(++g_count) + "-" + name;
        mkdirs(root + "/power_supply");
        mkdirs(root + "/backlight");
        mkdirs(root + "/asound");
        setenv("APPLAUNCH_POWER_SUPPLY_ROOT", (root + "/power_supply").c_str(), 1);
        setenv(CP0_CAPS_BACKLIGHT_ROOT_ENV, (root + "/backlight").c_str(), 1);
        setenv(CP0_CAPS_ASOUND_ROOT_ENV, (root + "/asound").c_str(), 1);
        setenv("FAKE_BOARD", root.c_str(), 1);
        unsetenv("APPLAUNCH_BACKLIGHT");
        unsetenv(CP0_CAPS_HINT_BATTERY_ENV);
        unsetenv(CP0_CAPS_HINT_BACKLIGHT_ENV);
        unsetenv(CP0_CAPS_HINT_SOUND_ENV);
        setenv(CP0_CAPS_PACTL_ENV, "/nonexistent/pactl", 1); /* no sound server unless the board has one */
    }
    void backlight(const std::string &name, const std::string &max)
    {
        put(root + "/backlight/" + name, "max_brightness", max + "\n");
        put(root + "/backlight/" + name, "brightness", max + "\n");
    }
    void card(int index, const std::string &line, const std::string &controls)
    {
        std::ofstream(root + "/asound/cards", std::ios::app) << line << "\n";
        mkdirs(root + "/asound/card" + std::to_string(index) + "/pcm0p");
        if (!controls.empty()) put(root, "amixer-c" + std::to_string(index) + ".txt", controls);
    }
    void server(const std::string &sinks)
    {
        put(root, "sinks.txt", sinks);
        setenv(CP0_CAPS_PACTL_ENV, (g_base + "/pactl").c_str(), 1);
    }
    void battery()
    {
        put(root + "/power_supply/ADP1", "type", "Mains\n");
        put(root + "/power_supply/BAT1", "type", "Battery\n");
        put(root + "/power_supply/BAT1", "present", "1\n");
        put(root + "/power_supply/BAT1", "capacity", "73\n");
    }
};

const char *show_text(settings_caps::RowShow show)
{
    switch (show) {
    case settings_caps::RowShow::Row: return "row";
    case settings_caps::RowShow::Info: return "info";
    case settings_caps::RowShow::Hidden: break;
    }
    return "-";
}

settings_caps::Rows rows_of(const char *name)
{
    cp0_caps_t caps;
    cp0_caps_probe(&caps, CP0_CAPS_SERVER_TIMEOUT_MS);
    const settings_caps::Rows rows = settings_caps::rows_for(caps);
    std::printf("  %-34s battery %-4s  brightness %-4s%-7s  speaker %-4s%s%s\n", name, show_text(rows.battery),
                show_text(rows.brightness), rows.brightness == settings_caps::RowShow::Row
                                                ? (rows.brightness_onoff ? " on/off" : " levels")
                                                : "",
                show_text(rows.speaker), caps.sound_control[0] ? " " : "", caps.sound_control);
    return rows;
}

using settings_caps::RowShow;

const char kHdmiCards[] = " 0 [vc4hdmi0       ]: vc4-hdmi - vc4-hdmi-0";
const char kHdmiCards1[] = " 1 [vc4hdmi1       ]: vc4-hdmi - vc4-hdmi-1";
const char kMaster[] = "Simple mixer control 'Master',0\n  Capabilities: pvolume pswitch\n"
                       "Simple mixer control 'Headphone',0\n  Capabilities: pvolume pswitch\n";
const char kPcm[] = "Simple mixer control 'PCM',0\n  Capabilities: pvolume pvolume-joined pswitch pswitch-joined\n";

} // namespace

int main()
{
    char base[] = "/tmp/applaunch-caps-XXXXXX";
    assert(mkdtemp(base));
    g_base = base;
    put_script(g_base + "/amixer",
               "[ $# -eq 2 ] && [ -f \"$FAKE_BOARD/amixer-c$2.txt\" ] && cat \"$FAKE_BOARD/amixer-c$2.txt\" && exit 0\n"
               "exit 1\n");
    put_script(g_base + "/pactl",
               "[ \"$1 $2 $3\" = \"list short sinks\" ] && [ -f \"$FAKE_BOARD/sinks.txt\" ] && cat \"$FAKE_BOARD/sinks.txt\" && exit 0\n"
               "exit 1\n");
    setenv(CP0_CAPS_AMIXER_ENV, (g_base + "/amixer").c_str(), 1);
    std::printf("Settings rows per board (report 030 c-1):\n");

    {   /* the deck: Zero 2 W, Waveshare DPI with the PWM backlight overlay ("backlight", levels); no audio output */
        Board deck("deck");
        deck.backlight("backlight", "100");
        deck.card(0, kHdmiCards, kMaster);
        const settings_caps::Rows rows = rows_of("deck (zero2w)");
        assert(rows.battery == RowShow::Hidden && rows.speaker == RowShow::Hidden);
        assert(rows.brightness == RowShow::Row && !rows.brightness_onoff);
    }
    {   /* Pi 3A+ Luckfox SPI: gpio on/off backlight from the profile; the 3.5 mm jack (bcm2835 Headphones, PCM) */
        Board pi3a("pi3a");
        pi3a.backlight("backlight_gpio", "1");
        setenv("APPLAUNCH_BACKLIGHT", ("gpio:" + pi3a.root + "/backlight/backlight_gpio").c_str(), 1);
        pi3a.card(0, " 0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones", kPcm);
        pi3a.card(1, " 1 [vc4hdmi        ]: vc4-hdmi - vc4-hdmi", kPcm);
        const settings_caps::Rows rows = rows_of("pi3a-luckfox35 (jack card)");
        assert(rows.battery == RowShow::Hidden && rows.speaker == RowShow::Row);
        assert(rows.brightness == RowShow::Row && rows.brightness_onoff);
    }
    {   /* the same board without the jack's card (not measured: HDMI only) */
        Board pi3a("pi3a-nojack");
        pi3a.backlight("backlight_gpio", "1");
        setenv("APPLAUNCH_BACKLIGHT", ("gpio:" + pi3a.root + "/backlight/backlight_gpio").c_str(), 1);
        pi3a.card(0, " 0 [vc4hdmi        ]: vc4-hdmi - vc4-hdmi", kPcm);
        const settings_caps::Rows rows = rows_of("pi3a-luckfox35 (no jack card)");
        assert(rows.speaker == RowShow::Hidden && rows.brightness_onoff);
    }
    {   /* Pi 3A+ Waveshare DSI: the overlay's regulator 10-0045 (levels, 255) from the profile */
        Board dsi("pi3a-dsi");
        dsi.backlight("10-0045", "255");
        setenv("APPLAUNCH_BACKLIGHT", ("sysfs:" + dsi.root + "/backlight/10-0045").c_str(), 1);
        dsi.card(0, " 0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones", kPcm);
        const settings_caps::Rows rows = rows_of("pi3a-wsdsi43 (jack card)");
        assert(rows.brightness == RowShow::Row && !rows.brightness_onoff && rows.speaker == RowShow::Row);
    }
    {   /* Pi 5 + HyperPixel 4: gpio-backlight "backlight" max 1 (no profile line); vc4-hdmi only; no pactl */
        Board pi5("pi5");
        pi5.backlight("backlight", "1");
        pi5.card(0, kHdmiCards, "");
        pi5.card(1, kHdmiCards1, "");
        const settings_caps::Rows rows = rows_of("pi5-hyperpixel4");
        assert(rows.battery == RowShow::Hidden && rows.speaker == RowShow::Hidden);
        assert(rows.brightness == RowShow::Row && rows.brightness_onoff); /* On / Off, no 10 dead levels */
    }
    {   /* HackBerry CM5: no backlight device, vc4-hdmi only, PipeWire with auto_null only (speaker not connected) */
        Board hb("hackberry");
        hb.card(0, kHdmiCards, "");
        hb.card(1, kHdmiCards1, "");
        hb.server("35\tauto_null\tPipeWire\tfloat32le 2ch 48000Hz\tSUSPENDED\n");
        const settings_caps::Rows rows = rows_of("hackberry-cm5 (speaker off)");
        assert(rows.battery == RowShow::Hidden && rows.brightness == RowShow::Hidden && rows.speaker == RowShow::Hidden);
        /* the case speaker connected: its Bluetooth sink */
        hb.server("35\tauto_null\tPipeWire\tfloat32le 2ch 48000Hz\tSUSPENDED\n"
                  "52\tbluez_output.9E_0C_81_E6_5D_1A.1\tPipeWire\ts16le 2ch 48000Hz\tIDLE\n");
        const settings_caps::Rows connected = rows_of("hackberry-cm5 (speaker connected)");
        assert(connected.speaker == RowShow::Row && connected.brightness == RowShow::Hidden);
        /* the hints a board profile may name (c-2): Info rows with the reason, never the row itself */
        hb.server("35\tauto_null\tPipeWire\tfloat32le 2ch 48000Hz\tSUSPENDED\n");
        setenv(CP0_CAPS_HINT_BACKLIGHT_ENV, "button", 1);
        setenv(CP0_CAPS_HINT_SOUND_ENV, "bluetooth:XWF-M18-M28-M38", 1);
        const settings_caps::Rows hinted = rows_of("hackberry-cm5 (with hints)");
        assert(hinted.brightness == RowShow::Info && hinted.brightness_info == "Brightness: use the button on the case");
        assert(hinted.speaker == RowShow::Info && hinted.speaker_info == "Speaker: pair XWF-M18-M28-M38 in Bluetooth");
        assert(hinted.battery == RowShow::Hidden && hinted.battery_info.empty());
    }
    {   /* FZ-M1 tablet: BAT1 + AC, intel_backlight (937) from the profile, HDA Intel PCH (Master) */
        Board tablet("tablet");
        tablet.battery();
        tablet.backlight("intel_backlight", "937");
        tablet.backlight("acpi_video0", "15");
        setenv("APPLAUNCH_BACKLIGHT", ("sysfs:" + tablet.root + "/backlight/intel_backlight").c_str(), 1);
        tablet.card(0, " 0 [PCH            ]: HDA-Intel - HDA Intel PCH", kMaster);
        const settings_caps::Rows rows = rows_of("pc-fzm1-2 (tablet)");
        assert(rows.battery == RowShow::Row && rows.brightness == RowShow::Row && !rows.brightness_onoff);
        assert(rows.speaker == RowShow::Row);
        /* with a sound server too: the same rows */
        tablet.server("0\talsa_output.pci-0000_00_1f.3.analog-stereo\tmodule-alsa-card.c\ts16le 2ch 44100Hz\tIDLE\n");
        assert(rows_of("pc-fzm1-2 (tablet, server)").speaker == RowShow::Row);
    }

    assert(settings_caps::info_label("Speaker", "") == "Speaker");
    const std::string clean = "rm -rf '" + g_base + "'";
    assert(std::system(clean.c_str()) == 0);
    std::printf("settings capabilities OK\n");
    return 0;
}
