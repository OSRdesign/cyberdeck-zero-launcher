// SPDX-License-Identifier: MIT
//
// Bluetooth audio facts for apps (bt_audio.hpp): the kind of a device from BlueZ's Icon, Class and UUIDs, with the real
// values read on the HackBerry (Jabra Evolve 65, XWF-M18-M28-M38 speaker) and from the study (Beats Fit Pro), a
// keyboard and a phone; and the bt_audio_dev value of status.state.
#include "bt_audio.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

using bt_audio::bt_audio_kind;

namespace {

// as the backend publishes them (cp0_bluetooth_device_facts.hpp: short form for Bluetooth-base UUIDs)
const char *kEvolveUuids = "1101,1108,110b,110c,110d,110e,111e,1200";
const char *kBeatsUuids = "1101,110b,110c,110d,110e,111e,1200,74ec2172-0bad-4d01-8f77-997b2be0722a";
const char *kXwfUuids = "110b,110c,110e,1200";
const char *kKeyboardUuids = "1124,1200,1800,1801,180a,180f";
const char *kPhoneUuids = "1105,110a,110c,110e,1112,111f,1115,1116,112f,1132";

bt_audio::Device device(const char *address, const char *icon, unsigned cls, const char *uuids, bool paired,
                        bool connected, bool a2dp = false)
{
    bt_audio::Device d;
    d.address = address;
    d.icon = icon;
    d.device_class = cls;
    d.uuids = uuids;
    d.paired = paired;
    d.connected = connected;
    d.a2dp = a2dp;
    return d;
}

} // namespace

int main()
{
    // the real devices
    CHECK(bt_audio_kind("audio-headset", 0x240404, kEvolveUuids) == "headset"); // Jabra Evolve 65
    CHECK(bt_audio_kind("audio-headset", 0x240404, kBeatsUuids) == "headset");  // Beats Fit Pro, Icon audio-headset
    CHECK(bt_audio_kind("audio-headphones", 0x240418, kBeatsUuids) == "headset"); // ... or audio-headphones: it has Handsfree
    CHECK(bt_audio_kind("audio-headset", 0x240404, kXwfUuids) == "speaker");     // XWF: A2DP sink only, Icon audio-headset
    CHECK(bt_audio_kind("input-keyboard", 0x2540, kKeyboardUuids) == "other");
    CHECK(bt_audio_kind("phone", 0x5a020c, kPhoneUuids) == "other");

    // sink only: the Icon decides between headphones and a speaker
    CHECK(bt_audio_kind("audio-headphones", 0x240418, "110b,110c,110e") == "headphones");
    CHECK(bt_audio_kind("audio-speakers", 0x240414, "110b,110c,110e") == "speaker");
    CHECK(bt_audio_kind("audio-card", 0x240400, "110b") == "speaker");
    CHECK(bt_audio_kind("", 0x240418, "110b") == "headphones"); // no Icon: Class minor 6
    CHECK(bt_audio_kind("", 0, "110b") == "speaker");

    // Headset (1108) alone or Handsfree (111e) alone is a microphone; full-form UUIDs and capitals count
    CHECK(bt_audio_kind("", 0, "1108") == "headset");
    CHECK(bt_audio_kind("", 0, "111e") == "headset");
    CHECK(bt_audio_kind("audio-card", 0, "0000111E-0000-1000-8000-00805F9B34FB,0000110b-0000-1000-8000-00805f9b34fb") ==
          "headset");
    // the AG role (111f) is a phone's, not a headset's; the Audio Source (110a) is not a sink
    CHECK(bt_audio_kind("", 0, "111f") == "other");
    CHECK(bt_audio_kind("", 0, "110a,110c") == "other");
    // a computer or phone that also offers a sink is not an audio endpoint
    CHECK(bt_audio_kind("computer", 0x10c, "110a,110b,111e") == "other");
    CHECK(bt_audio_kind("phone", 0x200204, "110b,111e") == "other");

    // no UUIDs read yet: Icon, then Class
    CHECK(bt_audio_kind("audio-headset", 0, "") == "headset");
    CHECK(bt_audio_kind("audio-headphones", 0, "") == "headphones");
    CHECK(bt_audio_kind("audio-speakers", 0, "") == "speaker");
    CHECK(bt_audio_kind("", 0x240404, "") == "headset");
    CHECK(bt_audio_kind("", 0x240418, "") == "headphones");
    CHECK(bt_audio_kind("", 0x240414, "") == "speaker");
    CHECK(bt_audio_kind("", 0x002540, "") == "other"); // a keyboard class
    CHECK(bt_audio_kind("", 0, "") == "other");

    // bt_audio_dev
    CHECK(bt_audio::status_value({}) == "");
    // only paired audio devices; address order; underscores and capitals
    const std::vector<bt_audio::Device> all = {
        device("9e:0c:81:e6:5d:1a", "audio-headset", 0x240404, kXwfUuids, true, false),
        device("70:BF:92:53:6B:8B", "audio-headset", 0x240404, kEvolveUuids, true, true, true),
        device("F8:66:5A:18:D3:69", "audio-headphones", 0x240418, kBeatsUuids, true, true, false),
        device("11:22:33:44:55:66", "input-keyboard", 0x2540, kKeyboardUuids, true, true),
        device("AA:AA:AA:AA:AA:AA", "audio-headset", 0x240404, kEvolveUuids, false, false), // scanned, not paired
    };
    CHECK(bt_audio::status_value(all) ==
          "70_BF_92_53_6B_8B:headset:1:a2dp;9E_0C_81_E6_5D_1A:speaker:0;F8_66_5A_18_D3_69:headset:1:hfp");
    // the same set in another order gives the same text (no rewrite of the file by itself)
    std::vector<bt_audio::Device> reversed(all.rbegin(), all.rend());
    CHECK(bt_audio::status_value(reversed) == bt_audio::status_value(all));
    // profile field: a2dp only when connected; hfp only for a connected headset; a speaker without a transport: none
    CHECK(bt_audio::status_value({device("70:BF:92:53:6B:8B", "audio-headset", 0x240404, kEvolveUuids, true, false, true)}) ==
          "70_BF_92_53_6B_8B:headset:0");
    CHECK(bt_audio::status_value({device("9E:0C:81:E6:5D:1A", "audio-headset", 0x240404, kXwfUuids, true, true, false)}) ==
          "9E_0C_81_E6_5D_1A:speaker:1");
    CHECK(bt_audio::status_value({device("9E:0C:81:E6:5D:1A", "audio-headset", 0x240404, kXwfUuids, true, true, true)}) ==
          "9E_0C_81_E6_5D_1A:speaker:1:a2dp");
    // at most kMaxDevices, and the value fits the 320-byte field of cp0_app_status_t
    std::vector<bt_audio::Device> many;
    for (int i = 0; i < 12; ++i) {
        char address[18];
        std::snprintf(address, sizeof(address), "AA:BB:CC:DD:EE:%02X", i);
        many.push_back(device(address, "audio-headset", 0x240404, kEvolveUuids, true, true, true));
    }
    const std::string full = bt_audio::status_value(many);
    CHECK(full.size() < 320);
    size_t entries = 1;
    for (char c : full) entries += c == ';';
    CHECK(entries == bt_audio::kMaxDevices);
    CHECK(full.compare(0, 32, "AA_BB_CC_DD_EE_00:headset:1:a2dp") == 0);

    std::puts("bt_audio: ok");
    return 0;
}
