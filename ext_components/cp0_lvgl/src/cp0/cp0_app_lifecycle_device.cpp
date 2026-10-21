/*
 * SPDX-License-Identifier: MIT
 *
 * The board's side of an app's lifecycle (cp0_app_lifecycle_lvgl.h, device builds): the display backend and the
 * keyboard thread as the port, the LVGL loop hook as the service, and G2 on the app's own status strip.
 */
#include "cp0_app_lifecycle_lvgl.h"

#include "cp0_display.h"
#include "cp0_gesture_g2.h"
#include "cp0_lvgl_app_runner.hpp"
#include "cp0_touch_reader.h"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_widgets.hpp"

#include <cstdio>
#include <cstdlib>

/* the keyboard thread's pause / resume (cp0_lvgl_keyboard.c): the keyboards' files closed and opened again; libinput
 * releases the keys still held when it lets a device go, so the modifiers come back clean */
extern "C" void keyboard_pause(void);
extern "C" void keyboard_resume(void);

namespace {

cp0_g2_t g_g2;
bool g_g2_on = false;

void output(int on)
{
    if (on) cp0_display_resume();
    else cp0_display_suspend();
}

void input(int on)
{
    if (!on) {
        keyboard_pause();
        return;
    }
    keyboard_resume();
    if (cp0_display_touch_resync() > 0) cp0_g2_block(&g_g2, cp0_touch_now_us()); /* that hand is not a G2 */
    else cp0_g2_reset(&g_g2);
}

const cp0_app_lifecycle_port_t kPort = {output, input};

const char *env_get(const char *name)
{
    return std::getenv(name);
}

/* LVGL thread, every touch frame while the app is in front (the display manager sends none while suspended): G2 on
 * the strip of the app's active screen sends "switcher"; the finger is then swallowed (no click under it). */
int touch_frame(const cp0_touch_frame_t *frame, int64_t now_us)
{
    if (!cp0_app_lifecycle_lvgl_active() || cp0_app_lifecycle_lvgl_hidden()) return 0;
    cp0_g2_event_t e;
    if (frame) {
        lv_area_t a;
        cp0_g2_rect_t strip = {0, 0, 0, 0};
        const bool on_screen = cp0::ui::StatusStrip::active_area(cp0_display_native(), &a);
        if (on_screen) strip = {a.x1, a.y1, a.x2 - a.x1 + 1, a.y2 - a.y1 + 1};
        e = cp0_g2_frame(&g_g2, frame, on_screen ? &strip : nullptr);
    } else {
        e = cp0_g2_tick(&g_g2, now_us);
    }
    if (!e.fired) return 0;
    const int sent = cp0_app_lifecycle_lvgl_send("switcher");
    std::fprintf(stderr, "[lifecycle] G2 (status strip held %d ms): switcher%s\n", e.held_ms,
                 sent == 0 ? " sent" : " not sent");
    return 1;
}

void loop_hook()
{
    cp0_app_lifecycle_lvgl_service();
}

} // namespace

extern "C" int cp0_app_lifecycle_device_start(void (*on_close)(void))
{
    if (!cp0_app_lifecycle_lvgl_start(&kPort, on_close)) return 0;
    cp0_lvgl_set_loop_hook(loop_hook);
    if (cp0_display_available() && cp0_display_touch_path()[0]) {
        cp0_g2_params_t params;
        cp0_g2_params_default(&params);
        (void)cp0_g2_params_apply_env(&params, env_get);
        const cp0_ui_metrics_t *m = cp0_ui_metrics_get();
        cp0_g2_init(&g_g2, &params, m ? m->ppmm_x100 : 0);
        cp0_display_set_touch_frame_cb(touch_frame);
        g_g2_on = true;
    }
    return 1;
}

extern "C" void cp0_app_lifecycle_device_stop(void)
{
    if (g_g2_on) cp0_display_set_touch_frame_cb(nullptr);
    g_g2_on = false;
    cp0_lvgl_set_loop_hook(nullptr);
    cp0_app_lifecycle_lvgl_stop();
}
