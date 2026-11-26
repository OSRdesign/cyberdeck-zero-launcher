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
 *
 * The clipboard strip (step 2): while the field is edited, Paste / Copy / Clear under it, in the row (ROW token high, no
 * keyboard focus, never a long press); CopyButton, at the end of this file, is the same Copy for a read-only value.
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
constexpr uint32_t kStripBg = 0x262626;        /* the strip's pills (the status strip's grey) */
constexpr uint32_t kStripBgHot = 0x4A4A4A;
constexpr uint32_t kStripBgOff = 0x161616;
constexpr uint32_t kStripText = 0xFFFFFF;
constexpr uint32_t kStripTextOff = 0x5F5F5F;
constexpr uint32_t kCopiedText = 0x2ECC71;     /* "Copied" */
constexpr uint32_t kCopiedMs = 2000;
constexpr int kStripFloorPx = 32;              /* the smallest pill, where the keyboard leaves no room for 7 mm */
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

/* What the strip knows of the clipboard: set by every call that asks it (a probe when a field starts editing, a copy, a
 * paste), so the buttons are drawn without a round trip of their own */
enum class ClipAvail { Unknown, Empty, Full, Absent };
ClipAvail g_clip = ClipAvail::Unknown;

bool clip_gone(int result)
{
    return result == CP0_APP_CLIPBOARD_NOSERVICE || result == CP0_APP_CLIPBOARD_UNSUPPORTED;
}

/* is there a text, or is there no clipboard at all (an older launcher, no socket, a stalled one)? Reads at most 8 bytes of
 * it (zeroed again) */
void clipboard_probe()
{
    if (!clipboard_ready()) {
        g_clip = ClipAvail::Absent;
        return;
    }
    char peek[8];
    size_t len = 0;
    const int result = cp0_app_clipboard_get(peek, sizeof(peek), &len, kClipboardTimeoutMs);
    clipboard_note(result);
    volatile char *bytes = peek;
    for (size_t i = 0; i < sizeof(peek); ++i) bytes[i] = 0;
    g_clip = clip_gone(result) ? ClipAvail::Absent : (result == CP0_APP_CLIPBOARD_OK && len > 0 ? ClipAvail::Full : ClipAvail::Empty);
}

void set_enabled(lv_obj_t *obj, bool enabled)
{
    if (!obj) return;
    if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

void set_shown(lv_obj_t *obj, bool shown)
{
    if (!obj) return;
    if (shown) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

/* a pill of the strip: the look of the status strip's buttons (dark grey, white text), a lighter grey while pressed, a
 * darker one with grey text when disabled; one ROW token high; no keyboard focus */
lv_obj_t *make_pill(lv_obj_t *parent, int height, int pad_hor, int text_px)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    if (lv_obj_get_group(button)) lv_group_remove_obj(button);
    lv_obj_set_size(button, LV_SIZE_CONTENT, height);
    lv_obj_set_style_radius(button, height / 2, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kStripBg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kStripBgHot), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(kStripBgOff), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(kStripText), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(kStripTextOff), LV_STATE_DISABLED);
    lv_obj_set_style_text_font(button, cp0_ui_font_px(text_px), 0);
    lv_obj_set_style_pad_hor(button, pad_hor, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE); /* a tap leaves the field being edited, and the keyboard, as they are */
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    return button;
}

int text_width(const char *text, int px)
{
    lv_point_t size;
    lv_text_get_size(&size, text, cp0_ui_font_px(px), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
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
    beside_ = beside;
    const int field_w = beside ? layout_.roller_w - spec_.label_w - layout_.gap : layout_.roller_w;

    row_ = lv_obj_create(parent);
    lv_obj_remove_style_all(row_);
    lv_obj_set_size(row_, layout_.roller_w, LV_SIZE_CONTENT);
    if (beside) {
        lv_obj_set_flex_flow(row_, spec_.clipboard_strip ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW); /* the strip wraps under */
        lv_obj_set_flex_align(row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row_, layout_.gap, 0);
        lv_obj_set_style_pad_row(row_, std::max(2, layout_.gap / 3), 0);
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
    build_strip();
    refresh();
    style_editing();
}

TextField::~TextField()
{
    if (spec_.type == TextType::Password) buf_.wipe(); /* the display copy goes with the row */
    if (copied_timer_) lv_timer_delete(copied_timer_);
    if (hide_timer_) lv_timer_delete(hide_timer_);
    copied_timer_ = hide_timer_ = nullptr;
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
    if (self->copied_timer_) lv_timer_delete(self->copied_timer_);
    if (self->hide_timer_) lv_timer_delete(self->hide_timer_);
    self->copied_timer_ = self->hide_timer_ = nullptr;
    self->row_ = self->label_ = self->box_ = self->area_ = self->eye_ = self->eye_label_ = nullptr;
    self->strip_ = self->paste_ = self->copy_ = self->copy_label_ = self->clear_ = nullptr;
}

/* ---- the clipboard strip */

lv_obj_t *TextField::clip_button(Clip which) const
{
    switch (which) {
    case Clip::Paste: return paste_;
    case Clip::Copy: return copy_;
    default: return clear_;
    }
}

void TextField::build_strip()
{
    if (!spec_.clipboard_strip || !row_) return;
    strip_ = lv_obj_create(row_);
    lv_obj_remove_style_all(strip_);
    if (lv_obj_get_group(strip_)) lv_group_remove_obj(strip_);
    lv_obj_set_size(strip_, layout_.roller_w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(strip_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(strip_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(strip_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(strip_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(strip_, LV_OBJ_FLAG_HIDDEN); /* only while the field is edited */

    const int px = layout_.text_px > 0 ? layout_.text_px : layout_.value_px;
    auto add = [&](const char *text, lv_obj_t **label_out) {
        lv_obj_t *button = make_pill(strip_, layout_.row_h, 0, px);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_add_event_cb(button, clip_event_cb, LV_EVENT_CLICKED, this);
        if (label_out) *label_out = label;
        return button;
    };
    paste_ = add("Paste", nullptr);
    copy_ = add("Copy", &copy_label_);
    clear_ = add("Clear", nullptr);
    size_strip(layout_.row_h, false);
}

/* The pills' size: one ROW token (7 mm) high where there is room; `tight` (the room above the on-screen keyboard cannot hold
 * the field and a strip: 640x480, 480x320) a smaller pill, text scaled with it, the label above the field gone and the gaps at
 * their minimum. The three pills (Copy as wide as Copied) always fit the row: on a narrow screen their padding gives. */
void TextField::size_strip(int pill_h, bool tight)
{
    if (!strip_) return;
    const int row_h = std::max(1, layout_.row_h);
    const int base_px = layout_.text_px > 0 ? layout_.text_px : layout_.value_px;
    const int px = tight ? std::max(12, base_px * pill_h / row_h) : base_px;
    const int columns = tight ? 4 : std::max(4, layout_.gap);
    const int texts = text_width("Paste", px) + text_width("Copied", px) + text_width("Clear", px);
    const int pad_hor = std::max(2, std::min(std::max(layout_.gap, pill_h * 2 / 5), (layout_.roller_w - 2 * columns - texts) / 6));
    lv_obj_set_style_pad_column(strip_, columns, 0);
    lv_obj_set_style_pad_top(strip_, tight ? 2 : std::max(2, layout_.gap / 2), 0);
    lv_obj_set_style_pad_row(row_, tight ? 2 : std::max(2, layout_.gap / 3), 0);
    for (lv_obj_t *button : {paste_, copy_, clear_}) {
        lv_obj_set_height(button, pill_h);
        lv_obj_set_style_radius(button, pill_h / 2, 0);
        lv_obj_set_style_text_font(button, cp0_ui_font_px(px), 0);
        lv_obj_set_style_pad_hor(button, pad_hor, 0);
    }
    lv_obj_set_style_min_width(copy_, text_width("Copied", px) + 2 * pad_hor, 0); /* the pill keeps its width when it says Copied */
    if (label_ && !beside_) set_shown(label_, !tight);
    strip_h_ = pill_h;
}

/* The strip must be fully visible above the on-screen keyboard: where the viewport (the page's body, a dialog's body) is too
 * short for the field's row with a 7 mm strip, the pills shrink to what is left, but not under kStripFloorPx; the field's own
 * label goes while they do. A page's body only (a dialog sizes itself to its content). Runs when the keyboard comes, goes or
 * changes the room. */
void TextField::fit_strip()
{
    if (!strip_ || !editing_ || fitting_ || !row_) return;
    /* the page's body is the row's own parent and scrolls; a dialog's content does not (its viewport lays itself out, with
     * its message, in Dialog::relayout): no shrinking there */
    lv_obj_t *view = lv_obj_get_parent(row_);
    if (!view || !lv_obj_has_flag(view, LV_OBJ_FLAG_SCROLLABLE)) return;
    lv_obj_update_layout(view);
    const int room = lv_obj_get_height(view);
    const lv_font_t *label_font = lv_obj_get_style_text_font(label_, LV_PART_MAIN);
    const int label_line = beside_ || !label_font ? 0 : lv_font_get_line_height(label_font) + std::max(2, layout_.gap / 3);
    const int pad = std::max(2, layout_.gap / 3);
    const int need = label_line + layout_.row_h + pad + std::max(2, layout_.gap / 2) + layout_.row_h;
    const int before = strip_h_;
    const bool tight = need > room;
    if (!tight) {
        size_strip(layout_.row_h, false);
    } else {
        const int left = room - layout_.row_h - 4;
        size_strip(std::max(kStripFloorPx, std::min(layout_.row_h, left)), true);
    }
    if (strip_h_ == before && !tight && !lv_obj_has_flag(strip_, LV_OBJ_FLAG_HIDDEN)) return;
    lv_obj_update_layout(row_);
    fitting_ = true;
    osk::content_changed(); /* a dialog lays itself out for the new height */
    fitting_ = false;
    lv_obj_update_layout(view);
    lv_area_t row_area, view_area;
    lv_obj_get_coords(row_, &row_area);
    lv_obj_get_coords(view, &view_area);
    /* a page or dialog scrolled to keep the field in view in content_changed(); only a shrunk strip that still sticks out of the
     * viewport (it has no room to spare) puts the row's top at the viewport's */
    if (tight && (row_area.y2 > view_area.y2 || row_area.y1 < view_area.y1))
        lv_obj_scroll_by(view, 0, view_area.y1 - row_area.y1, LV_ANIM_OFF);
}

/* which buttons show and which are greyed: from the text, the type and what the clipboard is known to hold */
void TextField::update_strip()
{
    if (!strip_) return;
    const bool has_text = !buf_.text().empty();
    set_shown(paste_, true);
    set_enabled(paste_, g_clip == ClipAvail::Full);
    set_shown(copy_, spec_.type != TextType::Password); /* a password never leaves the field */
    set_enabled(copy_, has_text && g_clip != ClipAvail::Absent);
    set_shown(clear_, has_text);
}

void TextField::show_strip(bool shown)
{
    if (!strip_) return;
    if (shown) {
        update_strip();
    } else {
        show_copied(false);
        size_strip(layout_.row_h, false); /* the label above the field is back for the next time */
    }
    set_shown(strip_, shown);
    lv_obj_update_layout(row_);
}

void TextField::show_copied(bool copied)
{
    if (copied_timer_) {
        lv_timer_delete(copied_timer_);
        copied_timer_ = nullptr;
    }
    if (!copy_label_) return;
    lv_label_set_text(copy_label_, copied ? "Copied" : "Copy");
    if (copied) {
        lv_obj_set_style_text_color(copy_label_, lv_color_hex(kCopiedText), 0);
        copied_timer_ = lv_timer_create(copied_timer_cb, kCopiedMs, this);
        lv_timer_set_repeat_count(copied_timer_, 1);
        lv_timer_set_auto_delete(copied_timer_, false); /* deleted here, so that a stale pointer is never kept */
    } else {
        lv_obj_remove_local_style_prop(copy_label_, LV_STYLE_TEXT_COLOR, 0);
    }
}

void TextField::copied_timer_cb(lv_timer_t *timer)
{
    auto *self = static_cast<TextField *>(lv_timer_get_user_data(timer));
    if (self) self->show_copied(false);
}

/* The field stopped being edited by a press elsewhere (a button or a row below it): the strip stays until the finger is
 * up, so that nothing moves under the finger between the press and the release (a moved button would lose its tap). */
void TextField::hide_timer_cb(lv_timer_t *timer)
{
    auto *self = static_cast<TextField *>(lv_timer_get_user_data(timer));
    if (!self) return;
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) return;
    lv_timer_t *self_timer = self->hide_timer_;
    self->hide_timer_ = nullptr;
    if (self_timer) lv_timer_delete(self_timer);
    if (self->editing_ || !self->row_) return;
    self->show_strip(false);
    osk::content_changed();
}

void TextField::clip_event_cb(lv_event_t *event)
{
    auto *self = static_cast<TextField *>(lv_event_get_user_data(event));
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (!self || !target) return;
    if (target == self->paste_) self->on_clip(Clip::Paste);
    else if (target == self->copy_) self->on_clip(Clip::Copy);
    else if (target == self->clear_) self->on_clip(Clip::Clear);
}

/* A tap on a strip button (a pointer tap; the field stays being edited). A greyed button does nothing. */
void TextField::on_clip(Clip which)
{
    lv_obj_t *button = clip_button(which);
    if (!editing_ || !button || lv_obj_has_state(button, LV_STATE_DISABLED)) return;
    switch (which) {
    case Clip::Paste: paste_clipboard(); break;
    case Clip::Copy: copy_selection(); break;
    case Clip::Clear: clear_text(); break;
    }
    update_strip();
    lv_obj_update_layout(row_);
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
    if (editing_) update_strip();
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
    if (hide_timer_) { /* the strip of the press that left this field a moment ago: this field takes it over */
        lv_timer_delete(hide_timer_);
        hide_timer_ = nullptr;
    }
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
    if (strip_) {
        clipboard_probe(); /* Paste is greyed when the clipboard is empty or the launcher has none */
        show_strip(true);
        fit_strip();
        if (!fit_listener_) fit_listener_ = osk::add_inset_listener([this](int) { fit_strip(); });
    }
    osk::request(box_, spec_.type, spec_.multiline);
    if (strip_) osk::content_changed(); /* a page keeps the field in view, a dialog lays itself out for the taller row */
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
    if (fit_listener_) osk::remove_inset_listener(fit_listener_);
    fit_listener_ = 0;
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
    if (!silent && strip_ && !lv_obj_has_flag(strip_, LV_OBJ_FLAG_HIDDEN)) {
        lv_indev_t *indev = lv_indev_active();
        const bool touching = indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER &&
                              lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
        if (touching) {
            if (!hide_timer_) hide_timer_ = lv_timer_create(hide_timer_cb, 30, this);
        } else {
            show_strip(false);
            osk::content_changed();
        }
    }
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
    g_clip = clip_gone(result) ? ClipAvail::Absent : result == CP0_APP_CLIPBOARD_OK ? ClipAvail::Full : g_clip;
    if (editing_ && strip_) update_strip();
    if (result != CP0_APP_CLIPBOARD_OK) return false;
    if (editing_ && strip_) show_copied(true);
    return true;
}

void TextField::cut_selection()
{
    if (spec_.type == TextType::Password || !buf_.selecting() || !clipboard_ready()) return;
    const std::string text = buf_.selection();
    if (text.size() > CP0_APP_CLIPBOARD_MAX) return; /* it would not fit whole: cutting must not lose text */
    const int result = cp0_app_clipboard_set(text.data(), text.size(), kClipboardTimeoutMs);
    clipboard_note(result);
    g_clip = clip_gone(result) ? ClipAvail::Absent : result == CP0_APP_CLIPBOARD_OK ? ClipAvail::Full : g_clip;
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
    g_clip = clip_gone(result) ? ClipAvail::Absent : (result == CP0_APP_CLIPBOARD_OK && len > 0 ? ClipAvail::Full : ClipAvail::Empty);
    raw.resize(result == CP0_APP_CLIPBOARD_OK ? len : 0);
    std::string clean = text_edit::sanitize_paste(raw, spec_.multiline, spec_.type == TextType::Number);
    secure_zero(raw); /* the clipboard may hold a secret: no copy of it outlives this call but the field's */
    if (!clean.empty() && buf_.paste(clean)) after_edit();
    secure_zero(clean);
    if (editing_) update_strip();
}

void TextField::clear_text()
{
    if (buf_.text().empty()) return;
    buf_.wipe(); /* zeroed in memory, a Password's or not */
    after_edit();
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
        if (buf_.selecting()) { /* with a selection Esc takes two steps: this one clears it, the next one ends the editing */
            buf_.collapse();
            refresh();
            return;
        }
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

/* ---- CopyButton: Copy for a value that is shown, not edited */

CopyButton::CopyButton(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, std::function<std::string()> value, bool with_label)
    : layout_(layout), value_(std::move(value)), with_label_(with_label)
{
    const int height = layout_.row_h; /* 7 mm */
    const int px = layout_.text_px > 0 ? layout_.text_px : layout_.value_px;
    button_ = make_pill(parent, height, with_label_ ? std::max(layout_.gap, height * 2 / 5) : 0, px);
    if (!with_label_) lv_obj_set_width(button_, height); /* round */
    label_ = lv_label_create(button_);
    lv_obj_add_event_cb(button_, click_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(button_, delete_cb, LV_EVENT_DELETE, this);
    clipboard_probe(); /* greyed when the launcher has no clipboard */
    show_copied(false);
    update();
}

CopyButton::~CopyButton()
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    if (!button_) return;
    lv_obj_remove_event_cb_with_user_data(button_, delete_cb, this);
    lv_obj_delete(button_);
}

void CopyButton::delete_cb(lv_event_t *event)
{
    auto *self = static_cast<CopyButton *>(lv_event_get_user_data(event));
    if (!self) return;
    if (self->timer_) lv_timer_delete(self->timer_);
    self->timer_ = nullptr;
    self->button_ = self->label_ = nullptr;
}

void CopyButton::update()
{
    if (!button_) return;
    set_enabled(button_, g_clip != ClipAvail::Absent && value_ && !value_().empty());
}

void CopyButton::show_copied(bool copied)
{
    copied_ = copied;
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (!label_) return;
    if (copied) {
        lv_label_set_text(label_, with_label_ ? LV_SYMBOL_OK " Copied" : LV_SYMBOL_OK);
        lv_obj_set_style_text_color(label_, lv_color_hex(kCopiedText), 0);
        timer_ = lv_timer_create(timer_cb, kCopiedMs, this);
        lv_timer_set_repeat_count(timer_, 1);
        lv_timer_set_auto_delete(timer_, false);
    } else {
        lv_label_set_text(label_, with_label_ ? LV_SYMBOL_COPY " Copy" : LV_SYMBOL_COPY);
        lv_obj_remove_local_style_prop(label_, LV_STYLE_TEXT_COLOR, 0);
    }
}

void CopyButton::timer_cb(lv_timer_t *timer)
{
    auto *self = static_cast<CopyButton *>(lv_timer_get_user_data(timer));
    if (self) self->show_copied(false);
}

bool CopyButton::copy()
{
    if (!button_ || !value_ || !clipboard_ready()) return false;
    std::string text = text_edit::clip_bytes(value_(), CP0_APP_CLIPBOARD_MAX);
    if (text.empty()) return false;
    const int result = cp0_app_clipboard_set(text.data(), text.size(), kClipboardTimeoutMs);
    secure_zero(text);
    clipboard_note(result);
    g_clip = clip_gone(result) ? ClipAvail::Absent : result == CP0_APP_CLIPBOARD_OK ? ClipAvail::Full : g_clip;
    update();
    if (result != CP0_APP_CLIPBOARD_OK) return false;
    show_copied(true);
    return true;
}

void CopyButton::click_cb(lv_event_t *event)
{
    auto *self = static_cast<CopyButton *>(lv_event_get_user_data(event));
    if (self && self->button_ && !lv_obj_has_state(self->button_, LV_STATE_DISABLED)) self->copy();
}

} // namespace ui
} // namespace cp0
