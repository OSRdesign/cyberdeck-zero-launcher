/*
 * SPDX-License-Identifier: MIT
 *
 * The native Settings > Apps page (task 014 P3d; P3e: Installed / Available split): the legacy page (deleted in P3e;
 * see git history) in the roller look, touch and keyboard, in levels, plus action lists so that every action also
 * works by touch. Rules in settings_apps_model.hpp, workers in settings_apps_jobs.hpp.
 *
 *   Apps      the root: "Installed (N)", "Available (M)", "Sources" (the counts follow every list; none before the
 *             first list or without the backend). Each opens a level ("Settings > Apps > Installed", ...).
 *   Installed "Update all (N)" when more than one update is offered, then every installed app (its version, amber
 *             "installed>offered" when an update is offered, green otherwise). Enter or a tap opens its action list.
 *   Available every app of the enabled sources that is not installed ("v<offered>", grey). Enter or a tap opens its page (Description, Install, Cancel).
 *             Empty: "Loading..." (spinner), "No apps installed" / "Nothing to install", or "The Store is needed
 *             (backend missing)". The app a job runs for shows a spinner.
 *   Sources   every source ("off", "waiting", a spinner while it syncs, "done", "failed" in red, "<N> apps" in green,
 *             "not synced"), "+ Add a GitHub source", "Sync my sources".
 *   actions   an installed app's list (Enter or a tap) or the highlighted source's (a tap) is a level titled with its
 *             name, the Wi-Fi / Bluetooth pattern: Description (when the app has one: its first line, a chevron;
 *             Enter or a tap opens the whole text in a read-only page, plain text, wrapped, scrollable; Esc goes
 *             back to the list), Update (an update is offered), Remove, Cancel; Enable or Disable,
 *             Sync (that source alone), Remove, Cancel. Remove asks first; the other rows start at once and go back to
 *             the list, where the row shows the job.
 *   status    the page's status line: the running step and its seconds ("Downloading Packet Radar (1/2) 5s"), the
 *             result ("Installed ...", amber "Synced 2 of 3 sources", red "Wrong password"), or, while nothing runs,
 *             the reason of the highlighted (or listed) failed source ("owner/repo: HTTP 404 (cached list)") in red. It
 *             wraps onto up to three lines instead of being cut.
 *   Enter     opens a level, an installed app's action list; opens an available app's page, whose Install row installs it (the shared sudo password
 *             prompt asks once per app); keeps the legacy page's direct action on Sources (a source on / off, the
 *             add-source entry, sync); runs Update all. On an action list Enter (or a tap) runs the highlighted row.
 *   dialogs   remove an app / a source: "Remove this app?" / "Remove this source?" with its name, Cancel and Remove;
 *             a failed install / update / remove: its headline and reason with OK; "Add a GitHub source": a text
 *             field (owner/repo or a registry.json address; the on-screen keyboard when no physical one), Cancel and
 *             Add, the address error in red under the field.
 *   keys      Up / Down, Enter; Right: the root opens the highlighted level (elsewhere nothing); Left: a level -> the
 *             root (the root: back); Esc: a dialog's Cancel, an action list -> its list, Installed / Available /
 *             Sources -> the root, the root -> Settings; on the lists U update all (Installed), S sync, A add a source
 *             (Sources), D or Del remove the highlighted installed app or source (after the confirmation). The letters are read from the physical key: W, E, R, T never move the highlight
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
 * stock builds, the SDL simulator) tolerates a null page, so the entry does nothing there. */
std::unique_ptr<DComponens::LvglComponensBase> settings_apps_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

std::unique_ptr<SettingsNativePanel> settings_apps_panel_create(const SettingsNativePanelContext &context);
