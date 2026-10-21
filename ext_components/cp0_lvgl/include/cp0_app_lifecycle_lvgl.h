/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking for an app built on cp0_lvgl (docs/APP-CONTRACT.md section 13): the lifecycle protocol
 * (cp0_app_lifecycle.h) driven from the app's own LVGL loop, without a thread of its own.
 *
 *   hide   the platform stops writing the framebuffer (device: cp0_display_suspend; the touch screen reads released),
 *          no display renders any more (the areas marked are dropped and invalidation is off: widgets may change while
 *          hidden, nothing is drawn), the press in progress is cancelled (lv_indev_reset: LV_EVENT_INDEV_RESET, no
 *          click), the platform lets the keyboards go (device: keyboard_pause, which also releases the keys held, Alt
 *          included), the keyboard queue is emptied; then "ack <n>".
 *   show   the platform reads input again from a clean state (device: keyboard_resume, the touch screen's queue
 *          dropped and its contacts read back, a finger still down presses nothing until lifted), the keyboard queue
 *          is emptied, the indevs are reset, invalidation is on again and every display's screen and layers are
 *          invalidated (the next refresh repaints everything as it is now) and the platform writes again; "ack <n>".
 *   close  on_close(): the app's normal exit (as Esc at its root), so it saves its state and releases its hardware.
 *   keys   while the lifecycle is on a key filter (cp0_keyboard_set_key_filter, chained to one set before) drops
 *          every key while hidden, Tab with Alt (the launcher's Alt+Tab) and, after a show, the release of a key not
 *          pressed since (cp0_app_lifecycle_key_drop).
 *
 * Without APPLAUNCH_LIFECYCLE / APPLAUNCH_CONTROL_FD start returns 0, installs nothing, and the app is exactly as
 * before. LVGL thread only.
 */
#ifndef CP0_APP_LIFECYCLE_LVGL_H
#define CP0_APP_LIFECYCLE_LVGL_H

#ifdef __cplusplus
extern "C" {
#endif

/* What differs between the board and the PC harness. NULL members do nothing. */
typedef struct {
    /* 0: from the return on, nothing reaches the framebuffer; 1: writes allowed again (the screen is repainted) */
    void (*output)(int on);
    /* 0: stop reading the keyboards and the pointer, releasing what is held; 1: read again from a clean state */
    void (*input)(int on);
} cp0_app_lifecycle_port_t;

/* After the display and the keypad exist. Reads the environment (cp0_app_lifecycle_init); when the launcher gave a
 * control socket, installs the key filter and returns 1. Otherwise 0, nothing installed. on_close: the app's exit. */
int cp0_app_lifecycle_lvgl_start(const cp0_app_lifecycle_port_t *port, void (*on_close)(void));
/* Every turn of the LVGL loop (cheap: one non-blocking recv while the lifecycle is on, nothing otherwise). */
void cp0_app_lifecycle_lvgl_service(void);
/* 1 while the launcher gave a control socket (until it goes away or stop). */
int cp0_app_lifecycle_lvgl_active(void);
/* 1 between hide and show: an X-Background=run app skips its own rendering work then. */
int cp0_app_lifecycle_lvgl_hidden(void);
/* "home" (leave to the home grid, the app staying open) or "switcher". 0, or -1 without a lifecycle. */
int cp0_app_lifecycle_lvgl_send(const char *msg);
/* Before LVGL goes: the key filter removed, the socket closed. */
void cp0_app_lifecycle_lvgl_stop(void);

/* Device builds (src/cp0: the dpi-scaled display backend and the keyboard thread) only. cp0_app_lifecycle_lvgl_start
 * with the board's port, serviced from cp0_lvgl_run()'s loop hook (cp0_lvgl_set_loop_hook: the loop turns at least
 * every indev read period, 33 ms, so an answer takes at most that), and G2 for the app: one finger held 500 ms on its
 * status strip (cp0::ui::StatusStrip, cp0_gesture_g2.h) sends "switcher". Returns 1 when the lifecycle is on. */
int cp0_app_lifecycle_device_start(void (*on_close)(void));
void cp0_app_lifecycle_device_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* CP0_APP_LIFECYCLE_LVGL_H */
