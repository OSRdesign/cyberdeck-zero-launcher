/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_audio_mixer_control.hpp"

#include "cp0_capabilities.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <initializer_list>
#include <string>

namespace {

/* a sound server's answer (pactl); a hung server costs at most this per call */
constexpr int kPactlTimeoutMs = 1500;
constexpr int kAmixerTimeoutMs = 2000;

/* the program's exit status (-1: it could not run or did not end in time); its output in `out` */
int run(std::initializer_list<const char *> args, std::string *out, int timeout_ms)
{
    const char *argv[12] = {};
    std::size_t count = 0;
    for (const char *arg : args)
        if (count + 1 < sizeof(argv) / sizeof(argv[0])) argv[count++] = arg;
    char buffer[4096];
    const int status = cp0_caps_run(argv, buffer, sizeof(buffer), timeout_ms);
    if (out) *out = buffer;
    return status;
}

int pactl(std::initializer_list<const char *> args, std::string *out)
{
    return run(args, out, kPactlTimeoutMs);
}

bool parse_bracket_percent(const char *open, int &percent)
{
    const char *digits = open + 1;
    const char *end = digits;
    while (*end >= '0' && *end <= '9') ++end;
    if (end == digits || end[0] != '%' || end[1] != ']') return false;
    int parsed = 0;
    const auto result = std::from_chars(digits, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < 0 || parsed > 100) return false;
    percent = parsed;
    return true;
}

} // namespace

bool Cp0AudioMixerControl::parse_volume_line(const char *line, int &percent)
{
    if (!line)
        return false;
    const char *marker = std::strchr(line, '%');
    if (!marker)
        return false;
    const char *start = marker;
    while (start > line && start[-1] >= '0' && start[-1] <= '9')
        --start;
    if (start == marker)
        return false;
    if (start > line && !std::isspace(static_cast<unsigned char>(start[-1])))
        return false;

    int parsed = 0;
    const auto result = std::from_chars(start, marker, parsed);
    if (result.ec != std::errc{} || result.ptr != marker || parsed < 0 || parsed > 100)
        return false;
    percent = parsed;
    return true;
}

bool Cp0AudioMixerControl::parse_alsa_volume(const char *output, int &percent)
{
    if (!output)
        return false;
    for (const char *open = std::strchr(output, '['); open; open = std::strchr(open + 1, '['))
        if (parse_bracket_percent(open, percent))
            return true;
    return false;
}

int Cp0AudioMixerControl::parse_alsa_mute(const char *output)
{
    if (!output)
        return -1;
    const char *on = std::strstr(output, "[on]");
    const char *off = std::strstr(output, "[off]");
    if (!on && !off)
        return -1;
    if (!on)
        return 1;
    if (!off)
        return 0;
    return off < on ? 1 : 0;
}

bool Cp0AudioMixerControl::alsa_target(int &card, std::string &control) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (alsa_card_ < 0) {
        char found[32] = {};
        alsa_card_ = cp0_caps_local_output_at(nullptr, found, sizeof(found));
        alsa_control_ = alsa_card_ >= 0 ? found : "";
    }
    card = alsa_card_;
    control = alsa_control_;
    return card >= 0 && !control.empty();
}

void Cp0AudioMixerControl::forget_alsa_target() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    alsa_card_ = -1;
    alsa_control_.clear();
}

int Cp0AudioMixerControl::alsa_read_volume() const
{
    int card = -1;
    std::string control;
    if (!alsa_target(card, control))
        return -1;
    const std::string card_text = std::to_string(card);
    std::string output;
    int percent = -1;
    if (run({cp0_caps_amixer(), "-c", card_text.c_str(), "-M", "sget", control.c_str()}, &output, kAmixerTimeoutMs) != 0 ||
        !parse_alsa_volume(output.c_str(), percent)) {
        forget_alsa_target();
        return -1;
    }
    return percent;
}

int Cp0AudioMixerControl::alsa_read_mute() const
{
    int card = -1;
    std::string control;
    if (!alsa_target(card, control))
        return -1;
    const std::string card_text = std::to_string(card);
    std::string output;
    if (run({cp0_caps_amixer(), "-c", card_text.c_str(), "sget", control.c_str()}, &output, kAmixerTimeoutMs) != 0) {
        forget_alsa_target();
        return -1;
    }
    return parse_alsa_mute(output.c_str());
}

int Cp0AudioMixerControl::read_volume() const
{
    std::string output;
    if (pactl({cp0_caps_pactl(), "get-sink-volume", "@DEFAULT_SINK@"}, &output) == 0) {
        std::size_t start = 0;
        while (start < output.size()) {
            const std::size_t end = output.find('\n', start);
            const std::string line = output.substr(start, end == std::string::npos ? std::string::npos : end - start);
            int volume = -1;
            if (parse_volume_line(line.c_str(), volume))
                return volume;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    return alsa_read_volume();
}

int Cp0AudioMixerControl::write_volume(int percent) const
{
    percent = clamp_percent(percent);
    const std::string level = std::to_string(percent) + "%";
    if (pactl({cp0_caps_pactl(), "set-sink-volume", "@DEFAULT_SINK@", level.c_str()}, nullptr) == 0)
        return percent;

    int card = -1;
    std::string control;
    if (!alsa_target(card, control))
        return -1;
    const std::string card_text = std::to_string(card);
    if (run({cp0_caps_amixer(), "-c", card_text.c_str(), "-M", "sset", control.c_str(), level.c_str()}, nullptr,
            kAmixerTimeoutMs) != 0) {
        forget_alsa_target();
        return -1;
    }
    return percent;
}

int Cp0AudioMixerControl::read_mute() const
{
    std::string output;
    if (pactl({cp0_caps_pactl(), "get-sink-mute", "@DEFAULT_SINK@"}, &output) == 0) {
        if (output.find("yes") != std::string::npos)
            return 1;
        if (output.find("no") != std::string::npos)
            return 0;
    }
    return alsa_read_mute();
}

int Cp0AudioMixerControl::toggle_mute() const
{
    if (pactl({cp0_caps_pactl(), "set-sink-mute", "@DEFAULT_SINK@", "toggle"}, nullptr) == 0)
        return read_mute();

    int card = -1;
    std::string control;
    if (!alsa_target(card, control))
        return -1;
    const std::string card_text = std::to_string(card);
    if (run({cp0_caps_amixer(), "-c", card_text.c_str(), "sset", control.c_str(), "toggle"}, nullptr,
            kAmixerTimeoutMs) != 0) {
        forget_alsa_target();
        return -1;
    }
    return alsa_read_mute();
}

int Cp0AudioMixerControl::clamp_percent(int percent)
{
    return std::max(0, std::min(100, percent));
}
