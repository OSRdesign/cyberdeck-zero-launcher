/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * ui_loading.cpp
 *
 * Transient "Loading..." overlay for app launches.
 *
 * Created lazily on first ui_loading::show() call as a child of
 * lv_layer_top() so it floats above any screen. Never deleted;
 * visibility is toggled via LV_OBJ_FLAG_HIDDEN. An lv_spinner sits
 * to the left of a short text label, both centered on-screen inside
 * a dark semi-transparent pill — matching the look of the hint toast
 * in ui_global_hint.cpp.
 *
 * Design notes:
 *   - Because internal page construction happens synchronously on the
 *     LVGL thread, callers should follow ui_loading::show() with
 *     lv_refr_now(NULL) to force the overlay to actually paint BEFORE
 *     the slow work begins. Otherwise LVGL would only render on the
 *     next frame, which is after the freeze.
 *   - For external (forked) apps, lv_refr_now() is already performed
 *     by launch_Exec() before the cp0 process callback - the
 *     overlay stays on-screen while the child owns the framebuffer,
 *     which is exactly the desired "something is happening" feedback.
 */

#include "ui_loading.h"
#include "ui.h"
#include "lvgl/lvgl.h"
#include "model/global_overlay_model.hpp"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_overlay.h"

#define LOADING_BG_COLOR    0x1F3A5F
#define LOADING_BG_OPA      LV_OPA_80
#define LOADING_TEXT_COLOR  0xFFFFFF

static lv_obj_t *s_loading_obj     = NULL;
static lv_obj_t *s_loading_spinner = NULL;
static lv_obj_t *s_loading_label   = NULL;
static GlobalOverlayModel s_loading_state;

static void loading_deleted_cb(lv_event_t *event) noexcept
{
    try {
        if (lv_event_get_code(event) != LV_EVENT_DELETE ||
            lv_event_get_target(event) != lv_event_get_current_target(event) ||
            lv_event_get_target(event) != s_loading_obj)
            return;
        s_loading_obj = NULL;
        s_loading_spinner = NULL;
        s_loading_label = NULL;
        s_loading_state.object_deleted();
    } catch (...) {
        s_loading_obj = NULL;
        s_loading_spinner = NULL;
        s_loading_label = NULL;
        s_loading_state.object_deleted();
    }
}

static void loading_child_deleted_cb(lv_event_t *event) noexcept
{
    try {
        if (lv_event_get_code(event) != LV_EVENT_DELETE ||
            lv_event_get_target(event) != lv_event_get_current_target(event)) return;
        lv_obj_t *deleted = static_cast<lv_obj_t *>(lv_event_get_target(event));
        if (deleted != s_loading_spinner && deleted != s_loading_label) return;
        if (deleted == s_loading_spinner) s_loading_spinner = NULL;
        if (deleted == s_loading_label) s_loading_label = NULL;
        if (s_loading_obj != NULL)
            lv_obj_add_flag(s_loading_obj, LV_OBJ_FLAG_HIDDEN);
        s_loading_state.object_deleted();
    } catch (...) {
        s_loading_spinner = NULL;
        s_loading_label = NULL;
        s_loading_state.object_deleted();
    }
}

/* Sizes from cp0_ui_overlay.h (task 014 P4): today's 200x40 pill on a display at most 320 px wide, the pinned DECK /
 * COMPACT scale or the body font's scale on the native display. Applied on every show, so the pill follows the
 * display and its size. */
static void apply_loading_layout(void) noexcept
{
    lv_display_t *display = lv_obj_get_display(s_loading_obj);
    const int dw = display ? (int)lv_display_get_horizontal_resolution(display) : CP0_UI_OVERLAY_COMPAT_MAX_W;
    const int dh = display ? (int)lv_display_get_vertical_resolution(display) : CP0_UI_COMPAT_H;
    cp0_ui_loading_t spec;
    cp0_ui_overlay_loading(cp0_ui_metrics_get(), dw, dh, &spec);
    lv_obj_set_size(s_loading_obj, spec.w, spec.h);
    lv_obj_center(s_loading_obj);
    lv_obj_set_style_radius(s_loading_obj, spec.radius, 0);
    lv_obj_set_style_pad_all(s_loading_obj, spec.pad, 0);
    lv_obj_set_style_pad_column(s_loading_obj, spec.gap, 0);
    if (s_loading_spinner != NULL) {
        lv_obj_set_size(s_loading_spinner, spec.spinner, spec.spinner);
        lv_obj_set_style_arc_width(s_loading_spinner, spec.arc_w, LV_PART_MAIN);
        lv_obj_set_style_arc_width(s_loading_spinner, spec.arc_w, LV_PART_INDICATOR);
    }
    if (s_loading_label != NULL)
        lv_obj_set_style_text_font(
            s_loading_label,
            launcher_fonts().get("AlibabaPuHuiTi-3-55-Regular.ttf", spec.font_px, LV_FREETYPE_FONT_STYLE_NORMAL), 0);
}

static void ensure_loading_created(void) noexcept
{
    try {
    if (s_loading_obj != NULL && s_loading_spinner != NULL &&
        s_loading_label != NULL) return;
    if (s_loading_obj != NULL) lv_obj_delete(s_loading_obj);

    lv_obj_t *parent = lv_layer_top();
    if (parent == NULL) return;

    s_loading_obj = lv_obj_create(parent);
    if (s_loading_obj == NULL) return;
    lv_obj_add_event_cb(s_loading_obj, loading_deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(s_loading_obj);
    lv_obj_center(s_loading_obj);

    lv_obj_set_style_bg_color(s_loading_obj, lv_color_hex(LOADING_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(s_loading_obj, LOADING_BG_OPA, 0);
    lv_obj_set_style_border_width(s_loading_obj, 0, 0);
    lv_obj_set_style_shadow_width(s_loading_obj, 0, 0);

    /* Purely visual — never steal focus or clicks. */
    lv_obj_clear_flag(s_loading_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_loading_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_loading_obj, LV_OBJ_FLAG_IGNORE_LAYOUT);

    /* Horizontal layout: spinner on the left, label to its right. */
    lv_obj_set_flex_flow(s_loading_obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_loading_obj,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* Spinner — 1s arc, 60 degrees long. */
    s_loading_spinner = lv_spinner_create(s_loading_obj);
    if (s_loading_spinner == NULL) {
        lv_obj_delete(s_loading_obj);
        return;
    }
    lv_obj_add_event_cb(
        s_loading_spinner, loading_child_deleted_cb, LV_EVENT_DELETE, NULL);
    lv_spinner_set_anim_params(s_loading_spinner, 1000, 60);
    lv_obj_set_style_arc_color(s_loading_spinner,
                               lv_color_hex(0x3B6FA0), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_loading_spinner,
                               lv_color_hex(LOADING_TEXT_COLOR),
                               LV_PART_INDICATOR);

    s_loading_label = lv_label_create(s_loading_obj);
    if (s_loading_label == NULL) {
        lv_obj_delete(s_loading_obj);
        return;
    }
    lv_obj_add_event_cb(
        s_loading_label, loading_child_deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_style_text_color(s_loading_label,
                                lv_color_hex(LOADING_TEXT_COLOR), 0);
    lv_label_set_text(s_loading_label, "");
    apply_loading_layout();

    lv_obj_add_flag(s_loading_obj, LV_OBJ_FLAG_HIDDEN);
    s_loading_state.mark_created();
    } catch (...) {
        if (s_loading_obj != NULL)
            lv_obj_delete(s_loading_obj);
        s_loading_obj = NULL;
        s_loading_spinner = NULL;
        s_loading_label = NULL;
        s_loading_state.object_deleted();
    }
}

namespace ui_loading {

void show(const char *label)
{
    try {
    /* Follow the default display: one made on another display (the compat window while a stock page ran) is
     * replaced by one on the display that is default now (its delete callback clears the handles). */
    if (s_loading_obj != NULL && lv_obj_get_display(s_loading_obj) != lv_display_get_default())
        lv_obj_delete(s_loading_obj);
    if (s_loading_state.request_show())
        ensure_loading_created();
    if (s_loading_obj == NULL || s_loading_label == NULL) return;
    apply_loading_layout();

    lv_label_set_text(s_loading_label,
                      (label && label[0]) ? label : "Loading...");
    /* Make sure the overlay stays on top of any other lv_layer_top
     * children (e.g. the ui_global_hint toast). */
    lv_obj_move_foreground(s_loading_obj);
    lv_obj_clear_flag(s_loading_obj, LV_OBJ_FLAG_HIDDEN);
    } catch (...) {
        if (s_loading_obj != NULL)
            lv_obj_add_flag(s_loading_obj, LV_OBJ_FLAG_HIDDEN);
        s_loading_state.hide();
    }
}

void hide()
{
    s_loading_state.hide();
    if (s_loading_obj == NULL) return;
    lv_obj_add_flag(s_loading_obj, LV_OBJ_FLAG_HIDDEN);
}

void shutdown()
{
    s_loading_state.hide();
    if (s_loading_obj != NULL)
        lv_obj_delete(s_loading_obj);
    s_loading_obj = NULL;
    s_loading_spinner = NULL;
    s_loading_label = NULL;
    s_loading_state.object_deleted();
}

} // namespace ui_loading
