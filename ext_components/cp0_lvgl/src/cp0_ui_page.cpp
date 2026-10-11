/*
 * SPDX-License-Identifier: MIT
 *
 * Page scaffold of the responsive shell (see cp0_ui_widgets.hpp): header with the home grid's title, a status
 * line, the on-screen Esc button, and room for the shared status strip; the area under it is the page's.
 */

#include "cp0_ui_widgets.hpp"

#include "cp0_ui_metrics_lvgl.h"

#include <algorithm>
#include <utility>

namespace cp0 {
namespace ui {
namespace {

constexpr uint32_t kGold = 0xF0B400;         /* the home grid's title */
constexpr uint32_t kKeyBg = 0x2A2A2A;        /* the stock-app toolbar keys ... */
constexpr uint32_t kKeyPressed = 0x2D9CDB;   /* ... and their pressed colour */
constexpr uint32_t kCaption = 0x66CC88;      /* today's value pages: status line ... */
constexpr uint32_t kCaptionError = 0xFF6666; /* ... and its error colour */

int text_width(const char *text, const lv_font_t *font)
{
    if (!text || !font) return 0;
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

} // namespace

Page::Page(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, TitleFactory make_title, std::function<void()> on_back)
    : layout_(layout), on_back_(std::move(on_back))
{
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, layout_.w, layout_.h);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root_, root_delete_cb, LV_EVENT_DELETE, this);

    header_ = lv_obj_create(root_);
    lv_obj_remove_style_all(header_);
    lv_obj_set_size(header_, layout_.w, layout_.header_h);
    lv_obj_set_pos(header_, 0, 0);
    lv_obj_remove_flag(header_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(header_, LV_OBJ_FLAG_CLICKABLE);

    if (make_title) title_ = make_title(header_, "");
    if (!title_) { /* same place, size and colour as the home grid's title */
        title_ = lv_label_create(header_);
        lv_label_set_text(title_, "");
        lv_obj_set_style_text_font(title_, cp0_ui_font_px(layout_.title_px), 0);
        lv_obj_set_style_text_color(title_, lv_color_hex(kGold), 0);
        lv_obj_align(title_, LV_ALIGN_TOP_LEFT, layout_.title_x, layout_.title_y);
    }

    // the status line lies over the top left of the body, on black, above whatever the page draws there
    caption_ = lv_label_create(root_);
    lv_label_set_long_mode(caption_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(caption_, layout_.caption_x, layout_.caption_y);
    lv_obj_set_style_text_font(caption_, cp0_ui_font_px(layout_.caption_px), 0);
    lv_obj_set_style_bg_color(caption_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(caption_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(caption_, 4, 0);
    lv_label_set_text(caption_, "");
    lv_obj_add_flag(caption_, LV_OBJ_FLAG_HIDDEN);

    /* the same look as the stock-app toolbar's Esc key (native_ui.cpp ensure_chrome) */
    esc_ = lv_button_create(header_);
    if (lv_obj_get_group(esc_)) lv_group_remove_obj(esc_); /* touch only: the keyboard has its own Esc */
    lv_obj_set_size(esc_, layout_.esc_w, layout_.esc_h);
    lv_obj_set_pos(esc_, layout_.esc_x, layout_.esc_y);
    lv_obj_set_style_radius(esc_, layout_.esc_radius, 0);
    lv_obj_set_style_bg_color(esc_, lv_color_hex(kKeyBg), 0);
    lv_obj_set_style_bg_opa(esc_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(esc_, lv_color_hex(kKeyPressed), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(esc_, 0, 0);
    lv_obj_set_style_border_width(esc_, 0, 0);
    lv_obj_add_event_cb(esc_, esc_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_t *label = lv_label_create(esc_);
    lv_label_set_text(label, "Esc");
    lv_obj_set_style_text_font(label, cp0_ui_font_px(layout_.esc_text_px), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
}

Page::~Page()
{
    if (inset_listener_) osk::remove_inset_listener(inset_listener_);
    inset_listener_ = 0;
    if (root_) {
        lv_obj_remove_event_cb_with_user_data(root_, root_delete_cb, this);
        lv_obj_delete(root_);
    }
}

void Page::root_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<Page *>(lv_event_get_user_data(event));
    if (!self) return;
    if (self->inset_listener_) osk::remove_inset_listener(self->inset_listener_);
    self->inset_listener_ = 0;
    self->root_ = self->header_ = self->title_ = self->caption_ = self->esc_ = self->body_ = nullptr;
}

/* The form body: under the header, the body's height minus the keyboard's inset (task 014 P2b). */
lv_obj_t *Page::body()
{
    if (body_ || !root_) return body_;
    body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(body_);
    lv_obj_set_pos(body_, 0, layout_.body_y);
    lv_obj_set_size(body_, layout_.w, std::max(0, layout_.body_h - osk::bottom_inset()));
    lv_obj_set_style_pad_top(body_, layout_.gap, 0);
    lv_obj_set_style_pad_bottom(body_, layout_.gap, 0);
    lv_obj_set_style_pad_row(body_, layout_.gap, 0);
    lv_obj_set_flex_flow(body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(body_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_remove_flag(body_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /* on_inset() scrolls the editing field itself */
    if (!inset_listener_) inset_listener_ = osk::add_inset_listener([this](int inset) { on_inset(inset); });
    if (caption_) lv_obj_move_foreground(caption_);
    return body_;
}

void Page::clear_body()
{
    if (inset_listener_) osk::remove_inset_listener(inset_listener_);
    inset_listener_ = 0;
    if (body_) lv_obj_delete(body_);
    body_ = nullptr;
}

/* The keyboard came or went: the body takes the room above it and the field it is up for stays in view. */
void Page::on_inset(int inset)
{
    if (!body_) return;
    lv_obj_set_height(body_, std::max(0, layout_.body_h - inset));
    lv_obj_update_layout(body_);
    lv_obj_t *field = osk::owner();
    for (lv_obj_t *obj = field; obj; obj = lv_obj_get_parent(obj)) {
        if (obj != body_) continue;
        lv_obj_t *row = lv_obj_get_parent(field); /* the field's row: its label stays in view too */
        lv_obj_scroll_to_view_recursive(row && row != body_ ? row : field, LV_ANIM_OFF);
        break;
    }
}

/* The Esc button calls the back action itself: no key is injected, so no Esc can be left pressed. */
void Page::esc_event_cb(lv_event_t *event)
{
    auto *self = static_cast<Page *>(lv_event_get_user_data(event));
    if (self && self->on_back_) self->on_back_();
}

void Page::set_title(const std::vector<std::string> &path)
{
    if (!title_ || path.empty()) return;
    const lv_font_t *font = lv_obj_get_style_text_font(title_, LV_PART_MAIN);
    for (size_t first = 0; first < path.size(); ++first) {
        std::string text;
        for (size_t i = first; i < path.size(); ++i) text += (i > first ? " > " : "") + path[i];
        if (text_width(text.c_str(), font) <= layout_.title_max_w) {
            /* the home grid's title label as it is: content width, wrap mode */
            lv_label_set_long_mode(title_, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_width(title_, LV_SIZE_CONTENT);
            lv_label_set_text(title_, text.c_str());
            return;
        }
    }
    lv_label_set_long_mode(title_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title_, layout_.title_max_w);
    lv_label_set_text(title_, path.back().c_str());
}

void Page::set_caption(const std::string &text, bool error)
{
    if (!caption_) return;
    lv_label_set_text(caption_, text.c_str());
    // one line: its own width, or the room it has with dots
    const lv_font_t *font = lv_obj_get_style_text_font(caption_, LV_PART_MAIN);
    const int padded = text_width(text.c_str(), font) + 2 * lv_obj_get_style_pad_left(caption_, LV_PART_MAIN);
    lv_obj_set_width(caption_, padded <= layout_.caption_w ? LV_SIZE_CONTENT : layout_.caption_w);
    if (font) lv_obj_set_height(caption_, lv_font_get_line_height(font)); /* dots need a fixed height */
    lv_obj_set_style_text_color(caption_, lv_color_hex(error ? kCaptionError : kCaption), 0);
    if (text.empty()) {
        lv_obj_add_flag(caption_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(caption_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(caption_); /* above the roller, created after the scaffold */
    }
}

} // namespace ui
} // namespace cp0
