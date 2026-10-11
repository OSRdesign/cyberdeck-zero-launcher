/*
 * SPDX-License-Identifier: MIT
 *
 * The read-only page widgets of the responsive shell (task 014 P3a; see cp0_ui_widgets.hpp): InfoTable (a Roller of
 * Info rows, layout in cp0_ui_info.c) and TextBlock (a long text in a scrolling column).
 */

#include "cp0_ui_widgets.hpp"

#include "cp0_ui_metrics_lvgl.h"

#include <algorithm>
#include <utility>

namespace cp0 {
namespace ui {
namespace {

constexpr uint32_t kText = 0xE0E0E0;      /* today's info pages' body text */
constexpr uint32_t kScrollbar = 0x555555;

RollerEvents info_events(std::function<void()> back)
{
    RollerEvents events;
    events.back = std::move(back); /* no activate: an info row has no action */
    return events;
}

} // namespace

// ------------------------------------------------------------------ InfoTable

InfoTable::InfoTable(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, std::function<void()> back)
    : roller_(parent, layout, info_events(std::move(back)))
{
}

RowSpec InfoTable::spec_of(const InfoItem &item)
{
    RowSpec spec;
    spec.kind = RowKind::Info;
    spec.label = item.key;
    spec.value = item.value;
    return spec;
}

void InfoTable::set_items(const std::vector<InfoItem> &items)
{
    bool same_keys = items.size() == items_.size();
    for (std::size_t i = 0; same_keys && i < items.size(); ++i) same_keys = items[i].key == items_[i].key;
    if (same_keys && !items.empty()) {
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].value != items_[i].value) roller_.update_row(static_cast<int>(i), spec_of(items[i]));
        items_ = items;
        return;
    }
    items_ = items;
    std::vector<RowSpec> specs;
    specs.reserve(items_.size());
    for (const InfoItem &item : items_) specs.push_back(spec_of(item));
    roller_.set_rows(specs, 0, false); /* a table stops at its ends */
}

void InfoTable::set_value(int index, const std::string &value)
{
    if (index < 0 || index >= count()) return;
    InfoItem &item = items_[static_cast<std::size_t>(index)];
    if (item.value == value) return;
    item.value = value;
    roller_.update_row(index, spec_of(item));
}

// ------------------------------------------------------------------ TextBlock

TextBlock::TextBlock(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, const std::string &text,
                     const lv_font_t *font, std::function<void()> back)
    : layout_(layout), back_(std::move(back))
{
    if (!font) font = cp0_ui_font(CP0_TEXT_BODY);
    box_ = lv_obj_create(parent);
    lv_obj_remove_style_all(box_);
    if (lv_obj_get_group(box_)) lv_group_remove_obj(box_); /* the page adds it to its own group */
    lv_obj_set_pos(box_, layout_.roller_x, layout_.body_y);
    lv_obj_set_size(box_, layout_.roller_w, layout_.body_h);
    lv_obj_set_style_pad_hor(box_, layout_.inset, 0);
    lv_obj_set_style_pad_ver(box_, layout_.gap, 0);
    lv_obj_set_scroll_dir(box_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(box_, std::max(3, layout_.gap / 3), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(box_, lv_color_hex(kScrollbar), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(box_, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_CLICK_FOCUSABLE); /* a drag must not move the group's focus */
    lv_obj_add_event_cb(box_, box_event_cb, LV_EVENT_KEY, this);

    label_ = lv_label_create(box_);
    lv_obj_set_width(label_, lv_pct(100));
    lv_label_set_long_mode(label_, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(label_, font, 0);
    lv_obj_set_style_text_color(label_, lv_color_hex(kText), 0);
    lv_obj_remove_flag(label_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(label_, text.c_str());
}

TextBlock::~TextBlock()
{
    if (box_ && lv_obj_is_valid(box_)) lv_obj_delete(box_);
    box_ = label_ = nullptr;
}

void TextBlock::set_hidden(bool hidden)
{
    if (!box_) return;
    if (hidden) lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(box_, LV_OBJ_FLAG_HIDDEN);
}

void TextBlock::box_event_cb(lv_event_t *event)
{
    auto *self = static_cast<TextBlock *>(lv_event_get_user_data(event));
    if (!self || !self->box_) return;
    lv_event_stop_processing(event);
    self->on_key(lv_event_get_key(event));
}

/* LVGL's positive scroll delta moves the content down (earlier text): the sign is the key's opposite. */
void TextBlock::on_key(uint32_t key)
{
    const int step = layout_.row_h;
    const int page = std::max(step, lv_obj_get_height(box_) - step);
    switch (key) {
    case LV_KEY_UP: lv_obj_scroll_by_bounded(box_, 0, step, LV_ANIM_ON); break;
    case LV_KEY_DOWN: lv_obj_scroll_by_bounded(box_, 0, -step, LV_ANIM_ON); break;
    case LV_KEY_PREV: lv_obj_scroll_by_bounded(box_, 0, page, LV_ANIM_ON); break;
    case LV_KEY_NEXT: lv_obj_scroll_by_bounded(box_, 0, -page, LV_ANIM_ON); break;
    case LV_KEY_HOME: lv_obj_scroll_to_y(box_, 0, LV_ANIM_ON); break;
    case LV_KEY_END:
        lv_obj_scroll_to_y(box_, lv_obj_get_scroll_y(box_) + lv_obj_get_scroll_bottom(box_), LV_ANIM_ON);
        break;
    case LV_KEY_ESC:
    case LV_KEY_LEFT:
        if (back_) back_();
        break;
    default:
        break;
    }
}

} // namespace ui
} // namespace cp0
