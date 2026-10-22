/*
 * SPDX-License-Identifier: MIT
 *
 * The battery of a Linux power_supply device, read from sysfs at run time (decision X12 of report 028): a PC's ACPI
 * battery (BAT0, BAT1), a fuel gauge driver's (bq27xxx), any device whose type is "Battery". The status strip
 * (cp0_ui_widgets.hpp) and Settings > Battery use it. Plain C, no LVGL, no service, no thread: a directory listing and
 * a few small reads (the kernel caches an ACPI battery's state), fine on the LVGL thread every few seconds.
 *
 * Which device: the entries of the power_supply directory in name order; the first whose `type` is "Battery", whose
 * `scope` is not "Device" (the battery of a mouse, a keyboard or a headset) and whose `present` is not 0 (an empty
 * battery bay), and which gives a charge (`capacity`, else energy_now / energy_full or charge_now / charge_full). An AC
 * adapter or a USB supply (type "Mains", "USB", ...) is never the battery. None: no battery UI anywhere.
 *
 * The directory is $APPLAUNCH_POWER_SUPPLY_ROOT when it is set and not empty (host tests and the render harness point
 * it at a fake tree), else /sys/class/power_supply.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_POWER_SUPPLY_ROOT_ENV "APPLAUNCH_POWER_SUPPLY_ROOT"
#define CP0_POWER_SUPPLY_DEFAULT_ROOT "/sys/class/power_supply"

typedef enum {
    CP0_BATTERY_UNKNOWN = 0,  /* "Unknown" or no status file */
    CP0_BATTERY_DISCHARGING,  /* "Discharging": on the battery */
    CP0_BATTERY_CHARGING,     /* "Charging" */
    CP0_BATTERY_FULL,         /* "Full": on external power, charged */
    CP0_BATTERY_NOT_CHARGING, /* "Not charging": on external power, held below full (a charge threshold) */
} cp0_battery_state_t;

typedef struct {
    char name[32];             /* the device's directory: "BAT1" */
    int percent;               /* charge, 0..100 */
    cp0_battery_state_t state;
    int power_mw;              /* power drawn from (discharging) or going into (charging) the battery, mW; -1 unknown */
    int minutes;               /* discharging: time to empty, charging: time to full, in minutes; -1 unknown */
    int voltage_mv;            /* -1 unknown */
    int health_percent;        /* full charge capacity / design capacity (at most 100); -1 unknown */
} cp0_power_battery_t;

/* The battery under `root` (NULL or "": the default directory above, $APPLAUNCH_POWER_SUPPLY_ROOT first). Returns 1 and
 * fills *out when there is one; 0 when there is none (or out is NULL): *out is then cleared, percent and the optional
 * fields -1. */
int cp0_power_supply_battery_at(const char *root, cp0_power_battery_t *out);

/* cp0_power_supply_battery_at(NULL, out). */
int cp0_power_supply_battery(cp0_power_battery_t *out);

/* The status text of a state as sysfs spells it ("Discharging", "Charging", "Full", "Not charging", "Unknown"). */
const char *cp0_power_supply_state_name(cp0_battery_state_t state);

#ifdef __cplusplus
}
#endif
