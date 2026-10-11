/*
 * SPDX-License-Identifier: MIT
 *
 * The native Wi-Fi page (task 014 P3b, Controller design): the whole Wi-Fi section of the tree (its Enable toggle,
 * Networks and Join Hidden Network) as one page in the roller look, touch and keyboard.
 *
 *   rows     "Wi-Fi" toggle (the Enable entry's API, as the section's toggle was); while on: the networks (connected
 *            first, then saved, then by signal: settings_wifi::sort_networks), each with its signal bars, a lock when
 *            secured, a blue check and a bold name when connected, "Saved" when saved; "Join Hidden Network" when the
 *            tree has it; "Rescan".
 *   status   the page's status line: the network and its IP, "Not connected" or "Wi-Fi is off"; a failed operation
 *            shows its error there (red) until the next action. Results also show as a toast.
 *   Enter    (or a tap on the highlighted row) on an open or a saved network connects at once (its row turns into a
 *            "Connecting..." row with a spinner); on a new secured network it opens the password dialog (the network's
 *            name as title, a Password field with the show / hide toggle, Cancel and Connect; Enter in the field
 *            connects; a refused password keeps the dialog, clears the field and says "Wrong password"); on the
 *            connected network it opens Details (IP, gateway, signal, MAC), Forget, Disconnect.
 *   hidden   "Join Hidden Network" opens a dialog with an SSID and a Password field (the legacy page's form).
 *   keys     Up / Down, Enter, Esc (a dialog: Cancel; Details / the actions: back to the list; the list: the section).
 *
 * The Wi-Fi service is used exactly as the legacy page uses it (settings_wifi_com in settings_wifi_model.hpp, on a
 * worker thread; results on the LVGL thread); the list refreshes every 8 s while it is shown, as the legacy page's.
 */
#pragma once

#include "settings_native_panel.hpp"

std::unique_ptr<SettingsNativePanel> settings_wifi_panel_create(const NodeIter &node,
                                                                const SettingsNativePanelContext &context);
