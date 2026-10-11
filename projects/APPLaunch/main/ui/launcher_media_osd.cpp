/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launcher_media_osd.h"

#include "ui.h"

#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_overlay.h"

namespace {

constexpr uint32_t kBackgroundColor = 0x20242C;
constexpr uint32_t kBarBackgroundColor = 0x555C66;
constexpr uint32_t kAccentColor = 0xF2C94C;
constexpr uint32_t kTextColor = 0xFFFFFF;

// The OSD's boxes come from cp0_ui_overlay.h (task 014 P4): today's numbers on a display at most 320 px wide, the
// pinned DECK / COMPACT scale (x2, x1.5) or the body font's scale on the native display. They are applied on every
// show, so the OSD follows the display and its size.
cp0_ui_osd_t spec_for(const lv_obj_t *obj, lv_coord_t *display_w, lv_coord_t *display_h)
{
    lv_display_t *display = obj ? lv_obj_get_display(obj) : lv_display_get_default();
    const lv_coord_t width = display ? lv_display_get_horizontal_resolution(display) : CP0_UI_OVERLAY_COMPAT_MAX_W;
    const lv_coord_t height = display ? lv_display_get_vertical_resolution(display) : 170;
    cp0_ui_osd_t spec;
    cp0_ui_overlay_osd(cp0_ui_metrics_get(), width, height, &spec);
    if (display_w) *display_w = width;
    if (display_h) *display_h = height;
    return spec;
}

void place(lv_obj_t *obj, const cp0_ui_orect_t &r)
{
    lv_obj_set_size(obj, r.w, r.h);
    lv_obj_set_pos(obj, r.x, r.y);
}

void center_osd(lv_obj_t *obj)
{
    if (!obj) return;
    lv_coord_t width = 0, height = 0;
    const cp0_ui_osd_t spec = spec_for(obj, &width, &height);
    lv_obj_set_size(obj, spec.w, spec.h);
    lv_obj_set_pos(obj, (width - spec.w) / 2, (height - spec.h) / 2);
}

void style_for(lv_obj_t *container, lv_obj_t *icon, lv_obj_t *title, lv_obj_t *value, lv_obj_t *bar)
{
    const cp0_ui_osd_t spec = spec_for(container, nullptr, nullptr);
    lv_obj_set_style_radius(container, spec.radius, 0);
    lv_obj_set_style_border_width(container, spec.border, 0);
    lv_font_t *font = launcher_fonts().get(
        "AlibabaPuHuiTi-3-55-Regular.ttf", spec.font_px, LV_FREETYPE_FONT_STYLE_BOLD);
    lv_obj_set_style_text_font(icon, spec.icon_px > 0 ? cp0_ui_font_px(spec.icon_px) : LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_font(title, font, 0);
    lv_obj_set_style_text_font(value, font, 0);
    lv_obj_set_style_radius(bar, spec.bar_radius, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, spec.bar_radius, LV_PART_INDICATOR);
}

} // namespace

bool LauncherMediaOsd::ensure_created() noexcept
{
    try {
    // Follow the default display: the OSD lives on a display's top layer, so one made on another display (the compat
    // window while a stock page ran) is replaced by one on the display that is default now.
    if (container_ && lv_obj_get_display(container_) != lv_display_get_default())
        lv_obj_delete(container_); // the delete callback clears our handles and the timer
    if (launcher_media_osd_ui_ready(container_, icon_, title_, value_, bar_))
        return true;
    if (container_) lv_obj_delete(container_);

    lv_obj_t *parent = lv_layer_top();
    if (!parent)
        return false;

    container_ = lv_obj_create(parent);
    if (!container_)
        return false;
    lv_obj_add_event_cb(container_, container_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(container_);
    center_osd(container_);
    lv_obj_set_style_bg_color(container_, lv_color_hex(kBackgroundColor), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(container_, lv_color_hex(0x3A404A), 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_shadow_width(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_IGNORE_LAYOUT);

    icon_ = lv_label_create(container_);
    if (!icon_) {
        lv_obj_delete(container_);
        return false;
    }
    lv_obj_add_event_cb(icon_, child_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_style_text_color(icon_, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_text_align(icon_, LV_TEXT_ALIGN_CENTER, 0);

    title_ = lv_label_create(container_);
    if (!title_) {
        lv_obj_delete(container_);
        return false;
    }
    lv_obj_add_event_cb(title_, child_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_style_text_color(title_, lv_color_hex(kTextColor), 0);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_CLIP);

    value_ = lv_label_create(container_);
    if (!value_) {
        lv_obj_delete(container_);
        return false;
    }
    lv_obj_add_event_cb(value_, child_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_style_text_color(value_, lv_color_hex(kTextColor), 0);
    lv_obj_set_style_text_align(value_, LV_TEXT_ALIGN_RIGHT, 0);

    bar_ = lv_bar_create(container_);
    if (!bar_) {
        lv_obj_delete(container_);
        return false;
    }
    lv_obj_add_event_cb(bar_, child_delete_cb, LV_EVENT_DELETE, this);
    lv_bar_set_range(bar_, 0, 100);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(kBarBackgroundColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(kAccentColor), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);

    style_for(container_, icon_, title_, value_, bar_);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    return true;
    } catch (...) {
        if (container_) lv_obj_delete(container_);
        container_ = nullptr;
        icon_ = nullptr;
        title_ = nullptr;
        value_ = nullptr;
        bar_ = nullptr;
        model_.hide();
        stop_hide_timer();
        return false;
    }
}

void LauncherMediaOsd::show() noexcept
{
    try {
    center_osd(container_);
    lv_obj_move_foreground(container_);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    if (!hide_timer_)
        hide_timer_ = lv_timer_create(hide_timer_cb, LauncherMediaOsdModel::SHOW_DURATION_MS, this);
    if (!hide_timer_) {
        model_.hide();
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_timer_set_period(hide_timer_, LauncherMediaOsdModel::SHOW_DURATION_MS);
    lv_timer_reset(hide_timer_);
    lv_timer_resume(hide_timer_);
    } catch (...) {
        model_.hide();
        if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        stop_hide_timer();
    }
}

void LauncherMediaOsd::show_level(const char *title, const char *icon, int percent) noexcept
{
    try {
    if (!ensure_created())
        return;

    model_.show_level(title ? title : "", icon ? icon : "", percent);
    const auto &state = model_.state();
    lv_label_set_text(icon_, state.icon.c_str());
    lv_label_set_text(title_, state.title.c_str());
    lv_label_set_text_fmt(value_, "%d%%", state.percent);
    lv_bar_set_value(bar_, state.percent, LV_ANIM_OFF);
    const cp0_ui_osd_t spec = spec_for(container_, nullptr, nullptr);
    style_for(container_, icon_, title_, value_, bar_);
    place(icon_, spec.icon);
    place(title_, spec.title);
    place(value_, spec.value);
    place(bar_, spec.bar);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_clear_flag(value_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    show();
    } catch (...) {
        model_.hide();
        if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        stop_hide_timer();
    }
}

void LauncherMediaOsd::show_mute(bool muted) noexcept
{
    try {
    if (!ensure_created())
        return;

    model_.show_mute(muted, LV_SYMBOL_MUTE, LV_SYMBOL_VOLUME_MAX);
    const auto &state = model_.state();
    lv_label_set_text(icon_, state.icon.c_str());
    lv_label_set_text(title_, state.title.c_str());
    const cp0_ui_osd_t spec = spec_for(container_, nullptr, nullptr);
    style_for(container_, icon_, title_, value_, bar_);
    place(icon_, spec.mute_icon);
    place(title_, spec.mute_title);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(value_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    show();
    } catch (...) {
        model_.hide();
        if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        stop_hide_timer();
    }
}

void LauncherMediaOsd::shutdown()
{
    model_.hide();
    stop_hide_timer();
    if (container_)
        lv_obj_delete(container_);
    container_ = nullptr;
    icon_ = nullptr;
    title_ = nullptr;
    value_ = nullptr;
    bar_ = nullptr;
}

void LauncherMediaOsd::stop_hide_timer()
{
    if (!hide_timer_) return;
    lv_timer_delete(hide_timer_);
    hide_timer_ = nullptr;
}

void LauncherMediaOsd::hide_timer_cb(lv_timer_t *timer) noexcept
{
    try {
    auto *osd = static_cast<LauncherMediaOsd *>(lv_timer_get_user_data(timer));
    if (!osd || !launcher_media_osd_timer_is_current(timer, osd->hide_timer_)) return;
    osd->model_.hide();
    if (osd->container_)
        lv_obj_add_flag(osd->container_, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
    } catch (...) {
    }
}

void LauncherMediaOsd::container_delete_cb(lv_event_t *event) noexcept
{
    try {
        auto *osd = static_cast<LauncherMediaOsd *>(lv_event_get_user_data(event));
        if (!osd || lv_event_get_code(event) != LV_EVENT_DELETE ||
            lv_event_get_target(event) != lv_event_get_current_target(event) ||
            lv_event_get_target(event) != osd->container_)
            return;
        osd->container_ = nullptr;
        osd->icon_ = nullptr;
        osd->title_ = nullptr;
        osd->value_ = nullptr;
        osd->bar_ = nullptr;
        osd->model_.hide();
        osd->stop_hide_timer();
    } catch (...) {
    }
}

void LauncherMediaOsd::child_delete_cb(lv_event_t *event) noexcept
{
    try {
        auto *osd = static_cast<LauncherMediaOsd *>(lv_event_get_user_data(event));
        auto *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (!osd || !target || target != lv_event_get_current_target(event)) return;
        if (osd->icon_ == target) osd->icon_ = nullptr;
        if (osd->title_ == target) osd->title_ = nullptr;
        if (osd->value_ == target) osd->value_ = nullptr;
        if (osd->bar_ == target) osd->bar_ = nullptr;
        osd->model_.hide();
        if (osd->container_) lv_obj_add_flag(osd->container_, LV_OBJ_FLAG_HIDDEN);
        osd->stop_hide_timer();
    } catch (...) {
    }
}

LauncherMediaOsd &launcher_media_osd()
{
    static LauncherMediaOsd osd;
    return osd;
}
