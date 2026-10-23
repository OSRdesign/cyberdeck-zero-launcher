/*
 * Status feed writer (see cp0_status_state.h).
 */

#define _GNU_SOURCE
#include "cp0_status_state.h"

#include "cp0_ui_metrics.h"

#include <errno.h>
#include <stdio.h>

int cp0_status_state_path(const char *runtime_dir, char *buf, size_t size)
{
    if (!buf || size == 0) return -1;
    int n;
    if (runtime_dir && runtime_dir[0])
        n = snprintf(buf, size, "%s/" CP0_APP_STATE_SUBDIR "/" CP0_APP_STATUS_FILE, runtime_dir);
    else
        n = snprintf(buf, size, "%s", CP0_STATUS_STATE_FALLBACK_PATH);
    if (n < 0 || (size_t)n >= size) {
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

int cp0_status_state_format(const cp0_app_status_t *state, char *buf, size_t size)
{
    if (!state || !buf) return -1;
    const char *bt = state->bt == CP0_APP_BT_CONNECTED ? "connected" : state->bt == CP0_APP_BT_ON ? "on" : "off";
    const int n = snprintf(buf, size,
                           "v=1\ngen=%u\npid=%d\nwifi=%d\nwifi_pct=%d\neth=%d\nbt=%s\nbattery=%d\nbattery_state=%s\n",
                           state->gen, state->pid, state->wifi ? 1 : 0, state->wifi_pct, state->eth ? 1 : 0, bt,
                           state->battery, state->battery_state[0] ? state->battery_state : "Unknown");
    if (n < 0 || (size_t)n >= size) return -1;
    return n;
}

int cp0_status_state_write(const char *path, const cp0_app_status_t *state)
{
    if (!path || !state) {
        errno = EINVAL;
        return -1;
    }
    char text[256];
    const int len = cp0_status_state_format(state, text, sizeof(text));
    if (len < 0) {
        errno = EOVERFLOW;
        return -1;
    }
    return cp0_ui_state_write(path, text, (size_t)len);
}
