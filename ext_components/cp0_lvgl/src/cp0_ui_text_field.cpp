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
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

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
constexpr uint32_t kDoubleTapMs = 500;         /* a second tap this soon after the first, near it, selects the word */
constexpr uint32_t kCopiedMs = 1500;           /* "Copied" stays in the bubble this long */
constexpr int kMinTouchPx = 44;                /* the TARGET token's floor */
constexpr int kDragSlopPx = 8;                 /* a handle press that moves less than this is a tap */
constexpr int kHandleDotPx = 14;               /* the drawn handle; its touch area is the TARGET token (7 mm) */
constexpr int kHandleStemPx = 6;
constexpr uint32_t kTouchPollMs = 100;         /* the handles follow a scrolling page and field (laid out only when something moved) */
constexpr uint32_t kBubbleBg = 0x2B2B2B;
constexpr uint32_t kBubbleBgPressed = 0x454545;
constexpr uint32_t kBubbleEdge = 0x5A5A5A;
enum Action { kActSelectAll = 1, kActPaste, kActCut, kActCopy };

/* delete a touch object later: it may be the target of the event being handled (a bubble button that rebuilds the bubble) */
void drop_async_cb(void *obj)
{
    if (lv_obj_is_valid(static_cast<lv_obj_t *>(obj))) lv_obj_delete(static_cast<lv_obj_t *>(obj));
}

void drop(lv_obj_t *&obj, bool later)
{
    if (!obj) return;
    if (later) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        lv_async_call(drop_async_cb, obj);
    } else {
        lv_obj_delete(obj);
    }
    obj = nullptr;
}

bool inside(lv_obj_t *obj, lv_obj_t *root)
{
    for (; obj; obj = lv_obj_get_parent(obj))
        if (obj == root) return true;
    return false;
}

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
    touch_refresh_model();
    if (eye_label_) lv_label_set_text(eye_label_, revealed_ ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    if (touch_ != Touch::Off) touch_sync();
}

/* the cursor and the highlight from the model (the text is already in the area) */
void TextField::touch_refresh_model()
{
    if (!area_) return;
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
    if (touch_ != Touch::Off) touch_layout(false);
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
    touch_end(true);
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

bool TextField::copy_selection()
{
    if (spec_.type == TextType::Password) return false; /* a password never leaves the field */
    const std::string text = text_edit::clip_bytes(buf_.selecting() ? buf_.selection() : buf_.text(),
                                                   CP0_APP_CLIPBOARD_MAX); /* nothing selected: the whole field */
    if (text.empty() || !clipboard_ready()) return false;
    const int result = cp0_app_clipboard_set(text.data(), text.size(), kClipboardTimeoutMs);
    clipboard_note(result);
    return result == CP0_APP_CLIPBOARD_OK;
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

/* The character boundary nearest to screen x (nearest) or the start of the character that contains x (!nearest), as a byte
 * offset. Found with the same letter positions the caret is drawn with (a hit test of its own would disagree with them by a
 * pixel or two further along a long text): the last boundary at or left of x, then, for nearest, the nearer of it and the
 * next. The label's own coordinates already include the field's horizontal scroll, as LVGL's textarea hit test does. */
std::size_t TextField::byte_at_x(int32_t screen_x, bool nearest) const
{
    lv_obj_t *label = lv_textarea_get_label(area_);
    lv_area_t coords;
    lv_obj_get_coords(label, &coords);
    const int32_t x = screen_x - coords.x1;
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
    if (nearest && index < count) {
        const int32_t from = left_of(index);
        if (x - from > (left_of(index + 1) - from) / 2) ++index;
    }
    return text_edit::byte_offset(buf_.text(), index);
}

/* A tap on the field: the caret goes to the tapped character and a selection ends; touch and pointer only. A second tap
 * soon after the first, near it, selects the word instead (the first tap's caret was placed at once: no wait). With the
 * touch bubble on, the caret handle appears (or the range handles and the bubble after a double tap). */
bool TextField::place_caret_at_tap()
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER || !box_ || !area_) return false;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    lv_area_t box;
    lv_obj_get_coords(box_, &box);
    if (point.x < box.x1 || point.x > box.x2 || point.y < box.y1 || point.y > box.y2)
        return false; /* the page moved under the finger (the keyboard came up) */
    const bool dbl = spec_.clipboard_touch && have_tap_ && lv_tick_elaps(last_tap_tick_) <= kDoubleTapMs &&
                     std::abs(point.x - last_tap_x_) <= cp0_ui_tok(CP0_TOK_TARGET) / 2;
    if (dbl && !buf_.text().empty()) {
        const auto word = text_edit::word_range(buf_.text(), byte_at_x(point.x, false));
        buf_.select_range(word.first, word.second);
        have_tap_ = false;
        refresh();
        touch_begin(buf_.selecting() ? Touch::Range : Touch::Caret, buf_.selecting());
        return true;
    }
    buf_.set_caret(byte_at_x(point.x, true), false);
    have_tap_ = true;
    last_tap_tick_ = lv_tick_get();
    last_tap_x_ = point.x;
    refresh();
    touch_begin(Touch::Caret, false);
    return true;
}

/* A key while editing (physical or on-screen: the same key_item). Releases change nothing. */
void TextField::on_key(const struct key_item &item)
{
    if (item.key_state == KBD_KEY_RELEASED) return;
    const bool press = item.key_state == KBD_KEY_PRESSED;
    if (touch_ != Touch::Off) touch_end(false); /* the bubble belongs to touch: the next key puts it away */
    have_tap_ = false;
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

/* ------------------------------------------------------------------ the touch bubble and the selection handles
 * (docs/dev/clipboard.md step 2). Plain objects on the display's top layer, not children of the page: the page's scrolling
 * cannot take a drag from them, they follow the field by a poll while shown, and the keyboard (also on that layer) is
 * never covered because everything is laid out above its top edge. CLICKABLE but not CLICK_FOCUSABLE and in no group (the
 * eye's rule): touching them never changes the focus, never ends the editing and never shows or hides the keyboard.
 * Shown only after a finger tap in the field; a key (physical or on the keyboard), Esc, a press elsewhere or the editing
 * stopping puts them away. No animation, no fade, no shadow. */

void TextField::touch_begin(Touch mode, bool bubble_open)
{
    if (!spec_.clipboard_touch || !editing_ || !box_ || !area_) return;
    touch_ = mode == Touch::Range && !buf_.selecting() ? Touch::Caret : mode;
    bubble_open_ = bubble_open && touch_ == Touch::Range;
    if (!touch_indev_) {
        touch_indev_ = lv_indev_active();
        if (touch_indev_) lv_indev_add_event_cb(touch_indev_, indev_press_cb, LV_EVENT_PRESSED, this);
    }
    if (!touch_timer_) touch_timer_ = lv_timer_create(touch_timer_cb, kTouchPollMs, this);
    touch_drop_bubble();
    touch_layout(true);
}

void TextField::touch_end(bool now)
{
    if (touch_indev_) {
        lv_indev_remove_event_cb_with_user_data(touch_indev_, indev_press_cb, this);
        touch_indev_ = nullptr;
    }
    if (touch_timer_) {
        lv_timer_delete(touch_timer_);
        touch_timer_ = nullptr;
    }
    if (copied_timer_) {
        lv_timer_delete(copied_timer_);
        copied_timer_ = nullptr;
    }
    touch_ = Touch::Off;
    bubble_open_ = dragging_ = false;
    copy_label_ = nullptr;
    drop(bubble_, !now);
    drop(h_caret_, !now);
    drop(h_start_, !now);
    drop(h_end_, !now);
}

/* a press that is not on the field, the bubble, a handle or the keyboard */
void TextField::touch_outside()
{
    buf_.collapse();
    touch_end(false);
    refresh();
}

/* the model changed (a paste, a cut, a tap): the handles follow the selection, the bubble closes unless it is the same
 * kind of selection and still wanted */
void TextField::touch_sync()
{
    const Touch want = buf_.selecting() ? Touch::Range : Touch::Caret;
    if (want != touch_) {
        touch_ = want;
        bubble_open_ = false;
    }
    touch_drop_bubble();
    touch_layout(true);
}

void TextField::touch_drop_bubble()
{
    copy_label_ = nullptr;
    if (copied_timer_) {
        lv_timer_delete(copied_timer_);
        copied_timer_ = nullptr;
    }
    drop(bubble_, true);
}

bool TextField::clipboard_has_text()
{
    if (!clipboard_ready()) return false;
    char probe[16];
    size_t len = 0;
    const int result = cp0_app_clipboard_get(probe, sizeof(probe), &len, kClipboardTimeoutMs);
    clipboard_note(result);
    const bool has = result == CP0_APP_CLIPBOARD_OK && len > 0;
    std::memset(probe, 0, sizeof(probe));
    return has;
}

void TextField::touch_make_handle(lv_obj_t *&handle, Role role)
{
    (void)role;
    if (handle) return;
    lv_obj_t *layer = lv_display_get_layer_top(lv_obj_get_display(box_));
    if (!layer) return;
    const int target = cp0_ui_tok(CP0_TOK_TARGET);
    handle = lv_obj_create(layer);
    lv_obj_remove_style_all(handle);
    if (lv_obj_get_group(handle)) lv_group_remove_obj(handle);
    lv_obj_set_size(handle, target, target); /* the touch area: 7 mm, at least 44 px, drawn smaller */
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_HIDDEN); /* shown by the first layout */
    lv_obj_add_event_cb(handle, handle_event_cb, LV_EVENT_ALL, this);
    lv_obj_t *stem = lv_obj_create(handle);
    lv_obj_remove_style_all(stem);
    lv_obj_remove_flag(stem, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(stem, 2, kHandleStemPx);
    lv_obj_set_style_bg_color(stem, lv_color_hex(kEditing), 0);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
    lv_obj_t *dot = lv_obj_create(handle);
    lv_obj_remove_style_all(dot);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(dot, kHandleDotPx, kHandleDotPx);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(kEditing), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
}

void TextField::touch_build_bubble()
{
    if (bubble_ || !bubble_open_ || dragging_) return;
    struct Item {
        int action;
        const char *text;
    };
    std::vector<Item> items;
    const bool password = spec_.type == TextType::Password;
    const bool clip = clipboard_has_text();
    if (touch_ == Touch::Caret) {
        if (!buf_.text().empty()) items.push_back({kActSelectAll, "Select all"});
        if (clip) items.push_back({kActPaste, "Paste"});
    } else {
        if (!password) {
            items.push_back({kActCut, "Cut"});
            items.push_back({kActCopy, "Copy"});
        }
        if (clip) items.push_back({kActPaste, "Paste"});
        if (buf_.sel_lo() > 0 || buf_.sel_hi() < buf_.text().size()) items.push_back({kActSelectAll, "All"});
    }
    if (items.empty()) {
        bubble_open_ = false;
        return;
    }
    lv_obj_t *layer = lv_display_get_layer_top(lv_obj_get_display(box_));
    if (!layer) return;
    const int target = cp0_ui_tok(CP0_TOK_TARGET);
    const int pad = cp0_ui_tok(CP0_TOK_SPACE_M);
    const lv_font_t *font = cp0_ui_font_px(layout_.value_px);
    bubble_ = lv_obj_create(layer);
    lv_obj_remove_style_all(bubble_);
    if (lv_obj_get_group(bubble_)) lv_group_remove_obj(bubble_);
    lv_obj_set_size(bubble_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bubble_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(bubble_, 1, 0);
    lv_obj_set_style_pad_column(bubble_, 1, 0); /* the 1 px between the buttons shows the edge colour: the dividers */
    lv_obj_set_style_bg_color(bubble_, lv_color_hex(kBubbleEdge), 0);
    lv_obj_set_style_bg_opa(bubble_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bubble_, cp0_ui_tok(CP0_TOK_RADIUS_M), 0);
    lv_obj_set_style_clip_corner(bubble_, true, 0);
    lv_obj_remove_flag(bubble_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bubble_, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(bubble_, LV_OBJ_FLAG_CLICKABLE); /* a tap between the buttons stays in the bubble */
    lv_obj_remove_flag(bubble_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(bubble_, LV_OBJ_FLAG_HIDDEN);
    for (const Item &item : items) {
        lv_point_t size;
        lv_text_get_size(&size, item.text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (item.action == kActCopy) { /* "Copied" replaces "Copy" in the same button: the bubble must not change size */
            lv_point_t copied;
            lv_text_get_size(&copied, "Copied", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            size.x = std::max(size.x, copied.x);
        }
        lv_obj_t *button = lv_obj_create(bubble_);
        lv_obj_remove_style_all(button);
        if (lv_obj_get_group(button)) lv_group_remove_obj(button);
        lv_obj_set_size(button, std::max(target, size.x + 2 * pad), target);
        lv_obj_set_style_bg_color(button, lv_color_hex(kBubbleBg), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(kBubbleBgPressed), LV_STATE_PRESSED);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE); /* a tap keeps the field as it is */
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, bubble_button_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(button, reinterpret_cast<void *>(static_cast<intptr_t>(item.action)));
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, item.text);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
        lv_obj_center(label);
        if (item.action == kActCopy) copy_label_ = label;
    }
    lv_obj_update_layout(bubble_);
}

/* Lay the handles and the bubble out for the field as it stands: handles under the line at the caret (or left of the
 * range's start and right of its end), the bubble above the field centred on the selection, clamped into the screen and
 * under the header; below the field when there is no room above and the keyboard leaves some; else over the label row.
 * Done again only when something that matters moved (box, edges, keyboard), so a slow panel redraws little. */
void TextField::touch_layout(bool force)
{
    if (touch_ == Touch::Off || !box_ || !area_ || !editing_) return;
    lv_display_t *display = lv_obj_get_display(box_);
    if (!display) return;
    lv_obj_update_layout(box_);
    lv_obj_t *label = lv_textarea_get_label(area_);
    lv_area_t box, lc;
    lv_obj_get_coords(box_, &box);
    lv_obj_get_coords(label, &lc);
    const int sw = static_cast<int>(lv_display_get_horizontal_resolution(display));
    const int sh = static_cast<int>(lv_display_get_vertical_resolution(display));
    const int osk_top = sh - osk::bottom_inset();
    const int head = layout_.header_h;
    auto edge_x = [&](std::size_t bytes) {
        lv_point_t pos;
        lv_label_get_letter_pos(label, static_cast<uint32_t>(text_edit::char_index(buf_.text(), bytes)), &pos);
        return static_cast<int>(lc.x1 + pos.x);
    };
    const bool range = touch_ == Touch::Range;
    const int lo_x = edge_x(range ? buf_.sel_lo() : buf_.caret());
    const int hi_x = range ? edge_x(buf_.sel_hi()) : lo_x;
    lv_point_t first;
    lv_label_get_letter_pos(label, 0, &first);
    const int line_h = lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN));
    const int attach_y = static_cast<int>(lc.y1) + first.y + line_h;
    const int line_mid = static_cast<int>(lc.y1) + first.y + line_h / 2;
    const bool in_view = box.y2 > head && box.y1 < osk_top && box.x2 > 0 && box.x1 < sw;
    const long long key[8] = {box.x1, box.y1, lo_x, hi_x, osk_top, attach_y,
                              (range ? 1 : 0) | (bubble_open_ ? 2 : 0) | (dragging_ ? 4 : 0) | (in_view ? 8 : 0) | (bubble_ ? 16 : 0), 0};
    if (!force && std::memcmp(key, geom_key_, sizeof(key)) == 0) return;
    std::memcpy(geom_key_, key, sizeof(key));

    if (range) {
        drop(h_caret_, true);
        touch_make_handle(h_start_, Role::Start);
        touch_make_handle(h_end_, Role::End);
    } else {
        drop(h_start_, true);
        drop(h_end_, true);
        touch_make_handle(h_caret_, Role::Caret);
    }
    const int target = cp0_ui_tok(CP0_TOK_TARGET);
    auto place = [&](lv_obj_t *handle, Role role, int ex) {
        if (!handle) return;
        const bool visible = in_view && ex >= box.x1 && ex <= box.x2;
        if (visible) lv_obj_remove_flag(handle, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(handle, LV_OBJ_FLAG_HIDDEN);
        int hx = role == Role::Caret ? ex - target / 2 : role == Role::Start ? ex - target : ex;
        hx = std::max(0, std::min(hx, sw - target));
        /* The touch area starts just under the text line and is 7 mm high. When the keyboard leaves less room it is cut at the
         * keyboard's edge (never under it); only if even that is less than 7 mm it may rise as far as the middle of the text line,
         * no further, so a tap on the text still reaches the field. */
        int hy = attach_y - 2;
        int hh = target;
        if (hy + hh > osk_top) {
            hy = std::max(std::min(hy, osk_top - target), line_mid + 4);
            hh = std::max(1, osk_top - hy);
        }
        lv_obj_set_size(handle, target, hh);
        lv_obj_set_pos(handle, hx, hy);
        lv_obj_t *stem = lv_obj_get_child(handle, 0);
        lv_obj_t *dot = lv_obj_get_child(handle, 1);
        const int rel = ex - hx;
        const int dot_cx = role == Role::Caret ? rel : role == Role::Start ? rel - kHandleDotPx / 2 - 1 : rel + kHandleDotPx / 2 + 1;
        lv_obj_set_pos(stem, rel - 1, attach_y - hy);
        lv_obj_set_pos(dot, dot_cx - kHandleDotPx / 2, attach_y - hy + kHandleStemPx - 1);
    };
    if (range) {
        place(h_start_, Role::Start, lo_x);
        place(h_end_, Role::End, hi_x);
    } else {
        place(h_caret_, Role::Caret, lo_x);
    }

    touch_build_bubble();
    if (!bubble_) return;
    /* the buttons start at the TARGET token (7 mm, at least 44 px) each time: a compressed bubble may be laid out again */
    for (uint32_t i = 0; i < lv_obj_get_child_count(bubble_); ++i) lv_obj_set_height(lv_obj_get_child(bubble_, i), target);
    lv_obj_update_layout(bubble_);
    const int bw = lv_obj_get_width(bubble_);
    int bh = lv_obj_get_height(bubble_);
    const int margin = cp0_ui_tok(CP0_TOK_SPACE_S);
    const int gap = 4;
    int mid = (lo_x + hi_x) / 2;
    mid = std::max(static_cast<int>(box.x1), std::min(mid, static_cast<int>(box.x2)));
    const int bx = std::max(margin, std::min(mid - bw / 2, sw - margin - bw));
    int by = static_cast<int>(box.y1) - gap - bh;
    if (by < head) {
        const int below = std::max(static_cast<int>(box.y2), attach_y + kHandleStemPx + kHandleDotPx) + gap;
        if (below + bh <= osk_top) { /* no room above: below the handles */
            by = below;
        } else {
            /* nor below (the keyboard is up): over the label row and the field's top padding, above the text line. The
             * buttons give up height for it, never below 44 px (the TARGET token's floor). */
            const int text_top = static_cast<int>(lc.y1) + first.y;
            const int buttons = std::max(kMinTouchPx, std::min(target, text_top - gap - head - 2));
            for (uint32_t i = 0; i < lv_obj_get_child_count(bubble_); ++i) lv_obj_set_height(lv_obj_get_child(bubble_, i), buttons);
            lv_obj_update_layout(bubble_);
            bh = lv_obj_get_height(bubble_);
            by = std::max(head, text_top - gap - bh);
        }
    }
    lv_obj_set_pos(bubble_, bx, by);
    if (in_view) lv_obj_remove_flag(bubble_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(bubble_, LV_OBJ_FLAG_HIDDEN);
}

void TextField::touch_action(int action)
{
    switch (action) {
    case kActSelectAll:
        buf_.select_all();
        touch_ = Touch::Range;
        bubble_open_ = true; /* the range's own bubble comes up at once */
        refresh();
        break;
    case kActPaste:
        bubble_open_ = false;
        paste_clipboard();
        refresh();
        break;
    case kActCut:
        bubble_open_ = false;
        cut_selection();
        refresh();
        break;
    case kActCopy:
        if (copy_selection() && copy_label_) {
            lv_label_set_text(copy_label_, "Copied"); /* the button keeps its size (it was measured for this text) */
            if (copied_timer_) lv_timer_delete(copied_timer_);
            copied_timer_ = lv_timer_create(copied_timer_cb, kCopiedMs, this);
            lv_timer_set_repeat_count(copied_timer_, 1);
        } else {
            bubble_open_ = false;
            touch_drop_bubble();
            touch_layout(true);
        }
        break;
    default:
        break;
    }
}

void TextField::touch_handle_event(lv_event_t *event, Role role)
{
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point = {0, 0};
    if (indev) lv_indev_get_point(indev, &point);
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED: {
        dragging_ = drag_moved_ = false;
        drag_press_x_ = point.x;
        drag_press_y_ = point.y;
        lv_obj_t *label = lv_textarea_get_label(area_);
        lv_area_t lc;
        lv_obj_get_coords(label, &lc);
        lv_point_t pos;
        const std::size_t at = role == Role::Start ? buf_.sel_lo() : role == Role::End ? buf_.sel_hi() : buf_.caret();
        lv_label_get_letter_pos(label, static_cast<uint32_t>(text_edit::char_index(buf_.text(), at)), &pos);
        drag_grab_dx_ = point.x - static_cast<int>(lc.x1 + pos.x); /* the finger's offset from the edge it holds */
        drag_fixed_ = role == Role::Start ? buf_.sel_hi() : buf_.sel_lo();
        break;
    }
    case LV_EVENT_PRESSING: {
        if (!drag_moved_ && std::abs(point.x - drag_press_x_) < kDragSlopPx && std::abs(point.y - drag_press_y_) < kDragSlopPx)
            break;
        drag_moved_ = true;
        if (!dragging_) { /* the bubble steps aside while a handle is held (and costs a slow panel nothing) */
            dragging_ = true;
            touch_drop_bubble();
        }
        std::size_t index = byte_at_x(point.x - drag_grab_dx_, true);
        if (role == Role::Caret) {
            buf_.set_caret(index, false);
        } else if (role == Role::Start) {
            index = std::min(index, text_edit::prev_boundary(buf_.text(), drag_fixed_)); /* a handle never crosses the other */
            buf_.select_range(index, drag_fixed_);
        } else {
            index = std::max(index, text_edit::next_boundary(buf_.text(), drag_fixed_));
            buf_.select_range(drag_fixed_, index);
        }
        touch_refresh_model();
        break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        if (lv_event_get_code(event) == LV_EVENT_RELEASED && !drag_moved_) bubble_open_ = !bubble_open_; /* a tap: the bubble */
        dragging_ = false;
        touch_drop_bubble();
        touch_layout(true);
        break;
    default:
        break;
    }
}

void TextField::handle_event_cb(lv_event_t *event)
{
    auto *self = static_cast<TextField *>(lv_event_get_user_data(event));
    if (!self || !self->area_) return;
    lv_obj_t *target = lv_event_get_target_obj(event);
    const Role role = target == self->h_start_ ? Role::Start : target == self->h_end_ ? Role::End : Role::Caret;
    self->touch_handle_event(event, role);
}

void TextField::bubble_button_cb(lv_event_t *event)
{
    auto *self = static_cast<TextField *>(lv_event_get_user_data(event));
    if (!self || !self->area_ || self->touch_ == Touch::Off) return;
    const int action = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    self->touch_action(action);
}

void TextField::indev_press_cb(lv_event_t *event)
{
    auto *self = static_cast<TextField *>(lv_event_get_user_data(event));
    if (!self || self->touch_ == Touch::Off) return;
    auto *obj = static_cast<lv_obj_t *>(lv_event_get_param(event));
    if (obj && (inside(obj, self->box_) || inside(obj, self->bubble_) || inside(obj, self->h_caret_) ||
                inside(obj, self->h_start_) || inside(obj, self->h_end_)))
        return;
    /* the keyboard is not "outside": typing over a selection must still find the selection */
    auto *indev = static_cast<lv_indev_t *>(lv_event_get_target(event));
    lv_point_t point = {0, 0};
    if (indev) lv_indev_get_point(indev, &point);
    lv_display_t *display = lv_obj_get_display(self->box_);
    if (display && osk::bottom_inset() > 0 &&
        point.y >= static_cast<int>(lv_display_get_vertical_resolution(display)) - osk::bottom_inset())
        return;
    self->touch_outside();
}

void TextField::touch_timer_cb(lv_timer_t *timer)
{
    auto *self = static_cast<TextField *>(lv_timer_get_user_data(timer));
    if (self) self->touch_layout(false);
}

void TextField::copied_timer_cb(lv_timer_t *timer)
{
    auto *self = static_cast<TextField *>(lv_timer_get_user_data(timer));
    if (!self) return;
    self->copied_timer_ = nullptr; /* a one-shot: LVGL deletes it after this call */
    self->bubble_open_ = false;    /* the selection and its handles stay; a tap on a handle opens the bubble again */
    self->touch_drop_bubble();
    self->touch_layout(true);
}

} // namespace ui
} // namespace cp0
