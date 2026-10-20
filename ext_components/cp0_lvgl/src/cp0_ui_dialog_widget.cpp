/*
 * SPDX-License-Identifier: MIT
 *
 * Dialog of the responsive shell (task 014 P3b; see cp0_ui_widgets.hpp, layout in cp0_ui_dialog.c).
 *
 * Objects: backdrop (the page's body area under the header, dimmed, takes the taps meant for the page) > box (title,
 * viewport > content column, buttons). The box follows the on-screen keyboard: on every inset change the layout is
 * computed again (buttons, padding, over the header, body height) and the field being edited is scrolled into view.
 * Colours from the shell's palette: the keyboard's key grey and line, the toolbar keys, the keyboard's Enter blue for
 * the primary button.
 */

#include "cp0_ui_widgets.hpp"

#include "cp0_font_service.hpp"
#include "cp0_ui_metrics_lvgl.h"

#include <algorithm>
#include <utility>

namespace cp0 {
namespace ui {
namespace {

constexpr uint32_t kBox = CP0_UI_OSK_KEY_RGB;        /* #1C1C1C */
constexpr uint32_t kBorder = CP0_UI_OSK_BORDER_RGB;  /* #333333 */
constexpr uint32_t kTitle = 0xFFFFFF;
constexpr uint32_t kText = 0xE0E0E0;
constexpr uint32_t kMessage = 0x66CC88;              /* the page status line's colours */
constexpr uint32_t kMessageError = 0xFF6666;
constexpr uint32_t kButton = 0x2A2A2A;               /* the toolbar keys ... */
constexpr uint32_t kButtonPressed = 0x2D9CDB;        /* ... and their pressed colour */
constexpr uint32_t kPrimary = CP0_UI_OSK_ENTER_RGB;  /* the keyboard's Enter */
constexpr uint32_t kPrimaryPressed = CP0_UI_OSK_ENTER_PRESSED_RGB;
constexpr uint32_t kDestructive = 0xFF8A8E;          /* a Destructive button's label (multitasking P-d) */

const lv_font_t *text_font(int px, bool bold)
{
    return cp0_fonts().get(bold ? "Montserrat-Bold.ttf" : "Montserrat-Medium.ttf", static_cast<uint16_t>(px));
}

lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    if (lv_obj_get_group(obj)) lv_group_remove_obj(obj);
    return obj;
}

lv_area_t coords(const lv_obj_t *obj)
{
    lv_area_t area{};
    lv_obj_get_coords(obj, &area);
    return area;
}

bool is_inside(lv_obj_t *obj, lv_obj_t *ancestor)
{
    for (; obj; obj = lv_obj_get_parent(obj))
        if (obj == ancestor) return true;
    return false;
}

} // namespace

Dialog::Dialog(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, DialogSpec spec)
    : layout_(layout), field_layout_(layout), spec_(std::move(spec))
{
    if (spec_.buttons.size() > CP0_UI_DIALOG_MAX_BUTTONS) spec_.buttons.resize(CP0_UI_DIALOG_MAX_BUTTONS);
    cp0_ui_dialog_layout(cp0_ui_metrics_get(), layout_.header_h, 0, 0, 0, static_cast<int>(spec_.buttons.size()),
                         spec_.page != nullptr, &geometry_);
    field_layout_.roller_w = geometry_.content_w;
    field_layout_.roller_x = 0;

    backdrop_ = lv_obj_create(parent);
    lv_obj_remove_style_all(backdrop_);
    if (lv_obj_get_group(backdrop_)) lv_group_remove_obj(backdrop_);
    lv_obj_set_pos(backdrop_, 0, 0);
    lv_obj_set_size(backdrop_, layout_.w, layout_.h);
    lv_obj_remove_flag(backdrop_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(backdrop_, LV_OBJ_FLAG_CLICKABLE); /* the header's Esc stays tappable (= Cancel) */
    lv_obj_add_event_cb(backdrop_, backdrop_delete_cb, LV_EVENT_DELETE, this);
    // the dimmed part: the body under the header; it swallows the taps meant for the page under it
    lv_obj_t *shade = plain(backdrop_);
    lv_obj_set_pos(shade, 0, layout_.header_h);
    lv_obj_set_size(shade, layout_.w, std::max(0, layout_.h - layout_.header_h));
    lv_obj_set_style_bg_color(shade, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(shade, LV_OPA_70, 0);
    lv_obj_add_flag(shade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(shade, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    box_ = lv_obj_create(backdrop_);
    lv_obj_remove_style_all(box_);
    if (lv_obj_get_group(box_)) lv_group_remove_obj(box_); /* the page adds it to its group when it has no field */
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(box_, lv_color_hex(kBox), 0);
    lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box_, lv_color_hex(kBorder), 0);
    lv_obj_set_style_border_width(box_, 1, 0);
    lv_obj_set_style_border_opa(box_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box_, geometry_.radius, 0);
    lv_obj_add_event_cb(box_, box_event_cb, LV_EVENT_KEY, this);

    title_ = lv_label_create(box_);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_remove_flag(title_, LV_OBJ_FLAG_SCROLLABLE); /* dots: not scrollable (Page's caption, cp0_ui_page.cpp) */
    lv_obj_set_style_text_font(title_, text_font(geometry_.title_px, true), 0);
    lv_obj_set_style_text_color(title_, lv_color_hex(kTitle), 0);
    lv_label_set_text(title_, spec_.title.c_str());

    viewport_ = lv_obj_create(box_);
    lv_obj_remove_style_all(viewport_);
    if (lv_obj_get_group(viewport_)) lv_group_remove_obj(viewport_);
    lv_obj_set_scroll_dir(viewport_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(viewport_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(viewport_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_remove_flag(viewport_, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /* relayout() scrolls the editing field itself */

    content_ = lv_obj_create(viewport_);
    lv_obj_remove_style_all(content_);
    if (lv_obj_get_group(content_)) lv_group_remove_obj(content_);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(content_, geometry_.content_w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content_, geometry_.gap, 0);

    if (spec_.message_line) {
        message_ = lv_label_create(content_);
        lv_label_set_long_mode(message_, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_remove_flag(message_, LV_OBJ_FLAG_SCROLLABLE); /* dots: not scrollable (see title_) */
        const lv_font_t *font = text_font(geometry_.text_px, false);
        lv_obj_set_style_text_font(message_, font, 0);
        lv_obj_set_width(message_, geometry_.content_w);
        lv_obj_set_height(message_, lv_font_get_line_height(font)); /* reserved: the layout does not jump */
        lv_label_set_text(message_, "");
    }

    for (std::size_t i = 0; i < spec_.buttons.size(); ++i) {
        const DialogButton &spec = spec_.buttons[i];
        const bool primary = spec.role == ButtonRole::Primary;
        lv_obj_t *button = lv_button_create(box_);
        if (lv_obj_get_group(button)) lv_group_remove_obj(button); /* touch only: Enter / Esc are the keyboard's */
        lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_set_style_radius(button, layout_.esc_radius, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(primary ? kPrimary : kButton), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(primary ? kPrimaryPressed : kButtonPressed), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_DISABLED); /* set_button_enabled(): the label follows */
        lv_obj_set_style_text_opa(button, LV_OPA_50, LV_STATE_DISABLED);
        lv_obj_set_user_data(button, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, spec.text.c_str());
        lv_obj_set_style_text_font(label, text_font(geometry_.button_px, primary), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(spec.role == ButtonRole::Destructive ? kDestructive : 0xFFFFFF), 0);
        lv_obj_center(label);
        buttons_.push_back(button);
    }

    inset_listener_ = osk::add_inset_listener([this](int) { relayout(); });
    relayout();
}

Dialog::~Dialog()
{
    if (inset_listener_) osk::remove_inset_listener(inset_listener_);
    inset_listener_ = 0;
    title_to_header(false);
    if (backdrop_) {
        lv_obj_remove_event_cb_with_user_data(backdrop_, backdrop_delete_cb, this);
        lv_obj_delete(backdrop_);
    }
}

void Dialog::backdrop_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<Dialog *>(lv_event_get_user_data(event));
    if (!self) return;
    if (self->inset_listener_) osk::remove_inset_listener(self->inset_listener_);
    self->inset_listener_ = 0;
    self->backdrop_ = self->box_ = self->title_ = self->viewport_ = self->content_ = self->message_ = nullptr;
    self->buttons_.clear();
    self->spec_.page = nullptr; /* the page's objects go with its root: nothing to give back */
    self->title_in_header_ = false;
}

/* Step 3 of the layout: the title shows in the page header's title slot instead of the box, or comes back. */
void Dialog::title_to_header(bool in_header)
{
    if (!spec_.page) return;
    if (in_header) spec_.page->set_title_override(this, spec_.title.c_str());
    else if (title_in_header_) spec_.page->set_title_override(this, nullptr);
    title_in_header_ = in_header;
}

lv_obj_t *Dialog::add_text(const std::string &text)
{
    if (!content_) return nullptr;
    lv_obj_t *label = lv_label_create(content_);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, geometry_.content_w);
    lv_obj_set_style_text_font(label, text_font(geometry_.text_px, false), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
    lv_label_set_text(label, text.c_str());
    if (message_) lv_obj_move_to_index(message_, -1); /* the status line stays last */
    return label;
}

void Dialog::set_title(const std::string &title)
{
    spec_.title = title;
    if (title_) lv_label_set_text(title_, title.c_str());
    relayout();
}

void Dialog::set_message(const std::string &text, bool error)
{
    if (!message_) return;
    if (message_ != lv_obj_get_child(content_, -1)) lv_obj_move_to_index(message_, -1);
    lv_label_set_text(message_, text.c_str());
    lv_obj_set_style_text_color(message_, lv_color_hex(error ? kMessageError : kMessage), 0);
    if (viewport_) {
        lv_obj_update_layout(viewport_);
        keep_in_view(); /* the message shows even when the body scrolls */
    }
}

void Dialog::set_button_enabled(std::size_t index, bool enabled)
{
    if (index >= buttons_.size()) return;
    if (enabled) lv_obj_remove_state(buttons_[index], LV_STATE_DISABLED);
    else lv_obj_add_state(buttons_[index], LV_STATE_DISABLED);
}

void Dialog::raise()
{
    if (backdrop_) lv_obj_move_foreground(backdrop_);
}

/* Sizes and place from the layout service (cp0_ui_dialog_layout) for the content as it is now and the keyboard as
 * it is now. */
void Dialog::relayout()
{
    if (!box_) return;
    if (message_ && message_ != lv_obj_get_child(content_, -1)) lv_obj_move_to_index(message_, -1); /* stays last */
    const bool has_title = !spec_.title.empty();
    const lv_font_t *title_font = lv_obj_get_style_text_font(title_, LV_PART_MAIN);
    const int title_h = has_title && title_font ? lv_font_get_line_height(title_font) : 0;
    lv_obj_update_layout(content_);
    const int content_h = lv_obj_get_height(content_);
    cp0_ui_dialog_layout(cp0_ui_metrics_get(), layout_.header_h, osk::bottom_inset(), title_h, content_h,
                         static_cast<int>(buttons_.size()), spec_.page != nullptr, &geometry_);
    const cp0_ui_dialog_t &g = geometry_;

    lv_obj_set_pos(box_, g.x, g.y);
    lv_obj_set_size(box_, g.w, g.h);
    title_to_header(has_title && g.title_in_header);
    if (has_title && !g.title_in_header) {
        lv_obj_remove_flag(title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(title_, g.pad, g.title_y);
        lv_obj_set_width(title_, g.content_w);
        lv_obj_set_height(title_, title_h); /* one line: a long title ends with dots instead of running into the body */
    } else {
        lv_obj_add_flag(title_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_pos(viewport_, g.pad, g.body_y);
    lv_obj_set_size(viewport_, g.content_w, g.body_h);
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        lv_obj_t *button = buttons_[i];
        if (static_cast<int>(i) >= g.buttons) {
            lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(button, g.button_w, g.button_h);
        lv_obj_set_pos(button, g.pad + static_cast<int>(i) * (g.button_w + g.button_gap), g.button_y);
    }
    lv_obj_update_layout(box_);
    keep_in_view();
}

/* What the body shows when it scrolls: the field being edited (the keyboard's) with its label and, under it, the
 * status line when it says something; when that is too tall the label goes first, then the bottom wins. */
void Dialog::keep_in_view()
{
    if (!viewport_ || !content_) return;
    if (!geometry_.scrolls) {
        lv_obj_scroll_to_y(viewport_, 0, LV_ANIM_OFF);
        return;
    }
    lv_obj_t *field = osk::owner();
    if (!field || !is_inside(field, content_)) field = nullptr;
    const bool message = message_ && lv_label_get_text(message_) && lv_label_get_text(message_)[0];
    if (!field && !message) return;
    lv_obj_t *row = field ? lv_obj_get_parent(field) : nullptr;
    if (row == content_) row = field;
    /* content coordinates: independent of the current scroll */
    const int32_t origin = coords(content_).y1;
    const int32_t top = coords(row ? row : message_).y1 - origin;
    const int32_t box_top = coords(field ? field : message_).y1 - origin;
    const int32_t bottom = coords(message ? message_ : field).y2 + 1 - origin;
    const int32_t view = geometry_.body_h;
    int32_t y = 0;
    if (bottom - top <= view) y = std::max<int32_t>(0, bottom - view); /* all of it, as low as it goes */
    else if (bottom - box_top <= view) y = box_top;                      /* without the field's label */
    else y = bottom - view;                                              /* the end: the message */
    lv_obj_scroll_to_y(viewport_, y, LV_ANIM_OFF);
}

void Dialog::run(ButtonRole role)
{
    for (const DialogButton &button : spec_.buttons)
        if (button.role == role && button.action) {
            const auto action = button.action; /* the action may delete this dialog */
            action();
            return;
        }
    // a notice (one button, task 014 P3c): Enter and Esc both press it
    if (spec_.buttons.size() == 1 && spec_.buttons.front().action) {
        const auto action = spec_.buttons.front().action;
        action();
    }
}

void Dialog::primary()
{
    run(ButtonRole::Primary);
}

void Dialog::cancel()
{
    run(ButtonRole::Cancel);
}

void Dialog::box_event_cb(lv_event_t *event)
{
    auto *self = static_cast<Dialog *>(lv_event_get_user_data(event));
    if (!self) return;
    const uint32_t key = lv_event_get_key(event);
    lv_event_stop_processing(event);
    if (key == LV_KEY_ENTER) self->primary();
    else if (key == LV_KEY_ESC) self->cancel();
}

void Dialog::button_event_cb(lv_event_t *event)
{
    auto *self = static_cast<Dialog *>(lv_event_get_user_data(event));
    auto *button = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (!self || !button) return;
    const auto index = static_cast<std::size_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(button)));
    if (index >= self->spec_.buttons.size()) return;
    const auto action = self->spec_.buttons[index].action; /* the action may delete this dialog */
    if (action) action();
}

} // namespace ui
} // namespace cp0
