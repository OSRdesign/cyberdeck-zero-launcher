/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's services behind the Quick controls buttons (model/quick_controls_model.hpp, Backend): the ones
 * Settings already uses, nothing new and no privileged mechanism of its own.
 *   Wi-Fi       cp0_signal_wifi_api RadioEnabled / RadioSetEnabled (nmcli radio wifi, as Settings > Wi-Fi)
 *   Bluetooth   cp0_signal_bt_api BtStatus / BtPower (BlueZ over D-Bus, as Settings > Bluetooth > Power)
 *   Shutdown    cp0_signal_process_api Shutdown (backlight off, systemctl poweroff -i)
 *   Reboot      cp0_signal_process_api Reboot (systemctl reboot -i)
 * The two power commands run as the launcher's own user: logind lets the active session user power off and reboot
 * without a password (the same call Settings > System > Shutdown / Reboot makes after its confirmation).
 * Every function blocks (a service call with a timeout): call from a worker thread only.
 */
#pragma once

#include "model/quick_controls_model.hpp"

namespace quick_controls {

Backend launcher_backend();

} // namespace quick_controls
