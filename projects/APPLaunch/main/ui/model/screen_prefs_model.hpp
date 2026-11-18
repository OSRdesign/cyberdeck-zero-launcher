/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Screen > Idle dim level, Terminal text size and Screen size (2026-11-18): the pure rules, no LVGL and no
 * file access, unit-tested on the PC (tests/test_screen_prefs_model.cpp). screen_prefs.hpp reads and writes them,
 * settings_screen_prefs_page.cpp shows them.
 *
 * Stored in ~/.config/cardputerzero/config.json (the launcher's user settings; install.sh and the update helper never
 * touch it, so the choices survive every update), flat keys, text or number:
 *   idle_dim_percent   1..100; 0 or absent = Default (the board profile's APPLAUNCH_DIM_PERCENT, else 10)
 *   terminal_font_px   10..48; 0 or absent = Auto
 *   screen_diag        the diagonal in inches as text ("7", "10.1"); "" or absent = Auto
 *   screen_mm          WxH in mm (landscape, larger side first) of that diagonal on this board's canvas; "" or absent = Auto
 * "Default" / "Auto" write the neutral value (the config store has no remove): a stored 0 or "" is no override.
 *
 * Precedence, the same for all three: board profile (install.sh writes it into board.conf) < hand edit of board.conf
 * (the same file) < the setting. For the terminal font Auto keeps honouring APPLAUNCH_TERM_FONT of board.conf
 * (st_viewport::font_px_resolved); for the panel size the setting sets APPLAUNCH_PANEL_MM at the launcher's start
 * (screen_prefs::apply_at_start) and so every app the launcher starts sees it; the idle dim override applies at once.
 *
 * The panel size from the diagonal is install.sh's screen_mm(): the same formula and the same measured exceptions, and
 * tests/test_screen_prefs_model.cpp runs install.sh's own function against this one for every diagonal and canvas.
 */
#pragma once

#include "cp0_config_json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace screen_prefs {

constexpr const char *kDimKey = "idle_dim_percent";
constexpr const char *kTermKey = "terminal_font_px";
constexpr const char *kDiagKey = "screen_diag";
constexpr const char *kMmKey = "screen_mm";

// ------------------------------------------------------------------ idle dim level

constexpr int kDimSteps[] = {5, 10, 20, 30, 50};
constexpr int kDimStepCount = static_cast<int>(sizeof(kDimSteps) / sizeof(kDimSteps[0]));

/* "20 %" */
inline std::string dim_label(int percent)
{
    return std::to_string(percent) + " %";
}

/* "Default (10 %)": the first row of the list, the profile's level in force without a setting */
inline std::string dim_default_label(int profile_percent)
{
    return "Default (" + dim_label(profile_percent) + ")";
}

/* A stored value is an override when it is 1..100 (a hand edit may hold a level that is not a step: it is honoured). */
constexpr bool dim_stored_valid(int stored)
{
    return stored >= 1 && stored <= 100;
}

/* What dims the screen: the setting, else the profile's level. */
constexpr int dim_effective(int profile_percent, int stored)
{
    return dim_stored_valid(stored) ? stored : profile_percent;
}

/* The list's rows: Default first, then the steps. */
inline std::vector<int> dim_choices()
{
    std::vector<int> values{0};
    for (const int step : kDimSteps) values.push_back(step);
    return values;
}

/* The row of the stored value (0 = Default); a stored level that is not a step gets no row of its own and shows as
 * Default's row would be wrong, so -1 is returned and the page appends a row for it (dim_choices_with). */
inline int dim_row_of(int stored)
{
    if (!dim_stored_valid(stored)) return 0;
    for (int i = 0; i < kDimStepCount; ++i)
        if (kDimSteps[i] == stored) return i + 1;
    return -1;
}

/* dim_choices() plus the stored level when it is not a step (kept so that the list shows what is in force). */
inline std::vector<int> dim_choices_with(int stored)
{
    std::vector<int> values = dim_choices();
    if (dim_row_of(stored) < 0) values.push_back(stored);
    return values;
}

// ------------------------------------------------------------------ terminal text size

/* Auto, then the sizes. 24 px is the largest that still gives a usable grid on the narrowest screens. */
constexpr int kTermSizes[] = {14, 17, 20, 24};
constexpr int kTermSizeCount = static_cast<int>(sizeof(kTermSizes) / sizeof(kTermSizes[0]));

inline const char *term_size_name(int px)
{
    switch (px) {
    case 14: return "Small";
    case 17: return "Medium";
    case 20: return "Large";
    case 24: return "XL";
    default: return "";
    }
}

/* "Small 14 px", "Auto (20 px)" (Auto: the size it resolves to) */
inline std::string term_label(int px, int auto_px)
{
    if (px == 0) return "Auto (" + std::to_string(auto_px) + " px)";
    const std::string name = term_size_name(px);
    return (name.empty() ? std::string("Custom") : name) + " " + std::to_string(px) + " px";
}

/* "57 x 40": the grid a size gives (columns x rows); the note on its row */
inline std::string grid_text(int cols, int rows)
{
    return std::to_string(cols) + " x " + std::to_string(rows);
}

inline std::vector<int> term_choices()
{
    std::vector<int> values{0};
    for (const int px : kTermSizes) values.push_back(px);
    return values;
}

inline int term_row_of(int stored)
{
    if (stored <= 0) return 0;
    for (int i = 0; i < kTermSizeCount; ++i)
        if (kTermSizes[i] == stored) return i + 1;
    return -1; /* a hand-edited size that is not in the list: the page appends it */
}

// ------------------------------------------------------------------ screen size

/* The diagonals of the list, as install.sh's --screen-size takes them (inches, text). */
inline const std::vector<std::string> &diagonals()
{
    static const std::vector<std::string> list{"2.8", "3.5", "4.3", "5", "5.5", "7", "8", "10.1"};
    return list;
}

/* install.sh's valid_size: digits and at most one point (no leading or trailing one), 1.5 to 15 inches. */
inline bool diag_valid(const std::string &text)
{
    if (text.empty() || text.front() == '.' || text.back() == '.') return false;
    int points = 0;
    for (const char c : text) {
        if (c == '.') ++points;
        else if (c < '0' || c > '9') return false;
    }
    if (points > 1) return false;
    const double value = std::strtod(text.c_str(), nullptr);
    return value >= 1.5 && value <= 15;
}

/* install.sh's valid_mm: WxH, 20 to 500 each. */
inline bool mm_parse(const std::string &text, int &w, int &h)
{
    const std::size_t x = text.find('x');
    if (x == std::string::npos || x == 0 || x + 1 >= text.size()) return false;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (i != x && (text[i] < '0' || text[i] > '9')) return false;
    if (text.size() - 1 - x > 3 || x > 3) return false;
    w = std::atoi(text.substr(0, x).c_str());
    h = std::atoi(text.substr(x + 1).c_str());
    return w >= 20 && w <= 500 && h >= 20 && h <= 500;
}

inline bool mm_valid(const std::string &text)
{
    int w = 0, h = 0;
    return mm_parse(text, w, h);
}

/* install.sh's screen_mm D W H: "WxH" in mm of a D inch diagonal on a W by H canvas (landscape: the larger side first),
 * each side rounded to the nearest mm, except for the sizes the board profiles already know (measured or from the data
 * sheet): 4.3, 5 and 7 inch at 800x480 and 3.5 inch at 480x320. "" for an unusable diagonal or canvas. */
inline std::string panel_mm(const std::string &diagonal, int canvas_w, int canvas_h)
{
    if (!diag_valid(diagonal) || canvas_w <= 0 || canvas_h <= 0) return {};
    const double d = std::strtod(diagonal.c_str(), nullptr);
    int w = canvas_w, h = canvas_h;
    if (w < h) {
        const int t = w;
        w = h;
        h = t;
    }
    if (w == 800 && h == 480) {
        if (d == 4.3) return "94x56";
        if (d == 5) return "108x65";
        if (d == 7) return "154x86";
    }
    if (w == 480 && h == 320 && d == 3.5) return "79x49";
    const double n = std::sqrt(static_cast<double>(w) * w + static_cast<double>(h) * h);
    char out[32];
    std::snprintf(out, sizeof(out), "%dx%d", static_cast<int>(d * 25.4 * w / n + 0.5), static_cast<int>(d * 25.4 * h / n + 0.5));
    return out;
}

/* "7 in", "10.1 in" */
inline std::string diag_label(const std::string &diagonal)
{
    return diagonal + " in";
}

/* "Auto (board profile)": the first row of the list, no setting */
inline const char *size_auto_label()
{
    return "Auto (board profile)";
}

/* Where the size in force came from. */
enum class SizeSource { Auto, Installer, Setting };

inline const char *source_name(SizeSource source)
{
    return source == SizeSource::Setting ? "setting" : source == SizeSource::Installer ? "installer" : "auto";
}

/* install.conf's text (key=value lines): the installer was given a size when screen_size= or panel_mm= has a value. The
 * value is returned for the page ("7", or "154x86 mm"); empty when the installer was not given one. */
inline std::string installer_size_from_conf(const std::string &conf)
{
    std::string diag, mm;
    std::size_t pos = 0;
    while (pos <= conf.size()) {
        std::size_t end = conf.find('\n', pos);
        if (end == std::string::npos) end = conf.size();
        std::string line = conf.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("screen_size=", 0) == 0) diag = line.substr(12);
        else if (line.rfind("panel_mm=", 0) == 0) mm = line.substr(9);
        pos = end + 1;
    }
    if (diag_valid(diag)) return diag_label(diag);
    if (mm_valid(mm)) return mm + " mm";
    return {};
}

/* The source of the size in force: a stored size (screen_mm valid) is the setting, else an installer size, else the
 * profile's own. */
inline SizeSource size_source(const std::string &stored_mm, const std::string &installer_size)
{
    if (mm_valid(stored_mm)) return SizeSource::Setting;
    return installer_size.empty() ? SizeSource::Auto : SizeSource::Installer;
}

/* APPLAUNCH_PANEL_MM to set at start: the stored WxH when it is valid, else "" (board.conf's value stays). */
inline std::string panel_mm_to_apply(const std::string &stored_mm)
{
    return mm_valid(stored_mm) ? stored_mm : std::string();
}

/* The list's row of the stored diagonal: 0 = Auto, 1.. the diagonals; -1 a valid diagonal that is not in the list (a
 * hand edit; the page appends it). */
inline int size_row_of(const std::string &stored_diag, const std::string &stored_mm)
{
    if (!mm_valid(stored_mm) || !diag_valid(stored_diag)) return 0;
    const std::vector<std::string> &list = diagonals();
    const double want = std::strtod(stored_diag.c_str(), nullptr);
    for (std::size_t i = 0; i < list.size(); ++i)
        if (std::strtod(list[i].c_str(), nullptr) == want) return static_cast<int>(i) + 1;
    return -1;
}

// ------------------------------------------------------------------ the stored values

struct Stored {
    int dim = 0;        /* idle_dim_percent, 0 = Default */
    int terminal = 0;   /* terminal_font_px, 0 = Auto */
    std::string diag;   /* screen_diag, "" = Auto */
    std::string mm;     /* screen_mm, "" = Auto */
};

/* A stored number: digits only, in low..high; anything else (absent, text, out of range) is the neutral 0. */
inline int stored_int(const std::string &text, int low, int high)
{
    if (text.empty() || text.size() > 4) return 0;
    for (const char c : text)
        if (c < '0' || c > '9') return 0;
    const int value = std::atoi(text.c_str());
    return value >= low && value <= high ? value : 0;
}

/* The values in config.json's text. The diagonal and the size go together: a size without a usable diagonal (or the
 * other way round) is no size. */
inline Stored stored_from_json(const std::string &json)
{
    Stored s;
    std::vector<std::pair<std::string, std::string>> entries;
    if (!cp0cfg::from_json(json, entries)) return s;
    for (const auto &entry : entries) { /* the last one counts */
        if (entry.first == kDimKey) s.dim = stored_int(entry.second, 1, 100);
        else if (entry.first == kTermKey) s.terminal = stored_int(entry.second, 10, 48);
        else if (entry.first == kDiagKey) s.diag = entry.second;
        else if (entry.first == kMmKey) s.mm = entry.second;
    }
    if (!diag_valid(s.diag) || !mm_valid(s.mm)) {
        s.diag.clear();
        s.mm.clear();
    }
    return s;
}

} // namespace screen_prefs
