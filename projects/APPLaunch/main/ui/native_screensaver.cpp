/*
 * Clock screensaver for the Raspberry Pi Zero 2W + Waveshare 2.8" DPI LCD port.
 */

#include "native_screensaver.hpp"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "cp0_backlight_profile.h"
#include "cp0_display.h"
#include "hal_lvgl_bsp.h"
#include "keyboard_input.h"
#include "launcher_media_controls.h"
#include "lvgl/lvgl.h"
#include "model/screensaver_runtime_contract.hpp"
#include "native_ui.hpp"
#include "sample_log.h"
#include "ui.h"

#include <cstdint>
#include <ctime>
#include <string>

namespace {

constexpr int kDimPercent = 10;
constexpr uint32_t kIdleCheckMs = 500;
constexpr int kClockFontPx = 180;
constexpr int kTimeOffsetY = -30;
constexpr int kDateOffsetY = 90;
// The sizes above are designed for the 640x480 deck. A shorter display (the compact profile of
// native_ui, e.g. 480x320) scales them by the tighter of its width/height ratios to that design.
constexpr int kDesignW = 640;
constexpr int kDesignH = 480;
constexpr int kCompactBelowH = 400;

struct ClockGeometry {
    int font_px;
    int time_y;
    int date_y;
    const lv_font_t *date_font;
};

ClockGeometry clock_geometry(int width, int height)
{
    if (height >= kCompactBelowH || width <= 0 || height <= 0)
        return {kClockFontPx, kTimeOffsetY, kDateOffsetY, &lv_font_montserrat_28};
    // num/den = min(width / 640, height / 480), kept as a fraction for exact integer math.
    const bool by_height = height * kDesignW <= width * kDesignH;
    const int num = by_height ? height : width;
    const int den = by_height ? kDesignH : kDesignW;
    return {kClockFontPx * num / den, kTimeOffsetY * num / den, kDateOffsetY * num / den,
            &lv_font_montserrat_18};
}

lv_obj_t *s_overlay = nullptr;
lv_obj_t *s_time = nullptr;
lv_obj_t *s_date = nullptr;
lv_timer_t *s_idle_timer = nullptr;
lv_timer_t *s_clock_timer = nullptr;

bool s_enabled = true;
bool s_active = false;
uint32_t s_last_activity = 0;
int s_saved_backlight = -1;
uint32_t s_wake_key = 0; // key whose press woke the screen; its release/repeat is swallowed

int timeout_seconds()
{
    try {
        bool succeeded = false;
        std::string response;
        cp0_signal_config_api({"GetInt", "dark_time", "30"}, [&](int code, std::string data) {
            succeeded = code == 0;
            response = std::move(data);
        });
        return screensaver_timeout_from_config(succeeded, response);
    } catch (...) {
        return screensaver_timeout_from_config(false, {});
    }
}

void update_clock(lv_timer_t *)
{
    if (!s_time || !s_date) return;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char text[16];
    std::strftime(text, sizeof(text), "%H:%M", &local);
    lv_label_set_text(s_time, text);
    std::strftime(text, sizeof(text), "%d/%m/%Y", &local);
    lv_label_set_text(s_date, text);
}

void ensure_overlay()
{
    if (s_overlay) return;
    lv_display_t *display = cp0_display_native();
    lv_obj_t *layer = lv_display_get_layer_sys(display);
    if (!layer) return;

    s_overlay = lv_obj_create(layer);
    lv_obj_remove_style_all(s_overlay);
    const int width = static_cast<int>(lv_display_get_horizontal_resolution(display));
    const int height = static_cast<int>(lv_display_get_vertical_resolution(display));
    const ClockGeometry geo = clock_geometry(width, height);
    lv_obj_set_size(s_overlay, width, height);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_time = lv_label_create(s_overlay);
    lv_obj_set_style_text_color(s_time, lv_color_hex(0xFFFFFF), 0);
    if (lv_font_t *font = launcher_fonts().get("Montserrat-Bold.ttf", geo.font_px,
                                               LV_FREETYPE_FONT_STYLE_NORMAL,
                                               LV_FREETYPE_FONT_RENDER_MODE_BITMAP))
        lv_obj_set_style_text_font(s_time, font, 0);
    else
        lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_label_set_text(s_time, "--:--");
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, geo.time_y);

    s_date = lv_label_create(s_overlay);
    lv_obj_set_style_text_color(s_date, lv_color_hex(0x9A9A9A), 0);
    lv_obj_set_style_text_font(s_date, geo.date_font, 0);
    lv_label_set_text(s_date, "--/--/----");
    lv_obj_align(s_date, LV_ALIGN_CENTER, 0, geo.date_y);

    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void wake()
{
    if (!s_active) return;
    s_active = false;
    if (s_clock_timer) {
        lv_timer_delete(s_clock_timer);
        s_clock_timer = nullptr;
    }
    if (s_overlay) lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    cp0_display_set_blackout(0); // repaints whatever was on screen
    if (s_saved_backlight > 0) launcher_media_controls::restore_backlight(s_saved_backlight);
    s_saved_backlight = -1;
    cp0_display_set_touch_swallow(2); // the touch that woke us must not click anything
    s_last_activity = lv_tick_get();
    SLOGI("[SAVER] woke");
}

void activate()
{
    if (s_active || !s_enabled) return;
    ensure_overlay();
    if (!s_overlay) return;
    s_active = true;
    // An on/off backlight (board profile gpio:<dir>) has no dim level: the clock shows at full
    // backlight instead of risking a dark panel.
    s_saved_backlight = cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_GPIO_ONOFF
                            ? -1
                            : launcher_media_controls::dim_backlight(kDimPercent);
    // The compat window is drawn straight to the panel: stop that so the overlay covers everything.
    if (cp0_display_get_mode() == CP0_DISPLAY_MODE_COMPAT) cp0_display_set_blackout(1);
    update_clock(nullptr);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_overlay);
    lv_obj_invalidate(s_overlay);
    if (!s_clock_timer) s_clock_timer = lv_timer_create(update_clock, 1000, nullptr);
    cp0_display_set_touch_swallow(1);
    SLOGI("[SAVER] active");
}

void idle_timer_cb(lv_timer_t *)
{
    if (s_active || !s_enabled) return;
    const int seconds = timeout_seconds();
    if (seconds > 0 && lv_tick_elaps(s_last_activity) >= static_cast<uint32_t>(seconds) * 1000u) activate();
}

void touch_activity(int press_edge)
{
    s_last_activity = lv_tick_get();
    if (s_active && press_edge) wake();
}

} // namespace

namespace native_screensaver {

void init()
{
    if (s_idle_timer || !native_ui::enabled()) return;
    s_last_activity = lv_tick_get();
    s_idle_timer = lv_timer_create(idle_timer_cb, kIdleCheckMs, nullptr);
    cp0_display_set_touch_activity_cb(touch_activity);
}

void shutdown()
{
    cp0_display_set_touch_activity_cb(nullptr);
    if (s_active) wake();
    if (s_idle_timer) lv_timer_delete(s_idle_timer);
    if (s_clock_timer) lv_timer_delete(s_clock_timer);
    s_idle_timer = s_clock_timer = nullptr;
    if (s_overlay) lv_obj_delete(s_overlay);
    s_overlay = s_time = s_date = nullptr;
}

bool filter_key(const struct key_item *item)
{
    if (!item || !native_ui::enabled()) return false;
    s_last_activity = lv_tick_get();

    if (s_active) {
        if (item->key_state == KBD_KEY_PRESSED) {
            s_wake_key = item->key_code;
            wake();
        }
        return true; // everything is consumed while the saver is up
    }
    if (s_wake_key != 0 && item->key_code == s_wake_key) {
        if (item->key_state == KBD_KEY_RELEASED) s_wake_key = 0;
        return true; // the rest of the key that woke the screen
    }
    return false;
}

bool active()
{
    return s_active;
}

void set_enabled(bool enabled)
{
    s_enabled = enabled;
    if (!enabled && s_active) wake();
    s_last_activity = lv_tick_get();
}

} // namespace native_screensaver

#else // SDL simulator and non-Linux builds

namespace native_screensaver {

void init() {}
void shutdown() {}
bool filter_key(const struct key_item *) { return false; }
bool active() { return false; }
void set_enabled(bool) {}

} // namespace native_screensaver

#endif
