/*
 * Raspberry Pi Zero 2W / Waveshare 2.8" DPI LCD display manager (public API).
 *
 * Two LVGL displays share one framebuffer:
 *   - native : full panel resolution (e.g. 640x480). Used by native screens.
 *   - compat : the legacy 320x170 stock resolution. Its output is
 *              blitted 2x into a window at the top of the panel, so the stock
 *              pages keep working unmodified.
 * The default LVGL display follows the mode, so code that creates screens or
 * reads lv_screen_active() lands on the right display.
 */
#pragma once

#include "lvgl/lvgl.h"
#include "cp0_touch_slots.h"

#include <stddef.h>
#include <stdint.h>

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

/* Places the compat window: integer scale of the 320x170 display and top-left corner in the native canvas. The
 * display manager calls it once at start with the layout service's placement (cp0_ui_metrics.h: compat, with the
 * toolbar below it); call it again only before the first compat page. LVGL thread. Returns 0, or -1 when the
 * window would not fit the canvas (nothing changes). */
int cp0_display_configure_compat(int scale, int x, int y);

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
 *    current finger is lifted (the touch that woke the screen must not act on the UI); with a
 *    multi-touch panel, until every finger is lifted (multitasking P-c). */
typedef void (*cp0_display_touch_activity_cb_t)(int press_edge);

/* External legacy fullscreen apps draw to a virtual 320x170 framebuffer; while one runs the
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

/* Multitasking P-b (task 015): the panel handed to a fullscreen app while the launcher keeps running.
 *  - suspend (any thread): from its return on, no flush, blit or fill of either display writes the framebuffer
 *    (they complete without drawing; one lock serialises them with this call), and the touch screen reads as
 *    released (no click, no screensaver activity). Idempotent.
 *  - resume (LVGL thread): writes are allowed again, the panel is panned back to page 0 if an app panned it, and
 *    both displays' screens and layers are invalidated so the next refresh repaints everything.
 *  - snapshot / restore (any thread): one framebuffer page (line length x height bytes, see page_bytes) copied
 *    from the page shown now / written back to the page given (and panned to it), under the same lock. Restore
 *    returns 0 when the page reads back identical (9 sample rows: reading the framebuffer is slow), 1 when it
 *    differs (someone wrote meanwhile), -1 on error.
 *  - touch grab (any thread): EVIOCGRAB on the launcher's own touch file descriptor (it keeps reading it; nobody
 *    else gets the touches). 0 = done or already in that state, -1 = no touch device or the ioctl failed. */
void cp0_display_suspend(void);
void cp0_display_resume(void);
/* The machine slept (system suspend) and the kernel may have cleared the framebuffer: panned back to page 0 and
 * every screen and layer of both displays invalidated, so the next refresh rewrites the whole panel. LVGL thread.
 * Does nothing while an app owns the panel (cp0_display_suspended) or before the display exists. */
void cp0_display_repaint_all(void);
int cp0_display_suspended(void);
size_t cp0_display_fb_page_bytes(void);
int cp0_display_fb_snapshot(void *dst, size_t len, int *page_y);
int cp0_display_fb_restore(const void *src, size_t len, int page_y);
int cp0_display_set_touch_grab(int on);

/* Multitasking P-d (task 015-pd): the legacy app sheet drawn over the frozen app's last frame.
 *  - backdrop (any thread): a page snapshot written to the launcher's page (page 0, panned to it) with every colour
 *    scaled to keep_pct percent (the dimmed picture behind the sheet). 0, or -1 on error.
 *  - native clip (LVGL thread): while w > 0 the native display writes only inside this rectangle (landscape
 *    coordinates), and there no pure black pixel (the screen's ground around a rounded box: the backdrop shows); the
 *    rest of the panel keeps what is there (the backdrop). w <= 0 clears it (the default). */
int cp0_display_fb_backdrop(const void *src, size_t len, int keep_pct);
void cp0_display_set_native_clip(int x, int y, int w, int h);

/* Multitasking P-c (task 015-pc): the touch screen's contacts for the launcher's gestures.
 *  - Every slot of a protocol B panel is tracked (cp0_touch_slots.h); the LVGL pointer is the first contact of a
 *    touch (one finger: as before; a second finger no longer moves or releases it). Other panels read as before.
 *  - frame callback (LVGL thread; NULL = none, the default): every frame (each SYN_REPORT) with its contacts in
 *    landscape coordinates, while the launcher draws (not while suspended); frame == NULL is a tick, once per read of
 *    the touch device, so a hold completes without events. Returning 1 says the contacts down now belong to a
 *    recognised gesture: they are swallowed (the pointer reads released, the press in progress ends with
 *    LV_EVENT_PRESS_LOST and no click, LIST / SWIPE keys are not sent) until every finger is up. Setting it also asks
 *    the device for monotonic
 *    event times (the frames' t_us; cp0_touch_now_us() is the same clock).
 *  - map (any thread, after the display exists): a raw position of the touch device -> landscape, as the pointer does
 *    (another reader of the same node uses it: the supervisor while an app is in front).
 *  - path: the touch device read ("" = none); slots: 0 none, 1 single touch (or no slots), N protocol B slots. */
typedef int (*cp0_display_touch_frame_cb_t)(const cp0_touch_frame_t *frame, int64_t now_us);
void cp0_display_set_touch_frame_cb(cp0_display_touch_frame_cb_t cb);
/* LVGL thread: reads what the touch device queued (into the tracker; no frame to the callback) and returns how many
 * contacts are down now: when the launcher takes the panel back, its queue holds what happened meanwhile. */
int cp0_display_touch_sync(void);
/* Multitasking P-e (an app's show, cp0_app_lifecycle_lvgl.h), LVGL thread: drops what the touch device queued, reads
 * back the contacts down now (EVIOCGMTSLOTS; BTN_TOUCH for a panel without slots), so a lift the app never saw (the
 * launcher grabbed or drained the device meanwhile) does not leave a stuck contact, and when a finger is still down the
 * pointer reads released until every finger is up (swallow mode 2). Returns the contacts down. */
int cp0_display_touch_resync(void);
void cp0_display_touch_map(int raw_x, int raw_y, int *x, int *y);
const char *cp0_display_touch_path(void);
int cp0_display_touch_slots(void);

/* The PDA profile's writing zone (report 041, cp0_zone.h, cp0_zone_ui.h), APPLAUNCH_PDA=1 on a portrait canvas:
 *  - active: 1 when the band exists. The native display is then the app area above it (cp0_ui_metrics_current()),
 *    cp0_ui_metrics_panel() the whole panel; the band is a third LVGL display under it, drawn and touched by itself: a
 *    touch whose first contact lands there never reaches the native or compat display (pen strokes go to the
 *    recogniser, the strip and the soft buttons are the band's own widgets). Suspend, resume and the touch hand-over
 *    cover it as they cover the others.
 *  - invalidate (LVGL thread): redraw the band (after an app drew over the whole panel without the display being
 *    suspended: the blocking fullscreen path). */
int cp0_display_zone_active(void);
void cp0_display_zone_invalidate(void);

#ifdef __cplusplus
}
#endif
