/*
 * SPDX-License-Identifier: MIT
 *
 * The shared status strip and the page title (see cp0_ui_widgets.hpp, task L1): the clock pill, Wi-Fi bars and
 * Bluetooth glyph the launcher's native screens show, drawn by the cp0_statusbar renderer into a transparent canvas at
 * the top right of a screen, placed and scaled by the layout service (shell bar_h / status_w / status_top /
 * status_pct). The renderer (FreeType) exists on the device build only: under the SDL simulator the strip is not drawn.
 */

#include "cp0_ui_widgets.hpp"

#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_status_strip.h"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)
#include "cp0_lvgl_app.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>
#endif

namespace cp0 {
namespace ui {
namespace {

constexpr uint32_t kGold = 0xF0B400; /* the home grid's title */

} // namespace

lv_obj_t *add_title(lv_obj_t *parent, const char *text)
{
    const cp0_ui_shell_t &s = cp0_ui_metrics_get()->shell;
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, text ? text : "");
    lv_obj_set_style_text_font(title, cp0_ui_font_px(s.title_px), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(kGold), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, s.title_x, s.title_y);
    return title;
}

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

namespace {

/* share/font/<name> where the running program finds its resources, else the launcher's installed copy (an app). */
std::string font_path(const char *name)
{
    const std::string rel = std::string("share/font/") + name;
    const char *own = cp0_file_path_c(rel.c_str());
    if (own && own[0] && ::access(own, R_OK) == 0) return own;
    const std::string installed = std::string("/usr/share/APPLaunch/") + rel;
    if (::access(installed.c_str(), R_OK) == 0) return installed;
    return own ? own : "";
}

/* The strips that exist (LVGL thread): StatusStrip::active_area() looks for the one on the active screen. */
std::vector<StatusStrip *> &live_strips()
{
    static std::vector<StatusStrip *> strips;
    return strips;
}

/* One renderer per process, made on first use (LVGL thread). */
cp0_statusbar_t *renderer()
{
    static cp0_statusbar_t *bar = [] {
        const std::string text = font_path("Montserrat-Medium.ttf");
        const std::string icon = font_path("FontAwesome5-Solid+Brands+Regular.woff");
        return cp0_statusbar_create(text.c_str(), icon.c_str());
    }();
    return bar;
}

} // namespace

StatusSource default_status_source()
{
    return [](cp0_statusbar_state_t &state) {
        cp0_statusbar_state_t read{};
        cp0_statusbar_read_state(&read);
        cp0_ui_status_set_wifi(&state, read.wifi_up, read.wifi_pct);
        state.bt_on = read.bt_on;
        state.bt_connected = read.bt_connected;
    };
}

StatusStrip *StatusStrip::add(lv_obj_t *parent, StatusSource source)
{
    if (!parent) return nullptr;
    return new StatusStrip(parent, source ? std::move(source) : default_status_source());
}

StatusStrip::StatusStrip(lv_obj_t *parent, StatusSource source) : parent_(parent), source_(std::move(source))
{
    lv_display_t *display = lv_obj_get_display(parent);
    const cp0_ui_metrics_t *m = cp0_ui_metrics_get();
    cp0_ui_status_strip_geom_t g{};
    cp0_ui_status_strip_geom(m, display ? static_cast<int>(lv_display_get_horizontal_resolution(display)) : m->w, &g);
    width_ = g.w;
    top_ = g.top;
    pct_ = g.pct;
    pixels_.assign(static_cast<size_t>(g.w) * g.rows, 0u);
    lv_obj_add_event_cb(parent, parent_delete_cb, LV_EVENT_DELETE, this);

    canvas_ = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas_, pixels_.data(), g.w, g.h, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas_, g.x, g.y);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);

    clock_timer_ = lv_timer_create(clock_timer_cb, 1000, this);
    status_timer_ = lv_timer_create(status_timer_cb, 2000, this);
    clock_tick();
    refresh();
    live_strips().push_back(this);
}

void StatusStrip::parent_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<StatusStrip *>(lv_event_get_user_data(event));
    if (!self) return;
    auto &strips = live_strips();
    strips.erase(std::remove(strips.begin(), strips.end(), self), strips.end());
    if (self->clock_timer_) lv_timer_delete(self->clock_timer_);
    if (self->status_timer_) lv_timer_delete(self->status_timer_);
    delete self;
}

void StatusStrip::clock_timer_cb(lv_timer_t *timer)
{
    if (auto *self = static_cast<StatusStrip *>(lv_timer_get_user_data(timer))) self->clock_tick();
}

void StatusStrip::status_timer_cb(lv_timer_t *timer)
{
    if (auto *self = static_cast<StatusStrip *>(lv_timer_get_user_data(timer))) self->refresh();
}

void StatusStrip::clock_tick()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::strftime(state_.clock, sizeof(state_.clock), "%H:%M", &local);
    redraw();
}

bool StatusStrip::screen_active() const
{
    lv_obj_t *screen = lv_obj_get_screen(parent_);
    lv_display_t *display = screen ? lv_obj_get_display(screen) : nullptr;
    return display && lv_display_get_screen_active(display) == screen;
}

void StatusStrip::refresh()
{
    // Nothing to do (and no work to pay for) while another screen owns the display.
    if (!screen_active()) return;
    if (source_) source_(state_);
    redraw();
}

void StatusStrip::redraw()
{
    cp0_statusbar_t *bar = renderer();
    if (!bar || !canvas_) return;
    if (drawn_ && std::memcmp(&state_, &shown_, sizeof(state_)) == 0) return;
    std::fill(pixels_.begin(), pixels_.end(), 0u);
    cp0_statusbar_render_scaled(bar, pixels_.data(), width_, width_, 0, top_, 0, &state_, pct_);
    shown_ = state_;
    drawn_ = true;
    lv_obj_invalidate(canvas_);
}

bool StatusStrip::active_area(lv_display_t *display, lv_area_t *out)
{
    if (!display || !out) return false;
    lv_obj_t *active = lv_display_get_screen_active(display);
    for (StatusStrip *strip : live_strips()) {
        if (!strip->canvas_ || lv_obj_get_screen(strip->canvas_) != active || !lv_obj_is_visible(strip->canvas_))
            continue;
        lv_obj_get_coords(strip->canvas_, out);
        const int left = status_strip_left();
        if (left > out->x1 && left <= out->x2) out->x1 = left;
        return true;
    }
    return false;
}

int status_strip_left()
{
    static int left = -1;
    if (left >= 0) return left;
    const cp0_ui_metrics_t *m = cp0_ui_metrics_get();
    cp0_ui_status_strip_geom_t g{};
    cp0_ui_status_strip_geom(m, m->w, &g);
    cp0_statusbar_t *bar = renderer();
    if (!bar || g.w <= 0) return g.x; /* not kept: measured when the renderer is there */
    // draw the strip in its fullest state (Bluetooth glyph shown, all bars, a clock) into a scratch canvas and take
    // its first column that has a pixel: the strip's own geometry, not a copy of it
    const int rows = g.rows + 16;
    std::vector<uint32_t> pixels(static_cast<size_t>(g.w) * rows, 0u);
    cp0_statusbar_state_t full{};
    full.wifi_up = 1;
    full.wifi_pct = 100;
    full.bt_on = 1;
    full.bt_connected = 1;
    std::snprintf(full.clock, sizeof(full.clock), "%s", "00:00");
    cp0_statusbar_render_scaled(bar, pixels.data(), g.w, g.w, 0, g.top, 0, &full, g.pct);
    const int first = cp0_ui_status_first_column(pixels.data(), g.w, rows);
    left = g.x + (first < g.w ? first : 0);
    return left;
}

#else // SDL simulator: no FreeType renderer, no strip

StatusSource default_status_source()
{
    return [](cp0_statusbar_state_t &) {};
}

StatusStrip *StatusStrip::add(lv_obj_t *, StatusSource) { return nullptr; }

void StatusStrip::refresh() {}

bool StatusStrip::active_area(lv_display_t *, lv_area_t *) { return false; }

int status_strip_left() { return 0; }

#endif

} // namespace ui
} // namespace cp0
