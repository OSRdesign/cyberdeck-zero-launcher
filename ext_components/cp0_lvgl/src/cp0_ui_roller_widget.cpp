/*
 * SPDX-License-Identifier: MIT
 *
 * The roller list of the responsive shell (see cp0_ui_widgets.hpp; maths in cp0_ui_roller.c).
 *
 * Objects: body (the roller's rectangle, clips) > highlight bar (fixed, centre row), list (scrolls; one flex column
 * of rows padded by edge_pad above and below so every row can reach the centre; snap: centre), chevrons (fixed).
 * Row i is centred at scroll offset i * row_h. Only the rows near the centre are restyled when it moves.
 */

#include "cp0_ui_widgets.hpp"

#include "cp0_font_service.hpp"
#include "cp0_ui_metrics_lvgl.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace cp0 {
namespace ui {
namespace {

constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kValue = 0x00CC66;  /* the green of today's Settings hints ("ok:enter") */
constexpr uint32_t kCheck = 0x2D9CDB;  /* the home grid's selection blue */
constexpr uint32_t kTrack = 0x3A3A3A;
constexpr uint32_t kKnob = 0xFFFFFF;

int text_width(const char *text, const lv_font_t *font)
{
    if (!text || !font) return 0;
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

/* Widgets whose class joins the default group must not: the roller's list takes the keys. */
void leave_group(lv_obj_t *obj)
{
    if (obj && lv_obj_get_group(obj)) lv_group_remove_obj(obj);
}

/* controls follow the centre row's text size */
int switch_h(const cp0_ui_roller_page_t &l) { return std::max(16, l.text_px); }
int switch_w(const cp0_ui_roller_page_t &l) { return switch_h(l) * 9 / 5; }
int slider_w(const cp0_ui_roller_page_t &l) { return l.roller_w / 3; }
int slider_h(const cp0_ui_roller_page_t &l) { return std::max(6, l.text_px / 4); }

/* Row text: Montserrat (the home grid's face), bold on the centre row; FreeType so any name renders */
const lv_font_t *row_font(int px, bool bold)
{
    return cp0_fonts().get(bold ? "Montserrat-Bold.ttf" : "Montserrat-Medium.ttf", static_cast<uint16_t>(px));
}

} // namespace

Roller::Roller(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, RollerEvents events)
    : layout_(layout), events_(std::move(events))
{
    body_ = plain(parent);
    lv_obj_set_size(body_, layout_.roller_w, layout_.body_h);
    lv_obj_set_pos(body_, layout_.roller_x, layout_.body_y);
    lv_obj_add_event_cb(body_, body_delete_cb, LV_EVENT_DELETE, this);

    bar_ = plain(body_);
    lv_obj_set_size(bar_, layout_.roller_w, layout_.row_h);
    lv_obj_set_pos(bar_, 0, layout_.edge_pad);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(CP0_UI_ROLLER_BAR_RGB), 0);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, 0);

    list_ = lv_obj_create(body_);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, layout_.roller_w, layout_.list_h);
    lv_obj_set_pos(list_, 0, 0);
    lv_obj_set_style_pad_top(list_, layout_.edge_pad, 0);
    lv_obj_set_style_pad_bottom(list_, layout_.edge_pad, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(list_, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(list_, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
    lv_obj_remove_flag(list_, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(list_, list_event_cb, LV_EVENT_ALL, this);

    // chevrons: two strokes, chevron_px wide (0.55 x the row), half as high, centred in the roller column
    const int cw = std::max(8, layout_.chevron_px);
    const int stroke = std::max(3, cw / 8);
    const int ch = cw / 2;
    up_points_[0] = {static_cast<lv_value_precise_t>(stroke), static_cast<lv_value_precise_t>(ch + stroke)};
    up_points_[1] = {static_cast<lv_value_precise_t>(cw / 2 + stroke), static_cast<lv_value_precise_t>(stroke)};
    up_points_[2] = {static_cast<lv_value_precise_t>(cw + stroke), static_cast<lv_value_precise_t>(ch + stroke)};
    for (int i = 0; i < 3; ++i) {
        down_points_[i].x = up_points_[i].x;
        down_points_[i].y = static_cast<lv_value_precise_t>(ch + 2 * stroke) - up_points_[i].y;
    }
    up_ = lv_line_create(body_);
    lv_line_set_points(up_, up_points_, 3);
    down_ = lv_line_create(body_);
    lv_line_set_points(down_, down_points_, 3);
    for (lv_obj_t *arrow : {up_, down_}) {
        lv_obj_set_size(arrow, cw + 2 * stroke, ch + 2 * stroke);
        lv_obj_set_style_line_width(arrow, stroke, 0);
        lv_obj_set_style_line_color(arrow, lv_color_hex(CP0_UI_ROLLER_CHEVRON_RGB), 0);
        lv_obj_set_style_line_rounded(arrow, true, 0);
        lv_obj_remove_flag(arrow, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(arrow, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(up_, LV_ALIGN_TOP_MID, 0, layout_.gap / 2);
    lv_obj_align(down_, LV_ALIGN_BOTTOM_MID, 0, -layout_.gap / 2);
}

Roller::~Roller()
{
    if (body_) {
        lv_obj_remove_event_cb_with_user_data(body_, body_delete_cb, this);
        lv_obj_delete(body_);
    }
}

void Roller::body_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<Roller *>(lv_event_get_user_data(event));
    if (!self) return;
    self->body_ = self->bar_ = self->list_ = self->up_ = self->down_ = nullptr;
    self->rows_.clear();
}

void Roller::set_rows(const std::vector<RowSpec> &rows, int selected, bool wrap)
{
    if (!list_) return;
    wrap_ = wrap;
    target_ = -1;
    // Deleting the rows shrinks the list and LVGL readjusts its scroll (LV_EVENT_SCROLL): the old rows must be gone
    // from rows_ first, and scroll events are ignored until the new rows are built and styled.
    rebuilding_ = true;
    rows_.clear();
    lv_anim_delete(list_, nullptr);
    lv_obj_clean(list_);
    rows_.reserve(rows.size());
    for (const RowSpec &spec : rows) {
        rows_.push_back(Row{});
        rows_.back().spec = spec;
    }
    for (int i = 0; i < count(); ++i) build_row(i);
    selected_ = rows_.empty() ? 0 : std::clamp(selected, 0, count() - 1);
    lv_obj_update_layout(list_);
    for (int i = 0; i < count(); ++i) style_row(i, true);
    rebuilding_ = false;
    lv_obj_scroll_to_y(list_, selected_ * layout_.row_h, LV_ANIM_OFF);
    update_chevrons();
}

void Roller::build_row(int index)
{
    Row &row = rows_[static_cast<size_t>(index)];
    row.obj = lv_obj_create(list_);
    lv_obj_remove_style_all(row.obj);
    lv_obj_set_size(row.obj, layout_.roller_w, layout_.row_h);
    lv_obj_remove_flag(row.obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(row.obj, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
    lv_obj_add_event_cb(row.obj, row_event_cb, LV_EVENT_CLICKED, this);

    row.label = lv_label_create(row.obj);
    lv_label_set_long_mode(row.label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(row.label, lv_color_hex(kText), 0);

    switch (row.spec.kind) {
    case RowKind::Nav:
        row.value = lv_label_create(row.obj);
        lv_obj_set_style_text_color(row.value, lv_color_hex(kValue), 0);
        break;
    case RowKind::Choice:
        row.value = lv_label_create(row.obj);
        lv_label_set_text(row.value, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(row.value, lv_color_hex(kCheck), 0);
        break;
    case RowKind::Toggle:
        row.control = lv_switch_create(row.obj);
        leave_group(row.control);
        lv_obj_remove_flag(row.control, LV_OBJ_FLAG_CLICKABLE); /* a tap acts on the row, like Enter */
        lv_obj_set_size(row.control, switch_w(layout_), switch_h(layout_));
        lv_obj_set_style_bg_color(row.control, lv_color_hex(kTrack), LV_PART_MAIN);
        lv_obj_set_style_bg_color(row.control, lv_color_hex(kCheck), static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(row.control, lv_color_hex(kKnob), LV_PART_KNOB);
        break;
    case RowKind::Slider:
        row.control = lv_slider_create(row.obj);
        leave_group(row.control);
        lv_obj_set_size(row.control, slider_w(layout_), slider_h(layout_));
        lv_obj_set_style_bg_color(row.control, lv_color_hex(kTrack), LV_PART_MAIN);
        lv_obj_set_style_bg_color(row.control, lv_color_hex(kCheck), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(row.control, lv_color_hex(kKnob), LV_PART_KNOB);
        lv_obj_set_style_pad_all(row.control, std::max(4, layout_.text_px / 4), LV_PART_KNOB);
        lv_obj_add_event_cb(row.control, slider_event_cb, LV_EVENT_ALL, this);
        lv_obj_set_user_data(row.control, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
        row.value = lv_label_create(row.obj);
        lv_obj_set_style_text_color(row.value, lv_color_hex(kValue), 0);
        break;
    }
    fill_row(row);
}

void Roller::fill_row(Row &row)
{
    lv_label_set_text(row.label, row.spec.label.c_str());
    switch (row.spec.kind) {
    case RowKind::Nav:
    case RowKind::Slider:
        lv_label_set_text(row.value, row.spec.value.c_str());
        break;
    case RowKind::Choice:
        if (row.spec.on) lv_obj_remove_flag(row.value, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(row.value, LV_OBJ_FLAG_HIDDEN);
        break;
    case RowKind::Toggle:
        if (row.spec.on) lv_obj_add_state(row.control, LV_STATE_CHECKED);
        else lv_obj_remove_state(row.control, LV_STATE_CHECKED);
        break;
    }
    if (row.spec.kind == RowKind::Slider) {
        lv_slider_set_range(row.control, row.spec.slider_min, row.spec.slider_max);
        lv_slider_set_value(row.control, row.spec.slider_value, LV_ANIM_OFF);
    }
}

void Roller::update_row(int index, const RowSpec &spec)
{
    if (index < 0 || index >= count()) return;
    Row &row = rows_[static_cast<size_t>(index)];
    if (spec.kind != row.spec.kind) return;
    row.spec = spec;
    fill_row(row);
    style_row(index, true);
}

/* Text 8 % smaller (at least 70 %) and 20 % fainter (at least 35 %) per row away from the centre; the centre row is
 * bold; values are 0.9 x the label size. */
void Roller::style_row(int index, bool force)
{
    Row &row = rows_[static_cast<size_t>(index)];
    const int distance = std::abs(index - selected_);
    if (!force && row.distance == distance) return;
    row.distance = distance;

    const int px = cp0_ui_roller_text_px(layout_.text_px, distance);
    const int value_px = cp0_ui_roller_text_px(layout_.value_px, distance);
    const lv_font_t *font = row_font(px, distance == 0);
    const lv_font_t *value_font = row_font(value_px, false);
    lv_obj_set_style_opa(row.obj, static_cast<lv_opa_t>(cp0_ui_roller_opa(distance)), 0);
    lv_obj_set_style_text_font(row.label, font, 0);

    const int inset = layout_.inset;
    int right = 0; /* room taken at the right of the row */
    switch (row.spec.kind) {
    case RowKind::Nav:
        lv_obj_set_style_text_font(row.value, value_font, 0);
        right = text_width(row.spec.value.c_str(), value_font);
        lv_obj_align(row.value, LV_ALIGN_RIGHT_MID, -inset, 0);
        break;
    case RowKind::Choice: {
        const lv_font_t *symbol = cp0_ui_font_px(value_px);
        lv_obj_set_style_text_font(row.value, symbol, 0);
        right = text_width(LV_SYMBOL_OK, symbol);
        lv_obj_align(row.value, LV_ALIGN_RIGHT_MID, -inset, 0);
        break;
    }
    case RowKind::Toggle:
        right = switch_w(layout_);
        lv_obj_align(row.control, LV_ALIGN_RIGHT_MID, -inset, 0);
        break;
    case RowKind::Slider: {
        const int slot = text_width("100%", row_font(layout_.value_px, false));
        lv_obj_set_style_text_font(row.value, value_font, 0);
        lv_obj_align(row.value, LV_ALIGN_RIGHT_MID, -inset, 0);
        lv_obj_align(row.control, LV_ALIGN_RIGHT_MID, -(inset + slot + layout_.gap), 0);
        right = slider_w(layout_) + layout_.gap + slot;
        break;
    }
    }
    const int label_w = layout_.roller_w - 2 * inset - (right > 0 ? right + layout_.gap : 0);
    lv_obj_set_width(row.label, std::max(0, label_w));
    lv_obj_set_height(row.label, lv_font_get_line_height(font)); /* one line: dots, never a wrap */
    lv_obj_align(row.label, LV_ALIGN_LEFT_MID, inset, 0);
}

/* Restyle the rows near the old and the new centre (the others are off screen and keep their faint style). */
void Roller::restyle_around(int previous)
{
    if (rows_.empty()) return;
    const int reach = layout_.body_h / std::max(1, layout_.row_h) / 2 + 2;
    const int first = std::max(0, std::min(previous, selected_) - reach);
    const int last = std::min(count() - 1, std::max(previous, selected_) + reach);
    for (int i = first; i <= last; ++i) style_row(i, false);
}

void Roller::update_chevrons()
{
    if (!up_ || !down_) return;
    if (selected_ > 0) lv_obj_remove_flag(up_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(up_, LV_OBJ_FLAG_HIDDEN);
    if (selected_ + 1 < count()) lv_obj_remove_flag(down_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(down_, LV_OBJ_FLAG_HIDDEN);
}

void Roller::set_selected(int index)
{
    if (rows_.empty() || index == selected_) return;
    const int previous = selected_;
    selected_ = std::clamp(index, 0, count() - 1);
    restyle_around(previous);
    update_chevrons();
}

void Roller::select(int index, bool animated)
{
    if (!list_ || rows_.empty()) return;
    index = std::clamp(index, 0, count() - 1);
    set_selected(index);
    target_ = animated ? index : -1;
    lv_obj_scroll_to_y(list_, index * layout_.row_h, animated ? LV_ANIM_ON : LV_ANIM_OFF);
    if (!animated) target_ = -1;
}

void Roller::slide_by(int index, int steps)
{
    if (index < 0 || index >= count()) return;
    const RowSpec &spec = rows_[static_cast<size_t>(index)].spec;
    const int value = std::clamp(spec.slider_value + steps * std::max(1, spec.slider_step), spec.slider_min,
                                 spec.slider_max);
    if (value != spec.slider_value && events_.slide) events_.slide(index, value);
}

void Roller::on_key(uint32_t key)
{
    const bool on_slider = !rows_.empty() && rows_[static_cast<size_t>(selected_)].spec.kind == RowKind::Slider;
    switch (key) {
    case LV_KEY_UP:
    case LV_KEY_DOWN: {
        if (rows_.empty()) return;
        const int next = cp0_ui_roller_step(selected_, key == LV_KEY_UP ? -1 : 1, count(), wrap_);
        select(next, std::abs(next - selected_) == 1); /* a wrap jumps without the long scroll */
        break;
    }
    case LV_KEY_LEFT:
        if (on_slider) slide_by(selected_, -1);
        else if (events_.back) events_.back();
        break;
    case LV_KEY_RIGHT:
        if (on_slider) slide_by(selected_, 1);
        else if (!rows_.empty() && events_.activate) events_.activate(selected_);
        break;
    case LV_KEY_ENTER:
        if (!rows_.empty() && events_.activate) events_.activate(selected_);
        break;
    case LV_KEY_ESC:
        if (events_.back) events_.back();
        break;
    default:
        break;
    }
}

/* A finger drag moves the centre live; a programmatic scroll (select) keeps its target until it arrives. */
void Roller::on_scroll(lv_event_code_t code)
{
    if (!list_ || rows_.empty() || rebuilding_) return;
    const int centre = cp0_ui_roller_centre(lv_obj_get_scroll_y(list_), layout_.row_h, count());
    if (code == LV_EVENT_SCROLL_END && target_ >= 0) {
        if (centre == target_) target_ = -1;
        return;
    }
    if (target_ < 0) set_selected(centre);
}

void Roller::on_row_clicked(int index)
{
    if (index < 0 || index >= count()) return;
    if (index == selected_ && target_ < 0) {
        if (events_.activate) events_.activate(index); /* may rebuild the rows: nothing touched after it */
        return;
    }
    select(index, true);
}

void Roller::list_event_cb(lv_event_t *event)
{
    auto *self = static_cast<Roller *>(lv_event_get_user_data(event));
    if (!self) return;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_KEY:
        lv_event_stop_processing(event);
        self->on_key(lv_event_get_key(event));
        break;
    case LV_EVENT_SCROLL_BEGIN:
        if (!lv_event_get_param(event)) self->target_ = -1; /* a finger took over */
        break;
    case LV_EVENT_SCROLL:
    case LV_EVENT_SCROLL_END:
        self->on_scroll(lv_event_get_code(event));
        break;
    default:
        break;
    }
}

void Roller::row_event_cb(lv_event_t *event)
{
    auto *self = static_cast<Roller *>(lv_event_get_user_data(event));
    auto *row = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (!self || !row) return;
    self->on_row_clicked(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(row))));
}

void Roller::slider_event_cb(lv_event_t *event)
{
    auto *self = static_cast<Roller *>(lv_event_get_user_data(event));
    auto *slider = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (!self || !slider) return;
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(slider)));
    if (index < 0 || index >= self->count()) return;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        if (index != self->selected_) self->select(index, true);
        break;
    case LV_EVENT_RELEASED: {
        const int value = lv_slider_get_value(slider);
        if (value != self->rows_[static_cast<size_t>(index)].spec.slider_value && self->events_.slide)
            self->events_.slide(index, value);
        break;
    }
    default:
        break;
    }
}

} // namespace ui
} // namespace cp0
