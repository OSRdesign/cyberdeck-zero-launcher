/*
 * SPDX-License-Identifier: MIT
 *
 * The built-in "Quick controls" page (see ui_app_quickctl.hpp).
 *
 *   header   the Settings header (cp0::ui::Page): the home grid title place ("Controls": "Quick controls" is cut by the Esc on the deck), the on-screen Esc
 *            (home), the shared status strip; its status line shows "Shutting down..." and the errors
 *   body     four buttons in a 2 x 2 grid (1 x 4 on a very narrow area): Shutdown, Reboot, Wi-Fi, Bluetooth. A radio
 *            button shows its state as a dot (filled On, ring Off, none Not available) AND the words On / Off / Not
 *            available / Switching..., so the state never rests on colour alone. Not available is dimmed and does
 *            nothing.
 *
 * Keys: the arrows move on the grid (no wrap), Tab / Shift+Tab step through the four (round), Enter acts, Esc goes home.
 * The keyboard delivers Tab as the raw KEY_TAB, not LV_KEY_NEXT, so the group does not move on it: the page reads it on
 * its screen (LV_EVENT_KEYBOARD), as the SSH form does, and steps the focus itself.
 * Touch: a tap acts. Opening the page and moving the focus only READ: the model's Controller runs an action only from
 * activate(), and activate() only from a press that began on the button (a stray key release or a finger that slid
 * onto it never acts). The service calls run on a worker; the page polls the result on a timer that runs only while
 * something is pending.
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_quickctl.hpp"

#include "../native_ui.hpp"
#include "../quick_controls_backend.hpp"

#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_roller.h"
#include "cp0_ui_widgets.hpp"
#include "input_keys.h"
#include "keyboard_input.h"

#include <algorithm>
#include <string>

namespace {

using quick_controls::Control;
using quick_controls::Radio;

constexpr uint32_t kFocus = 0x2D9CDB;        // the focused button's border (as the other native pages' buttons)
constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kTextDim = 0x707070;
constexpr uint32_t kTextOff = 0xB0B0B0;
constexpr uint32_t kTextOn = 0xC8FFD8;
constexpr uint32_t kDotOn = 0x66FF99;
constexpr uint32_t kPollMs = 120;

struct Look {
    uint32_t bg, pressed, sub, dot;
    bool dot_filled, dot_shown, dim;
};

Look look_of(Control control, const quick_controls::State &state)
{
    const bool live = quick_controls::active(control, state);
    switch (control) {
    case Control::Shutdown:
        return live ? Look{0x8E2B2B, 0xB03A3A, kText, 0, false, false, false}
                    : Look{0x3A2020, 0x3A2020, kTextDim, 0, false, false, true};
    case Control::Reboot:
        return live ? Look{0x8A5A12, 0xB07418, kText, 0, false, false, false}
                    : Look{0x3A3020, 0x3A3020, kTextDim, 0, false, false, true};
    default: break;
    }
    const bool wifi = control == Control::Wifi;
    const Radio radio = wifi ? state.wifi : state.bluetooth;
    const bool busy = wifi ? state.wifi_busy : state.bluetooth_busy;
    if (busy) return Look{0x242424, 0x242424, kTextOff, 0xB0B0B0, false, true, false};
    switch (radio) {
    case Radio::On: return Look{0x1E6B47, 0x2A8A5C, kTextOn, kDotOn, true, true, false};
    case Radio::Off: return Look{0x2A2A2A, 0x3C3C3C, kTextOff, 0xB0B0B0, false, true, false};
    case Radio::Unavailable: return Look{0x181818, 0x181818, kTextDim, 0, false, false, true};
    case Radio::Unknown: break;
    }
    return Look{0x2A2A2A, 0x3C3C3C, kTextOff, 0xB0B0B0, false, true, false}; // Unknown: tap reads again
}

const char *glyph(Control control)
{
    switch (control) {
    case Control::Shutdown: return LV_SYMBOL_POWER;
    case Control::Reboot: return LV_SYMBOL_REFRESH;
    case Control::Wifi: return LV_SYMBOL_WIFI;
    case Control::Bluetooth: return LV_SYMBOL_BLUETOOTH;
    }
    return "";
}

int text_width(const char *text, const lv_font_t *font)
{
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return static_cast<int>(size.x);
}

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_SCROLLABLE); // dots: not scrollable (see cp0_ui_page.cpp)
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(label, font, 0);
    return label;
}

} // namespace

UIQuickControlsPage::UIQuickControlsPage() : AppPage(), controller_(quick_controls::launcher_backend())
{
    set_page_title("Quick controls");

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
        lv_obj_remove_flag(root_screen_, LV_OBJ_FLAG_CLICK_FOCUSABLE); // a tap beside the buttons keeps the key focus
    }
    if (lv_obj_t *parent = ui_APP_Container ? ui_APP_Container : root_screen_) build(parent);
    // Reads only (the adapters, the two radios), on a worker: nothing is switched by opening the page.
    controller_.refresh();
    ensure_polling();
}

UIQuickControlsPage::~UIQuickControlsPage()
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    if (root_screen_) {
        lv_obj_remove_event_cb_with_user_data(root_screen_, screen_loaded_cb, this);
        lv_obj_remove_event_cb_with_user_data(root_screen_, tab_cb, this);
    }
    page_.reset();
}

void UIQuickControlsPage::build(lv_obj_t *parent)
{
    cp0_ui_roller_page_t page_layout{};
    if (cp0_ui_roller_page(cp0_ui_metrics_get(), cp0::ui::status_strip_left(), &page_layout) != 0) return;
    page_ = std::make_unique<cp0::ui::Page>(
        parent, page_layout, [](lv_obj_t *header, const char *text) { return cp0::ui::add_title(header, text); },
        [this] {
            if (navigate_home) navigate_home();
        });
    page_->set_title({"Controls"}); // the tile is "Quick controls"; that does not fit left of the Esc on the deck

    const cp0_ui_metrics_t *metrics = cp0_ui_metrics_get();
    const quick_controls_layout::Input in = quick_controls_layout::input_from_page(page_layout, metrics);
    layout_ = quick_controls_layout::compute(in, page_layout.esc_radius);
    if (!layout_.valid) return;

    lv_group_t *group = input_group();
    if (group) {
        lv_group_remove_all_objs(group);
        lv_group_set_wrap(group, true); // Tab goes round (tab_cb); the arrows never wrap (move_focus)
    }
    const Control order[quick_controls_layout::kButtons] = {Control::Shutdown, Control::Reboot, Control::Wifi,
                                                            Control::Bluetooth};
    const lv_font_t *icon_font = cp0_ui_font_px(layout_.icon_px);
    for (int i = 0; i < quick_controls_layout::kButtons; ++i) {
        Tile &tile = tiles_[static_cast<size_t>(i)];
        tile.page = this;
        tile.control = order[i];
        const quick_controls_layout::Cell &cell = layout_.cell[i];
        lv_obj_t *button = lv_button_create(page_->root());
        tile.button = button;
        if (lv_obj_get_group(button)) lv_group_remove_obj(button); // the group is filled in order below
        lv_obj_set_size(button, cell.w, cell.h);
        lv_obj_set_pos(button, cell.x, cell.y);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(button, layout_.radius, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_outline_width(button, 0, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        for (lv_state_t state : {LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY}) {
            lv_obj_set_style_border_color(button, lv_color_hex(kFocus), state);
            lv_obj_set_style_border_width(button, std::max(2, layout_.pad / 2), state);
            lv_obj_set_style_outline_width(button, 0, state);
        }
        lv_obj_add_event_cb(button, button_cb, LV_EVENT_ALL, &tile);

        tile.icon = make_label(button, icon_font);
        lv_label_set_long_mode(tile.icon, LV_LABEL_LONG_MODE_CLIP);
        lv_label_set_text(tile.icon, glyph(tile.control));
        tile.name = make_label(button, cp0_ui_font_px(layout_.name_px));
        lv_label_set_text(tile.name, quick_controls::label(tile.control));
        tile.sub = make_label(button, cp0_ui_font_px(layout_.sub_px));
        tile.dot = lv_obj_create(button);
        lv_obj_remove_style_all(tile.dot);
        lv_obj_remove_flag(tile.dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(tile.dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(tile.dot, layout_.dot_px, layout_.dot_px);
        lv_obj_set_style_radius(tile.dot, LV_RADIUS_CIRCLE, 0);
        if (group) lv_group_add_obj(group, button);
    }
    refresh_view();
    // the first focus: Wi-Fi (a reversible one), never a power button, so a stray Enter cannot power the deck off
    if (group) lv_group_focus_obj(tiles_[2].button);

    // the strip goes in when the screen is shown (it reads the Wi-Fi / Bluetooth state at once only on the active
    // screen), as in the native Settings
    if (root_screen_) {
        lv_obj_add_event_cb(root_screen_, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, this);
        if (LV_EVENT_KEYBOARD != 0)
            lv_obj_add_event_cb(root_screen_, tab_cb, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
    }
}

/* One button's look and the places of its parts, for the model's state. */
void UIQuickControlsPage::place_tile(Tile &tile, const std::string &text)
{
    const quick_controls::State &state = controller_.state();
    const Look look = look_of(tile.control, state);
    const quick_controls_layout::Cell &cell = layout_.cell[&tile - tiles_.data()];
    const int pad = layout_.pad;

    lv_obj_set_style_bg_color(tile.button, lv_color_hex(look.bg), 0);
    lv_obj_set_style_bg_color(tile.button, lv_color_hex(look.pressed), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(tile.name, lv_color_hex(look.dim ? kTextDim : kText), 0);
    lv_obj_set_style_text_color(tile.icon, lv_color_hex(look.dim ? kTextDim : kText), 0);
    lv_obj_set_style_text_color(tile.sub, lv_color_hex(look.sub), 0);
    lv_label_set_text(tile.sub, text.c_str());

    // the dot: filled = On, a ring = Off / checking / unknown, none = not available and the power buttons
    if (look.dot_shown) {
        lv_obj_remove_flag(tile.dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(tile.dot, lv_color_hex(look.dot), 0);
        lv_obj_set_style_bg_opa(tile.dot, look.dot_filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(tile.dot, lv_color_hex(look.dot), 0);
        lv_obj_set_style_border_width(tile.dot, look.dot_filled ? 0 : std::max(2, layout_.dot_px / 5), 0);
    } else {
        lv_obj_add_flag(tile.dot, LV_OBJ_FLAG_HIDDEN);
    }

    const lv_font_t *icon_font = lv_obj_get_style_text_font(tile.icon, LV_PART_MAIN);
    const lv_font_t *name_font = lv_obj_get_style_text_font(tile.name, LV_PART_MAIN);
    const lv_font_t *sub_font = lv_obj_get_style_text_font(tile.sub, LV_PART_MAIN);
    const int icon_h = static_cast<int>(lv_font_get_line_height(icon_font));
    const int name_h = static_cast<int>(lv_font_get_line_height(name_font));
    const int sub_h = static_cast<int>(lv_font_get_line_height(sub_font));
    const int icon_w = text_width(glyph(tile.control), icon_font);
    const int dot_w = look.dot_shown ? layout_.dot_px + pad : 0;

    int text_x = 0, text_w = 0, icon_x = 0, icon_y = 0, name_y = 0, sub_y = 0;
    if (layout_.stacked) {
        const int total = icon_h + pad + name_h + sub_h;
        const int top = std::max(0, (cell.h - total) / 2);
        icon_x = (cell.w - icon_w) / 2;
        icon_y = top;
        name_y = top + icon_h + pad;
        sub_y = name_y + name_h;
        text_x = pad;
        text_w = cell.w - 2 * pad;
    } else {
        const int block = name_h + sub_h;
        icon_x = pad * 2;
        icon_y = (cell.h - icon_h) / 2;
        name_y = (cell.h - block) / 2;
        sub_y = name_y + name_h;
        text_x = icon_x + icon_w + pad * 2;
        text_w = cell.w - text_x - pad;
    }
    lv_obj_set_pos(tile.icon, icon_x, icon_y);
    lv_obj_set_size(tile.icon, icon_w + 2, icon_h);
    lv_obj_set_pos(tile.name, text_x, name_y);
    lv_obj_set_size(tile.name, std::max(1, text_w), name_h);
    lv_obj_set_style_text_align(tile.name, layout_.stacked ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);

    // the state line: the dot, then the words, as one group (centred when stacked, at the text column otherwise)
    const int words_w = std::min(text_width(text.c_str(), sub_font) + 2, std::max(1, text_w - dot_w));
    const int group_w = dot_w + words_w;
    const int group_x = layout_.stacked ? text_x + (text_w - group_w) / 2 : text_x;
    lv_obj_set_pos(tile.sub, group_x + dot_w, sub_y);
    lv_obj_set_size(tile.sub, words_w, sub_h);
    lv_obj_set_style_text_align(tile.sub, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(tile.dot, group_x, sub_y + (sub_h - layout_.dot_px) / 2);
}

void UIQuickControlsPage::refresh_view()
{
    if (!layout_.valid) return;
    for (Tile &tile : tiles_)
        if (tile.button) place_tile(tile, quick_controls::state_text(tile.control, controller_.state()));
    if (page_) {
        const std::string &message = controller_.message();
        page_->set_caption(message, message.empty() ? cp0::ui::CaptionTone::Normal
                                   : controller_.message_error() ? cp0::ui::CaptionTone::Error
                                                                 : cp0::ui::CaptionTone::Warn);
    }
}

void UIQuickControlsPage::ensure_polling()
{
    if (!timer_) timer_ = lv_timer_create(poll_cb, kPollMs, this);
    else lv_timer_resume(timer_);
}

void UIQuickControlsPage::poll_cb(lv_timer_t *timer)
{
    auto *self = static_cast<UIQuickControlsPage *>(lv_timer_get_user_data(timer));
    if (!self) return;
    if (self->controller_.poll()) self->refresh_view();
    if (!self->controller_.pending()) lv_timer_pause(timer); // nothing in flight: no timer work until a tap
}

void UIQuickControlsPage::activate(Tile &tile)
{
    if (controller_.activate(tile.control) == quick_controls::ActionKind::None) return;
    refresh_view(); // "Switching..." / "Shutting down..." at once
    ensure_polling();
}

void UIQuickControlsPage::move_focus(const Tile &from, quick_controls::Move move)
{
    const int index = static_cast<int>(&from - tiles_.data());
    const int next = quick_controls::move_focus(index, layout_.cols, quick_controls_layout::kButtons, move);
    if (next != index && tiles_[static_cast<size_t>(next)].button)
        lv_group_focus_obj(tiles_[static_cast<size_t>(next)].button);
}

void UIQuickControlsPage::button_cb(lv_event_t *event)
{
    auto *tile = static_cast<Tile *>(lv_event_get_user_data(event));
    if (!tile || !tile->page) return;
    UIQuickControlsPage *self = tile->page;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        tile->armed = true;
        break;
    case LV_EVENT_PRESS_LOST:
        tile->armed = false;
        break;
    case LV_EVENT_CLICKED:
        // only the end of a press that began here acts (a key release left over from opening the page, or a finger
        // that slid onto the button, never does)
        if (tile->armed) {
            tile->armed = false;
            self->activate(*tile);
        }
        break;
    case LV_EVENT_KEY:
        switch (lv_event_get_key(event)) {
        case LV_KEY_UP: self->move_focus(*tile, quick_controls::Move::Up); break;
        case LV_KEY_DOWN: self->move_focus(*tile, quick_controls::Move::Down); break;
        case LV_KEY_LEFT: self->move_focus(*tile, quick_controls::Move::Left); break;
        case LV_KEY_RIGHT: self->move_focus(*tile, quick_controls::Move::Right); break;
        case LV_KEY_ESC:
            if (self->navigate_home) self->navigate_home();
            break;
        default: break;
        }
        break;
    default:
        break;
    }
}

void UIQuickControlsPage::screen_loaded_cb(lv_event_t *event)
{
    auto *self = static_cast<UIQuickControlsPage *>(lv_event_get_user_data(event));
    if (!self || self->strip_added_ || !self->page_) return;
    self->strip_added_ = true;
    cp0::ui::StatusStrip::add(self->page_->header(), native_ui::fill_status);
}

/* Tab / Shift+Tab: the focus one step round the four buttons (a press only; moving the focus never acts). */
void UIQuickControlsPage::tab_cb(lv_event_t *event)
{
    auto *self = static_cast<UIQuickControlsPage *>(lv_event_get_user_data(event));
    const auto *item = static_cast<const struct key_item *>(lv_event_get_param(event));
    if (!self || !item || item->key_state != KBD_KEY_PRESSED || item->key_code != KEY_TAB) return;
    lv_group_t *group = self->input_group();
    if (!group) return;
    if (item->mods & KBD_MOD_SHIFT) lv_group_focus_prev(group);
    else lv_group_focus_next(group);
}
