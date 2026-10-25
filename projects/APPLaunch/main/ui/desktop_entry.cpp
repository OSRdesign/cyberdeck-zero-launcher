/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "desktop_entry.h"

#include <sstream>

namespace {
void trim(std::string &value)
{
    const size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        value.clear();
        return;
    }
    const size_t last = value.find_last_not_of(" \t");
    value = value.substr(first, last - first + 1);
}

bool parse_bool(const std::string &value)
{
    return value == "true" || value == "True" || value == "1";
}

bool contains_nul(const std::string &value)
{
    return value.find('\0') != std::string::npos;
}

bool contains_control(const std::string &value)
{
    for (const unsigned char character : value) {
        if (character < 0x20 || character == 0x7F) return true;
    }
    return false;
}
} // namespace

bool desktop_entry_filename_valid(std::string_view name)
{
    constexpr std::string_view suffix = ".desktop";
    if (name.size() <= suffix.size() ||
        name.substr(name.size() - suffix.size()) != suffix)
        return false;
    for (const unsigned char character : name) {
        if (character < 0x20 || character == 0x7F || character == '/' || character == '\\')
            return false;
    }
    return true;
}

namespace {
bool parse_dimension(std::string_view text, int &out)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    if (text.empty() || text.size() > 5) return false;
    int value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') return false;
        value = value * 10 + (character - '0');
    }
    if (value < 1 || value > 65535) return false;
    out = value;
    return true;
}
} // namespace

bool parse_desktop_min_size(std::string_view value, int &w, int &h)
{
    const size_t separator = value.find_first_of("xX");
    if (separator == std::string_view::npos) return false;
    int parsed_w = 0;
    int parsed_h = 0;
    if (!parse_dimension(value.substr(0, separator), parsed_w) ||
        !parse_dimension(value.substr(separator + 1), parsed_h))
        return false;
    w = parsed_w;
    h = parsed_h;
    return true;
}

bool desktop_min_size_fits(int min_w, int min_h, bool has_screen, int screen_w, int screen_h)
{
    if (!has_screen) return true;
    return screen_w >= min_w && screen_h >= min_h;
}

bool desktop_portrait_screen(int screen_w, int screen_h)
{
    return screen_w > 0 && screen_h > screen_w;
}

DesktopFit desktop_fit(const DesktopEntry &entry, bool has_screen, int screen_w, int screen_h)
{
    if (!has_screen) return DesktopFit::Native;
    /* a portrait launcher shows responsive fullscreen apps and terminal apps only: a compat app that is not a
     * terminal is a fixed 320x170 page (report 040, S2 owner decision), too small at the top of a tall screen */
    if (!entry.fullscreen && !entry.terminal && desktop_portrait_screen(screen_w, screen_h)) return DesktopFit::Hidden;
    if (!entry.fullscreen || !desktop_portrait_screen(screen_w, screen_h))
        return desktop_min_size_fits(entry.min_w, entry.min_h, true, screen_w, screen_h) ? DesktopFit::Native
                                                                                         : DesktopFit::Hidden;
    const bool declares_portrait = entry.portrait_w > 0 && entry.portrait_h > 0;
    if (declares_portrait && desktop_min_size_fits(entry.portrait_w, entry.portrait_h, true, screen_w, screen_h))
        return DesktopFit::Native;
    /* landscape only, or a portrait minimum larger than this panel: turned, X-MinSize against the landscape canvas */
    return desktop_min_size_fits(entry.min_w, entry.min_h, true, screen_h, screen_w) ? DesktopFit::Turned
                                                                                     : DesktopFit::Hidden;
}

std::optional<DesktopEntry> parse_desktop_entry(std::string_view contents)
{
    DesktopEntry entry;
    bool in_desktop_entry = false;
    bool hidden = false;
    bool valid_type = true;
    std::istringstream stream{std::string(contents)};
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            in_desktop_entry = line == "[Desktop Entry]";
            continue;
        }
        if (!in_desktop_entry) continue;

        const size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        std::string key = line.substr(0, separator);
        std::string value = line.substr(separator + 1);
        trim(key);
        trim(value);

        if (key == "Name") entry.name = std::move(value);
        else if (key == "Icon") entry.icon = std::move(value);
        else if (key == "Exec") entry.exec = std::move(value);
        else if (key == "Terminal") entry.terminal = parse_bool(value);
        else if (key == "Sysplause") entry.sysplause = parse_bool(value);
        else if (key == "X-Fullscreen") entry.fullscreen = parse_bool(value);
        else if (key == "X-MinSize") {
            int w = 0;
            int h = 0;
            if (!parse_desktop_min_size(value, w, h)) w = h = 0; // unparsable: no restriction
            entry.min_w = w;
            entry.min_h = h;
        }
        else if (key == "X-Portrait") {
            int w = 0;
            int h = 0;
            if (!parse_desktop_min_size(value, w, h)) w = h = 0; // unparsable: landscape only
            entry.portrait_w = w;
            entry.portrait_h = h;
        }
        else if (key == "X-Background") entry.background = std::move(value);
        else if (key == "Hidden" || key == "NoDisplay") hidden = hidden || parse_bool(value);
        else if (key == "Type") valid_type = value == "Application";
    }
    if (hidden || !valid_type || entry.name.empty() || entry.exec.empty() ||
        entry.name.size() > DESKTOP_ENTRY_MAX_NAME_BYTES ||
        entry.icon.size() > DESKTOP_ENTRY_MAX_ICON_BYTES ||
        entry.exec.size() > DESKTOP_ENTRY_MAX_EXEC_BYTES ||
        contains_control(entry.name) || contains_control(entry.icon) || contains_nul(entry.exec))
        return std::nullopt;
    return entry;
}
