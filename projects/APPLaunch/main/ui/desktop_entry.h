/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>

struct DesktopEntry
{
    std::string name;
    std::string icon;
    std::string exec;
    bool terminal = false;
    bool sysplause = true;
    bool fullscreen = false;   // X-Fullscreen=true: the app owns the whole panel, no toolbar (Pi port)
    // X-MinSize=WxH (app contract 1, task 014 P5): the smallest logical landscape screen the app supports, px.
    // 0 x 0 = no restriction (key absent or unparsable).
    int min_w = 0;
    int min_h = 0;
};

// Parses an X-MinSize value: "WxH" (also 'X'), decimal, each 1..65535, spaces around the separator allowed.
// Returns false (and leaves w / h untouched) for anything else; the caller then applies no restriction.
bool parse_desktop_min_size(std::string_view value, int &w, int &h);

// Whether a tile with this X-MinSize is shown on a screen of screen_w x screen_h logical px. has_screen = false (no
// layout metrics: stock / SDL builds) always shows it; so does min 0 x 0. Hidden when either dimension is smaller.
bool desktop_min_size_fits(int min_w, int min_h, bool has_screen, int screen_w, int screen_h);

inline constexpr std::size_t DESKTOP_ENTRY_MAX_NAME_BYTES = 128;
inline constexpr std::size_t DESKTOP_ENTRY_MAX_ICON_BYTES = 512;
inline constexpr std::size_t DESKTOP_ENTRY_MAX_EXEC_BYTES = 512;

std::optional<DesktopEntry> parse_desktop_entry(std::string_view contents);
bool desktop_entry_filename_valid(std::string_view name);
