/*
 * SPDX-License-Identifier: MIT
 *
 * Pure logic of the shared status strip (see cp0_ui_status_strip.h).
 */
#include "cp0_ui_status_strip.h"

#include <stddef.h>

void cp0_ui_status_strip_geom(const cp0_ui_metrics_t *m, int display_w, cp0_ui_status_strip_geom_t *out)
{
    if (!m || !out) return;
    const cp0_ui_shell_t *s = &m->shell;
    out->w = s->status_w;
    out->h = s->bar_h;
    out->x = (display_w > 0 ? display_w : m->w) - s->status_w;
    out->y = 0;
    out->rows = s->bar_h > CP0_STATUSBAR_HEIGHT ? s->bar_h : CP0_STATUSBAR_HEIGHT;
    out->top = s->status_top;
    out->pct = s->status_pct;
}

int cp0_ui_status_signal_percent(int signal)
{
    if (signal > 0) return signal < 100 ? signal : 100;
    if (signal == 0) return 0;
    const int pct = 2 * (signal + 100); /* -100 dBm -> 0, -50 dBm -> 100 */
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

void cp0_ui_status_set_wifi(cp0_statusbar_state_t *state, int connected, int signal)
{
    if (!state) return;
    int pct = 0;
    if (connected) {
        pct = cp0_ui_status_signal_percent(signal);
        if (pct < 1) pct = 1;
    }
    state->wifi_up = pct > 0;
    state->wifi_pct = pct;
}

void cp0_ui_status_set_battery(cp0_statusbar_state_t *state, const cp0_power_battery_t *battery)
{
    if (!state) return;
    if (!battery || battery->percent < 0) {
        state->battery = 0;
        state->battery_pct = 0;
        state->battery_charging = 0;
        return;
    }
    state->battery = 1;
    state->battery_pct = battery->percent > 100 ? 100 : battery->percent;
    state->battery_charging = battery->state == CP0_BATTERY_CHARGING;
}

void cp0_ui_status_set_ssh(cp0_statusbar_state_t *state, int connections, int active)
{
    if (!state) return;
    state->ssh = connections > 0 ? connections : 0;
    state->ssh_active = state->ssh > 0 && active ? 1 : 0;
    if (!state->ssh_active) state->ssh_bright = 0;
}

int cp0_ui_status_ssh_tick(cp0_statusbar_state_t *state, int screen_active)
{
    if (!state) return 0;
    const int before = state->ssh_bright;
    if (state->ssh <= 0 || !state->ssh_active) state->ssh_bright = 0;
    else if (screen_active) state->ssh_bright = !state->ssh_bright;
    return state->ssh_bright != before;
}

int cp0_ui_status_first_column(const uint32_t *argb, int w, int rows)
{
    if (!argb || w <= 0) return w > 0 ? w : 0;
    int first = w;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < first; ++x)
            if (argb[(size_t)y * w + x] >> 24) {
                first = x;
                break;
            }
    return first;
}
