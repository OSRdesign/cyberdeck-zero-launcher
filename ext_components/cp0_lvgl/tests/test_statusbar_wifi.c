/*
 * The sysfs fallback of the status strip (cp0_statusbar_read_state) finds the Wi-Fi interface under any name (report 032):
 * fake trees under $APPLAUNCH_STATUS_SYSFS_ROOT with a wlan0 and a wlp2s0 interface, an Ethernet-only machine, an
 * interface that is down, a wireless interface that /proc/net/wireless does not list, and the Bluetooth part of the state.
 */
#define _GNU_SOURCE

#include "cp0_statusbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static char g_root[64];

/* text NULL: a directory */
static void put(const char *rel, const char *text)
{
    char path[512];
    snprintf(path, sizeof(path), "%s%s", g_root, rel);
    for (char *p = path + strlen(g_root) + 1; *p; ++p)
        if (*p == '/') {
            *p = '\0';
            mkdir(path, 0755);
            *p = '/';
        }
    if (!text) {
        mkdir(path, 0755);
        return;
    }
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs(text, f);
    fclose(f);
}

static void reset(void)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'/*", g_root);
    CHECK(system(cmd) == 0);
}

static void wireless_file(const char *iface, const char *rest)
{
    char text[400];
    snprintf(text, sizeof(text),
             "Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE\n"
             " face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22\n"
             "%6s:%s\n",
             iface, rest);
    put("/proc/net/wireless", text);
}

#define DBM " 0000   58.  -52.  -256        0      0      0      0      0        0"
#define PERCENT " 0000   70.   58.  -256        0      0      0      0      0        0"

static void tree(const char *iface, const char *operstate)
{
    char rel[160];
    snprintf(rel, sizeof(rel), "/sys/class/net/%s/operstate", iface);
    put(rel, operstate);
    snprintf(rel, sizeof(rel), "/sys/class/net/%s/wireless", iface);
    put(rel, NULL);
}

static cp0_statusbar_state_t read_state(void)
{
    cp0_statusbar_state_t s;
    cp0_statusbar_read_state(&s);
    return s;
}

int main(void)
{
    snprintf(g_root, sizeof(g_root), "/tmp/cp0-statusbar-XXXXXX");
    CHECK(mkdtemp(g_root) != NULL);
    setenv("APPLAUNCH_STATUS_SYSFS_ROOT", g_root, 1);

    /* the Pi: wlan0, up, level -52 dBm -> 96 % */
    tree("wlan0", "up\n");
    wireless_file("wlan0", DBM);
    put("/sys/class/net/eth0/operstate", "down\n");
    cp0_statusbar_state_t s = read_state();
    CHECK(s.wifi_up == 1 && s.wifi_pct == 96);
    CHECK(s.battery == 0); /* the battery is not this reader's */

    /* the FZ-M1 tablet: wlp2s0, the same numbers */
    reset();
    tree("wlp2s0", "up\n");
    wireless_file("wlp2s0", DBM);
    put("/sys/class/net/enp1s0/operstate", "up\n");
    s = read_state();
    CHECK(s.wifi_up == 1 && s.wifi_pct == 96);

    /* a USB dongle's name */
    reset();
    tree("wlx00e04c680001", "up\n");
    wireless_file("wlx00e04c680001", DBM);
    s = read_state();
    CHECK(s.wifi_up == 1 && s.wifi_pct == 96);

    /* percent already (no dBm) */
    reset();
    tree("wlp2s0", "up\n");
    wireless_file("wlp2s0", PERCENT);
    s = read_state();
    CHECK(s.wifi_up == 1 && s.wifi_pct == 58);

    /* the interface is down: no Wi-Fi, whatever /proc says */
    reset();
    tree("wlp2s0", "down\n");
    wireless_file("wlp2s0", DBM);
    s = read_state();
    CHECK(s.wifi_up == 0 && s.wifi_pct == 0);

    /* Ethernet only: no wireless interface at all */
    reset();
    put("/sys/class/net/enp1s0/operstate", "up\n");
    put("/sys/class/net/lo/operstate", "unknown\n");
    s = read_state();
    CHECK(s.wifi_up == 0 && s.wifi_pct == 0);

    /* a first, down wireless interface does not hide the up one (the file lists both) */
    reset();
    tree("wlan0", "down\n");
    tree("wlp3s0", "up\n");
    {
        char both[600];
        snprintf(both, sizeof(both),
                 "Inter-| sta-|   Quality\n face | tus | link level noise\n wlan0:" DBM "\n wlp3s0:" PERCENT "\n");
        put("/proc/net/wireless", both);
    }
    s = read_state();
    CHECK(s.wifi_up == 1 && s.wifi_pct == 58);

    /* up, a wireless directory, but no /proc/net/wireless line: connected, signal unknown (the strip shows one bar) */
    reset();
    tree("wlp2s0", "up\n");
    s = read_state();
    CHECK(s.wifi_up == 1 && s.wifi_pct == 0);

    /* Bluetooth: adapter present, a connected device, rfkill soft block */
    reset();
    put("/sys/class/bluetooth/hci0", NULL);
    s = read_state();
    CHECK(s.bt_on == 1 && s.bt_connected == 0);
    put("/sys/class/bluetooth/hci0:11", NULL);
    s = read_state();
    CHECK(s.bt_on == 1 && s.bt_connected == 1);
    put("/sys/class/rfkill/rfkill0/type", "bluetooth\n");
    put("/sys/class/rfkill/rfkill0/soft", "1\n");
    s = read_state();
    CHECK(s.bt_on == 0);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
    CHECK(system(cmd) == 0);
    printf("test_statusbar_wifi: PASS\n");
    return 0;
}
