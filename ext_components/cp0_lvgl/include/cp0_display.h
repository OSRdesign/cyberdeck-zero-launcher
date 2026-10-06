/*
 * Raspberry Pi Zero 2W / Waveshare 2.8" DPI LCD display manager (public API).
 *
 * Two LVGL displays share one framebuffer:
 *   - native : full panel resolution (e.g. 640x480). Used by native screens.
 *   - compat : the legacy 320x170 CardputerZero resolution. Its output is
 *              blitted 2x into a window at the top of the panel, so the stock
 *              pages keep working unmodified.
 * The default LVGL display follows the mode, so code that creates screens or
 * reads lv_screen_active() lands on the right display.
 */
#pragma once

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CP0_DISPLAY_MODE_NATIVE = 0,
    CP0_DISPLAY_MODE_COMPAT = 1,
} cp0_display_mode_t;

/* 1 when the dpi-scaled backend was selected (APPLAUNCH_DISPLAY=dpi-scaled) and initialised. */
int cp0_display_available(void);

lv_display_t *cp0_display_native(void);
lv_display_t *cp0_display_compat(void);

/* Window of the compat display inside the native coordinate space. */
void cp0_display_compat_window(int *x, int *y, int *w, int *h);

/* How touches inside the compat window are handled. */
typedef enum {
    /* Forwarded to the compat display as an ordinary pointer (pages with clickable widgets). */
    CP0_DISPLAY_TOUCH_POINTER = 0,
    /* Turned into key events for list-style pages that only understand the keyboard:
     * dragging steps Up/Down once per row, a tap on the highlighted row sends Enter, a tap
     * on another row moves the selection there, and a swipe in from the left edge sends Esc. */
    CP0_DISPLAY_TOUCH_LIST = 1,
    /* Turned into key events for games and stock apps: a swipe sends the arrow key for its
     * direction (one per swipe), a tap sends Enter. */
    CP0_DISPLAY_TOUCH_SWIPE = 2,
} cp0_display_touch_mode_t;

/* Where gesture keys go: NULL = the launcher's own key queue, otherwise this function (value 1 =
 * press, 0 = release), e.g. the virtual keyboard that a running stock app reads. */
typedef void (*cp0_display_key_sink_t)(unsigned short code, int value);
void cp0_display_set_key_sink(cp0_display_key_sink_t sink);

/* Key sent by a tap in SWIPE mode (0 = Enter), e.g. Space to fire in a game. */
void cp0_display_set_swipe_tap_key(unsigned short code);

/* LIST mode: reverse the drag direction (1 = dragging up selects the row above), for pages whose highlight moves
 * instead of the list. Reset to 0 by the page when it closes. */
void cp0_display_set_list_drag_inverted(int inverted);

/* center_y / row_h describe the highlighted row (compat 320x170 coordinates); ignored for POINTER. */
void cp0_display_set_touch_mode(cp0_display_touch_mode_t mode, int center_y, int row_h);

/* Screensaver support.
 *  - blackout: both displays stop drawing the compat window, so a native overlay can cover the
 *    whole panel (and the compat window repaints when switched off again).
 *  - touch activity: called on the LVGL thread whenever a finger is down; press_edge is 1 on the
 *    first report of a new touch.
 *  - touch swallow: 1 = touches are not delivered to any UI (screensaver up); 2 = same, until the
 *    current finger is lifted (the touch that woke the screen must not act on the UI). */
typedef void (*cp0_display_touch_activity_cb_t)(int press_edge);

/* External (stock CardputerZero) apps draw to a virtual 320x170 framebuffer; while one runs the
 * compat display stops drawing and the launcher feeds the app's picture to the compat window with
 * cp0_display_external_blit() (XRGB8888, scaled up like the compat display). */
void cp0_display_set_external(int on);
/* While an external app runs, the compat display still draws inside this rectangle (compat
 * 320x170 coordinates) and the blit leaves it alone: used for the "hold Esc" ribbon. w <= 0 clears. */
void cp0_display_set_overlay_rect(int x, int y, int w, int h);
void cp0_display_external_blit(const void *xrgb8888, int width, int height, int stride_bytes);
void cp0_display_set_blackout(int on);
void cp0_display_set_touch_activity_cb(cp0_display_touch_activity_cb_t cb);
void cp0_display_set_touch_swallow(int mode);

cp0_display_mode_t cp0_display_get_mode(void);
/* Switches the default display and forces a redraw of the newly visible one.
 * LVGL thread only. */
void cp0_display_set_mode(cp0_display_mode_t mode);

#ifdef __cplusplus
}
#endif
