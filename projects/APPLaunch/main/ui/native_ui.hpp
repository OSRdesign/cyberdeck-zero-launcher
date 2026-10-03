/*
 * Native full-screen UI for the Raspberry Pi Zero 2W + Waveshare 2.8" DPI LCD port.
 *
 * The stock pages are laid out for 320x170 and keep running on the "compat"
 * display (drawn 2x into a window). The home screen is native 640x480 and
 * touch-friendly; while a compat page is up, a native toolbar below the window
 * injects real key events (see cp0_display.h for the display model).
 *
 * Everything here is a no-op unless the dpi-scaled display backend is active.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <string>

class Launch;
typedef struct _lv_obj_t lv_obj_t;

namespace native_ui {

/* True when the dpi-scaled backend (two displays) is active. */
bool enabled();

/* Clock, Wi-Fi bars and Bluetooth icon in the top-right corner of a native 640x480 screen
 * (same as the home grid); updates itself and is freed with the parent. */
void add_status_icons(lv_obj_t *parent);

/* Remember the launcher whose app list drives the home grid. */
void attach(Launch *launch);

/* Switch to native mode and show the home grid. */
void show_home();

/* Switch to compat mode and show the toolbar chrome; call before creating a compat page. */
void enter_compat();

/* Call before creating a page: native pages (full-panel layouts) stay on the native
 * display; all others run in the compat window with the toolbar. */
void begin_page(bool native_layout, bool touch_list = false, bool touch_swipe = false,
                unsigned short swipe_tap_key = 0);

/* The app about to be launched (its display name): touch behaviour follows Settings > Touch. */
void set_launching_app(const std::string &name);

/* Rebuild the home grid from the launcher's app list. */
void refresh_apps();

/* Run a stock (framebuffer) CardputerZero app inside the compat window, scaled up. The app runs on a
 * worker thread (the UI stays alive: toolbar, Esc hold); on_exit is called on the UI thread when it
 * has ended. Returns false if the app could not be started (on_exit is not called then). */
bool run_external(const std::string &command, bool keep_root, std::function<void()> on_exit);

} // namespace native_ui
