/*
 * Status feed published by the launcher for apps: $XDG_RUNTIME_DIR/applaunch/status.state
 * (/tmp/applaunch-status.state when XDG_RUNTIME_DIR is unset), report 032. Written by atomic rename, one key per line:
 *
 *   v=1
 *   gen=G              increases on every write
 *   pid=P              the launcher process (a reader trusts the file only while it is alive)
 *   wifi=0|1           connected
 *   wifi_pct=0..100    signal
 *   eth=0|1            a wired link is connected
 *   bt=off|on|connected  the adapter's BlueZ Powered state and whether a device is connected, as the launcher shows them
 *   battery=-1|0..100  -1 = no battery
 *   battery_state=Charging|Discharging|Full|Not charging|Unknown
 *
 * Readers must ignore unknown keys. Apps read it with cp0_app_status_read() (cp0_app_contract.h).
 *
 * Device builds only (src/cp0); no LVGL dependency.
 */
#ifndef CP0_STATUS_STATE_H
#define CP0_STATUS_STATE_H

#include "cp0_app_contract.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_STATUS_STATE_FALLBACK_PATH "/tmp/applaunch-status.state"

/* Path of the file: <runtime_dir>/applaunch/status.state, or the /tmp fallback when runtime_dir is NULL or empty. Pass
 * getenv("XDG_RUNTIME_DIR") (the same place as screen.state). Returns 0, or -1 when `buf` is too small. */
int cp0_status_state_path(const char *runtime_dir, char *buf, size_t size);

/* The file text. Returns its length, or -1 if it does not fit. */
int cp0_status_state_format(const cp0_app_status_t *state, char *buf, size_t size);

/* Writes `path` atomically (cp0_ui_state_write: parent directory, temporary file, rename, mode 0644). 0 or -1 (errno). */
int cp0_status_state_write(const char *path, const cp0_app_status_t *state);

#ifdef __cplusplus
}
#endif

#endif /* CP0_STATUS_STATE_H */
