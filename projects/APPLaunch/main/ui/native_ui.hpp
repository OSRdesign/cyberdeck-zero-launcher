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

#include "cp0_statusbar.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class Launch;
typedef struct _lv_obj_t lv_obj_t;

namespace native_ui {

/* True when the dpi-scaled backend (two displays) is active. */
bool enabled();

/* The launcher's source for the shared status strip (cp0::ui::StatusStrip, cp0_ui_widgets.hpp): Wi-Fi from the
 * launcher's Wi-Fi service, Bluetooth from its Bluetooth service. Native pages add the strip with
 * cp0::ui::StatusStrip::add(header, native_ui::fill_status), the title with cp0::ui::add_title and keep their controls
 * left of cp0::ui::status_strip_left() (task L1). No-op under the SDL simulator. */
void fill_status(cp0_statusbar_state_t &state);

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

/* Multitasking P-d (task 015-pd; only called with multitasking on): the apps left open, by their .desktop Exec
 * line. Their tiles get a small green dot at the top right (a ring of the tile's colour around it); the others none.
 * Never called: no dot object exists. LVGL thread. */
void set_resident_apps(const std::vector<std::string> &exec_keys);
/* The icon file of the tile whose Exec line is exec_key ("" when none): the switcher's cards show it. */
std::string app_icon(const std::string &exec_key);
/* A stock (wrapped) app runs in the compat window (run_external). */
bool external_app_running();

/* Run a stock (framebuffer) app inside the compat window, scaled up. The app runs on a
 * worker thread (the UI stays alive: toolbar, Esc hold); on_exit is called on the UI thread when it
 * has ended. Returns false if the app could not be started (on_exit is not called then). */
bool run_external(const std::string &command, bool keep_root, std::function<void()> on_exit);

} // namespace native_ui
