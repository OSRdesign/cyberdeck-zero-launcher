/*
 * SPDX-License-Identifier: MIT
 *
 * Decision X12 (report 028): the power_supply battery reader (cp0_power_supply.h) on fake sysfs trees: no directory,
 * an empty one, an AC adapter only, a PC's ACPI battery discharging / charging / full (energy and charge flavours,
 * the driver's own time estimate), the device choice (name order, a peripheral's battery, an empty bay, a battery
 * without a charge) and $APPLAUNCH_POWER_SUPPLY_ROOT.
 */
#define _DEFAULT_SOURCE /* mkdtemp, setenv */
#include "cp0_power_supply.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_base[256];
static int g_tree;

/* a new empty tree under the test's temporary directory */
static const char *new_tree(char *root, size_t size)
{
    snprintf(root, size, "%s/tree%d", g_base, ++g_tree);
    assert(mkdir(root, 0755) == 0);
    return root;
}

/* root/device/file = text (the device's directory made on demand) */
static void put(const char *root, const char *device, const char *file, const char *text)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, device);
    if (mkdir(path, 0755) != 0) assert(errno == EEXIST);
    snprintf(path, sizeof(path), "%s/%s/%s", root, device, file);
    FILE *out = fopen(path, "w");
    assert(out);
    fputs(text, out);
    fclose(out);
}

static void put_mains(const char *root, const char *device, int online)
{
    put(root, device, "type", "Mains\n");
    put(root, device, "online", online ? "1\n" : "0\n");
}

/* an ACPI battery that reports energy (uWh, uW), like the FZ-M1's BAT1 */
static void put_acpi_energy(const char *root, const char *device, const char *status, int capacity,
                            const char *energy_now, const char *power_now)
{
    char text[32];
    put(root, device, "type", "Battery\n");
    put(root, device, "present", "1\n");
    put(root, device, "status", status);
    snprintf(text, sizeof(text), "%d\n", capacity);
    put(root, device, "capacity", text);
    put(root, device, "energy_full_design", "50000000\n");
    put(root, device, "energy_full", "40000000\n");
    put(root, device, "energy_now", energy_now);
    put(root, device, "power_now", power_now);
    put(root, device, "voltage_now", "7612000\n");
    put(root, device, "technology", "Li-ion\n");
}

static void test_absent(void)
{
    char root[300];
    cp0_power_battery_t b;
    memset(&b, 0x5a, sizeof(b));

    snprintf(root, sizeof(root), "%s/does-not-exist", g_base);
    assert(cp0_power_supply_battery_at(root, &b) == 0);
    assert(b.percent == -1 && b.state == CP0_BATTERY_UNKNOWN && b.name[0] == '\0');
    assert(b.power_mw == -1 && b.minutes == -1 && b.voltage_mv == -1 && b.health_percent == -1);

    assert(cp0_power_supply_battery_at(new_tree(root, sizeof(root)), &b) == 0); /* empty: a Pi board */
    assert(cp0_power_supply_battery_at(root, NULL) == 0);
}

static void test_mains_only(void)
{
    char root[300];
    cp0_power_battery_t b;
    new_tree(root, sizeof(root));
    put_mains(root, "ADP1", 1);
    put(root, "ucsi-source-psy-USBC000:001", "type", "USB\n");
    put(root, "ucsi-source-psy-USBC000:001", "online", "0\n");
    assert(cp0_power_supply_battery_at(root, &b) == 0);
    assert(b.percent == -1);
}

static void test_discharging(void)
{
    char root[300];
    cp0_power_battery_t b;
    new_tree(root, sizeof(root));
    put_mains(root, "ADP1", 0);
    put_acpi_energy(root, "BAT1", "Discharging\n", 85, "30000000\n", "6000000\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(strcmp(b.name, "BAT1") == 0);
    assert(b.percent == 85);
    assert(b.state == CP0_BATTERY_DISCHARGING);
    assert(b.power_mw == 6000);
    assert(b.minutes == 300); /* 30 Wh at 6 W */
    assert(b.voltage_mv == 7612);
    assert(b.health_percent == 80); /* 40 of 50 Wh */
    assert(strcmp(cp0_power_supply_state_name(b.state), "Discharging") == 0);
}

static void test_charging(void)
{
    char root[300];
    cp0_power_battery_t b;
    new_tree(root, sizeof(root));
    put_mains(root, "ADP1", 1);
    put_acpi_energy(root, "BAT1", "Charging\n", 50, "20000000\n", "10000000\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(b.percent == 50 && b.state == CP0_BATTERY_CHARGING);
    assert(b.power_mw == 10000);
    assert(b.minutes == 120); /* the 20 Wh missing at 10 W */
    assert(strcmp(cp0_power_supply_state_name(b.state), "Charging") == 0);
}

static void test_full(void)
{
    char root[300];
    cp0_power_battery_t b;
    new_tree(root, sizeof(root));
    put_mains(root, "AC", 1);
    /* a line end written on Windows, no power while full */
    put_acpi_energy(root, "BAT0", "Full\r\n", 100, "40000000\r\n", "0\r\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(strcmp(b.name, "BAT0") == 0);
    assert(b.percent == 100 && b.state == CP0_BATTERY_FULL);
    assert(b.power_mw == 0);
    assert(b.minutes == -1); /* no time while full */
    assert(strcmp(cp0_power_supply_state_name(b.state), "Full") == 0);

    put(root, "BAT0", "status", "Not charging\n"); /* on AC, held below full by a charge threshold */
    put(root, "BAT0", "capacity", "80\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(b.state == CP0_BATTERY_NOT_CHARGING && b.percent == 80 && b.minutes == -1);
    assert(strcmp(cp0_power_supply_state_name(b.state), "Not charging") == 0);

    put(root, "BAT0", "status", "Unknown\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(b.state == CP0_BATTERY_UNKNOWN && b.minutes == -1);
    assert(strcmp(cp0_power_supply_state_name(b.state), "Unknown") == 0);
}

/* an ACPI battery that reports charge (uAh, uA) and no capacity file: the charge from charge_now / charge_full, the
 * power from current x voltage */
static void test_charge_flavour(void)
{
    char root[300];
    cp0_power_battery_t b;
    new_tree(root, sizeof(root));
    put(root, "BAT0", "type", "Battery\n");
    put(root, "BAT0", "status", "Discharging\n");
    put(root, "BAT0", "charge_full_design", "4000000\n");
    put(root, "BAT0", "charge_full", "3000000\n");
    put(root, "BAT0", "charge_now", "1500000\n");
    put(root, "BAT0", "current_now", "-500000\n"); /* some drivers sign it */
    put(root, "BAT0", "voltage_now", "7400000\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(b.percent == 50);
    assert(b.power_mw == 3700); /* 0.5 A x 7.4 V */
    assert(b.minutes == 180);   /* 1.5 Ah at 0.5 A */
    assert(b.health_percent == 75);

    put(root, "BAT0", "status", "Charging\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(b.minutes == 180); /* the 1.5 Ah missing at 0.5 A */

    put(root, "BAT0", "current_now", "0\n"); /* idle: no estimate */
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(b.minutes == -1 && b.power_mw == 0);
}

/* a gauge driver (bq27xxx): capacity, status and its own time estimates, no energy files */
static void test_gauge_estimate(void)
{
    char root[300];
    cp0_power_battery_t b;
    new_tree(root, sizeof(root));
    put(root, "bq27220-0", "type", "Battery\n");
    put(root, "bq27220-0", "present", "1\n");
    put(root, "bq27220-0", "capacity", "64\n");
    put(root, "bq27220-0", "status", "Discharging\n");
    put(root, "bq27220-0", "time_to_empty_now", "5400\n");
    put(root, "bq27220-0", "time_to_full_now", "1200\n");
    put(root, "bq27220-0", "current_now", "-350000\n");
    put(root, "bq27220-0", "voltage_now", "3800000\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(b.percent == 64 && b.minutes == 90);
    assert(b.power_mw == 1330);
    assert(b.health_percent == -1);
    put(root, "bq27220-0", "status", "Charging\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(b.minutes == 20);
    put(root, "bq27220-0", "capacity", "250\n"); /* out of range: clamped */
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(b.percent == 100);
}

/* which device: name order; a peripheral's battery (scope Device), an empty bay and a battery without a charge are
 * skipped; a garbled number is no number */
static void test_choice(void)
{
    char root[300];
    cp0_power_battery_t b;
    new_tree(root, sizeof(root));
    put_mains(root, "ADP1", 1);
    put(root, "BAT0", "type", "Battery\n"); /* empty bay */
    put(root, "BAT0", "present", "0\n");
    put(root, "BAT0", "capacity", "0\n");
    put(root, "BAT1", "type", "Battery\n"); /* no charge to read */
    put(root, "BAT1", "capacity", "abc\n");
    put(root, "BAT2", "type", "Battery\n");
    put(root, "BAT2", "capacity", "33\n");
    put(root, "BAT2", "status", "Discharging\n");
    put(root, "BAT3", "type", "Battery\n");
    put(root, "BAT3", "capacity", "99\n");
    put(root, "1-mouse-battery", "type", "Battery\n"); /* sorts first, a peripheral */
    put(root, "1-mouse-battery", "scope", "Device\n");
    put(root, "1-mouse-battery", "capacity", "10\n");
    put(root, "hid-keyboard-battery", "type", "Battery\n");
    put(root, "hid-keyboard-battery", "scope", "Device\n");
    put(root, "hid-keyboard-battery", "capacity", "20\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(strcmp(b.name, "BAT2") == 0 && b.percent == 33);
    assert(b.power_mw == -1 && b.minutes == -1 && b.voltage_mv == -1 && b.health_percent == -1);

    /* scope System is a system battery */
    put(root, "1-mouse-battery", "scope", "System\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(strcmp(b.name, "1-mouse-battery") == 0 && b.percent == 10);
}

static void test_long_name(void)
{
    char root[300];
    cp0_power_battery_t b;
    new_tree(root, sizeof(root));
    const char *name = "a-very-long-power-supply-device-name-0123456789";
    put(root, name, "type", "Battery\n");
    put(root, name, "capacity", "7\n");
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    assert(strlen(b.name) == sizeof(b.name) - 1 && strncmp(b.name, name, sizeof(b.name) - 1) == 0);
}

static void test_environment(void)
{
    char root[300];
    cp0_power_battery_t b;
    new_tree(root, sizeof(root));
    put_acpi_energy(root, "BAT1", "Discharging\n", 42, "16800000\n", "5600000\n");
    assert(setenv(CP0_POWER_SUPPLY_ROOT_ENV, root, 1) == 0);
    assert(cp0_power_supply_battery(&b) == 1 && b.percent == 42 && b.minutes == 180);
    assert(cp0_power_supply_battery_at("", &b) == 1 && b.percent == 42);
    assert(cp0_power_supply_battery_at(NULL, &b) == 1 && b.percent == 42);

    char empty[300];
    assert(setenv(CP0_POWER_SUPPLY_ROOT_ENV, new_tree(empty, sizeof(empty)), 1) == 0);
    assert(cp0_power_supply_battery(&b) == 0);
    /* an explicit root wins over the variable */
    assert(cp0_power_supply_battery_at(root, &b) == 1);
    unsetenv(CP0_POWER_SUPPLY_ROOT_ENV);
}

/* ---- the MAX17048 gauge (report 030, C5): registers in a regular file, the byte offset is the register ---- */

static void put_gauge(const char *path, unsigned version, unsigned vcell, unsigned soc, int crate)
{
    unsigned char regs[0x20] = {0};
    regs[0x02] = (unsigned char)(vcell >> 8), regs[0x03] = (unsigned char)vcell;
    regs[0x04] = (unsigned char)(soc >> 8), regs[0x05] = (unsigned char)soc;
    regs[0x08] = (unsigned char)(version >> 8), regs[0x09] = (unsigned char)version;
    regs[0x16] = (unsigned char)((unsigned)crate >> 8), regs[0x17] = (unsigned char)crate;
    FILE *out = fopen(path, "wb");
    assert(out);
    assert(fwrite(regs, 1, sizeof(regs), out) == sizeof(regs));
    fclose(out);
}

static void test_gauge(void)
{
    char empty[300], file[400], root[300];
    cp0_power_battery_t b;
    new_tree(empty, sizeof(empty));
    snprintf(file, sizeof(file), "%s/gauge.bin", g_base);
    assert(setenv(CP0_POWER_SUPPLY_ROOT_ENV, empty, 1) == 0);
    assert(setenv(CP0_GAUGE_FILE_ENV, file, 1) == 0);
    assert(setenv(CP0_GAUGE_ENV, "max17048:/dev/i2c-15:0x36", 1) == 0);

    /* the probed unit: 4.188 V (53606 LSB), 100.3 %, version 0x0012, rate 0: full */
    put_gauge(file, 0x0012, 53606, 0x644D, 0);
    assert(cp0_power_supply_battery(&b) == 1);
    assert(strcmp(b.name, "max17048") == 0 && b.percent == 100 && b.voltage_mv == 4188);
    assert(b.state == CP0_BATTERY_FULL && b.power_mw == -1 && b.minutes == -1 && b.health_percent == -1);

    /* 55.5 %, rate 0: state unknown; charging beyond +0.5 %/h (3 LSB = 0.624), discharging beyond -0.5 %/h */
    put_gauge(file, 0x0012, 48000, 0x3780, 0);
    assert(cp0_power_supply_battery(&b) == 1 && b.percent == 55 && b.voltage_mv == 3750);
    assert(b.state == CP0_BATTERY_UNKNOWN);
    put_gauge(file, 0x0012, 48000, 0x3780, 2); /* 0.416 %/h: below the threshold */
    assert(cp0_power_supply_battery(&b) == 1 && b.state == CP0_BATTERY_UNKNOWN);
    put_gauge(file, 0x0012, 48000, 0x3780, 3);
    assert(cp0_power_supply_battery(&b) == 1 && b.state == CP0_BATTERY_CHARGING);
    put_gauge(file, 0x0012, 48000, 0x3780, -3);
    assert(cp0_power_supply_battery(&b) == 1 && b.state == CP0_BATTERY_DISCHARGING);
    put_gauge(file, 0x0012, 48000, 0x3780, -2);
    assert(cp0_power_supply_battery(&b) == 1 && b.state == CP0_BATTERY_UNKNOWN);
    put_gauge(file, 0x0012, 53000, 0x6300, -10); /* 99 % and discharging: not full */
    assert(cp0_power_supply_battery(&b) == 1 && b.state == CP0_BATTERY_DISCHARGING && b.percent == 99);

    /* the percent is clamped to 0..100 */
    put_gauge(file, 0x0012, 48000, 0xFF00, 0);
    assert(cp0_power_supply_battery(&b) == 1 && b.percent == 100);
    put_gauge(file, 0x0012, 40000, 0x0000, 0);
    assert(cp0_power_supply_battery(&b) == 1 && b.percent == 0);

    /* a bad VERSION (not a MAX1704x), a zero or all-ones VCELL: no battery, and the output is cleared */
    put_gauge(file, 0x0000, 48000, 0x3780, 0);
    assert(cp0_power_supply_battery(&b) == 0 && b.percent == -1 && b.name[0] == '\0');
    put_gauge(file, 0xFFFF, 48000, 0x3780, 0);
    assert(cp0_power_supply_battery(&b) == 0);
    put_gauge(file, 0x0112, 48000, 0x3780, 0);
    assert(cp0_power_supply_battery(&b) == 0);
    put_gauge(file, 0x0012, 0, 0x3780, 0);
    assert(cp0_power_supply_battery(&b) == 0);

    /* a missing or short register file, and an explicit root, never use the gauge */
    put_gauge(file, 0x0012, 48000, 0x3780, 0);
    assert(cp0_power_supply_battery_at(empty, &b) == 0);
    assert(truncate(file, 6) == 0);
    assert(cp0_power_supply_battery(&b) == 0);
    unlink(file);
    assert(cp0_power_supply_battery(&b) == 0);

    /* a malformed APPLAUNCH_GAUGE is ignored */
    static const char *const bad[] = {"", "max17048", "max17048:", "max17048::0x36", "max17048:/dev/i2c-15",
                                      "max17048:/dev/i2c-15:", "max17048:/dev/i2c-15:zz", "max17048:/dev/i2c-15:0x80",
                                      "max17048:/dev/i2c-15:0x01", "max17048:i2c-15:0x36", "bq27xxx:/dev/i2c-15:0x36",
                                      "max17048:auto:0x36" /* no touch bus in the default sysfs tree on a test host */};
    put_gauge(file, 0x0012, 48000, 0x3780, 0);
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(setenv(CP0_GAUGE_ENV, bad[i], 1) == 0);
        assert(setenv(CP0_I2C_SYSFS_ROOT_ENV, empty, 1) == 0);
        assert(cp0_power_supply_battery(&b) == 0);
    }
    unsetenv(CP0_I2C_SYSFS_ROOT_ENV);

    /* the sysfs battery wins over the gauge */
    new_tree(root, sizeof(root));
    put_acpi_energy(root, "BAT1", "Discharging\n", 42, "16800000\n", "5600000\n");
    assert(setenv(CP0_POWER_SUPPLY_ROOT_ENV, root, 1) == 0);
    assert(setenv(CP0_GAUGE_ENV, "max17048:/dev/i2c-15:0x36", 1) == 0);
    assert(cp0_power_supply_battery(&b) == 1 && strcmp(b.name, "BAT1") == 0 && b.percent == 42);

    /* no power_supply directory at all: the gauge still answers */
    snprintf(root, sizeof(root), "%s/does-not-exist", g_base);
    assert(setenv(CP0_POWER_SUPPLY_ROOT_ENV, root, 1) == 0);
    assert(cp0_power_supply_battery(&b) == 1 && strcmp(b.name, "max17048") == 0);

    /* "auto": the bus of the touch controller (a <bus>-0048 entry), the lowest when several */
    char sys[300];
    new_tree(sys, sizeof(sys));
    put(sys, "15-0036", "name", "x\n");
    assert(setenv(CP0_I2C_SYSFS_ROOT_ENV, sys, 1) == 0);
    assert(setenv(CP0_GAUGE_ENV, "max17048:auto:0x36", 1) == 0);
    assert(cp0_power_supply_battery(&b) == 0); /* no 0048 entry */
    put(sys, "15-0048", "name", "edt-ft5406\n");
    assert(cp0_power_supply_battery(&b) == 1 && b.percent == 55);

    unsetenv(CP0_GAUGE_ENV);
    unsetenv(CP0_GAUGE_FILE_ENV);
    unsetenv(CP0_I2C_SYSFS_ROOT_ENV);
    unsetenv(CP0_POWER_SUPPLY_ROOT_ENV);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(g_base, sizeof(g_base), "%s/cp0-power-supply-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    assert(mkdtemp(g_base) != NULL);

    test_absent();
    test_mains_only();
    test_discharging();
    test_charging();
    test_full();
    test_charge_flavour();
    test_gauge_estimate();
    test_choice();
    test_long_name();
    test_environment();
    test_gauge();

    char command[400];
    snprintf(command, sizeof(command), "rm -rf '%s'", g_base);
    if (system(command) != 0) fprintf(stderr, "could not remove %s\n", g_base);
    printf("power supply reader OK\n");
    return 0;
}
