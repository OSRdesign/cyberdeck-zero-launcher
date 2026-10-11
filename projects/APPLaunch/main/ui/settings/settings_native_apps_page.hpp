/*
 * SPDX-License-Identifier: MIT
 *
 * The native Settings > Apps page (task 014 P3d): the legacy page's two tabs (deleted in P3e; see git history) in the roller
 * look, touch and keyboard, plus action lists so that every action also works by touch. Rules in
 * settings_apps_model.hpp, workers in settings_apps_jobs.hpp.
 *
 *   Apps      "Update all (N)" when more than one update is offered, every app of the user's sources (its installed
 *             version, "installed>offered" when an update is offered, "v<offered>" when not installed; amber, green,
 *             grey as on the legacy page), then "Sources". Empty: "Loading..." (spinner), "No apps: add your source in
 *             Sources" or "The Store is needed (backend missing)". The app a job runs for shows a spinner.
 *   Sources   a level of its own ("Settings > Apps > Sources"): every source ("off", "waiting", a spinner while it
 *             syncs, "done", "failed" in red, "<N> apps" in green, "not synced"), "+ Add a GitHub source", "Sync my
 *             sources".
 *   actions   a tap on the highlighted app or source opens its action list (a level titled with its name, the Wi-Fi /
 *             Bluetooth pattern): Install / Update / Remove (what applies), Cancel; Enable or Disable, Sync (that source
 *             alone), Remove, Cancel. Remove asks first; the other rows start at once and go back to the list, where
 *             the row shows the job.
 *   status    the page's status line: the running step and its seconds ("Downloading Packet Radar (1/2) 5s"), the
 *             result ("Installed ...", amber "Synced 2 of 3 sources", red "Wrong password"), or, while nothing runs,
 *             the reason of the highlighted (or listed) failed source ("owner/repo: HTTP 404 (cached list)") in red. It
 *             wraps onto up to three lines instead of being cut.
 *   Enter     keeps the legacy page's direct action: installs or updates an app (the shared sudo password prompt asks
 *             once per app), switches a source on / off, runs Update all, opens Sources, the add-source entry, syncs;
 *             on an app that is up to date (nothing to install or update) it opens the action list (Remove). On an
 *             action list Enter (or a tap) runs the highlighted row.
 *   dialogs   remove an app / a source: "Remove this app?" / "Remove this source?" with its name, Cancel and Remove;
 *             a failed install / update / remove: its headline and reason with OK; "Add a GitHub source": a text
 *             field (owner/repo or a registry.json address; the on-screen keyboard when no physical one), Cancel and
 *             Add, the address error in red under the field.
 *   keys      Up / Down, Enter; Right: the Sources tab (on Sources: nothing); Left: back to Apps (on Apps: back);
 *             Esc: a dialog's Cancel, an action list -> its list, Sources -> Apps, Apps -> Settings; on the lists U
 *             update all (Apps), S sync, A add a source (Sources), D or Del remove the highlighted app or source (after
 *             the confirmation). The letters are read from the physical key: W, E, R, T never move the highlight
 *             (their LVGL codes are the arrows').
 *
 * No geometry here: the roller, the dialogs and the field take every size from the layout service.
 */
#pragma once

#include "settings_native_panel.hpp"
#include "settings_tree_types.hpp"

#include "lvgl_components.hpp"

#include <functional>
#include <memory>

/* The Apps entry's page identity in the Settings tree (settings_native_page() and the legacy roller know the entry by
 * this function). The legacy Apps page was deleted in P3e: it builds nothing, and the legacy roller (stock
 * CardputerZero, the SDL simulator) tolerates a null page, so the entry does nothing there. */
std::unique_ptr<DComponens::LvglComponensBase> settings_apps_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

std::unique_ptr<SettingsNativePanel> settings_apps_panel_create(const SettingsNativePanelContext &context);
