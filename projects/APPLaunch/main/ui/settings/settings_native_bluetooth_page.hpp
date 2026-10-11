/*
 * SPDX-License-Identifier: MIT
 *
 * The native Bluetooth pages (task 014 P3c): Bluetooth > Paired Devices, Scan and Alias in the roller look, touch and
 * keyboard, at every screen size. The Bluetooth section itself (Power, Discoverable, Named Only toggles) stays the
 * host's roller.
 *
 *   Paired Devices  the paired / connected devices (Named Only as the legacy page), each with its state ("Connected",
 *                   "Paired"); Enter / a tap opens the device's actions: Connect or Disconnect, Forget (a confirmation
 *                   dialog), Rename (a dialog with a text field: the device's BlueZ alias).
 *   Scan            a "Scan" toggle (discovery on / off), then the devices found (Named Only), refreshed every 1.5 s
 *                   while discovering; Enter / a tap on a new device pairs it (discovery stops, Pair, then Connect, then
 *                   a fresh discovery); on a paired one it opens the same actions as Paired Devices.
 *   Alias           the adapter's name in a text field (the on-screen keyboard when no physical keyboard is present);
 *                   Enter saves it and goes back.
 *   status line     the adapter's name and address, "Checking Bluetooth...", "Bluetooth is off", the last operation's
 *                   result ("Paired with X") or a failure (red) until the next one. An operation in flight turns its
 *                   device's row into a busy row ("Pairing...", "Connecting...", "Removing...").
 *   power off       the legacy notice ("Bluetooth power is off" / "Turn on Power before continuing."), OK goes back.
 *   agent prompts   while one of these pages shows, the BlueZ agent's requests are Dialogs over it: PIN (number pad)
 *                   and passkey entry with their checks, the pairing code to confirm (No / Yes), a device or service
 *                   authorization (No / Yes), and a code to type on the remote device (a keyboard) with OK. With the
 *                   on-screen keyboard up the dialog's buttons go and the keyboard's Enter / Esc act for them (as the
 *                   Wi-Fi password, cp0_ui_dialog.h). Esc (a key, the header's Esc) rejects the request.
 *
 * The state machines and the rules are settings_bluetooth_model.hpp (the legacy page's protocol, shared helpers); the
 * Bluetooth API runs on a worker and its replies come back on the LVGL thread, as the legacy page's.
 */
#pragma once

#include "settings_native_panel.hpp"

enum class SettingsNativePage;

std::unique_ptr<SettingsNativePanel> settings_bluetooth_panel_create(SettingsNativePage page, const NodeIter &node,
                                                                     const SettingsNativePanelContext &context);
