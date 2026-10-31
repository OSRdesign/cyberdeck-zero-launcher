/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates (report 047 P1, owner decisions D1-D19 as recommended): the launcher checks GitHub for a newer
 * release and shows its notes. Native Settings only (the row after Apps; no legacy page). Rules in
 * settings_update_model.hpp, the check in settings_update_check.hpp, the daily background check and the toasts in
 * launcher_update.hpp.
 *
 *   root row  "Updates", its value: "0.8.13 available" (amber, the Apps page's update token), "Up to date" (green),
 *             "Check failed" / "Dev build" (grey), nothing before the first check; a chevron
 *   Updates   Launcher (the same value, opens the Launcher level; the running version is in its status line), Check now (an action: its dot turns blue
 *             while a check runs), Last checked (information: "today 10:42"). Opening the page starts a check (D3).
 *             The page's status line: "Checking for updates...", why a check failed (no network, GitHub's rate limit
 *             with its reset time, ...), "No build for amd64 in 0.8.13".
 *   Launcher  What's new in <version> (opens the notes), on a development build the information row "Development
 *             build  install by hand" (D4: why there is no install), Check again (an action); the status line
 *             "0.8.12 -> 0.8.13, release, arm64".
 *             P1 installs nothing: the Install row of the study's mockup comes with P2.
 *   What's new  the notes as plain text with version headings (D9: no styling), newest first, the notes of every
 *             skipped version, at most 5 versions or about 24 KB, up to the marker <!-- launcher-notes-end --> or the
 *             line **Install** (D10); a TextBlock (drag, Up / Down, Page Up / Down, Home / End)
 *   keys      Up / Down, Enter / Right open or run the highlighted row, Esc / Left go back a level (the root:
 *             Settings); touch: a tap highlights a row, a tap on the highlighted row runs it, the header's Esc
 *
 * The lists are CompactLists at the Comfortable density (the roller's 7 mm rows) because rows that run an action carry
 * the row affordances (chevron, dot, grey information), which the roller cannot draw; they start under the page's
 * status line. No geometry here: every size comes from the layout service.
 */
#pragma once

#include "settings_native_panel.hpp"
#include "settings_tree_types.hpp"

#include "lvgl_components.hpp"

#include <functional>
#include <memory>
#include <string>

/* The Updates entry's page identity in the Settings tree (settings_native_page() knows the entry by this function).
 * Native only: it builds nothing. */
std::unique_ptr<DComponens::LvglComponensBase> settings_updates_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

std::unique_ptr<SettingsNativePanel> settings_updates_panel_create(const SettingsNativePanelContext &context);

/* The root row's value and its colour (0xRRGGBB, -1: the roller's green) from the saved state. */
std::string settings_updates_row_value(int &value_rgb);
