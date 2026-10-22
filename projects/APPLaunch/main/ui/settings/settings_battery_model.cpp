/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Battery's lines (see settings_battery_model.hpp).
 */

#include "settings_battery_model.hpp"

#include <cstdio>

namespace settings_battery {

bool present()
{
    cp0_power_battery_t battery{};
    return cp0_power_supply_battery(&battery) != 0;
}

std::string duration_text(int minutes)
{
    if (minutes < 0) minutes = 0;
    char text[32];
    if (minutes < 60) std::snprintf(text, sizeof(text), "%d min", minutes);
    else std::snprintf(text, sizeof(text), "%d h %02d min", minutes / 60, minutes % 60);
    return text;
}

std::string power_text(int milliwatts)
{
    if (milliwatts < 0) milliwatts = 0;
    const int tenths = (milliwatts + 50) / 100;
    char text[32];
    std::snprintf(text, sizeof(text), "%d.%d W", tenths / 10, tenths % 10);
    return text;
}

std::string voltage_text(int millivolts)
{
    if (millivolts < 0) millivolts = 0;
    const int hundredths = (millivolts + 5) / 10;
    char text[32];
    std::snprintf(text, sizeof(text), "%d.%02d V", hundredths / 100, hundredths % 100);
    return text;
}

std::vector<std::string> info_lines(bool found, const cp0_power_battery_t &battery)
{
    if (!found || battery.percent < 0) return {"No battery"};
    std::vector<std::string> lines;
    lines.push_back("Charge: " + std::to_string(battery.percent) + "%");
    lines.push_back(std::string("Status: ") + cp0_power_supply_state_name(battery.state));
    if (battery.minutes >= 0 && battery.state == CP0_BATTERY_DISCHARGING)
        lines.push_back("Time left: " + duration_text(battery.minutes));
    else if (battery.minutes >= 0 && battery.state == CP0_BATTERY_CHARGING)
        lines.push_back("Time to full: " + duration_text(battery.minutes));
    if (battery.power_mw > 0) lines.push_back("Power: " + power_text(battery.power_mw));
    if (battery.voltage_mv > 0) lines.push_back("Voltage: " + voltage_text(battery.voltage_mv));
    if (battery.health_percent >= 0) lines.push_back("Health: " + std::to_string(battery.health_percent) + "%");
    if (battery.name[0]) lines.push_back(std::string("Device: ") + battery.name);
    return lines;
}

} // namespace settings_battery
