/*
 * SPDX-License-Identifier: MIT
 *
 * Task L1: the shared status strip's pure logic (cp0_ui_status_strip.h): its place from the shell tokens at the
 * pinned deck / Pi 3A+ sizes and a computed one, the Wi-Fi signal rule and the leftmost drawn column.
 */
#include "cp0_ui_status_strip.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_geom(void)
{
    cp0_ui_metrics_t m;
    cp0_ui_status_strip_geom_t g;

    /* deck 640x480: today's numbers (320 px canvas at the right, 56 high, pill at 8, 100 %) */
    assert(cp0_ui_metrics_compute(640, 480, 0, 0, CP0_UI_DENSITY_DEFAULT, 0, &m) == 0);
    cp0_ui_status_strip_geom(&m, 0, &g);
    assert(g.w == 320 && g.h == 56 && g.x == 320 && g.y == 0);
    assert(g.rows == 56 && g.top == 8 && g.pct == 100);
    /* the display's own width wins over the metrics' */
    cp0_ui_status_strip_geom(&m, 800, &g);
    assert(g.x == 480);

    /* Pi 3A+ 480x320: the compact strip (72 %, 40 high, buffer still CP0_STATUSBAR_HEIGHT rows) */
    assert(cp0_ui_metrics_compute(480, 320, 0, 0, CP0_UI_DENSITY_DEFAULT, 0, &m) == 0);
    cp0_ui_status_strip_geom(&m, 0, &g);
    assert(g.pct == 72 && g.h == 40 && g.top == 5);
    assert(g.w == 320 * 72 / 100 && g.x == 480 - g.w);
    assert(g.rows == CP0_STATUSBAR_HEIGHT);

    /* a computed size follows the shell tokens */
    assert(cp0_ui_metrics_compute(1280, 720, 0, 0, CP0_UI_DENSITY_DEFAULT, 0, &m) == 0);
    cp0_ui_status_strip_geom(&m, 0, &g);
    assert(g.w == m.shell.status_w && g.h == m.shell.bar_h && g.x == 1280 - m.shell.status_w);
    assert(g.rows == (m.shell.bar_h > CP0_STATUSBAR_HEIGHT ? m.shell.bar_h : CP0_STATUSBAR_HEIGHT));
}

static void test_signal(void)
{
    assert(cp0_ui_status_signal_percent(0) == 0);
    assert(cp0_ui_status_signal_percent(1) == 1);
    assert(cp0_ui_status_signal_percent(73) == 73);
    assert(cp0_ui_status_signal_percent(140) == 100);
    assert(cp0_ui_status_signal_percent(-50) == 100);
    assert(cp0_ui_status_signal_percent(-40) == 100);
    assert(cp0_ui_status_signal_percent(-70) == 60);
    assert(cp0_ui_status_signal_percent(-100) == 0);
    assert(cp0_ui_status_signal_percent(-120) == 0);

    cp0_statusbar_state_t s;
    memset(&s, 0, sizeof(s));
    cp0_ui_status_set_wifi(&s, 1, -70);
    assert(s.wifi_up == 1 && s.wifi_pct == 60);
    cp0_ui_status_set_wifi(&s, 1, -110); /* connected but no usable level: still one bar */
    assert(s.wifi_up == 1 && s.wifi_pct == 1);
    cp0_ui_status_set_wifi(&s, 1, 0);
    assert(s.wifi_up == 1 && s.wifi_pct == 1);
    cp0_ui_status_set_wifi(&s, 0, 90); /* not connected: no bars whatever the level */
    assert(s.wifi_up == 0 && s.wifi_pct == 0);
    cp0_ui_status_set_wifi(NULL, 1, 50); /* no crash */
}

static void test_first_column(void)
{
    uint32_t px[4 * 3];
    memset(px, 0, sizeof(px));
    assert(cp0_ui_status_first_column(px, 4, 3) == 4); /* empty */
    px[2 * 4 + 3] = 0x01000000u;                         /* row 2, column 3 */
    assert(cp0_ui_status_first_column(px, 4, 3) == 3);
    px[0 * 4 + 2] = 0xFF000000u;                         /* row 0, column 2 */
    px[1 * 4 + 1] = 0x00FFFFFFu;                         /* colour without alpha: not drawn */
    assert(cp0_ui_status_first_column(px, 4, 3) == 2);
    px[2 * 4 + 0] = 0x80FFFFFFu;
    assert(cp0_ui_status_first_column(px, 4, 3) == 0);
    assert(cp0_ui_status_first_column(NULL, 4, 3) == 4);
    assert(cp0_ui_status_first_column(px, 0, 3) == 0);
}

/* Decision X12: a battery shows its charge, green while charging; none (or no charge) shows nothing, as before */
static void test_battery(void)
{
    cp0_statusbar_state_t s;
    memset(&s, 0, sizeof(s));
    assert(s.battery == 0); /* a zeroed state (an app's, the Pi boards') has no battery */

    cp0_power_battery_t b;
    memset(&b, 0, sizeof(b));
    b.percent = 85;
    b.state = CP0_BATTERY_DISCHARGING;
    cp0_ui_status_set_battery(&s, &b);
    assert(s.battery == 1 && s.battery_pct == 85 && s.battery_charging == 0);
    b.percent = 42;
    b.state = CP0_BATTERY_CHARGING;
    cp0_ui_status_set_battery(&s, &b);
    assert(s.battery == 1 && s.battery_pct == 42 && s.battery_charging == 1);
    b.percent = 100;
    b.state = CP0_BATTERY_FULL; /* on AC but not charging: no charging colour */
    cp0_ui_status_set_battery(&s, &b);
    assert(s.battery == 1 && s.battery_pct == 100 && s.battery_charging == 0);
    b.state = CP0_BATTERY_NOT_CHARGING;
    cp0_ui_status_set_battery(&s, &b);
    assert(s.battery_charging == 0);
    b.percent = -1; /* cp0_power_supply_battery() found none */
    cp0_ui_status_set_battery(&s, &b);
    assert(s.battery == 0 && s.battery_pct == 0 && s.battery_charging == 0);
    b.percent = 64;
    cp0_ui_status_set_battery(&s, &b);
    cp0_ui_status_set_battery(&s, NULL);
    assert(s.battery == 0 && s.battery_pct == 0 && s.battery_charging == 0);
    /* the other fields stay */
    s.wifi_up = 1;
    s.bt_on = 1;
    cp0_ui_status_set_battery(&s, &b);
    assert(s.wifi_up == 1 && s.bt_on == 1 && s.battery == 1);
    cp0_ui_status_set_battery(NULL, &b); /* no crash */
}

/* The SSH indicator (2026-10-30): the source's rule and the 1 s blink tick */
static void test_ssh(void)
{
    cp0_statusbar_state_t s;
    memset(&s, 0, sizeof(s));
    cp0_ui_status_set_ssh(&s, 0, 1); /* no session: nothing, activity ignored */
    assert(s.ssh == 0 && s.ssh_active == 0 && s.ssh_bright == 0);
    cp0_ui_status_set_ssh(&s, -2, 1);
    assert(s.ssh == 0 && s.ssh_active == 0);
    cp0_ui_status_set_ssh(&s, 2, 0); /* idle session: steady blue */
    assert(s.ssh == 2 && s.ssh_active == 0 && s.ssh_bright == 0);
    assert(cp0_ui_status_ssh_tick(&s, 1) == 0 && s.ssh_bright == 0);

    /* traffic: the phase flips on each tick while the screen is active, holds while it is not */
    cp0_ui_status_set_ssh(&s, 1, 1);
    assert(s.ssh == 1 && s.ssh_active == 1 && s.ssh_bright == 0);
    assert(cp0_ui_status_ssh_tick(&s, 1) == 1 && s.ssh_bright == 1);
    assert(cp0_ui_status_ssh_tick(&s, 1) == 1 && s.ssh_bright == 0);
    assert(cp0_ui_status_ssh_tick(&s, 1) == 1 && s.ssh_bright == 1);
    assert(cp0_ui_status_ssh_tick(&s, 0) == 0 && s.ssh_bright == 1);
    /* the source still says active: the phase is kept (no jump back to blue between ticks) */
    cp0_ui_status_set_ssh(&s, 1, 1);
    assert(s.ssh_bright == 1);
    /* traffic stops (or the lock screen: the launcher's source says idle): blue at once, and the tick keeps it */
    cp0_ui_status_set_ssh(&s, 1, 0);
    assert(s.ssh_bright == 0);
    assert(cp0_ui_status_ssh_tick(&s, 1) == 0 && s.ssh_bright == 0);
    /* the session ends while bright: nothing drawn, phase reset */
    cp0_ui_status_set_ssh(&s, 1, 1);
    assert(cp0_ui_status_ssh_tick(&s, 1) == 1 && s.ssh_bright == 1);
    cp0_ui_status_set_ssh(&s, 0, 0);
    assert(s.ssh == 0 && s.ssh_active == 0 && s.ssh_bright == 0);
    /* a stale phase without a session is cleared by the tick */
    s.ssh_bright = 1;
    assert(cp0_ui_status_ssh_tick(&s, 1) == 1 && s.ssh_bright == 0);
    cp0_ui_status_set_ssh(NULL, 1, 1);
    assert(cp0_ui_status_ssh_tick(NULL, 1) == 0);
}

int main(void)
{
    test_geom();
    test_signal();
    test_first_column();
    test_battery();
    test_ssh();
    printf("test_ui_status_strip: ok\n");
    return 0;
}
