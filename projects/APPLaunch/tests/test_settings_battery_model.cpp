/*
 * SPDX-License-Identifier: MIT
 *
 * Decision X12 (report 028): Settings > Battery on our boards. Its row shows only with a power_supply battery
 * (settings_battery::present() on fake sysfs trees: discharging, charging, full; none for an empty tree, an AC adapter
 * alone, a peripheral's battery), and its page's lines (settings_battery::info_lines) from the reader's values.
 */
#include "settings_battery_model.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

std::string g_base;
int g_tree = 0;

std::string new_tree()
{
    const std::string root = g_base + "/tree" + std::to_string(++g_tree);
    assert(mkdir(root.c_str(), 0755) == 0);
    return root;
}

void put(const std::string &root, const std::string &device, const std::string &file, const std::string &text)
{
    mkdir((root + "/" + device).c_str(), 0755);
    FILE *out = std::fopen((root + "/" + device + "/" + file).c_str(), "w");
    assert(out);
    std::fputs(text.c_str(), out);
    std::fclose(out);
}

void put_battery(const std::string &root, const std::string &status, const std::string &capacity)
{
    put(root, "ADP1", "type", "Mains\n");
    put(root, "ADP1", "online", status == "Discharging\n" ? "0\n" : "1\n");
    put(root, "BAT1", "type", "Battery\n");
    put(root, "BAT1", "present", "1\n");
    put(root, "BAT1", "status", status);
    put(root, "BAT1", "capacity", capacity);
    put(root, "BAT1", "energy_full_design", "33300000\n");
    put(root, "BAT1", "energy_full", "26640000\n");
    put(root, "BAT1", "energy_now", "22644000\n");
    put(root, "BAT1", "power_now", "6540000\n");
    put(root, "BAT1", "voltage_now", "7412000\n");
}

/* the row: settings_battery::present() with $APPLAUNCH_POWER_SUPPLY_ROOT on a tree */
bool present_in(const std::string &root)
{
    assert(setenv(CP0_POWER_SUPPLY_ROOT_ENV, root.c_str(), 1) == 0);
    return settings_battery::present();
}

void test_row_visibility()
{
    std::string root = new_tree();
    assert(!present_in(root)); /* a Pi board: no power_supply device at all */
    assert(!present_in(g_base + "/missing"));

    root = new_tree();
    put(root, "ADP1", "type", "Mains\n");
    put(root, "ADP1", "online", "1\n");
    assert(!present_in(root)); /* an AC adapter alone */

    root = new_tree();
    put(root, "hid-mouse-battery", "type", "Battery\n");
    put(root, "hid-mouse-battery", "scope", "Device\n");
    put(root, "hid-mouse-battery", "capacity", "70\n");
    assert(!present_in(root)); /* a mouse's battery is not the board's */

    for (const char *status : {"Discharging\n", "Charging\n", "Full\n"}) {
        root = new_tree();
        put_battery(root, status, "85\n");
        assert(present_in(root));
    }
    unsetenv(CP0_POWER_SUPPLY_ROOT_ENV);
}

/* the page through the reader: a discharging tablet battery */
void test_lines_from_tree()
{
    const std::string root = new_tree();
    put_battery(root, "Discharging\n", "85\n");
    cp0_power_battery_t battery{};
    assert(cp0_power_supply_battery_at(root.c_str(), &battery) == 1);
    const std::vector<std::string> lines = settings_battery::info_lines(true, battery);
    const std::vector<std::string> expected = {
        "Charge: 85%",       "Status: Discharging", "Time left: 3 h 27 min", "Power: 6.5 W",
        "Voltage: 7.41 V",   "Health: 80%",         "Device: BAT1",
    };
    assert(lines == expected);

    put(root, "BAT1", "status", "Charging\n");
    assert(cp0_power_supply_battery_at(root.c_str(), &battery) == 1);
    const std::vector<std::string> charging = settings_battery::info_lines(true, battery);
    assert(charging[1] == "Status: Charging");
    assert(charging[2] == "Time to full: 36 min"); /* (26.64 - 22.644) Wh at 6.54 W */

    put(root, "BAT1", "status", "Full\n");
    put(root, "BAT1", "power_now", "0\n");
    put(root, "BAT1", "capacity", "100\n");
    assert(cp0_power_supply_battery_at(root.c_str(), &battery) == 1);
    const std::vector<std::string> full = settings_battery::info_lines(true, battery);
    const std::vector<std::string> expected_full = {
        "Charge: 100%", "Status: Full", "Voltage: 7.41 V", "Health: 80%", "Device: BAT1",
    };
    assert(full == expected_full); /* no time, no power while full */
}

void test_lines()
{
    cp0_power_battery_t battery{};
    battery.percent = -1;
    assert(settings_battery::info_lines(false, battery) == std::vector<std::string>{"No battery"});
    battery.percent = 50;
    assert(settings_battery::info_lines(false, battery) == std::vector<std::string>{"No battery"});

    /* only what the driver gives: no name, no power, no voltage, no health, no time */
    battery = cp0_power_battery_t{};
    battery.percent = 7;
    battery.state = CP0_BATTERY_UNKNOWN;
    battery.power_mw = -1;
    battery.minutes = -1;
    battery.voltage_mv = -1;
    battery.health_percent = -1;
    assert((settings_battery::info_lines(true, battery) == std::vector<std::string>{"Charge: 7%", "Status: Unknown"}));
    battery.state = CP0_BATTERY_NOT_CHARGING;
    battery.minutes = 50; /* a time only for charging / discharging */
    assert(settings_battery::info_lines(true, battery)[1] == "Status: Not charging");
    assert(settings_battery::info_lines(true, battery).size() == 2);

    assert(settings_battery::duration_text(0) == "0 min");
    assert(settings_battery::duration_text(59) == "59 min");
    assert(settings_battery::duration_text(60) == "1 h 00 min");
    assert(settings_battery::duration_text(125) == "2 h 05 min");
    assert(settings_battery::duration_text(-3) == "0 min");
    assert(settings_battery::power_text(6540) == "6.5 W");
    assert(settings_battery::power_text(6550) == "6.6 W");
    assert(settings_battery::power_text(949) == "0.9 W");
    assert(settings_battery::power_text(12000) == "12.0 W");
    assert(settings_battery::voltage_text(7412) == "7.41 V");
    assert(settings_battery::voltage_text(3805) == "3.81 V");
    assert(settings_battery::voltage_text(12000) == "12.00 V");
}

} // namespace

int main()
{
    const char *tmp = std::getenv("TMPDIR");
    std::string pattern = std::string(tmp && tmp[0] ? tmp : "/tmp") + "/settings-battery-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    assert(mkdtemp(buffer.data()) != nullptr);
    g_base = buffer.data();

    test_row_visibility();
    test_lines_from_tree();
    test_lines();

    const std::string command = "rm -rf '" + g_base + "'";
    if (std::system(command.c_str()) != 0) std::fprintf(stderr, "could not remove %s\n", g_base.c_str());
    std::printf("settings battery model OK\n");
    return 0;
}
