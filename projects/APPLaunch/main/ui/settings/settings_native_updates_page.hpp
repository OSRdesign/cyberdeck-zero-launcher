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
 *             P2 (release channel only, D4): "Install 0.8.13  57 MB" (an action). It checks first (the update helper is
 *             installed, no update running or interrupted, a battery board at 50 % or charging (D16), 300 MB free, a
 *             network) and says why in the status line when it cannot; then downloads as the user ("Downloading
 *             21 of 55 MB" with "Stop the download"; "Checking the download  sha256"), then asks (a Dialog, the
 *             study's text: downloaded and checked, the launcher restarts and closes open apps, settings, apps and
 *             the board profile kept, the old version back by itself if the new one does not start; "Keep the board
 *             plugged in" without a battery), then the shared sudo prompt runs systemd-run ... cyberdeckzero-update
 *             apply (settings_update_install.hpp). A cancelled password keeps the checked bundle ("Install 0.8.13
 *             downloaded": Install asks again). After a failed update: "Last update  rolled back" (information) and
 *             "Show log" (the result in words and the end of the helper's log); after an interruption "Repair the
 *             update" (the helper's resume, the same prompt).
 *   Updating  (P2) while the root helper runs, on both sides of the launcher's restart: its steps from the status file
 *             (Checking the download, Backup of 0.8.12, Installing 0.8.13, Restart and check: done / seconds / pending,
 *             the running step highlighted) and "Do not switch off: it restarts by itself"; Esc stays (report 047
 *             section 4.2: no Esc in a phase that cannot be cancelled); the lock screen is held off. When the helper
 *             ends the page goes back to the Launcher level, which says how it ended.
 *   What's new  the notes as plain text with version headings (D9: no styling), newest first, the notes of every
 *             skipped version, at most 5 versions or about 24 KB, up to the marker <!-- launcher-notes-end --> or the
 *             line **Install** (D10); a TextBlock (drag, Up / Down, Page Up / Down, Home / End)
 *   P3 (apt, LIST ONLY: nothing is upgraded from here, that is P4). The Updates level gains two rows between Launcher and
 *             Check now: "System packages" (amber "<N> updates", green "Up to date", grey "Lists are N days old" /
 *             "not checked") and "App updates" (amber "<N> updates", grey "None"; opens Settings > Apps > Installed, D19;
 *             hidden without the Store backend).
 *   System packages  "Refresh package lists" (its value: the lists' age; the dot; the shared sudo prompt, then systemd-run
 *             ... cyberdeckzero-update apt-refresh: apt-get update in a transient unit, progress bar and the percent as the
 *             value; D12: only on request), then the sections of the plan of `apt-get -s upgrade` (settings_apt_model.hpp):
 *             "Safe to upgrade (N)", "Launcher libraries (N)" (the board's ldd closure, curl, the session services),
 *             "Kernel, firmware and boot (N)" and "Needs the terminal (N)" (kept back by apt, removals: a row opens the page
 *             with the command to run in a terminal), "On this board" (grey information: automatic updates, PackageKit,
 *             snapd, holds, Store apps left out: D18, shown and never changed). Package rows: name, "old > new" in grey, the
 *             grey information dot. The Store's own apps never appear as system updates. The status line says why the list
 *             may mislead (never refreshed, N days old) or: upgrading from here comes later, the terminal now.
 *   keys      Up / Down, Enter / Right open or run the highlighted row, Esc / Left go back a level (the root:
 *             Settings); touch: a tap highlights a row, a tap on the highlighted row runs it, the header's Esc
 *
 * The progress bar (P2): an lv_bar under the status line, the row's width within the page margins, 2.5 mm high at the
 * panel's density (cp0_ui_mm_px, at least 6 px), the home grid's hold bar colours (track #3A3A3A, fill #2D9CDB); never
 * focused or tapped. It shows the download's bytes ("Downloading", full while the finished file is checked) and, on the
 * Updating page, the run's steps (checking 25 %, backup 50 %, installing 75 %, restart and check 100 %; Installing keeps
 * its seconds counter, no guessed percentage); values jump, no animation. The list moves under it while it shows.
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
