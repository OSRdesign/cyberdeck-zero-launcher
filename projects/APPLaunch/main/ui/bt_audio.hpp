/*
 * SPDX-License-Identifier: MIT
 *
 * Bluetooth audio facts for apps: what kind of audio device a paired Bluetooth device is (headset with a microphone,
 * headphones, speaker, or not an audio device), and the `bt_audio_dev` value of status.state built from them. Pure
 * functions (no BlueZ, no LVGL, no threads): the facts come from BlueZ's Icon, Class and UUIDs properties, read by the
 * Bluetooth backend (cp0_bt_device_t). The launcher only reports; it adds no audio control and never changes the
 * default sound source or sink.
 */
#pragma once

#include <string>
#include <vector>

namespace bt_audio {

/* The kind of a device from its BlueZ Icon (e.g. "audio-headset"), Class (Class of Device, 0 = unknown) and UUIDs (comma
 * separated, 16-bit services as four hex digits "111e" or as full UUIDs, any case):
 *
 *   "headset"     a Headset (1108) or Handsfree (111e) service is offered: it has a microphone
 *   "headphones"  an Audio Sink (110b) and no call profile, and BlueZ says headphones (Icon audio-headphones, or Class
 *                 minor 6)
 *   "speaker"     an Audio Sink and no call profile otherwise (Icon audio-speakers, audio-card, or even audio-headset:
 *                 the XWF speaker says audio-headset but has no Handsfree)
 *   "other"       anything else (keyboards, phones, computers, ...)
 *
 * A device whose Icon says phone, computer, input-*, camera, printer, ... is "other" even if it offers a sink. With no
 * UUIDs known at all (not read yet) the Icon, then the Class, decide. */
std::string bt_audio_kind(const std::string &icon, unsigned device_class, const std::string &uuids);

struct Device {
    std::string address; /* "AA:BB:CC:DD:EE:FF" */
    std::string icon;
    unsigned device_class = 0;
    std::string uuids;
    bool paired = false;
    bool connected = false;
    bool a2dp = false; /* an A2DP media transport exists: the music profile is available */
};

/* The `bt_audio_dev` value of status.state: for every paired device whose kind is headset, headphones or speaker (at
 * most kMaxDevices, in address order, so the text does not change by itself)
 *     AA_BB_CC_DD_EE_FF:kind:connected[:profile]
 * joined with ';'. connected is 0 or 1; profile is "a2dp" for a connected device with a music transport, "hfp" for a
 * connected headset without one (call profiles only), and left out (with its colon) otherwise. Readers must ignore
 * fields they do not know after the profile. "" when there is none. */
constexpr std::size_t kMaxDevices = 8;
std::string status_value(const std::vector<Device> &devices);

} // namespace bt_audio
