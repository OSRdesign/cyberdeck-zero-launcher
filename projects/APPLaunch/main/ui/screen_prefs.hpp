/*
 * SPDX-License-Identifier: MIT
 *
 * The three Settings > Screen preferences of 2026-11-18, their storage and their start-up effect (the rules are in
 * model/screen_prefs_model.hpp):
 *
 *   Idle dim level     config "idle_dim_percent"; launcher_media_controls::set_idle_dim_override() at once
 *   Terminal text size config "terminal_font_px"; read by the terminal page as it opens (stored_terminal_font())
 *   Screen size        config "screen_diag" + "screen_mm"; apply_at_start() sets APPLAUNCH_PANEL_MM before the display opens
 *
 * main() calls apply_at_start() first (before pda_mode::apply_at_start(): whether the writing band fits depends on the
 * panel size). Everything else runs on the LVGL thread through the config service (cp0_signal_config_api), and a write
 * is one SetManyAndSave transaction: when the store is full (64 keys) it is refused and the caller says so.
 */
#pragma once

#include "model/screen_prefs_model.hpp"

#include <string>

namespace screen_prefs {

/* What this launcher started with. */
struct Started {
    Stored stored;
    bool applied = false;       /* APPLAUNCH_PANEL_MM was set from the setting */
    std::string board_mm;       /* board.conf's APPLAUNCH_PANEL_MM before (empty: none) */
};

/* main(), before the display opens: reads ~/.config/cardputerzero/config.json (the config service starts later), sets
 * APPLAUNCH_PANEL_MM from a valid stored screen_mm, logs one "[screen-prefs]" line. Once; later calls return the first
 * result. Without it (the render harness) startup() is the board's own: nothing stored, nothing applied. */
const Started &apply_at_start();
const Started &startup();

/* The stored values as the config service holds them now (a change made in Settings shows at once). */
Stored stored();
int stored_terminal_font();

/* Persist a choice (nothing is written when it is what is stored). False when it could not be saved. */
bool save_dim(int percent);
bool save_terminal_font(int px);
bool save_size(const std::string &diag, const std::string &mm);

/* The screen size install.sh recorded in /etc/applaunch/install.conf (APPLAUNCH_INSTALL_CONF overrides the path, for
 * tests): "7 in" or "154x86 mm"; empty when the installer was not given one. */
std::string installer_size();

/* Where the size in force came from at this start: the setting (startup().applied), the installer, or the profile. */
SizeSource size_source_in_force();

} // namespace screen_prefs
