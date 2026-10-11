/*
 * SPDX-License-Identifier: MIT
 *
 * TextField (TextFieldRow, task 014 P2b; see cp0_ui_widgets.hpp).
 *
 * Objects: row (label above the field, roller wide) > label, box (the field: in the page's input group, focus starts
 * editing) > area (an lv_textarea used for display only: not clickable, in no group; one line, horizontal scroll to
 * the cursor, password bullets) and, for a Password, the show / hide toggle at the right inside the box (a field
 * control, not a keyboard key: it never summons or hides the keyboard).
 *
 * Editing reads LV_EVENT_KEYBOARD on the field's screen with the LVGL keypad intercepted (AGENTS.md "Dual delivery and
 * interception"), so a physical keyboard and the on-screen keyboard type the same way; the previous input context,
 * intercept and text hint are saved when editing starts and restored on every way out (Esc, Enter, focus leaving,
 * deletion).
 */

#include "cp0_ui_widgets.hpp"

#include "cp0_font_service.hpp"
#include "cp0_ui_metrics_lvgl.h"
#include "input_keys.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

namespace cp0 {
namespace ui {
namespace {

constexpr uint32_t kLabel = 0x9A9A9A;
constexpr uint32_t kFieldBg = 0x1C1C1C;     /* the keyboard's key colour */
constexpr uint32_t kFieldBorder = 0x3A3A3A;
constexpr uint32_t kEditing = 0x2D9CDB;     /* the home grid's selection blue: field being edited, cursor */
constexpr uint32_t kText = 0xEEEEEE;
constexpr uint32_t kPlaceholder = 0x666666;

const lv_font_t *user_font(int px)
{
    return cp0_fonts().get("Montserrat-Medium.ttf", static_cast<uint16_t>(px)); /* user text: any UTF-8 */
}

cp0_keyboard_text_hint_t hint_of(TextType type)
{
    switch (type) {
    case TextType::Number: return KBD_TEXT_HINT_NUMBER;
    case TextType::Password: return KBD_TEXT_HINT_PASSWORD;
    default: return KBD_TEXT_HINT_TEXT;
    }
}

bool continuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

/* the longest UTF-8 prefix of text that fits max bytes */
std::string clip(const std::string &text, std::size_t max)
{
    if (text.size() <= max) return text;
    std::size_t end = max;
    while (end > 0 && continuation(text[end])) --end;
    return text.substr(0, end);
}

std::uint32_t chars_before(const std::string &text, std::size_t bytes)
{
    std::uint32_t n = 0;
    for (std::size_t i = 0; i < bytes && i < text.size(); ++i)
        if (!continuation(text[i])) ++n;
    return n;
}

} // namespace

TextField::TextField(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, TextFieldSpec spec, TextFieldEvents events)
    : layout_(layout), spec_(std::move(spec)), events_(std::move(events))
{
    text_ = clip(spec_.text, spec_.max_bytes);
    cursor_ = text_.size();
    const int field_h = layout_.row_h;
    const int inset = std::max(layout_.gap, field_h / 5);

    row_ = lv_obj_create(parent);
    lv_obj_remove_style_all(row_);
    lv_obj_set_size(row_, layout_.roller_w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row_, std::max(2, layout_.gap / 3), 0);
    lv_obj_remove_flag(row_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row_, row_delete_cb, LV_EVENT_DELETE, this);

    label_ = lv_label_create(row_);
    lv_label_set_text(label_, spec_.label.c_str());
    lv_obj_set_style_text_font(label_, user_font(layout_.caption_px), 0);
    lv_obj_set_style_text_color(label_, lv_color_hex(kLabel), 0);

    box_ = lv_obj_create(row_);
    lv_obj_remove_style_all(box_);
    if (lv_obj_get_group(box_)) lv_group_remove_obj(box_); /* the page adds it to its own group */
    lv_obj_set_size(box_, layout_.roller_w, field_h);
    lv_obj_set_style_radius(box_, cp0_ui_tok(CP0_TOK_RADIUS_S), 0);
    lv_obj_set_style_bg_color(box_, lv_color_hex(kFieldBg), 0);
    lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box_, 2, 0);
    lv_obj_set_style_border_opa(box_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(box_, inset, 0);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /* the page scrolls the editing field itself */
    lv_obj_add_flag(box_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box_, box_event_cb, LV_EVENT_ALL, this);

    const bool password = spec_.type == TextType::Password;
    const int eye_w = password ? field_h : 0;
    area_ = lv_textarea_create(box_);
    if (lv_obj_get_group(area_)) lv_group_remove_obj(area_);
    lv_obj_remove_style_all(area_);
    lv_obj_remove_flag(area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(area_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_one_line(area_, true);
    lv_obj_set_width(area_, layout_.roller_w - 2 * inset - (password ? eye_w - inset : 0));
    lv_obj_set_style_text_font(area_, user_font(layout_.value_px), 0);
    lv_obj_set_style_text_color(area_, lv_color_hex(kText), 0);
    lv_obj_set_style_text_color(area_, lv_color_hex(kPlaceholder), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_scrollbar_mode(area_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_hor(area_, 2, 0); /* room for the 2 px cursor before the first character */
    lv_obj_set_style_border_side(area_, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_color(area_, lv_color_hex(kEditing), LV_PART_CURSOR);
    lv_obj_set_style_border_opa(area_, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(area_, 0, LV_PART_CURSOR); /* a steady cursor, no blink timer */
    if (!spec_.placeholder.empty()) lv_textarea_set_placeholder_text(area_, spec_.placeholder.c_str());
    if (password) {
        lv_textarea_set_password_show_time(area_, 0); /* masked at once */
        lv_textarea_set_password_mode(area_, true);
    }
    lv_obj_align(area_, LV_ALIGN_LEFT_MID, 0, 0);

    if (password) {
        eye_ = lv_obj_create(box_);
        lv_obj_remove_style_all(eye_);
        if (lv_obj_get_group(eye_)) lv_group_remove_obj(eye_);
        lv_obj_set_size(eye_, eye_w, field_h);
        lv_obj_align(eye_, LV_ALIGN_RIGHT_MID, inset, 0);
        lv_obj_remove_flag(eye_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(eye_, LV_OBJ_FLAG_CLICK_FOCUSABLE); /* a tap on it keeps the field as it is */
        lv_obj_add_flag(eye_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(eye_, eye_event_cb, LV_EVENT_CLICKED, this);
        eye_label_ = lv_label_create(eye_);
        lv_obj_set_style_text_font(eye_label_, cp0_ui_font_px(layout_.value_px), 0);
        lv_obj_set_style_text_color(eye_label_, lv_color_hex(kLabel), 0);
        lv_obj_center(eye_label_);
    }
    refresh();
    style_editing();
}

TextField::~TextField()
{
    if (!row_) return;
    lv_obj_remove_event_cb_with_user_data(row_, row_delete_cb, this);
    stop(true);
    lv_obj_delete(row_);
}

void TextField::row_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<TextField *>(lv_event_get_user_data(event));
    if (!self) return;
    self->stop(true);
    self->row_ = self->label_ = self->box_ = self->area_ = self->eye_ = self->eye_label_ = nullptr;
}

void TextField::set_text(const std::string &text)
{
    text_ = clip(text, spec_.max_bytes);
    cursor_ = text_.size();
    refresh();
}

void TextField::set_revealed(bool revealed)
{
    if (spec_.type != TextType::Password || !area_) return;
    revealed_ = revealed;
    lv_textarea_set_password_mode(area_, !revealed_);
    refresh();
}

void TextField::refresh()
{
    if (!area_) return;
    lv_textarea_set_text(area_, text_.c_str());
    lv_textarea_set_cursor_pos(area_, static_cast<int32_t>(chars_before(text_, cursor_)));
    if (eye_label_) lv_label_set_text(eye_label_, revealed_ ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

void TextField::style_editing()
{
    if (!box_ || !area_) return;
    lv_obj_set_style_border_color(box_, lv_color_hex(editing_ ? kEditing : kFieldBorder), 0);
    lv_obj_set_style_border_width(area_, editing_ ? 2 : 0, LV_PART_CURSOR);
}

void TextField::begin_edit()
{
    if (editing_ || !box_) return;
    editing_ = true;
    saved_context_ = cp0_keyboard_get_input_context();
    saved_intercept_ = cp0_keyboard_get_lvgl_keypad_intercept();
    saved_hint_ = cp0_keyboard_get_text_hint();
    cp0_keyboard_set_input_context(KBD_INPUT_CONTEXT_TEXT);
    cp0_keyboard_set_lvgl_keypad_intercept(1);
    cp0_keyboard_set_text_hint(hint_of(spec_.type));
    key_screen_ = lv_obj_get_screen(box_);
    if (key_screen_ && LV_EVENT_KEYBOARD != 0)
        lv_obj_add_event_cb(key_screen_, keyboard_event_cb, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
    if (lv_group_t *group = lv_obj_get_group(box_))
        if (lv_group_get_focused(group) != box_) lv_group_focus_obj(box_);
    style_editing();
    osk::request(box_, spec_.type, spec_.multiline);
    lv_obj_scroll_to_view_recursive(row_, LV_ANIM_OFF);
}

void TextField::end_edit()
{
    stop(false);
}

void TextField::stop(bool silent)
{
    if (!editing_) return;
    editing_ = false;
    if (key_screen_) lv_obj_remove_event_cb_with_user_data(key_screen_, keyboard_event_cb, this);
    key_screen_ = nullptr;
    cp0_keyboard_set_input_context(saved_context_);
    cp0_keyboard_set_lvgl_keypad_intercept(saved_intercept_);
    cp0_keyboard_set_text_hint(saved_hint_);
    osk::release(box_);
    if (!silent) style_editing();
}

void TextField::focus_step(bool next)
{
    lv_group_t *group = box_ ? lv_obj_get_group(box_) : nullptr;
    if (!group) return;
    if (next) lv_group_focus_next(group);
    else lv_group_focus_prev(group);
}

void TextField::insert(const char *utf8)
{
    const std::size_t len = std::strlen(utf8);
    if (len == 0 || text_.size() + len > spec_.max_bytes) return;
    text_.insert(cursor_, utf8, len);
    cursor_ += len;
    refresh();
    if (events_.changed) events_.changed(text_);
}

void TextField::erase_before()
{
    if (cursor_ == 0) return;
    std::size_t start = cursor_ - 1;
    while (start > 0 && continuation(text_[start])) --start;
    text_.erase(start, cursor_ - start);
    cursor_ = start;
    refresh();
    if (events_.changed) events_.changed(text_);
}

void TextField::erase_after()
{
    if (cursor_ >= text_.size()) return;
    std::size_t end = cursor_ + 1;
    while (end < text_.size() && continuation(text_[end])) ++end;
    text_.erase(cursor_, end - cursor_);
    refresh();
    if (events_.changed) events_.changed(text_);
}

void TextField::move_cursor(int delta)
{
    if (delta < 0 && cursor_ > 0) {
        --cursor_;
        while (cursor_ > 0 && continuation(text_[cursor_])) --cursor_;
    } else if (delta > 0 && cursor_ < text_.size()) {
        ++cursor_;
        while (cursor_ < text_.size() && continuation(text_[cursor_])) ++cursor_;
    }
    refresh();
}

/* A key while editing (physical or on-screen: the same key_item). Releases change nothing. */
void TextField::on_key(const struct key_item &item)
{
    if (item.key_state == KBD_KEY_RELEASED) return;
    const bool press = item.key_state == KBD_KEY_PRESSED;
    switch (item.key_code) {
    case KEY_ESC:
        if (press) end_edit(); /* the keyboard goes; Esc again goes back */
        return;
    case KEY_ENTER:
    case KEY_KPENTER:
        if (!press) return;
        if (spec_.multiline) insert("\n");
        else if (events_.submit) events_.submit(text_);
        else end_edit();
        return;
    case KEY_BACKSPACE:
        erase_before();
        return;
    case KEY_DELETE:
        erase_after();
        return;
    case KEY_LEFT:
    case KEY_RIGHT:
        move_cursor(item.key_code == KEY_LEFT ? -1 : 1);
        return;
    case KEY_HOME:
    case KEY_END:
        cursor_ = item.key_code == KEY_HOME ? 0 : text_.size();
        refresh();
        return;
    case KEY_UP:
    case KEY_DOWN:
        if (press && !spec_.multiline) focus_step(item.key_code == KEY_DOWN);
        return;
    case KEY_TAB:
        if (spec_.multiline && !(item.mods & KBD_MOD_CTRL) && std::strcmp(item.utf8, "\t") == 0) insert("\t");
        else if (press) focus_step(!(item.mods & KBD_MOD_SHIFT));
        return;
    default:
        break;
    }
    if (item.mods & (KBD_MOD_CTRL | KBD_MOD_ALT | KBD_MOD_LOGO)) return; /* shortcuts type no text */
    const unsigned char first = static_cast<unsigned char>(item.utf8[0]);
    if (first < 0x20 || first == 0x7F) return;
    if (spec_.type == TextType::Number && !(first >= '0' && first <= '9' && item.utf8[1] == '\0')) return;
    char text[sizeof(item.utf8) + 1];
    std::memcpy(text, item.utf8, sizeof(item.utf8));
    text[sizeof(item.utf8)] = '\0';
    insert(text);
}

void TextField::keyboard_event_cb(lv_event_t *event)
{
    auto *self = static_cast<TextField *>(lv_event_get_user_data(event));
    const auto *item = static_cast<const struct key_item *>(lv_event_get_param(event));
    if (!self || !item || !self->editing_) return;
    self->on_key(*item); /* the item is the dispatcher's: read during this call only */
}

/* The field in the input group: focus edits; while not editing, Enter edits, Esc goes back, Up / Down move. */
void TextField::box_event_cb(lv_event_t *event)
{
    auto *self = static_cast<TextField *>(lv_event_get_user_data(event));
    if (!self || !self->box_) return;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_FOCUSED:
        self->begin_edit();
        break;
    case LV_EVENT_DEFOCUSED:
        // focus left (Tab / Up / Down, another field tapped, a click-focusable control outside the group): stop
        // before the next field's FOCUSED saves the input state
        self->end_edit();
        break;
    case LV_EVENT_CLICKED:
        self->begin_edit(); /* a tap on the field edits again after Esc: the keyboard comes back */
        break;
    case LV_EVENT_KEY: {
        if (self->editing_) break;
        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ENTER) self->begin_edit();
        else if (key == LV_KEY_ESC && self->events_.back) self->events_.back();
        else if (key == LV_KEY_UP || key == LV_KEY_DOWN) self->focus_step(key == LV_KEY_DOWN);
        break;
    }
    default:
        break;
    }
}

void TextField::eye_event_cb(lv_event_t *event)
{
    auto *self = static_cast<TextField *>(lv_event_get_user_data(event));
    if (self) self->set_revealed(!self->revealed_);
}

} // namespace ui
} // namespace cp0
