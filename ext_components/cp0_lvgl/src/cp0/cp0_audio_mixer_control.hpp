/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mutex>
#include <string>

/*
 * The volume and mute of the board's sound output (report 030, decision C3): a sound server first (pactl on the
 * default sink: PulseAudio, PipeWire-pulse, a Bluetooth speaker included), else the local ALSA card's playback control
 * (cp0_capabilities.h: the first non-HDMI card with a playback volume among Master, Speaker, PCM, Headphone; amixer -M,
 * the mapped percentage alsamixer shows). The programs are $APPLAUNCH_PACTL / $APPLAUNCH_AMIXER when set. Every call
 * runs programs (at most a few seconds when a server hangs): worker threads, or the volume keys' handler.
 */
class Cp0AudioMixerControl
{
public:
    static bool parse_volume_line(const char *line, int &percent);
    /* amixer sget output: the first "[NN%]" (0..100) */
    static bool parse_alsa_volume(const char *output, int &percent);
    /* amixer sget output: 1 when the first switch is "[off]" (muted), 0 when "[on]", -1 when it has none */
    static int parse_alsa_mute(const char *output);

    int read_volume() const;
    int write_volume(int percent) const;
    int read_mute() const;
    int toggle_mute() const;

private:
    static int clamp_percent(int percent);
    /* the local card and its control (found once, found again after a failure); false when there is none */
    bool alsa_target(int &card, std::string &control) const;
    void forget_alsa_target() const;
    int alsa_read_volume() const;
    int alsa_read_mute() const;

    mutable std::mutex mutex_;
    mutable int alsa_card_ = -1;
    mutable std::string alsa_control_;
};
