/*
 * Board facts published by the launcher for apps: $XDG_RUNTIME_DIR/applaunch/board.state
 * (/tmp/applaunch-board.state when XDG_RUNTIME_DIR is unset), report 035. Written by atomic rename, one key per line;
 * a fact the launcher does not know is an absent key, never 0:
 *
 *   v=1
 *   gen=G                   increases on every write
 *   pid=P                   the launcher process (a reader trusts the file only while it is alive)
 *   launcher=0.8.6          launcher version
 *   board=hackberry-cm5     APPLAUNCH_BOARD (absent: no profile, the deck, or unknown)
 *   arch=aarch64            uname -m
 *   ram_mb=1800             MemTotal in MB
 *   battery=0|1|2           none, present, not set up (a profile hint says there should be one)
 *   battery_source=power_supply:<name> | gauge:max17048
 *   backlight=none|onoff|levels
 *   backlight_steps=10      the steps the launcher offers (10, 2 for on / off, 0)
 *   sound=0|1|2             none, present, not set up
 *   sound_kind=alsa|server|bluetooth
 *   sound_guess=1           sound_kind comes from a guess (a sink named bluez_output.*), not a measurement
 *   requests=hello,beep,toast,volume,brightness,dim   the verbs launcher.sock answers on this board
 *   battery_note= backlight_note= sound_note=   the board profile's hint text, for display only
 *
 * Readers must ignore unknown keys. Apps read it with cp0_app_board_read() (cp0_app_contract.h).
 *
 * Device builds only (src/cp0); no LVGL dependency.
 */
#ifndef CP0_BOARD_STATE_H
#define CP0_BOARD_STATE_H

#include "cp0_app_contract.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_BOARD_STATE_FALLBACK_PATH "/tmp/applaunch-board.state"

/* Path of the file: <runtime_dir>/applaunch/board.state, or the /tmp fallback when runtime_dir is NULL or empty.
 * Returns 0, or -1 when `buf` is too small. */
int cp0_board_state_path(const char *runtime_dir, char *buf, size_t size);

/* The file text from a board (the absent values of cp0_app_board_t are left out; control characters in strings become
 * spaces). Returns its length, or -1 if it does not fit. */
int cp0_board_state_format(const cp0_app_board_t *board, char *buf, size_t size);

/* True when the two boards would write different files (gen and pid are not compared). */
int cp0_board_state_differs(const cp0_app_board_t *a, const cp0_app_board_t *b);

/* Writes `path` atomically (cp0_ui_state_write). 0 or -1 (errno). */
int cp0_board_state_write(const char *path, const cp0_app_board_t *board);

#ifdef __cplusplus
}
#endif

#endif /* CP0_BOARD_STATE_H */
