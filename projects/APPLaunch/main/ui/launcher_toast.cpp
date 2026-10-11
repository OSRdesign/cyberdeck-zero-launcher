/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launcher_toast.h"

#include "ui.h"

#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_overlay.h"

#include <cstring>

namespace {

constexpr uint32_t kShowDurationMs = 1500;
constexpr uint32_t kBackgroundColor = 0x1F3A5F;
constexpr uint32_t kTextColor = 0xFFFFFF;

// The size comes from cp0_ui_overlay.h (task 014 P4): a display at most 320 px wide (the compat window, stock
// CardputerZero, SDL) keeps today's 280x22 / 38 toast; the native display gets the pinned DECK / COMPACT table (the
// old width / 320 scaling, byte-identical there) or a size from the tokens.
cp0_ui_toast_t spec_for(const lv_obj_t *object, int lines, bool *native)
{
    lv_display_t *display = object ? lv_obj_get_display(object) : lv_display_get_default();
    const int width = display ? static_cast<int>(lv_display_get_horizontal_resolution(display))
                              : CP0_UI_OVERLAY_COMPAT_MAX_W;
    cp0_ui_toast_t spec;
    cp0_ui_overlay_toast(cp0_ui_metrics_get(), width, lines, &spec);
    if (native) *native = !cp0_ui_overlay_is_compat_display(width);
    return spec;
}

} // namespace

bool LauncherToast::ensure_created() noexcept
{
    try {
    if (container_ && label_) return true;
    if (container_) lv_obj_delete(container_);

    lv_obj_t *parent = lv_layer_top();
    if (!parent)
        return false;

    container_ = lv_obj_create(parent);
    if (!container_)
        return false;
    const cp0_ui_toast_t spec = spec_for(parent, 1, nullptr);
    lv_obj_add_event_cb(container_, container_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, spec.w, spec.h);
    lv_obj_align(container_, LV_ALIGN_TOP_MID, 0, spec.top);
    lv_obj_set_style_bg_color(container_, lv_color_hex(kBackgroundColor), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_80, 0);
    lv_obj_set_style_radius(container_, spec.radius, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_shadow_width(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_IGNORE_LAYOUT);

    label_ = lv_label_create(container_);
    if (!label_) {
        lv_obj_delete(container_);
        return false;
    }
    lv_obj_add_event_cb(label_, label_delete_cb, LV_EVENT_DELETE, this);
    lv_obj_set_style_text_color(label_, lv_color_hex(kTextColor), 0);
    lv_obj_set_style_text_font(
        label_, launcher_fonts().get("AlibabaPuHuiTi-3-55-Regular.ttf", spec.font_px,
                                    LV_FREETYPE_FONT_STYLE_BOLD), 0);
    lv_obj_set_width(label_, spec.label_w);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    return true;
    } catch (...) {
        if (container_) lv_obj_delete(container_);
        container_ = nullptr;
        label_ = nullptr;
        return false;
    }
}

void LauncherToast::show(const char *text) noexcept
{
    try {
    // Follow the active display: the toast lives on a display's top layer.
    if (container_ && lv_obj_get_display(container_) != lv_display_get_default())
        lv_obj_delete(container_); // the delete callback clears our handles and timer
    if (!ensure_created())
        return;

    // Re-apply the size on every show: it follows the display's width (and the layout service), not the width the
    // toast was created at.
    bool native = false;
    const bool two_lines = text && std::strchr(text, '\n');
    const cp0_ui_toast_t spec = spec_for(container_, two_lines ? 2 : 1, &native);
    lv_label_set_text(label_, text ? text : "");
    lv_obj_set_size(container_, spec.w, spec.h);
    lv_obj_set_style_radius(container_, spec.radius, 0);
    lv_obj_set_style_text_font(
        label_, launcher_fonts().get("AlibabaPuHuiTi-3-55-Regular.ttf", spec.font_px,
                                    LV_FREETYPE_FONT_STYLE_BOLD), 0);
    lv_obj_set_width(label_, spec.label_w);
    if (native) {
        // a text that wraps to more lines than the box was sized for grows the box (the compat toast never did)
        lv_obj_update_layout(container_);
        const int label_h = static_cast<int>(lv_obj_get_height(label_));
        if (label_h > spec.h)
            lv_obj_set_height(container_, label_h + 2 * cp0_ui_tok(CP0_TOK_SPACE_S));
    }
    lv_obj_center(label_);
    lv_obj_align(container_, LV_ALIGN_TOP_MID, 0, spec.top);
    lv_obj_move_foreground(container_);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    if (!hide_timer_)
        hide_timer_ = lv_timer_create(hide_timer_cb, kShowDurationMs, this);
    if (!hide_timer_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_timer_set_period(hide_timer_, kShowDurationMs);
    lv_timer_reset(hide_timer_);
    lv_timer_resume(hide_timer_);
    } catch (...) {
        hide();
    }
}

void LauncherToast::show_persistent(const char *text) noexcept
{
    show(text);
    if (hide_timer_) lv_timer_pause(hide_timer_);
}

void LauncherToast::hide()
{
    if (container_)
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    if (hide_timer_)
        lv_timer_pause(hide_timer_);
}

void LauncherToast::shutdown()
{
    if (container_)
        lv_obj_delete(container_);
    else if (hide_timer_) {
        lv_timer_delete(hide_timer_);
        hide_timer_ = nullptr;
    }
    container_ = nullptr;
    label_ = nullptr;
}

void LauncherToast::hide_timer_cb(lv_timer_t *timer) noexcept
{
    try {
    auto *toast = static_cast<LauncherToast *>(lv_timer_get_user_data(timer));
    if (toast && toast->hide_timer_ == timer)
        toast->hide();
    } catch (...) {
    }
}

void LauncherToast::container_delete_cb(lv_event_t *event) noexcept
{
    try {
        auto *toast = static_cast<LauncherToast *>(lv_event_get_user_data(event));
        if (!toast || lv_event_get_code(event) != LV_EVENT_DELETE ||
            lv_event_get_target(event) != lv_event_get_current_target(event) ||
            lv_event_get_target(event) != toast->container_)
            return;
        toast->container_ = nullptr;
        toast->label_ = nullptr;
        if (toast->hide_timer_) {
            lv_timer_delete(toast->hide_timer_);
            toast->hide_timer_ = nullptr;
        }
    } catch (...) {
    }
}

void LauncherToast::label_delete_cb(lv_event_t *event) noexcept
{
    try {
        if (lv_event_get_code(event) != LV_EVENT_DELETE ||
            lv_event_get_target(event) != lv_event_get_current_target(event)) return;
        auto *toast = static_cast<LauncherToast *>(lv_event_get_user_data(event));
        if (toast && lv_event_get_target(event) == toast->label_) {
            toast->label_ = nullptr;
            toast->hide();
        }
    } catch (...) {
    }
}

LauncherToast &launcher_toast()
{
    static LauncherToast toast;
    return toast;
}
