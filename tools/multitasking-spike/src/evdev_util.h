/*
 * SPDX-License-Identifier: MIT
 *
 * evdev helpers for the P-a spike tools: event names, the touch screen chosen by capability (the same scoring as the
 * launcher's cp0_lvgl_dpi_scaled.c touch_score), keyboards, device summaries. Read-only: nothing here grabs.
 */
#pragma once

#include <stddef.h>

/* "EV_ABS:ABS_MT_SLOT", "EV_KEY:KEY_ENTER", "EV_ABS:0x2a"... */
void ev_name(int type, int code, char *out, size_t n);

/* -1 = not a touch screen, else higher is better (direct 4 + multitouch 2 + BTN_TOUCH 1) */
int ev_touch_score(int fd, char *name, size_t name_n);

/* the best /dev/input/eventN touch screen (lowest N on a tie). 0 on success. */
int ev_find_touch(char *path, size_t n, char *name, size_t name_n);

/* 1 when the device reports KEY_ENTER or KEY_A (a keyboard of some kind) */
int ev_is_keyboard(int fd);

/* every /dev/input/eventN that is a keyboard (or, with also_touch, the touch screen too); returns the count */
int ev_list_inputs(char paths[][64], int max, int also_touch);

/* prints every /dev/input/eventN with its name, touch score and keyboard flag */
void ev_print_inputs(void);

/* 1 when the fd is an evdev node (EVIOCGVERSION answers) */
int ev_is_evdev(int fd);
