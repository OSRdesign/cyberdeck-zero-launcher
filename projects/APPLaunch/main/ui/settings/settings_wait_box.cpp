/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates: the "working" message box (see settings_wait_box.hpp).
 */

#include "settings_wait_box.hpp"

#include "settings_wait_model.hpp"

namespace settings_wait {

namespace {

constexpr uint32_t kBorderRgb = 0x3A3A3A; /* the hold bar's track, the tiles' border */
constexpr uint32_t kTrackRgb = 0x3A3A3A;
constexpr uint32_t kFillRgb = 0x2D9CDB;   /* the accent, the progress bar's fill */
constexpr uint32_t kTextRgb = 0xFFFFFF;
constexpr uint32_t kMutedRgb = 0x8A929B;  /* the Apps page's grey */

lv_obj_t *add_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    if (font) lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN); /* shown with its first text (set_text) */
    return label;
}

} // namespace

WaitBox::WaitBox(lv_obj_t *parent, const BoxLook &look)
{
    box_ = lv_obj_create(parent);
    lv_obj_remove_style_all(box_);
    if (lv_obj_get_group(box_)) lv_group_remove_obj(box_);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(box_, look.x, look.y);
    lv_obj_set_width(box_, look.w);
    lv_obj_set_height(box_, LV_SIZE_CONTENT);
    lv_obj_set_style_border_color(box_, lv_color_hex(kBorderRgb), 0);
    lv_obj_set_style_border_width(box_, 2, 0);
    lv_obj_set_style_border_opa(box_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box_, look.gap, 0);
    lv_obj_set_style_bg_color(box_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(box_, look.pad, 0);
    lv_obj_set_style_pad_row(box_, look.gap, 0);
    lv_obj_set_flex_flow(box_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(box_, delete_cb, LV_EVENT_DELETE, this);

    title_ = add_label(box_, look.title_font, kTextRgb);
    step_ = add_label(box_, look.text_font, kTextRgb);
    bar_ = lv_bar_create(box_);
    if (lv_obj_get_group(bar_)) lv_group_remove_obj(bar_);
    lv_obj_remove_flag(bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(bar_, lv_pct(100));
    lv_obj_set_height(bar_, look.bar_h);
    lv_bar_set_range(bar_, 0, 100);
    lv_obj_set_style_radius(bar_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(kTrackRgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(kFillRgb), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar_, 0, LV_PART_MAIN);
    elapsed_ = add_label(box_, look.text_font, kMutedRgb);
    slow_ = add_label(box_, look.text_font, kMutedRgb);
    still_ = add_label(box_, look.text_font, kMutedRgb);
    cancel_ = add_label(box_, look.text_font, kMutedRgb);
    fit(look);
    lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
}

/* The block with every line at its longest must fit the room under it (expect_unclipped at 640x480): measured once
 * here, so the fonts never change during a wait; the grey lines take the small font first, then the step and the title. */
void WaitBox::fit(const BoxLook &look)
{
    if (look.max_h <= 0 || !look.small_font) return;
    lv_label_set_text(title_, "Preparing the updates");
    lv_label_set_text(step_, "Finding the packages that can be upgraded");
    lv_label_set_text(elapsed_, "00:00");
    lv_label_set_text(slow_, slow_line());
    lv_label_set_text(still_, still_line());
    lv_label_set_text(cancel_, "Esc cancels");
    for (lv_obj_t *label : {title_, step_, elapsed_, slow_, still_, cancel_}) lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    auto too_tall = [this, &look] {
        lv_obj_update_layout(box_);
        return lv_obj_get_height(box_) > look.max_h;
    };
    if (too_tall()) {
        for (lv_obj_t *label : {elapsed_, slow_, still_, cancel_}) lv_obj_set_style_text_font(label, look.small_font, 0);
        if (too_tall()) {
            lv_obj_set_style_text_font(step_, look.small_font, 0);
            if (too_tall()) lv_obj_set_style_text_font(title_, look.small_font, 0);
        }
    }
    for (lv_obj_t *label : {title_, step_, elapsed_, slow_, still_, cancel_}) {
        lv_label_set_text(label, "");
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

WaitBox::~WaitBox()
{
    if (box_) {
        lv_obj_remove_event_cb_with_user_data(box_, delete_cb, this);
        if (lv_obj_is_valid(box_)) lv_obj_delete(box_);
    }
    box_ = nullptr;
}

void WaitBox::delete_cb(lv_event_t *event)
{
    auto *self = static_cast<WaitBox *>(lv_event_get_user_data(event));
    if (self) self->box_ = nullptr; /* the parent went first */
}

void WaitBox::set_text(lv_obj_t *label, std::string &shown, const std::string &text)
{
    if (!label || shown == text) return;
    shown = text;
    lv_label_set_text(label, text.c_str());
    if (text.empty()) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}

void WaitBox::show(const BoxState &state)
{
    if (!box_) return;
    set_text(title_, title_text_, state.title);
    set_text(step_, step_text_, state.step);
    set_text(elapsed_, elapsed_text_, elapsed_text(state.elapsed_ms));
    set_text(slow_, slow_text_, state.slow ? slow_line() : "");
    set_text(still_, still_text_, still(state.since_change_ms) ? still_line() : "");
    set_text(cancel_, cancel_text_, state.cancellable ? "Esc cancels" : "");
    if (state.percent < 0) {
        int start = 0, end = 0;
        sweep(state.elapsed_ms - state.elapsed_ms % 250, start, end); /* 4 steps a second at most */
        if (bar_mode_ != -1) {
            lv_bar_set_mode(bar_, LV_BAR_MODE_RANGE);
            bar_mode_ = -1;
        }
        if (start != bar_start_ || end != bar_end_) {
            bar_start_ = start;
            bar_end_ = end;
            lv_bar_set_start_value(bar_, start, LV_ANIM_OFF);
            lv_bar_set_value(bar_, end, LV_ANIM_OFF);
        }
    } else if (bar_mode_ != state.percent) {
        if (bar_mode_ == -1 || bar_mode_ == -2) lv_bar_set_mode(bar_, LV_BAR_MODE_NORMAL);
        bar_mode_ = state.percent;
        bar_start_ = bar_end_ = -1;
        lv_bar_set_value(bar_, state.percent, LV_ANIM_OFF);
    }
    if (!visible_) {
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(box_);
        visible_ = true;
    }
}

void WaitBox::hide()
{
    if (!box_ || !visible_) return;
    lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
    visible_ = false;
}

} // namespace settings_wait
