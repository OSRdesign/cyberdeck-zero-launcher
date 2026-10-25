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
    // X-Background=run|freeze (app contract 1, task 015, APP-CONTRACT section 13): the value as written (trimmed);
    // multitask::parse_background() reads it. Empty = key absent (a legacy app). Used only with multitasking on.
    std::string background = std::string();
    // X-Portrait=WxH (app contract 1, additive, report 038 section 3.3): the smallest canvas the app supports on a
    // portrait launcher, px (same syntax as X-MinSize). 0 x 0 = key absent or unparsable: landscape only (a fullscreen
    // app then runs turned on a portrait launcher).
    int portrait_w = 0;
    int portrait_h = 0;
};

// Parses an X-MinSize value: "WxH" (also 'X'), decimal, each 1..65535, spaces around the separator allowed.
// Returns false (and leaves w / h untouched) for anything else; the caller then applies no restriction.
// X-Portrait uses the same syntax.
bool parse_desktop_min_size(std::string_view value, int &w, int &h);

// Whether a tile with this X-MinSize is shown on a screen of screen_w x screen_h logical px. has_screen = false (no
// layout metrics: stock / SDL builds) always shows it; so does min 0 x 0. Hidden when either dimension is smaller.
bool desktop_min_size_fits(int min_w, int min_h, bool has_screen, int screen_w, int screen_h);

// How a desktop app runs on a launcher canvas of screen_w x screen_h (report 038 section 3.2, option b):
//   Hidden  its minimum does not fit: no tile
//   Native  on the launcher's canvas as it is (every app on a landscape or square launcher; a compat app anywhere; a
//           fullscreen app with X-Portrait on a portrait launcher)
//   Turned  a fullscreen app without X-Portrait (or whose X-Portrait does not fit) on a portrait launcher: it gets the
//           panel's landscape canvas (screen_h x screen_w, app_launch_env turned_from_metrics), full screen
enum class DesktopFit { Hidden, Native, Turned };

// A portrait launcher: the logical canvas is taller than wide (480x640, 480x800).
bool desktop_portrait_screen(int screen_w, int screen_h);

// The rule. has_screen = false (no layout metrics) = Native. A portrait launcher hides a compat app that is not a
// terminal (fixed-pixel, report 040). A landscape or square launcher, and a terminal app on any launcher: X-MinSize against the canvas (desktop_min_size_fits). A fullscreen app on a portrait launcher: Native when it
// declares X-Portrait and that fits the portrait canvas; else Turned when X-MinSize fits the landscape canvas
// (screen_h x screen_w); else Hidden.
DesktopFit desktop_fit(const DesktopEntry &entry, bool has_screen, int screen_w, int screen_h);

inline constexpr std::size_t DESKTOP_ENTRY_MAX_NAME_BYTES = 128;
inline constexpr std::size_t DESKTOP_ENTRY_MAX_ICON_BYTES = 512;
inline constexpr std::size_t DESKTOP_ENTRY_MAX_EXEC_BYTES = 512;

std::optional<DesktopEntry> parse_desktop_entry(std::string_view contents);
bool desktop_entry_filename_valid(std::string_view name);
