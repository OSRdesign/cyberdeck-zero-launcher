/*
 * SPDX-License-Identifier: MIT
 *
 * The battery of a Linux power_supply device (see cp0_power_supply.h).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* scandir */
#endif
#include "cp0_power_supply.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MINUTES (7 * 24 * 60) /* a longer estimate is a driver's nonsense (an idle current near 0) */

static void clear(cp0_power_battery_t *out)
{
    memset(out, 0, sizeof(*out));
    out->percent = -1;
    out->state = CP0_BATTERY_UNKNOWN;
    out->power_mw = -1;
    out->minutes = -1;
    out->voltage_mv = -1;
    out->health_percent = -1;
}

/* The first line of dir/file without its line end (sysfs writes "\n"; a fake tree made on Windows may have "\r\n"). */
static int read_line(const char *dir, const char *file, char *text, size_t size)
{
    char path[640];
    const int n = snprintf(path, sizeof(path), "%s/%s", dir, file);
    if (n < 0 || (size_t)n >= sizeof(path)) return 0;
    FILE *in = fopen(path, "r");
    if (!in) return 0;
    const int ok = fgets(text, (int)size, in) != NULL;
    fclose(in);
    if (!ok) return 0;
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ' || text[len - 1] == '\t'))
        text[--len] = '\0';
    return 1;
}

static int read_number(const char *dir, const char *file, long long *value)
{
    char text[64];
    if (!read_line(dir, file, text, sizeof(text)) || !text[0]) return 0;
    char *end = NULL;
    errno = 0;
    const long long parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return 0;
    *value = parsed;
    return 1;
}

static long long magnitude(long long value) { return value < 0 ? -value : value; }

static int clamp_percent(long long value) { return value < 0 ? 0 : value > 100 ? 100 : (int)value; }

/* part / whole in percent, rounded */
static int ratio_percent(long long part, long long whole) { return clamp_percent((part * 100 + whole / 2) / whole); }

static cp0_battery_state_t parse_state(const char *status)
{
    if (strcmp(status, "Discharging") == 0) return CP0_BATTERY_DISCHARGING;
    if (strcmp(status, "Charging") == 0) return CP0_BATTERY_CHARGING;
    if (strcmp(status, "Full") == 0) return CP0_BATTERY_FULL;
    if (strcmp(status, "Not charging") == 0) return CP0_BATTERY_NOT_CHARGING;
    return CP0_BATTERY_UNKNOWN;
}

/* charge in percent: `capacity`, else energy_now / energy_full (uWh), else charge_now / charge_full (uAh) */
static int read_percent(const char *dir, int *percent)
{
    long long value = 0, now = 0, full = 0;
    if (read_number(dir, "capacity", &value)) {
        *percent = clamp_percent(value);
        return 1;
    }
    if (read_number(dir, "energy_now", &now) && read_number(dir, "energy_full", &full) && full > 0 && now >= 0) {
        *percent = ratio_percent(now, full);
        return 1;
    }
    if (read_number(dir, "charge_now", &now) && read_number(dir, "charge_full", &full) && full > 0 && now >= 0) {
        *percent = ratio_percent(now, full);
        return 1;
    }
    return 0;
}

static int minutes_of(long long numerator_x60, long long rate)
{
    if (rate <= 0 || numerator_x60 <= 0) return -1;
    const long long minutes = numerator_x60 / rate;
    return minutes > 0 && minutes <= MAX_MINUTES ? (int)minutes : -1;
}

/* time to empty (discharging) or to full (charging): the driver's own estimate (seconds), else energy / power or
 * charge / current */
static int read_minutes(const char *dir, cp0_battery_state_t state, long long power_uw, long long current_ua)
{
    if (state != CP0_BATTERY_DISCHARGING && state != CP0_BATTERY_CHARGING) return -1;
    const int discharging = state == CP0_BATTERY_DISCHARGING;
    long long seconds = 0, now = 0, full = 0;
    if (read_number(dir, discharging ? "time_to_empty_now" : "time_to_full_now", &seconds) && seconds > 0)
        return minutes_of(seconds, 60);
    if (power_uw > 0 && read_number(dir, "energy_now", &now) && now >= 0) {
        if (discharging) return minutes_of(now * 60, power_uw);
        if (read_number(dir, "energy_full", &full) && full > now) return minutes_of((full - now) * 60, power_uw);
        return -1;
    }
    if (current_ua > 0 && read_number(dir, "charge_now", &now) && now >= 0) {
        if (discharging) return minutes_of(now * 60, current_ua);
        if (read_number(dir, "charge_full", &full) && full > now) return minutes_of((full - now) * 60, current_ua);
    }
    return -1;
}

static int read_health(const char *dir)
{
    long long full = 0, design = 0;
    if (read_number(dir, "energy_full", &full) && read_number(dir, "energy_full_design", &design) && full > 0 &&
        design > 0)
        return ratio_percent(full, design);
    if (read_number(dir, "charge_full", &full) && read_number(dir, "charge_full_design", &design) && full > 0 &&
        design > 0)
        return ratio_percent(full, design);
    return -1;
}

/* root/name when it is a battery that is there and gives its charge */
static int read_battery(const char *root, const char *name, cp0_power_battery_t *out)
{
    char dir[512];
    const int n = snprintf(dir, sizeof(dir), "%s/%s", root, name);
    if (n < 0 || (size_t)n >= sizeof(dir)) return 0;

    char text[64];
    if (!read_line(dir, "type", text, sizeof(text)) || strcmp(text, "Battery") != 0) return 0;
    if (read_line(dir, "scope", text, sizeof(text)) && strcmp(text, "Device") == 0) return 0; /* a peripheral's */
    long long value = 0;
    if (read_number(dir, "present", &value) && value == 0) return 0; /* an empty bay */

    cp0_power_battery_t battery;
    clear(&battery);
    if (!read_percent(dir, &battery.percent)) return 0;

    size_t len = strlen(name);
    if (len >= sizeof(battery.name)) len = sizeof(battery.name) - 1;
    memcpy(battery.name, name, len);
    battery.name[len] = '\0';

    if (read_line(dir, "status", text, sizeof(text))) battery.state = parse_state(text);

    long long voltage_uv = 0, power_uw = -1, current_ua = -1;
    const int has_voltage = read_number(dir, "voltage_now", &voltage_uv) && voltage_uv > 0;
    if (has_voltage) battery.voltage_mv = (int)((voltage_uv + 500) / 1000);
    if (read_number(dir, "current_now", &value)) current_ua = magnitude(value);
    if (read_number(dir, "power_now", &value)) power_uw = magnitude(value);
    else if (current_ua >= 0 && has_voltage) power_uw = current_ua * voltage_uv / 1000000; /* uA x uV = pW */
    if (power_uw >= 0) battery.power_mw = (int)((power_uw + 500) / 1000);

    battery.minutes = read_minutes(dir, battery.state, power_uw, current_ua);
    battery.health_percent = read_health(dir);
    *out = battery;
    return 1;
}

static int visible(const struct dirent *entry) { return entry->d_name[0] != '.'; }

/* byte order, not the locale's: the same device wins on every system */
static int by_name(const struct dirent **a, const struct dirent **b) { return strcmp((*a)->d_name, (*b)->d_name); }

int cp0_power_supply_battery_at(const char *root, cp0_power_battery_t *out)
{
    if (!out) return 0;
    clear(out);
    if (!root || !root[0]) {
        const char *env = getenv(CP0_POWER_SUPPLY_ROOT_ENV);
        root = env && env[0] ? env : CP0_POWER_SUPPLY_DEFAULT_ROOT;
    }
    struct dirent **entries = NULL;
    const int count = scandir(root, &entries, visible, by_name);
    if (count < 0) return 0;
    int found = 0;
    for (int i = 0; i < count; ++i) {
        if (!found) found = read_battery(root, entries[i]->d_name, out);
        free(entries[i]);
    }
    free(entries);
    return found;
}

int cp0_power_supply_battery(cp0_power_battery_t *out) { return cp0_power_supply_battery_at(NULL, out); }

const char *cp0_power_supply_state_name(cp0_battery_state_t state)
{
    switch (state) {
    case CP0_BATTERY_DISCHARGING: return "Discharging";
    case CP0_BATTERY_CHARGING: return "Charging";
    case CP0_BATTERY_FULL: return "Full";
    case CP0_BATTERY_NOT_CHARGING: return "Not charging";
    case CP0_BATTERY_UNKNOWN: break;
    }
    return "Unknown";
}
