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
 * The text, the caret and the selection are a text_edit::Buffer (cp0_ui_text_edit.hpp: byte offsets on character
 * boundaries, unit tested). Shift+arrows, Home and End extend the selection, Ctrl+A selects all, typing and Backspace
 * replace or delete it; the highlight is the label's own text selection (LV_LABEL_TEXT_SELECTION). Ctrl+C / X / V (and
 * Ctrl+Insert, Shift+Delete, Shift+Insert) use the launcher's clipboard (launcher.sock, cp0_app_client.h). A tap puts
 * the caret at the tapped character.
 *
 * Editing reads LV_EVENT_KEYBOARD on the field's screen with the LVGL keypad intercepted (AGENTS.md "Dual delivery and
 * interception"), so a physical keyboard and the on-screen keyboard type the same way; the previous input context,
 * intercept and text hint are saved when editing starts and restored on every way out (Esc, Enter, focus leaving,
 * deletion).
 */

#include "cp0_ui_widgets.hpp"

#include "cp0_app_client.h"
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
constexpr uint32_t kSelectionBg = 0x2D9CDB;    /* the selected text: the accent blue behind dark text (readable on the field) */
constexpr uint32_t kSelectionText = 0x101010;
constexpr int kClipboardTimeoutMs = 100;       /* a paste or copy waits for the launcher this long at most */
constexpr uint32_t kClipboardBackoffMs = 3000; /* after "no service" the next try waits this long (a stalled launcher) */

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

/* zero the bytes (the compiler may not drop the stores) and empty the string */
void secure_zero(std::string &text)
{
    volatile char *data = text.empty() ? nullptr : &text[0];
    for (std::size_t i = 0; data && i < text.size(); ++i) data[i] = 0;
    text.clear();
}

/* the clipboard service: one failed attempt (no socket, an older launcher, a launcher too busy to answer in time) makes the
 * next ones wait, so a key held down never stalls the UI thread more than once in a while */
bool g_clipboard_backoff = false;
uint32_t g_clipboard_failed_at = 0;

bool clipboard_ready()
{
    if (!g_clipboard_backoff) return true;
    if (lv_tick_elaps(g_clipboard_failed_at) < kClipboardBackoffMs) return false;
    g_clipboard_backoff = false;
    return true;
}

void clipboard_note(int result)
{
    g_clipboard_backoff = result == CP0_APP_CLIPBOARD_NOSERVICE || result == CP0_APP_CLIPBOARD_UNSUPPORTED;
    if (g_clipboard_backoff) g_clipboard_failed_at = lv_tick_get();
}

} // namespace

TextField::TextField(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, TextFieldSpec spec, TextFieldEvents events)
    : layout_(layout), spec_(std::move(spec)), events_(std::move(events)), buf_(spec_.max_bytes)
{
    buf_.set_text(spec_.text);
    const int field_h = layout_.row_h;
    const int inset = std::max(layout_.gap, field_h / 5);
    // label beside the field (spec_.label_w > 0): one row of the row token's height, the field after the label column
    const bool beside = spec_.label_w > 0 && spec_.label_w + layout_.gap < layout_.roller_w;
    const int field_w = beside ? layout_.roller_w - spec_.label_w - layout_.gap : layout_.roller_w;

    row_ = lv_obj_create(parent);
    lv_obj_remove_style_all(row_);
    lv_obj_set_size(row_, layout_.roller_w, LV_SIZE_CONTENT);
    if (beside) {
        lv_obj_set_flex_flow(row_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row_, layout_.gap, 0);
    } else {
        lv_obj_set_flex_flow(row_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(row_, std::max(2, layout_.gap / 3), 0);
    }
    lv_obj_remove_flag(row_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row_, row_delete_cb, LV_EVENT_DELETE, this);

    label_ = lv_label_create(row_);
    lv_label_set_text(label_, spec_.label.c_str());
    const cp0_ui_metrics_t *metrics = cp0_ui_metrics_get();
    const int label_px = beside && metrics ? metrics->text_px[CP0_TEXT_BODY] : layout_.caption_px;
    lv_obj_set_style_text_font(label_, user_font(label_px), 0);
    lv_obj_set_style_text_color(label_, lv_color_hex(kLabel), 0);
    if (beside) {
        lv_obj_set_width(label_, spec_.label_w);
        lv_label_set_long_mode(label_, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_remove_flag(label_, LV_OBJ_FLAG_SCROLLABLE); /* dots: not scrollable (Page's caption, cp0_ui_page.cpp) */
    }

    box_ = lv_obj_create(row_);
    lv_obj_remove_style_all(box_);
    if (lv_obj_get_group(box_)) lv_group_remove_obj(box_); /* the page adds it to its own group */
    lv_obj_set_size(box_, field_w, field_h);
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
    lv_obj_set_width(area_, field_w - 2 * inset - (password ? eye_w - inset : 0));
    lv_obj_set_style_text_font(area_, user_font(layout_.value_px), 0);
    lv_obj_set_style_text_color(area_, lv_color_hex(kText), 0);
    lv_obj_set_style_text_color(area_, lv_color_hex(kPlaceholder), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_scrollbar_mode(area_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_hor(area_, 2, 0); /* room for the 2 px cursor before the first character */
    lv_obj_set_style_border_side(area_, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_color(area_, lv_color_hex(kEditing), LV_PART_CURSOR);
    lv_obj_set_style_border_opa(area_, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(area_, 0, LV_PART_CURSOR); /* a steady cursor, no blink timer */
#if LV_LABEL_TEXT_SELECTION
    lv_obj_t *text_label = lv_textarea_get_label(area_); /* the selection highlight is drawn by this label */
    lv_obj_set_style_bg_color(text_label, lv_color_hex(kSelectionBg), LV_PART_SELECTED);
    lv_obj_set_style_text_color(text_label, lv_color_hex(kSelectionText), LV_PART_SELECTED);
#endif
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
    if (spec_.type == TextType::Password) buf_.wipe(); /* the display copy goes with the row */
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
    buf_.set_text(text);
    refresh();
}

void TextField::wipe()
{
    buf_.wipe();
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
    lv_textarea_set_text(area_, buf_.text().c_str());
    lv_textarea_set_cursor_pos(area_, static_cast<int32_t>(text_edit::char_index(buf_.text(), buf_.caret())));
#if LV_LABEL_TEXT_SELECTION
    lv_obj_t *label = lv_textarea_get_label(area_);
    if (buf_.selecting()) {
        lv_label_set_text_selection_start(label, static_cast<uint32_t>(text_edit::char_index(buf_.text(), buf_.sel_lo())));
        lv_label_set_text_selection_end(label, static_cast<uint32_t>(text_edit::char_index(buf_.text(), buf_.sel_hi())));
    } else {
        lv_label_set_text_selection_start(label, LV_LABEL_TEXT_SELECTION_OFF);
        lv_label_set_text_selection_end(label, LV_LABEL_TEXT_SELECTION_OFF);
    }
#endif
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
    /* a field on a display layer (the sudo prompt on the top layer): the keys go to the active screen, not the layer */
    if (key_screen_ == lv_layer_top() || key_screen_ == lv_layer_bottom() || key_screen_ == lv_layer_sys())
        key_screen_ = lv_screen_active();
    if (key_screen_ && LV_EVENT_KEYBOARD != 0) {
        lv_obj_add_event_cb(key_screen_, keyboard_event_cb, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
        lv_obj_add_event_cb(key_screen_, screen_delete_cb, LV_EVENT_DELETE, this); /* the screen may go first */
    }
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
    buf_.collapse(); /* a selection never outlives the editing */
    if (!silent) refresh();
    if (key_screen_) {
        lv_obj_remove_event_cb_with_user_data(key_screen_, keyboard_event_cb, this);
        lv_obj_remove_event_cb_with_user_data(key_screen_, screen_delete_cb, this);
    }
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

void TextField::after_edit()
{
    refresh();
    if (events_.changed) events_.changed(buf_.text());
}

void TextField::insert(const char *utf8)
{
    if (buf_.insert(utf8)) after_edit(); /* a typed character replaces the selection; refused when over the maximum */
}

void TextField::erase_before()
{
    if (buf_.erase_before()) after_edit();
}

void TextField::erase_after()
{
    if (buf_.erase_after()) after_edit();
}

void TextField::move_cursor(int delta, bool extend)
{
    buf_.move(delta, extend);
    refresh();
}

void TextField::select_all()
{
    buf_.select_all();
    refresh();
}

void TextField::copy_selection()
{
    if (spec_.type == TextType::Password) return; /* a password never leaves the field */
    const std::string text = text_edit::clip_bytes(buf_.selecting() ? buf_.selection() : buf_.text(),
                                                   CP0_APP_CLIPBOARD_MAX); /* nothing selected: the whole field */
    if (text.empty() || !clipboard_ready()) return;
    clipboard_note(cp0_app_clipboard_set(text.data(), text.size(), kClipboardTimeoutMs));
}

void TextField::cut_selection()
{
    if (spec_.type == TextType::Password || !buf_.selecting() || !clipboard_ready()) return;
    const std::string text = buf_.selection();
    if (text.size() > CP0_APP_CLIPBOARD_MAX) return; /* it would not fit whole: cutting must not lose text */
    const int result = cp0_app_clipboard_set(text.data(), text.size(), kClipboardTimeoutMs);
    clipboard_note(result);
    if (result != CP0_APP_CLIPBOARD_OK) return; /* the text leaves the field only once the clipboard has it */
    buf_.erase_selection();
    after_edit();
}

void TextField::paste_clipboard()
{
    if (!clipboard_ready()) return;
    std::string raw(CP0_APP_CLIPBOARD_MAX + 1, '\0');
    std::size_t len = 0;
    const int result = cp0_app_clipboard_get(&raw[0], raw.size(), &len, kClipboardTimeoutMs);
    clipboard_note(result);
    raw.resize(result == CP0_APP_CLIPBOARD_OK ? len : 0);
    std::string clean = text_edit::sanitize_paste(raw, spec_.multiline, spec_.type == TextType::Number);
    secure_zero(raw); /* the clipboard may hold a secret: no copy of it outlives this call but the field's */
    if (!clean.empty() && buf_.paste(clean)) after_edit();
    secure_zero(clean);
}

/* A tap on the field: the caret goes to the tapped character (the label's own coordinates include the field's
 * horizontal scroll, as LVGL's textarea hit test does), and a selection ends. Touch and pointer only. */
void TextField::place_caret_at_tap()
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER || !box_ || !area_) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    lv_area_t box;
    lv_obj_get_coords(box_, &box);
    if (point.x < box.x1 || point.x > box.x2 || point.y < box.y1 || point.y > box.y2)
        return; /* the page moved under the finger (the keyboard came up) */
    lv_obj_t *label = lv_textarea_get_label(area_);
    lv_area_t coords;
    lv_obj_get_coords(label, &coords);
    const int32_t x = point.x - coords.x1; /* the label's coordinates already include the field's horizontal scroll */
    /* The character boundary nearest to x, found with the same letter positions the caret is drawn with (a hit test of
     * its own would disagree with them by a pixel or two further along a long text): the last boundary at or left of x,
     * then the nearer of it and the next. */
    const uint32_t count = static_cast<uint32_t>(text_edit::char_index(buf_.text(), buf_.text().size()));
    auto left_of = [&](uint32_t index) {
        lv_point_t pos;
        lv_label_get_letter_pos(label, index, &pos);
        return pos.x;
    };
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        const uint32_t mid = (lo + hi + 1) / 2;
        if (left_of(mid) <= x) lo = mid;
        else hi = mid - 1;
    }
    uint32_t index = lo;
    if (index < count) {
        const int32_t from = left_of(index);
        if (x - from > (left_of(index + 1) - from) / 2) ++index;
    }
    buf_.set_caret(text_edit::byte_offset(buf_.text(), index), false);
    refresh();
}

/* A key while editing (physical or on-screen: the same key_item). Releases change nothing. */
void TextField::on_key(const struct key_item &item)
{
    if (item.key_state == KBD_KEY_RELEASED) return;
    const bool press = item.key_state == KBD_KEY_PRESSED;
    const bool ctrl = (item.mods & KBD_MOD_CTRL) != 0;
    const bool shift = (item.mods & KBD_MOD_SHIFT) != 0;
    if (spec_.clipboard_keys && !(item.mods & (KBD_MOD_ALT | KBD_MOD_LOGO))) {
        /* select all, copy, cut and paste: by key code (the on-screen keyboard's Ctrl sends the same), a press only so
         * a held key does not repeat them; the field takes them only while it is the one being edited */
        if (ctrl) {
            switch (item.key_code) {
            case KEY_A: if (press) select_all(); return;
            case KEY_C: case KEY_INSERT: if (press) copy_selection(); return;
            case KEY_X: if (press) cut_selection(); return;
            case KEY_V: if (press) paste_clipboard(); return;
            default: break;
            }
        } else if (shift) {
            switch (item.key_code) {
            case KEY_INSERT: if (press) paste_clipboard(); return;
            case KEY_DELETE: if (press) cut_selection(); return;
            default: break;
            }
        }
    }
    switch (item.key_code) {
    case KEY_ESC:
        if (!press) return;
        if (events_.escape) events_.escape(); /* a dialog's Cancel */
        else end_edit();                      /* the keyboard goes; Esc again goes back */
        return;
    case KEY_ENTER:
    case KEY_KPENTER:
        if (!press) return;
        if (spec_.multiline) insert("\n");
        else if (events_.submit) events_.submit(buf_.text());
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
        move_cursor(item.key_code == KEY_LEFT ? -1 : 1, shift);
        return;
    case KEY_HOME:
    case KEY_END:
        if (item.key_code == KEY_HOME) buf_.home(shift);
        else buf_.end(shift);
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

void TextField::screen_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<TextField *>(lv_event_get_user_data(event));
    if (self) self->key_screen_ = nullptr; /* its callbacks go with it */
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
        self->place_caret_at_tap(); /* ... and puts the caret at the tapped character */
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
