/*
 * SPDX-License-Identifier: MIT
 *
 * Bluetooth audio facts for apps (see bt_audio.hpp).
 */
#include "bt_audio.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace bt_audio {
namespace {

std::string lower(const std::string &text)
{
    std::string result;
    result.reserve(text.size());
    for (const char c : text) result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return result;
}

/* "0000110b-0000-1000-8000-00805f9b34fb" -> "110b"; short forms stay; other 128-bit UUIDs stay whole. */
std::string short_form(const std::string &raw)
{
    const std::string text = lower(raw);
    static const std::string tail = "-0000-1000-8000-00805f9b34fb";
    if (text.size() == 36 && text.compare(0, 4, "0000") == 0 && text.compare(8, tail.size(), tail) == 0)
        return text.substr(4, 4);
    return text;
}

std::set<std::string> uuid_set(const std::string &uuids)
{
    std::set<std::string> result;
    std::size_t at = 0;
    while (at <= uuids.size()) {
        std::size_t end = uuids.find(',', at);
        if (end == std::string::npos) end = uuids.size();
        const std::string item = short_form(uuids.substr(at, end - at));
        if (!item.empty()) result.insert(item);
        at = end + 1;
    }
    return result;
}

bool starts_with(const std::string &text, const char *prefix)
{
    return text.rfind(prefix, 0) == 0;
}

/* Icons BlueZ gives to things that are never an audio endpoint for us, even when they offer an Audio Sink. */
bool non_audio_icon(const std::string &icon)
{
    for (const char *prefix : {"input-", "phone", "computer", "camera", "printer", "video", "modem", "network", "scanner"})
        if (starts_with(icon, prefix)) return true;
    return false;
}

/* The audio kind from the Class of Device alone: major class 4 (audio/video), minor class in bits 2..7. */
std::string kind_from_class(unsigned device_class)
{
    if (((device_class >> 8) & 0x1f) != 4) return "other";
    switch ((device_class >> 2) & 0x3f) {
    case 1: /* wearable headset */
    case 2: /* hands-free */
        return "headset";
    case 6: return "headphones";
    case 5:  /* loudspeaker */
    case 7:  /* portable audio */
    case 8:  /* car audio */
    case 10: /* hi-fi */
        return "speaker";
    default: return "other";
    }
}

} // namespace

std::string bt_audio_kind(const std::string &icon_raw, unsigned device_class, const std::string &uuids)
{
    const std::string icon = lower(icon_raw);
    if (non_audio_icon(icon)) return "other";
    const std::set<std::string> have = uuid_set(uuids);
    if (have.empty()) { /* nothing read yet: Icon, then Class */
        if (icon == "audio-headset") return "headset";
        if (icon == "audio-headphones") return "headphones";
        if (icon == "audio-speakers" || icon == "audio-card") return "speaker";
        return kind_from_class(device_class);
    }
    const bool call = have.count("1108") || have.count("111e"); /* Headset, Handsfree: a microphone */
    if (call) return "headset";
    if (!have.count("110b")) return "other"; /* no Audio Sink: not something we can play to */
    if (icon == "audio-headphones" || (icon.empty() && kind_from_class(device_class) == "headphones")) return "headphones";
    return "speaker";
}

std::string status_value(const std::vector<Device> &devices)
{
    struct Entry {
        std::string mac;
        std::string text;
    };
    std::vector<Entry> entries;
    for (const Device &device : devices) {
        if (!device.paired) continue;
        const std::string kind = bt_audio_kind(device.icon, device.device_class, device.uuids);
        if (kind == "other") continue;
        std::string mac = device.address;
        for (char &c : mac) c = c == ':' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (mac.empty() || mac.find_first_of(";, \t\r\n") != std::string::npos) continue;
        std::string text = mac + ":" + kind + ":" + (device.connected ? "1" : "0");
        if (device.connected) {
            if (device.a2dp)
                text += ":a2dp";
            else if (kind == "headset")
                text += ":hfp";
        }
        entries.push_back({mac, text});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.mac < b.mac; });
    if (entries.size() > kMaxDevices) entries.resize(kMaxDevices);
    std::string value;
    for (const Entry &entry : entries) {
        if (!value.empty()) value += ';';
        value += entry.text;
    }
    return value;
}

} // namespace bt_audio
