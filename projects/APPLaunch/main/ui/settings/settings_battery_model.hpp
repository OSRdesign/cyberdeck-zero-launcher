/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Battery on our boards (decision X12 of report 028): what the page shows, from a power_supply battery
 * (cp0_power_supply.h). The root row exists when the board has such a battery as Settings opens (native Settings only,
 * settings_battery::append_entry in settings_native_info_page.hpp); its page is a read-only table of these lines,
 * refreshed every 2 s. The default profile's bq27220 pages (Battery > Info, the calibration) are another thing
 * (APPLAUNCH_SETTINGS_BATTERY, settings_battery_info_page.hpp). Pure: no LVGL.
 */
#pragma once

#include "cp0_power_supply.h"

#include <string>
#include <vector>

namespace settings_battery {

/* The board has a power_supply battery now (cp0_power_supply_battery()). */
bool present();

/* The page's "Key: value" lines (settings_info::items_of makes them rows): Charge, Status, Time left (discharging) or
 * Time to full (charging), Power, Voltage, Health, Device; a value the driver does not give is left out. Not found
 * (present false): one line, "No battery" (a key alone: it fits the narrowest table). */
std::vector<std::string> info_lines(bool found, const cp0_power_battery_t &battery);

/* "45 min", "2 h 05 min" (minutes >= 0) */
std::string duration_text(int minutes);
/* "6.2 W" (mW, rounded to 0.1 W; no locale) */
std::string power_text(int milliwatts);
/* "7.61 V" (mV, rounded to 0.01 V; no locale) */
std::string voltage_text(int millivolts);

} // namespace settings_battery
