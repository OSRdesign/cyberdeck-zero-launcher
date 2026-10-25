/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-d: the switcher screen and the legacy sheet (see switcher_view.hpp).
 *
 * Sizes, from the approved mockup (640x480 px) turned into the layout service's terms:
 *   header           the shared page header (cp0::ui::Page: title, Esc, status strip), the home grid's bar height
 *   margins, gaps    1.1 mm (12 px on the deck)
 *   bottom bar       TARGET high (7 mm: 79 px on the deck), Home | Close all, the toolbar keys' look
 *   cards            the rest in a fixed 2x2 grid; inside: 1.4 mm padding, the icon 5.7 mm (64 px on the deck), the name
 *                    in BODY bold, the state in CAPTION with a dot 2/3 of it, the X key's target the TARGET square in
 *                    the top-right corner with its visible key SPACE_S inside it
 * Colours: the home tiles' (ground, border, the blue selection and press), the mockup's state dots (green #35C25A,
 * grey #9B9BA1, amber #E7A31A) and its red for the closing actions (#FF8A8E).
 */
#include "switcher_view.hpp"

#include "native_ui.hpp"

#include "cp0_display.h"
#include "cp0_font_service.hpp"
#include "cp0_ui_metrics.h"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_roller.h"
#include "cp0_ui_widgets.hpp"
#include "input_keys.h"
#include "keyboard_input.h"

#include <algorithm>
#include <utility>

namespace multitask_ui {
namespace {

using multitask::CardTone;
using multitask::kItemCloseAll;
using multitask::kItemHome;
using multitask::kSwitcherSlots;

constexpr uint32_t kGround = 0x000000;
constexpr uint32_t kCard = 0x1E1E1E;        /* the home tile's ground ... */
constexpr uint32_t kCardBorder = 0x3A3A3A;  /* ... its border ... */
constexpr uint32_t kCardPressed = 0x33414D; /* ... pressed (touch) ... */
constexpr uint32_t kSelect = 0x2D9CDB;      /* ... and the keyboard selection */
constexpr uint32_t kKey = 0x2A2A2A;         /* the toolbar keys / dialog buttons */
constexpr uint32_t kKeyPressed = 0x2D9CDB;
constexpr uint32_t kCloseKey = 0x2E2E32;    /* the X key (mockup) */
constexpr uint32_t kText = 0xF2F2F2;
constexpr uint32_t kMuted = 0x9B9BA1;
constexpr uint32_t kGreen = 0x35C25A;
constexpr uint32_t kAmber = 0xE7A31A;
constexpr uint32_t kDanger = 0xFF8A8E;
constexpr int kBorder = 2;                  /* the mockup's card border, also the selection's */
constexpr int kGapMmX10 = 11;               /* 12 px on the deck */
constexpr int kInsetMmX10 = 14;             /* 16 px */
constexpr int kIconMmX10 = 57;              /* 64 px */
constexpr int kDash = 8, kDashGap = 6;      /* the free slot's dashed frame */

const cp0_ui_metrics_t &metrics()
{
    return *cp0_ui_metrics_get();
}

int mm(int mm_x10, int floor_px)
{
    return cp0_ui_mm_px(metrics().ppmm_x100, mm_x10, floor_px);
}

const lv_font_t *text_font(int px, bool bold)
{
    const lv_font_t *f = cp0_fonts().get(bold ? "Montserrat-Bold.ttf" : "Montserrat-Medium.ttf", static_cast<uint16_t>(px));
    return f ? f : cp0_ui_font_px(px);
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

lv_obj_t *black_screen()
{
    lv_display_t *previous = lv_display_get_default();
    if (lv_display_t *native = cp0_display_native()) lv_display_set_default(native);
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_display_set_default(previous);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kGround), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    return screen;
}

uint32_t tone_rgb(CardTone tone)
{
    switch (tone) {
    case CardTone::Green: return kGreen;
    case CardTone::Grey: return kMuted;
    case CardTone::Amber: return kAmber;
    }
    return kMuted;
}

/* The Cardputer keyboard's F / X / Z / C as arrows, as the home grid reads them */
uint32_t nav_alias(uint32_t key)
{
    switch (key) {
    case KEY_F: return KEY_UP;
    case KEY_X: return KEY_DOWN;
    case KEY_Z: return KEY_LEFT;
    case KEY_C: return KEY_RIGHT;
    default: return key;
    }
}

int text_width(const std::string &text, const lv_font_t *font)
{
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/* A first line that fits in `width` and the rest: whole words when the first word fits, else the first word cut
 * between characters (UTF-8), at least one character. */
std::pair<std::string, std::string> split_line(const std::string &text, const lv_font_t *font, int width)
{
    if (text_width(text, font) <= width) return {text, std::string()};
    std::size_t cut = std::string::npos;
    for (std::size_t at = text.find(' '); at != std::string::npos; at = text.find(' ', at + 1)) {
        if (text_width(text.substr(0, at), font) > width) break;
        cut = at;
    }
    if (cut != std::string::npos && cut > 0) {
        std::size_t rest = cut;
        while (rest < text.size() && text[rest] == ' ') ++rest;
        return {text.substr(0, cut), text.substr(rest)};
    }
    std::size_t chars = 0;
    for (std::size_t i = 1; i <= text.size(); ++i) {
        if (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue; /* inside a character */
        if (chars > 0 && text_width(text.substr(0, i), font) > width) break;
        chars = i;
    }
    return {text.substr(0, chars), text.substr(chars)};
}

bool area_of(lv_obj_t *obj, lv_area_t *out)
{
    if (!obj || !out || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_obj_update_layout(obj);
    lv_obj_get_coords(obj, out);
    return true;
}

} // namespace

/* ------------------------------------------------------------------------------------------- SwitcherScreen */

SwitcherScreen::SwitcherScreen(SwitcherEvents events) : events_(std::move(events))
{
    const cp0_ui_metrics_t &m = metrics();
    cp0_ui_roller_page_t layout{};
    cp0_ui_roller_page(&m, cp0::ui::status_strip_left(), &layout);
    screen_ = black_screen();
    lv_obj_add_event_cb(screen_, loaded_event_cb, LV_EVENT_SCREEN_LOADED, this);
    if (LV_EVENT_KEYBOARD != 0)
        lv_obj_add_event_cb(screen_, key_event_cb, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
    page_ = std::make_unique<cp0::ui::Page>(
        screen_, layout, [](lv_obj_t *header, const char *text) { return cp0::ui::add_title(header, text); },
        [this] {
            if (events_.cancel) events_.cancel();
        });
    page_->set_title({"Open apps"});

    pad_ = gap_ = mm(kGapMmX10, 6);
    target_ = m.tok[CP0_TOK_TARGET];
    bar_h_ = target_;
    const int body_h = m.h - layout.header_h;
    card_w_ = std::max(1, (m.w - 2 * pad_ - gap_) / 2);
    card_h_ = std::max(1, (body_h - 2 * pad_ - bar_h_ - 2 * gap_) / 2);
    cards_y_ = layout.header_h + pad_;
    bar_y_ = m.h - pad_ - bar_h_;

    for (int i = 0; i < kSwitcherSlots; ++i) build_slot(i);

    const int button_w = card_w_;
    const int text_px = m.text_px[CP0_TEXT_BODY];
    const auto add_button = [&](const char *text, int x, bool danger) {
        lv_obj_t *button = lv_button_create(screen_);
        if (lv_obj_get_group(button)) lv_group_remove_obj(button); /* the screen's own keys move the selection */
        lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_set_pos(button, x, bar_y_);
        lv_obj_set_size(button, button_w, bar_h_);
        lv_obj_set_style_radius(button, layout.esc_radius, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(kKey), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(kKeyPressed), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_outline_width(button, 0, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(kSelect), 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, text_font(text_px, true), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(danger ? kDanger : kText), 0);
        lv_obj_center(label);
        return button;
    };
    home_ = add_button("Home", pad_, false);
    close_all_ = add_button("Close all", pad_ + card_w_ + gap_, true);
    set_cards({});
}

SwitcherScreen::~SwitcherScreen()
{
    page_.reset(); /* its objects go with the screen; it only forgets them */
    if (screen_) lv_obj_delete(screen_);
    screen_ = nullptr;
}

void SwitcherScreen::build_slot(int index)
{
    const cp0_ui_metrics_t &m = metrics();
    Slot &s = slots_[static_cast<std::size_t>(index)];
    const int x = pad_ + (index % 2) * (card_w_ + gap_);
    const int y = cards_y_ + (index / 2) * (card_h_ + gap_);

    s.card = lv_obj_create(screen_);
    if (lv_obj_get_group(s.card)) lv_group_remove_obj(s.card);
    lv_obj_remove_flag(s.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s.card, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_pos(s.card, x, y);
    lv_obj_set_size(s.card, card_w_, card_h_);
    lv_obj_set_style_radius(s.card, m.tok[CP0_TOK_RADIUS_S] + 6, 0); /* 14 px on the deck (mockup) */
    lv_obj_set_style_pad_all(s.card, 0, 0);
    lv_obj_set_style_shadow_width(s.card, 0, 0);
    lv_obj_set_style_border_width(s.card, kBorder, 0);
    lv_obj_set_style_bg_color(s.card, lv_color_hex(kCardPressed), LV_STATE_PRESSED);
    lv_obj_set_user_data(s.card, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
    lv_obj_add_event_cb(s.card, card_event_cb, LV_EVENT_CLICKED, this);

    inset_ = mm(kInsetMmX10, 8);
    const int icon = std::min(mm(kIconMmX10, 32), std::max(16, card_h_ - 2 * inset_));
    const int name_px = m.text_px[CP0_TEXT_BODY];
    const int state_px = m.text_px[CP0_TEXT_CAPTION];
    name_font_ = text_font(name_px, true);
    const lv_font_t *state_font = text_font(state_px, false);
    name_h_ = lv_font_get_line_height(name_font_);
    state_h_ = lv_font_get_line_height(state_font);
    line_gap_ = std::max(2, state_px / 4);
    text_x_ = inset_ + icon + inset_;
    dot_ = std::max(6, state_px * 2 / 3);

    s.icon = lv_image_create(s.card);
    lv_obj_remove_flag(s.icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s.icon, icon, icon);
    lv_image_set_inner_align(s.icon, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_pos(s.icon, inset_, (card_h_ - icon) / 2);

    /* the name on one or two lines and the state line: placed for each name by layout_text() */
    for (lv_obj_t **line : {&s.name, &s.name2}) {
        *line = lv_label_create(s.card);
        lv_obj_remove_flag(*line, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_long_mode(*line, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_remove_flag(*line, LV_OBJ_FLAG_SCROLLABLE); /* dots: not scrollable (cp0_ui_page.cpp's caption) */
        lv_obj_set_style_text_font(*line, name_font_, 0);
        lv_obj_set_style_text_color(*line, lv_color_hex(kText), 0);
    }

    s.dot = plain(s.card);
    lv_obj_set_size(s.dot, dot_, dot_);
    lv_obj_set_style_radius(s.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s.dot, LV_OPA_COVER, 0);

    s.state = lv_label_create(s.card);
    lv_obj_remove_flag(s.state, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_long_mode(s.state, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_remove_flag(s.state, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(s.state, state_font, 0);
    lv_obj_set_style_text_color(s.state, lv_color_hex(kMuted), 0);

    /* the X: a TARGET square in the corner takes the touch; the key drawn inside it is the mockup's smaller one */
    s.close = plain(s.card);
    lv_obj_add_flag(s.close, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s.close, target_, target_);
    lv_obj_set_pos(s.close, card_w_ - target_, 0);
    lv_obj_set_user_data(s.close, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
    lv_obj_add_event_cb(s.close, close_event_cb, LV_EVENT_ALL, this);
    const int key_inset = m.tok[CP0_TOK_SPACE_S];
    const int key = std::max(16, target_ - 2 * key_inset);
    s.close_key = plain(s.close);
    lv_obj_set_size(s.close_key, key, key);
    lv_obj_set_pos(s.close_key, target_ - key_inset - key, key_inset);
    lv_obj_set_style_radius(s.close_key, key * 12 / 56, 0);
    lv_obj_set_style_bg_color(s.close_key, lv_color_hex(kCloseKey), 0);
    lv_obj_set_style_bg_opa(s.close_key, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s.close_key, lv_color_hex(kKeyPressed), LV_STATE_PRESSED);
    lv_obj_t *glyph = lv_label_create(s.close_key);
    lv_label_set_text(glyph, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(glyph, cp0_ui_font_px(name_px), 0);
    lv_obj_set_style_text_color(glyph, lv_color_hex(kMuted), 0);
    lv_obj_center(glyph);

    /* a free slot: a dashed frame (straight dashes; the corners stay open where the cards are rounded) */
    using P = lv_value_precise_t; /* float or int, as the build's LVGL has it */
    const P r = static_cast<P>(m.tok[CP0_TOK_RADIUS_S] + 6);
    const P w = static_cast<P>(card_w_ - 1), h = static_cast<P>(card_h_ - 1), o = static_cast<P>(0);
    const lv_point_precise_t ends[4][2] = {{{r, o}, {w - r, o}},
                                           {{r, h}, {w - r, h}},
                                           {{o, r}, {o, h - r}},
                                           {{w, r}, {w, h - r}}};
    for (int i = 0; i < 4; ++i) {
        s.dash_points[static_cast<std::size_t>(i)] = {ends[i][0], ends[i][1]};
        lv_obj_t *line = lv_line_create(s.card);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
        lv_line_set_points(line, s.dash_points[static_cast<std::size_t>(i)].data(), 2);
        lv_obj_set_pos(line, 0, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(kCardBorder), 0);
        lv_obj_set_style_line_width(line, kBorder, 0);
        lv_obj_set_style_line_dash_width(line, kDash, 0);
        lv_obj_set_style_line_dash_gap(line, kDashGap, 0);
        s.dashes[static_cast<std::size_t>(i)] = line;
    }
    s.room = lv_label_create(s.card);
    lv_obj_remove_flag(s.room, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(s.room, "Room for one more");
    lv_obj_set_style_text_font(s.room, state_font, 0);
    lv_obj_set_style_text_color(s.room, lv_color_hex(kMuted), 0);
    lv_obj_center(s.room);
}

void SwitcherScreen::fill_slot(int index)
{
    Slot &s = slots_[static_cast<std::size_t>(index)];
    const bool used = index < static_cast<int>(cards_.size());
    const auto shown = [](lv_obj_t *obj, bool on) {
        if (on) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    };
    for (lv_obj_t *obj : {s.icon, s.name, s.dot, s.state, s.close}) shown(obj, used);
    for (lv_obj_t *line : s.dashes) shown(line, !used);
    shown(s.room, !used);
    if (!used) {
        shown(s.name2, false);
        lv_obj_remove_flag(s.card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(s.card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s.card, 0, 0);
        return;
    }
    const CardView &c = cards_[static_cast<std::size_t>(index)];
    lv_obj_add_flag(s.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s.card, lv_color_hex(kCard), 0);
    lv_obj_set_style_bg_opa(s.card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s.card, kBorder, 0);
    if (c.icon != s.icon_src) {
        s.icon_src = c.icon;
        if (c.icon.empty()) lv_image_set_src(s.icon, nullptr);
        else lv_image_set_src(s.icon, c.icon.c_str());
    }
    layout_text(s, c.name);
    lv_label_set_text(s.state, c.label.text);
    lv_obj_set_style_bg_color(s.dot, lv_color_hex(tone_rgb(c.label.tone)), 0);
}

/* The name and the state line in the card's text column. A line beside the X's target ends at the target's left
 * edge, a line under it runs to the card's inset. A name too long for one line wraps onto a second (words first; a
 * first word longer than the line is cut between characters), placed under the target when the card has the room,
 * and only that second line ends with dots. The state line follows. */
void SwitcherScreen::layout_text(Slot &s, const std::string &name)
{
    const int beside = std::max(8, card_w_ - target_ - text_x_);
    const int under = std::max(8, card_w_ - inset_ - text_x_);
    const auto width_at = [&](int y) { return y >= target_ ? under : beside; };
    const int one_h = name_h_ + line_gap_ + state_h_;
    const int two_h = 2 * name_h_ + line_gap_ + state_h_;
    int top = (card_h_ - one_h) / 2;
    std::pair<std::string, std::string> lines{name, std::string()};
    if (text_width(name, name_font_) > width_at(top)) {
        top = (card_h_ - two_h) / 2;
        if (top + name_h_ < target_ && target_ - name_h_ + two_h <= card_h_ - kBorder)
            top = target_ - name_h_; /* the second line under the X, where it has the card's width */
        top = std::max(kBorder, top);
        /* a narrow card (a 480 px wide portrait panel, report 038 S2): a first word wider than the line beside the X
         * would be cut between its characters there; when it fits the card's width, the name starts under the X */
        if (top < target_ && target_ + two_h <= card_h_ - kBorder) {
            const std::string first_word = name.substr(0, name.find(' '));
            const int first_w = text_width(first_word, name_font_);
            if (first_w > width_at(top) && first_w <= width_at(target_)) top = target_;
        }
        lines = split_line(name, name_font_, width_at(top));
    }
    const bool two = !lines.second.empty();
    lv_label_set_text(s.name, lines.first.c_str());
    lv_obj_set_pos(s.name, text_x_, top);
    lv_obj_set_size(s.name, width_at(top), name_h_);
    if (two) {
        lv_label_set_text(s.name2, lines.second.c_str());
        lv_obj_set_pos(s.name2, text_x_, top + name_h_);
        lv_obj_set_size(s.name2, width_at(top + name_h_), name_h_);
        lv_obj_remove_flag(s.name2, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s.name2, LV_OBJ_FLAG_HIDDEN);
    }
    const int state_y = top + (two ? 2 : 1) * name_h_ + line_gap_;
    const int state_x = text_x_ + dot_ + dot_ * 2 / 3;
    lv_obj_set_pos(s.dot, text_x_, state_y + (state_h_ - dot_) / 2);
    lv_obj_set_pos(s.state, state_x, state_y);
    lv_obj_set_size(s.state, std::max(8, width_at(state_y) - (state_x - text_x_)), state_h_);
}

void SwitcherScreen::set_cards(const std::vector<CardView> &cards)
{
    if (cards_.empty()) selected_ = 0; /* the first cards: the selection starts on the first one */
    cards_.assign(cards.begin(), cards.begin() + std::min<std::size_t>(cards.size(), kSwitcherSlots));
    for (int i = 0; i < kSwitcherSlots; ++i) fill_slot(i);
    selected_ = multitask::switcher_clamp(selected_, card_count());
    style_selection();
}

void SwitcherScreen::select(int item)
{
    selected_ = multitask::switcher_clamp(item, card_count());
    style_selection();
}

void SwitcherScreen::style_selection()
{
    for (int i = 0; i < kSwitcherSlots; ++i) {
        Slot &s = slots_[static_cast<std::size_t>(i)];
        if (i >= card_count()) continue;
        lv_obj_set_style_border_color(s.card, lv_color_hex(i == selected_ ? kSelect : kCardBorder), 0);
    }
    lv_obj_set_style_border_width(home_, selected_ == kItemHome ? kBorder : 0, 0);
    lv_obj_set_style_border_width(close_all_, selected_ == kItemCloseAll ? kBorder : 0, 0);
}

void SwitcherScreen::activate(int item)
{
    if (item == kItemHome) {
        if (events_.home) events_.home();
    } else if (item == kItemCloseAll) {
        if (events_.close_all) events_.close_all();
    } else if (item >= 0 && item < card_count()) {
        if (events_.pick) events_.pick(cards_[static_cast<std::size_t>(item)].id);
    }
}

bool SwitcherScreen::close_area(int card, lv_area_t *out) const
{
    if (card < 0 || card >= card_count()) return false;
    return area_of(slots_[static_cast<std::size_t>(card)].close, out);
}

bool SwitcherScreen::card_area(int card, lv_area_t *out) const
{
    if (card < 0 || card >= kSwitcherSlots) return false;
    return area_of(slots_[static_cast<std::size_t>(card)].card, out);
}

bool SwitcherScreen::bar_area(int item, lv_area_t *out) const
{
    if (item == kItemHome) return area_of(home_, out);
    if (item == kItemCloseAll) return area_of(close_all_, out);
    return false;
}

void SwitcherScreen::card_event_cb(lv_event_t *event)
{
    auto *self = static_cast<SwitcherScreen *>(lv_event_get_user_data(event));
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    if (!self || index < 0 || index >= self->card_count()) return;
    self->select(index);
    self->activate(index);
}

void SwitcherScreen::close_event_cb(lv_event_t *event)
{
    auto *self = static_cast<SwitcherScreen *>(lv_event_get_user_data(event));
    lv_obj_t *target = lv_event_get_target_obj(event);
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
    if (!self || index < 0 || index >= kSwitcherSlots) return;
    lv_obj_t *key = self->slots_[static_cast<std::size_t>(index)].close_key;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED: lv_obj_add_state(key, LV_STATE_PRESSED); break; /* the whole target shows on its key */
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST: lv_obj_remove_state(key, LV_STATE_PRESSED); break;
    case LV_EVENT_CLICKED:
        if (index < self->card_count() && self->events_.close)
            self->events_.close(self->cards_[static_cast<std::size_t>(index)].id);
        break;
    default: break;
    }
}

void SwitcherScreen::button_event_cb(lv_event_t *event)
{
    auto *self = static_cast<SwitcherScreen *>(lv_event_get_user_data(event));
    if (!self) return;
    const int item = lv_event_get_target_obj(event) == self->home_ ? kItemHome : kItemCloseAll;
    self->select(item);
    self->activate(item);
}

void SwitcherScreen::loaded_event_cb(lv_event_t *event)
{
    auto *self = static_cast<SwitcherScreen *>(lv_event_get_user_data(event));
    if (!self || self->strip_added_ || !self->page_) return;
    /* the strip reads the Wi-Fi / Bluetooth state at once only on the active screen (as the native Settings) */
    self->strip_added_ = true;
    if (cp0::ui::StatusStrip *strip = cp0::ui::StatusStrip::add(self->page_->header(), native_ui::fill_status))
        strip->refresh();
}

/* Keys on the screen (LV_EVENT_KEYBOARD, as the home grid): arrows and Tab move, Enter (released) activates, Esc and
 * Backspace leave. During an Alt+Tab session the launcher's key filter takes the keys before they get here. */
void SwitcherScreen::key_event_cb(lv_event_t *event)
{
    auto *self = static_cast<SwitcherScreen *>(lv_event_get_user_data(event));
    auto *item = static_cast<struct key_item *>(lv_event_get_param(event));
    if (!self || !item) return;
    if (item->input_context == KBD_INPUT_CONTEXT_TEXT || cp0_keyboard_get_input_context() == KBD_INPUT_CONTEXT_TEXT)
        return;
    const uint32_t code = item->semantic_key ? item->semantic_key : nav_alias(item->key_code);
    using multitask::SwitcherKey;
    if (item->key_state == KBD_KEY_RELEASED) {
        if (code == KEY_ENTER || code == KEY_KPENTER) self->activate(self->selected_);
        return;
    }
    int move = -1;
    switch (code) {
    case KEY_LEFT: move = static_cast<int>(SwitcherKey::Left); break;
    case KEY_RIGHT: move = static_cast<int>(SwitcherKey::Right); break;
    case KEY_UP: move = static_cast<int>(SwitcherKey::Up); break;
    case KEY_DOWN: move = static_cast<int>(SwitcherKey::Down); break;
    case KEY_TAB:
        move = static_cast<int>((item->mods & KBD_MOD_SHIFT) ? SwitcherKey::Prev : SwitcherKey::Next);
        break;
    case KEY_ESC:
    case KEY_BACKSPACE:
        if (item->key_state == KBD_KEY_PRESSED && self->events_.cancel) self->events_.cancel();
        return;
    default: return;
    }
    self->select(multitask::switcher_move(self->selected_, static_cast<SwitcherKey>(move), self->card_count()));
}

/* ------------------------------------------------------------------------------------------- LegacySheet */

LegacySheet::LegacySheet(const std::string &app_name, std::function<void()> keep, std::function<void()> close)
    : keep_(std::move(keep)), close_(std::move(close))
{
    const cp0_ui_metrics_t &m = metrics();
    cp0_ui_roller_page_t layout{};
    cp0_ui_roller_page(&m, cp0::ui::status_strip_left(), &layout);
    layout.header_h = 0; /* no page header on this screen: the dialog is centred on the whole panel */
    screen_ = black_screen();
    if (LV_EVENT_KEYBOARD != 0)
        lv_obj_add_event_cb(screen_, key_event_cb, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
    cp0::ui::DialogSpec spec;
    spec.title = "Close " + app_name + "?";
    spec.buttons.push_back({"Keep", cp0::ui::ButtonRole::Cancel, [this] {
                                if (keep_) keep_();
                            }});
    spec.buttons.push_back({"Close", cp0::ui::ButtonRole::Destructive, [this] {
                                if (close_) close_();
                            }});
    dialog_ = std::make_unique<cp0::ui::Dialog>(screen_, layout, spec);
    dialog_->add_text("This app can't stay in the background. Closing ends it; keeping it returns you to it.");
    dialog_->relayout();
}

LegacySheet::~LegacySheet()
{
    dialog_.reset();
    if (screen_) lv_obj_delete(screen_);
    screen_ = nullptr;
}

lv_area_t LegacySheet::box() const
{
    lv_area_t a{};
    if (!dialog_) return a;
    const cp0_ui_dialog_t &g = dialog_->geometry();
    a.x1 = g.x;
    a.y1 = g.y;
    a.x2 = g.x + g.w - 1;
    a.y2 = g.y + g.h - 1;
    return a;
}

bool LegacySheet::button_area(int index, lv_area_t *out) const
{
    if (!dialog_ || !out || index < 0 || index > 1) return false;
    const cp0_ui_dialog_t &g = dialog_->geometry();
    if (g.buttons <= index) return false;
    out->x1 = g.x + g.pad + index * (g.button_w + g.button_gap);
    out->y1 = g.y + g.button_y;
    out->x2 = out->x1 + g.button_w - 1;
    out->y2 = out->y1 + g.button_h - 1;
    return true;
}

void LegacySheet::key_event_cb(lv_event_t *event)
{
    auto *self = static_cast<LegacySheet *>(lv_event_get_user_data(event));
    auto *item = static_cast<struct key_item *>(lv_event_get_param(event));
    if (!self || !item || item->key_state != KBD_KEY_PRESSED) return;
    if (item->key_code == KEY_ESC || item->key_code == KEY_BACKSPACE) {
        if (self->keep_) self->keep_();
    }
}

} // namespace multitask_ui
