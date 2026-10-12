/*
 * SPDX-License-Identifier: MIT
 *
 * The SSH connection form on the native display (task 014 P4): built from the responsive shell's widgets, no geometry
 * of its own (every size from the layout tokens through the roller page layout).
 *
 *   header   the Settings header (cp0::ui::Page): the home grid's title place ("SSH", "SSH > Help"), the on-screen
 *            Esc (form: home; help: back to the form), the shared status strip
 *   body     Host, Port (number pad, digits only), User as TextFields, each label beside its field (one row token per
 *            field); Connect (primary) and Help buttons at the TARGET height; one status line (the model's validation
 *            messages, start failures). While the on-screen keyboard is up the buttons and the status line go (an
 *            error stays, under the focused field) and the focused field is scrolled into view (native_relayout)
 *   help     a TextBlock with the help text (ssh_native_form::help_text), Esc / Left back to the form
 *
 * Keys: Up / Down and Tab (Shift+Tab back) move between the fields and buttons (no wrap, as the compat form); Left /
 * Right between the buttons; Enter in Host / Port goes to the next field, in User connects (the keyboard goes first),
 * on Connect connects, on Help opens the help; F1 / KEY_HELP (ssh_shortcut_action) opens the help from anywhere in the
 * form; fn+F / fn+X (Cardputer keyboard) move like Up / Down; Esc stops editing a field, then leaves to home. The
 * on-screen keyboard comes only through the fields (the launcher's policy). Connecting copies the fields into the
 * same model, validates, saves the profile and opens the same terminal page as the compat form; its return comes back
 * here, on the native display.
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_ssh.hpp"

#include "../model/ssh_native_form_model.hpp"
#include "../model/ssh_shortcut_action.hpp"
#include "../native_ui.hpp"
#include "../settings/settings_hw_profile.hpp"

#include "cp0_font_service.hpp"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_roller.h"
#include "cp0_ui_widgets.hpp"
#include "input_keys.h"

#include <algorithm>

namespace {

constexpr uint32_t kButton = 0x2A2A2A;          /* the toolbar keys (the Dialog's buttons) ... */
constexpr uint32_t kButtonPressed = 0x2D9CDB;   /* ... and their pressed colour */
constexpr uint32_t kPrimary = CP0_UI_OSK_ENTER_RGB; /* the keyboard's Enter (the Dialog's primary button) */
constexpr uint32_t kPrimaryPressed = CP0_UI_OSK_ENTER_PRESSED_RGB;
constexpr uint32_t kFocus = 0x2D9CDB;           /* the TextField's editing border: the focused button */
constexpr uint32_t kStatus = 0x66CC88;          /* the page status line's colours */
constexpr uint32_t kStatusError = 0xFF6666;
constexpr int kLabelColumnPct = 22;             /* the label column beside the fields (Controller design: 20-25 %) */

const lv_font_t *button_font(int px, bool bold)
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

} // namespace

void UISSHPage::build_native()
{
    lv_display_t *display = root_screen_ ? lv_obj_get_display(root_screen_) : lv_display_get_default();
    const int width = static_cast<int>(display ? lv_display_get_horizontal_resolution(display) : 640);
    const int height = static_cast<int>(display ? lv_display_get_vertical_resolution(display) : 480);
    disable_top_bar();
    if (ui_APP_Container) {
        lv_obj_set_size(ui_APP_Container, width, height);
        lv_obj_set_pos(ui_APP_Container, 0, 0);
        lv_obj_remove_flag(ui_APP_Container, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    }
    if (root_screen_) {
        lv_obj_set_style_bg_color(root_screen_, lv_color_black(), LV_PART_MAIN);
        // a tap beside the form must not take the keys' focus away (as the native Settings host)
        lv_obj_remove_flag(root_screen_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    }
    lv_obj_t *parent = ui_APP_Container ? ui_APP_Container : root_screen_;
    if (!parent) return;

    cp0_ui_roller_page_t layout{};
    if (cp0_ui_roller_page(cp0_ui_metrics_get(), cp0::ui::status_strip_left(), &layout) != 0) return;
    native_page_ = std::make_unique<cp0::ui::Page>(
        parent, layout, [](lv_obj_t *header, const char *text) { return cp0::ui::add_title(header, text); },
        [this] {
            // the header's Esc: the help goes back to the form, the form leaves to home
            if (view_state_ == ViewState::HELP) {
                native_close_help();
                return;
            }
            if (view_state_ == ViewState::INPUT && navigate_home) navigate_home();
        });
    native_page_->set_title({"SSH"});

    const cp0_ui_metrics_t *metrics = cp0_ui_metrics_get();
    const int text_px = metrics ? metrics->text_px[CP0_TEXT_BODY] : layout.text_px;

    // labels beside their fields (one row token per field, Controller design): a column of 22 % of the row, at least
    // the longest label at the BODY size plus a gap
    int label_w = layout.roller_w * kLabelColumnPct / 100;
    if (const lv_font_t *label_font = button_font(text_px, false)) {
        for (size_t i = 0; i < SshConnectionModel::FIELD_COUNT; ++i) {
            lv_point_t size{};
            lv_text_get_size(&size, model_.label(i), label_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            label_w = std::max(label_w, static_cast<int>(size.x) + layout.gap);
        }
    }

    lv_obj_t *body = native_page_->body();
    for (size_t i = 0; i < SshConnectionModel::FIELD_COUNT; ++i) {
        cp0::ui::TextFieldSpec spec;
        spec.label = model_.label(i);
        spec.type = ssh_native_form::numeric_field(i) ? cp0::ui::TextType::Number : cp0::ui::TextType::Text;
        spec.text = model_.value(i);
        spec.max_bytes = ssh_native_form::field_max_bytes(i);
        spec.label_w = label_w;
        cp0::ui::TextFieldEvents events;
        events.submit = [this, i](const std::string &) { native_on_field_submit(i); };
        events.back = [this] {
            if (navigate_home) navigate_home();
        };
        native_fields_[i] = std::make_unique<cp0::ui::TextField>(body, layout, spec, events);
        // after the field's own handler (it starts editing first): the form follows the focus
        lv_obj_add_event_cb(native_fields_[i]->focus_object(), native_field_focus_cb, LV_EVENT_FOCUSED, this);
    }

    // Connect and Help: one row as wide as the fields, the TARGET height
    const int target = metrics ? metrics->tok[CP0_TOK_TARGET] : layout.row_h;
    lv_obj_t *buttons = plain(body);
    native_buttons_row_ = buttons;
    lv_obj_set_size(buttons, layout.roller_w, target);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, layout.gap, 0);
    auto add_button = [&](const char *text, bool primary) {
        lv_obj_t *button = lv_button_create(buttons);
        if (lv_obj_get_group(button)) lv_group_remove_obj(button); /* the group is filled in order below */
        lv_obj_set_height(button, target);
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_radius(button, layout.esc_radius, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(primary ? kPrimary : kButton), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(primary ? kPrimaryPressed : kButtonPressed), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_outline_width(button, 0, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(kFocus), LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(button, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUS_KEY);
        lv_obj_add_event_cb(button, native_button_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(button, native_button_cb, LV_EVENT_KEY, this);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, button_font(text_px, primary), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
        return button;
    };
    native_connect_ = add_button("Connect", true);
    native_help_button_ = add_button("Help", false);

    // the status line: one line reserved (the form does not jump when a message comes)
    native_status_ = lv_label_create(body);
    const lv_font_t *status_font = cp0_ui_font_px(layout.caption_px);
    lv_label_set_long_mode(native_status_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(native_status_, status_font, 0);
    lv_obj_set_width(native_status_, layout.roller_w);
    lv_obj_set_height(native_status_, status_font ? lv_font_get_line_height(status_font) : layout.caption_px);
    native_set_status();

    // the strip goes in when the screen is shown (it reads the Wi-Fi / Bluetooth state at once only on the active
    // screen), as in the native Settings; the help key and fn moves are read on the screen (LV_EVENT_KEYBOARD) before
    // the fields' own handler, which a field registers when it starts editing
    if (root_screen_) {
        lv_obj_add_event_cb(root_screen_, native_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, this);
        if (LV_EVENT_KEYBOARD != 0)
            lv_obj_add_event_cb(root_screen_, native_keyboard_cb, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD),
                                this);
    }
    native_inset_listener_ = cp0::ui::osk::add_inset_listener([this](int) { native_relayout(); });
    native_fill_group();
    native_relayout();
}

void UISSHPage::destroy_native()
{
    if (!native_) return;
    if (native_inset_listener_) cp0::ui::osk::remove_inset_listener(native_inset_listener_);
    native_inset_listener_ = 0;
    lv_async_call_cancel(native_close_help_cb, this);
    native_help_close_posted_ = false;
    if (root_screen_) {
        lv_obj_remove_event_cb_with_user_data(root_screen_, native_screen_loaded_cb, this);
        lv_obj_remove_event_cb_with_user_data(root_screen_, native_keyboard_cb, this);
    }
    native_help_.reset();
    for (auto &field : native_fields_) field.reset(); /* each stops editing: context, intercept, hint, keyboard */
    native_connect_ = native_help_button_ = native_status_ = native_buttons_row_ = nullptr;
    native_page_.reset();
}

/* The fields and buttons in the page's input group, in form order, without the screen (a key must never land on it)
 * and without wrapping (Up on Host and Down on Help stop, as the compat form's field moves). The first one added takes
 * the focus: Host edits at once, as typing went to Host in the compat form (the on-screen keyboard comes per the
 * launcher's policy). The help adds its text to the same group and focuses it; the form keeps its order. */
void UISSHPage::native_fill_group()
{
    lv_group_t *group = input_group();
    if (!group) return;
    lv_group_remove_all_objs(group);
    lv_group_set_wrap(group, false);
    for (const auto &field : native_fields_)
        if (field) lv_group_add_obj(group, field->focus_object());
    if (native_connect_) lv_group_add_obj(group, native_connect_);
    if (native_help_button_) lv_group_add_obj(group, native_help_button_);
}

void UISSHPage::native_screen_loaded_cb(lv_event_t *event)
{
    auto *self = static_cast<UISSHPage *>(lv_event_get_user_data(event));
    if (!self || self->native_strip_added_ || !self->native_page_) return;
    self->native_strip_added_ = true;
    cp0::ui::StatusStrip::add(self->native_page_->header(), native_ui::fill_status);
}

void UISSHPage::native_set_status()
{
    if (!native_status_) return;
    lv_label_set_text(native_status_, status_message_.c_str());
    lv_obj_set_style_text_color(native_status_, lv_color_hex(status_error_ ? kStatusError : kStatus), 0);
    native_relayout();
}

void UISSHPage::native_field_focus_cb(lv_event_t *event)
{
    auto *self = static_cast<UISSHPage *>(lv_event_get_user_data(event));
    if (self) self->native_relayout();
}

/* The form for the keyboard's state (Controller design, as the Wi-Fi password dialog hides its buttons):
 *   keyboard down  fields, Connect / Help, the status line, in that order; a form that fits shows from its top, one
 *                  that does not (480x320) scrolls to the focused field
 *   keyboard up    no Connect / Help row (the keyboard's Enter on User connects, Esc brings the row back); the status
 *                  line only for an error, right under the focused field; the focused field (and that error) scrolled
 *                  into view above the keyboard */
void UISSHPage::native_relayout()
{
    if (!native_page_ || view_state_ == ViewState::HELP) return;
    lv_obj_t *body = native_page_->body();
    if (!body) return;
    const bool keyboard = cp0::ui::osk::bottom_inset() > 0;
    const bool error = status_error_ && !status_message_.empty();

    lv_group_t *group = input_group();
    lv_obj_t *focused = group ? lv_group_get_focused(group) : nullptr;
    int field = -1;
    for (size_t i = 0; i < native_fields_.size(); ++i)
        if (native_fields_[i] && native_fields_[i]->focus_object() == focused) field = static_cast<int>(i);
    lv_obj_t *field_row = field >= 0 ? native_fields_[field]->row() : nullptr;

    if (native_buttons_row_) {
        if (keyboard) lv_obj_add_flag(native_buttons_row_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(native_buttons_row_, LV_OBJ_FLAG_HIDDEN);
    }
    bool status_shown = false;
    if (native_status_) {
        if (!keyboard) {
            lv_obj_move_to_index(native_status_, -1);
            lv_obj_remove_flag(native_status_, LV_OBJ_FLAG_HIDDEN);
        } else if (error && field_row) {
            lv_obj_move_to_index(native_status_, lv_obj_get_index(field_row) + 1);
            lv_obj_remove_flag(native_status_, LV_OBJ_FLAG_HIDDEN);
            status_shown = true;
        } else {
            lv_obj_add_flag(native_status_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    // with the error under it, the field may touch the header (no padding above the first row), so both fit where the
    // room is only a little taller than a row and a line (640x480: 125 px above the keyboard)
    lv_obj_set_style_pad_top(body, status_shown ? 0 : native_page_->layout().gap, 0);
    lv_obj_update_layout(body);
    if (!keyboard) lv_obj_scroll_to_y(body, 0, LV_ANIM_OFF);
    if (status_shown && field_row) lv_obj_scroll_to_y(body, lv_obj_get_y(field_row), LV_ANIM_OFF);
    if (field_row) lv_obj_scroll_to_view(field_row, LV_ANIM_OFF);
}

void UISSHPage::native_sync_model()
{
    std::array<std::string, SshConnectionModel::FIELD_COUNT> texts;
    for (size_t i = 0; i < SshConnectionModel::FIELD_COUNT; ++i)
        if (native_fields_[i]) texts[i] = native_fields_[i]->text();
    ssh_native_form::sync(model_, texts);
}

void UISSHPage::native_stop_editing()
{
    for (auto &field : native_fields_)
        if (field) field->end_edit();
}

void UISSHPage::native_on_field_submit(size_t index)
{
    if (ssh_native_form::submit_action(index) == ssh_native_form::SubmitAction::NEXT_FIELD) {
        if (index + 1 < native_fields_.size() && native_fields_[index + 1])
            lv_group_focus_obj(native_fields_[index + 1]->focus_object());
        return;
    }
    // the last field: the keyboard goes, so a validation message shows with the whole form
    native_stop_editing();
    do_connect();
}

void UISSHPage::native_focus_step(lv_obj_t *from, bool next)
{
    lv_group_t *group = input_group();
    if (!group || !from) return;
    if (next) lv_group_focus_next(group);
    else lv_group_focus_prev(group);
}

void UISSHPage::native_show_help()
{
    if (view_state_ != ViewState::INPUT || !native_page_ || native_help_) return;
    lv_group_t *group = input_group();
    native_help_return_ = group ? lv_group_get_focused(group) : nullptr;
    native_stop_editing();
    if (lv_obj_t *body = native_page_->body()) lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    native_page_->set_title({"SSH", "Help"});
    native_help_ = std::make_unique<cp0::ui::TextBlock>(
        native_page_->root(), native_page_->layout(), ssh_native_form::help_text(APPLAUNCH_TXT_SSH_HELP_HINT),
        nullptr, [this] { native_close_help(); });
    view_state_ = ViewState::HELP;
    if (group) {
        lv_group_add_obj(group, native_help_->focus_object());
        lv_group_focus_obj(native_help_->focus_object());
    }
}

/* Esc / Left in the TextBlock (inside its own key event) or the header's Esc: the help goes on the next turn. */
void UISSHPage::native_close_help()
{
    if (view_state_ != ViewState::HELP || native_help_close_posted_) return;
    native_help_close_posted_ = lv_async_call(native_close_help_cb, this) == LV_RESULT_OK;
}

void UISSHPage::native_close_help_cb(void *user)
{
    auto *self = static_cast<UISSHPage *>(user);
    if (!self) return;
    self->native_help_close_posted_ = false;
    if (self->view_state_ != ViewState::HELP) return;
    if (self->native_page_) {
        self->native_page_->set_title({"SSH"});
        if (lv_obj_t *body = self->native_page_->body()) lv_obj_remove_flag(body, LV_OBJ_FLAG_HIDDEN);
    }
    self->view_state_ = ViewState::INPUT;
    // focus back where the help was asked (a field edits again) before the text goes: its removal from the group
    // must not move the focus through the form
    lv_obj_t *back_to = self->native_help_return_ ? self->native_help_return_ : self->native_help_button_;
    self->native_help_return_ = nullptr;
    if (back_to) lv_group_focus_obj(back_to);
    self->native_help_.reset();
    self->native_relayout();
}

void UISSHPage::native_button_cb(lv_event_t *event)
{
    auto *self = static_cast<UISSHPage *>(lv_event_get_user_data(event));
    auto *button = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (!self || !button || self->view_state_ != ViewState::INPUT) return;
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        if (button == self->native_connect_) self->do_connect();
        else if (button == self->native_help_button_) self->native_show_help();
        return;
    }
    switch (lv_event_get_key(event)) {
    case LV_KEY_UP:
        self->native_focus_step(button, false);
        break;
    case LV_KEY_DOWN:
        self->native_focus_step(button, true);
        break;
    case LV_KEY_LEFT:
        if (button == self->native_help_button_) lv_group_focus_obj(self->native_connect_);
        break;
    case LV_KEY_RIGHT:
        if (button == self->native_connect_) lv_group_focus_obj(self->native_help_button_);
        break;
    case LV_KEY_ESC:
        if (self->navigate_home) self->navigate_home();
        break;
    default:
        break;
    }
}

/* LV_EVENT_KEYBOARD on the page's screen, before a field's own handler: the help key (F1 / KEY_HELP), the Cardputer
 * fn+F / fn+X field moves, and Tab while no field edits (a field that edits moves on Tab itself). */
void UISSHPage::native_keyboard_cb(lv_event_t *event)
{
    auto *self = static_cast<UISSHPage *>(lv_event_get_user_data(event));
    const auto *item = static_cast<const struct key_item *>(lv_event_get_param(event));
    if (!self || !item || self->view_state_ != ViewState::INPUT || item->key_state != KBD_KEY_PRESSED) return;
    lv_group_t *group = self->input_group();
    lv_obj_t *focused = group ? lv_group_get_focused(group) : nullptr;
    switch (ssh_shortcut_action(item)) {
    case SshShortcutAction::SHOW_HELP:
        lv_event_stop_processing(event);
        self->native_show_help();
        return;
    case SshShortcutAction::PREVIOUS_FIELD:
    case SshShortcutAction::NEXT_FIELD:
        // Up / Down are the fields' and buttons' own keys; only the Cardputer's fn moves are taken here
        if (!(item->mods & KBD_MOD_FN)) break;
        lv_event_stop_processing(event); /* the field must not type the letter */
        self->native_focus_step(focused, ssh_shortcut_action(item) == SshShortcutAction::NEXT_FIELD);
        return;
    case SshShortcutAction::NONE:
        break;
    }
    if (item->key_code != KEY_TAB) return;
    for (const auto &field : self->native_fields_)
        if (field && field->editing()) return;
    self->native_focus_step(focused, !(item->mods & KBD_MOD_SHIFT));
}
