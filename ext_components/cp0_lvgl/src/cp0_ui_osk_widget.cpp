/*
 * SPDX-License-Identifier: MIT
 *
 * The on-screen keyboard widget and its service (task 014 P2b; see cp0_ui_widgets.hpp, maths in cp0_ui_osk.c).
 *
 * Objects: root (the keyboard's rectangle on the display's top layer: #111, 1 px #333 line along the top; it takes the
 * taps that fall between keys) > one plain object per key (#1c1c1c, radius 10, lighter while pressed; Enter blue;
 * Shift / Ctrl highlighted while armed, blue while locked) with its label. Nothing here is click-focusable, so a tap on
 * a key never moves the input group's focus away from the field being edited.
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

constexpr const char *kTextFace = "Montserrat-Medium.ttf"; /* labels that may be non-ASCII (euro, pound, yen) */
constexpr const char *kShiftFace = "DejaVuSans.ttf";        /* the Shift arrow (U+21E7) */
constexpr const char *kShiftGlyph = "\xe2\x87\xa7";

const lv_font_t *text_font(int px)
{
    return cp0_fonts().get(kTextFace, static_cast<uint16_t>(px));
}

lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    if (lv_obj_get_group(obj)) lv_group_remove_obj(obj);
    return obj;
}

const char *arrow_symbol(uint32_t code)
{
    switch (code) {
    case KEY_LEFT: return LV_SYMBOL_LEFT;
    case KEY_UP: return LV_SYMBOL_UP;
    case KEY_DOWN: return LV_SYMBOL_DOWN;
    default: return LV_SYMBOL_RIGHT;
    }
}

bool is_letter(const cp0_ui_osk_key_t *k)
{
    return k->kind == CP0_UI_OSK_K_CHAR && k->text && k->text[0] >= 'a' && k->text[0] <= 'z' && !k->text[1];
}

} // namespace

// ------------------------------------------------------------------ the widget

OnScreenKeyboard::OnScreenKeyboard(lv_obj_t *layer, const cp0_ui_osk_layout_t &layout, TextType type, bool multiline)
    : layout_(layout), type_(type), multiline_(multiline)
{
    cp0_ui_osk_state_init(&state_, type == TextType::Number, multiline);
    root_ = plain(layer);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE); /* a tap between keys stops here, never on the page below */
    lv_obj_set_pos(root_, 0, layout_.y);
    lv_obj_set_size(root_, layout_.w, layout_.height);
    lv_obj_set_style_bg_color(root_, lv_color_hex(CP0_UI_OSK_BG_RGB), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(root_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(root_, layout_.border, 0);
    lv_obj_set_style_border_color(root_, lv_color_hex(CP0_UI_OSK_BORDER_RGB), 0);
    lv_obj_set_style_border_opa(root_, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(root_, root_delete_cb, LV_EVENT_DELETE, this);
    build();
}

OnScreenKeyboard::~OnScreenKeyboard()
{
    lv_async_call_cancel(rebuild_cb, this);
    // the release an Esc (or Backspace, an arrow) still owes goes out before anything else: never a stuck Esc
    cp0_ui_osk_event_t events[CP0_UI_OSK_MAX_EVENTS];
    inject(events, cp0_ui_osk_key_cancel(&state_, events, CP0_UI_OSK_MAX_EVENTS));
    if (root_) {
        lv_obj_remove_event_cb_with_user_data(root_, root_delete_cb, this);
        lv_obj_delete(root_); /* its keys' DELETE finds nothing more owed */
    }
}

void OnScreenKeyboard::root_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<OnScreenKeyboard *>(lv_event_get_user_data(event));
    if (!self) return;
    cp0_ui_osk_event_t events[CP0_UI_OSK_MAX_EVENTS];
    self->inject(events, cp0_ui_osk_key_cancel(&self->state_, events, CP0_UI_OSK_MAX_EVENTS));
    self->root_ = nullptr;
}

void OnScreenKeyboard::release_all()
{
    cp0_ui_osk_event_t events[CP0_UI_OSK_MAX_EVENTS];
    inject(events, cp0_ui_osk_release_all(&state_, events, CP0_UI_OSK_MAX_EVENTS));
    restyle();
}

void OnScreenKeyboard::inject(const cp0_ui_osk_event_t *events, int count)
{
    for (int i = 0; i < count; ++i)
        cp0_keyboard_inject_key(events[i].code, events[i].state, events[i].mods, events[i].utf8);
}

void OnScreenKeyboard::build()
{
    if (!root_) return;
    cp0_ui_osk_place_t places[CP0_UI_OSK_MAX_KEYS];
    const int count = cp0_ui_osk_place_keys(&layout_, state_.page, places, CP0_UI_OSK_MAX_KEYS);
    keys_.clear();
    keys_.reserve(static_cast<size_t>(std::max(0, count)));
    for (int i = 0; i < count; ++i) {
        Key key;
        key.place = places[i];
        key.obj = plain(root_);
        lv_obj_add_flag(key.obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(key.obj, places[i].x, places[i].y);
        lv_obj_set_size(key.obj, places[i].w, places[i].h);
        lv_obj_set_style_radius(key.obj, CP0_UI_OSK_RADIUS, 0);
        lv_obj_set_style_bg_opa(key.obj, LV_OPA_COVER, 0);
        lv_obj_set_user_data(key.obj, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(key.obj, key_event_cb, LV_EVENT_ALL, this);
        key.label = lv_label_create(key.obj);
        lv_obj_remove_flag(key.label, LV_OBJ_FLAG_CLICKABLE);
        keys_.push_back(key);
    }
    for (Key &key : keys_) style_key(key);
}

void OnScreenKeyboard::restyle()
{
    for (Key &key : keys_) style_key(key);
}

void OnScreenKeyboard::style_key(Key &key)
{
    const cp0_ui_osk_key_t *k = key.place.key;
    uint32_t bg = CP0_UI_OSK_KEY_RGB, pressed = CP0_UI_OSK_KEY_PRESSED_RGB, text = CP0_UI_OSK_LABEL_RGB;
    const cp0_ui_osk_mod_t *mod = k->kind == CP0_UI_OSK_K_SHIFT  ? &state_.shift
                                  : k->kind == CP0_UI_OSK_K_CTRL ? &state_.ctrl
                                                                 : nullptr;
    if (k->kind == CP0_UI_OSK_K_ENTER) {
        bg = CP0_UI_OSK_ENTER_RGB;
        pressed = CP0_UI_OSK_ENTER_PRESSED_RGB;
    } else if (mod && mod->state == CP0_UI_OSK_MOD_ARMED) {
        bg = pressed = CP0_UI_OSK_ARMED_RGB;
        text = CP0_UI_OSK_ARMED_LABEL_RGB;
    } else if (mod && mod->state == CP0_UI_OSK_MOD_LOCKED) {
        bg = pressed = CP0_UI_OSK_LOCKED_RGB;
    }
    lv_obj_set_style_bg_color(key.obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(key.obj, lv_color_hex(pressed), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(key.label, lv_color_hex(text), 0);

    // label: letters follow Shift; control keys draw LVGL symbols (builtin font), names in the smaller size
    const lv_font_t *font = text_font(layout_.label_px);
    char upper[2] = {0, 0};
    const char *label = k->label ? k->label : k->text;
    switch (k->kind) {
    case CP0_UI_OSK_K_CHAR:
        if (k->code == KEY_SPACE) label = "";
        else if (is_letter(k) && cp0_ui_osk_mod_on(&state_.shift)) {
            upper[0] = static_cast<char>(k->text[0] - 'a' + 'A');
            label = upper;
        }
        break;
    case CP0_UI_OSK_K_SHIFT:
        label = kShiftGlyph;
        font = cp0_fonts().get(kShiftFace, static_cast<uint16_t>(layout_.label_px));
        break;
    case CP0_UI_OSK_K_BACKSPACE:
        label = LV_SYMBOL_BACKSPACE;
        font = cp0_ui_font_px(layout_.label_px);
        break;
    case CP0_UI_OSK_K_ENTER:
        label = LV_SYMBOL_NEW_LINE;
        font = cp0_ui_font_px(layout_.label_px);
        break;
    case CP0_UI_OSK_K_ARROW:
        label = arrow_symbol(k->code);
        font = cp0_ui_font_px(layout_.small_px);
        break;
    default: /* Esc, Tab, Ctrl, 123 / abc / #+= */
        font = text_font(layout_.small_px);
        break;
    }
    lv_obj_set_style_text_font(key.label, font, 0);
    lv_label_set_text(key.label, label ? label : "");
    lv_obj_center(key.label);
}

void OnScreenKeyboard::key_event_cb(lv_event_t *event)
{
    auto *self = static_cast<OnScreenKeyboard *>(lv_event_get_user_data(event));
    auto *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (!self || !obj) return;
    const lv_event_code_t code = lv_event_get_code(event);
    switch (code) {
    case LV_EVENT_PRESSED:
    case LV_EVENT_LONG_PRESSED_REPEAT:
    case LV_EVENT_RELEASED:
    case LV_EVENT_CLICKED:
    case LV_EVENT_PRESS_LOST:
    case LV_EVENT_DELETE:
        self->on_key_event(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj))), code);
        break;
    default:
        break;
    }
}

void OnScreenKeyboard::on_key_event(int index, lv_event_code_t code)
{
    if (index < 0 || index >= static_cast<int>(keys_.size())) return;
    const cp0_ui_osk_key_t *k = keys_[static_cast<size_t>(index)].place.key;
    const bool hold = cp0_ui_osk_is_hold_key(k) != 0;
    const cp0_ui_osk_page_t page = state_.page;
    const cp0_ui_osk_mod_state_t shift = state_.shift.state, ctrl = state_.ctrl.state;
    cp0_ui_osk_event_t events[CP0_UI_OSK_MAX_EVENTS];
    int count = 0;
    switch (code) {
    case LV_EVENT_PRESSED:
        count = cp0_ui_osk_key_down(&state_, k, events, CP0_UI_OSK_MAX_EVENTS);
        break;
    case LV_EVENT_LONG_PRESSED_REPEAT:
        count = cp0_ui_osk_key_repeat(&state_, k, events, CP0_UI_OSK_MAX_EVENTS);
        break;
    case LV_EVENT_RELEASED: /* a hold key's release: always, wherever the finger came up */
        if (hold) count = cp0_ui_osk_key_up(&state_, k, lv_tick_get(), events, CP0_UI_OSK_MAX_EVENTS);
        break;
    case LV_EVENT_CLICKED: /* the other keys act on a click (finger up on the key) */
        if (!hold) count = cp0_ui_osk_key_up(&state_, k, lv_tick_get(), events, CP0_UI_OSK_MAX_EVENTS);
        break;
    case LV_EVENT_PRESS_LOST:
    case LV_EVENT_DELETE: /* the toolbar rule: an owed release is sent even if the key goes away mid-press */
        count = cp0_ui_osk_key_cancel(&state_, events, CP0_UI_OSK_MAX_EVENTS);
        break;
    default:
        break;
    }
    inject(events, count);
    if (code == LV_EVENT_DELETE) return;
    if (state_.page != page) {
        // a page key: the keys change on the next cycle (this key is still processing its click)
        if (!rebuild_armed_) rebuild_armed_ = lv_async_call(rebuild_cb, this) == LV_RESULT_OK;
    } else if (state_.shift.state != shift || state_.ctrl.state != ctrl) {
        restyle();
    }
}

void OnScreenKeyboard::rebuild_cb(void *data)
{
    auto *self = static_cast<OnScreenKeyboard *>(data);
    if (!self) return;
    self->rebuild_armed_ = false;
    if (!self->root_) return;
    lv_obj_clean(self->root_); /* the keys' DELETE releases anything owed (nothing: a page key is not a hold key) */
    self->keys_.clear();
    self->build();
}

// ------------------------------------------------------------------ the service

namespace osk {
namespace {

struct Listener {
    int id;
    std::function<void(int)> fn;
};

struct Service {
    std::function<bool()> allowed;
    lv_obj_t *owner = nullptr;
    TextType type = TextType::Text;
    bool multiline = false;
    std::unique_ptr<OnScreenKeyboard> keyboard;
    int inset = 0;
    std::vector<Listener> listeners;
    int next_id = 1;
    lv_timer_t *poll = nullptr;
    bool apply_armed = false;
};

constexpr uint32_t kPolicyPollMs = 250;

/* never destroyed: no LVGL call may run from a static destructor after LVGL is gone */
Service &service()
{
    static Service *s = new Service;
    return *s;
}

void notify(int inset)
{
    Service &s = service();
    if (inset == s.inset) return;
    s.inset = inset;
    const std::vector<Listener> listeners = s.listeners; /* a listener may remove itself */
    for (const Listener &l : listeners)
        if (l.fn) l.fn(inset);
}

void hide_now(bool release)
{
    Service &s = service();
    if (s.keyboard) {
        if (release) s.keyboard->release_all(); /* latched Shift / Ctrl dropped, owed releases out */
        s.keyboard.reset();
    }
    notify(0);
}

/* The keyboard's layout for the display the field is on: the layout service's metrics when they are that display's
 * (the native one), else the same density on that display's size. */
bool layout_for(lv_obj_t *field, cp0_ui_osk_layout_t &out)
{
    lv_display_t *display = lv_obj_get_display(field);
    if (!display) return false;
    const int w = static_cast<int>(lv_display_get_horizontal_resolution(display));
    const int h = static_cast<int>(lv_display_get_vertical_resolution(display));
    const cp0_ui_metrics_t *m = cp0_ui_metrics_get();
    if (m->w == w && m->h == h) return cp0_ui_osk_layout(m, &out) == 0;
    cp0_ui_metrics_t other;
    if (cp0_ui_metrics_compute(w, h, 0, m->ppmm_x100, m->density_src, 0, &other) != 0) return false;
    return cp0_ui_osk_layout(&other, &out) == 0;
}

void poll_cb(lv_timer_t *);

void apply()
{
    Service &s = service();
    if (s.owner && !lv_obj_is_valid(s.owner)) s.owner = nullptr;
    const bool want = s.owner && s.allowed && s.allowed();
    if (!want) {
        hide_now(true);
    } else {
        lv_obj_t *layer = lv_display_get_layer_top(lv_obj_get_display(s.owner));
        if (s.keyboard && (s.keyboard->type() != s.type || s.keyboard->multiline() != s.multiline ||
                           !s.keyboard->root() || lv_obj_get_parent(s.keyboard->root()) != layer))
            s.keyboard.reset(); /* another kind of field, or another display: built again below */
        cp0_ui_osk_layout_t layout;
        if (!s.keyboard && layer && layout_for(s.owner, layout)) {
            s.keyboard = std::make_unique<OnScreenKeyboard>(layer, layout, s.type, s.multiline);
            lv_obj_move_foreground(s.keyboard->root());
        }
        notify(s.keyboard ? s.keyboard->height() : 0);
    }
    // the policy is asked again while a field wants the keyboard (a physical keyboard appears or goes, lock screen)
    if (s.owner && !s.poll) s.poll = lv_timer_create(poll_cb, kPolicyPollMs, nullptr);
    if (!s.owner && s.poll) {
        lv_timer_delete(s.poll);
        s.poll = nullptr;
    }
}

void poll_cb(lv_timer_t *)
{
    apply();
}

void apply_cb(void *)
{
    service().apply_armed = false;
    apply();
}

/* request / release are coalesced to the next cycle: moving from one field to the next keeps the keyboard up */
void schedule()
{
    Service &s = service();
    if (s.apply_armed) return;
    s.apply_armed = lv_async_call(apply_cb, nullptr) == LV_RESULT_OK;
    if (!s.apply_armed) apply();
}

struct Keeper {
    int id = 0;
    int top = 0;
};

void place_above(lv_obj_t *obj, int top, int inset)
{
    lv_display_t *display = lv_obj_get_display(obj);
    if (!display) return;
    lv_obj_update_layout(obj);
    const int h = static_cast<int>(lv_display_get_vertical_resolution(display));
    lv_obj_set_align(obj, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(obj, 0, cp0_ui_osk_dialog_y(h, top, inset, lv_obj_get_height(obj)));
}

void keeper_delete_cb(lv_event_t *event)
{
    auto *keeper = static_cast<Keeper *>(lv_event_get_user_data(event));
    if (!keeper) return;
    remove_inset_listener(keeper->id);
    delete keeper;
}

} // namespace

void set_policy(std::function<bool()> allowed)
{
    service().allowed = std::move(allowed);
    schedule();
}

void request(lv_obj_t *field, TextType type, bool multiline)
{
    if (!field) return;
    Service &s = service();
    s.owner = field;
    s.type = type;
    s.multiline = multiline;
    schedule();
}

void release(lv_obj_t *field)
{
    Service &s = service();
    if (!field || s.owner != field) return;
    s.owner = nullptr;
    schedule();
}

void dismiss()
{
    Service &s = service();
    s.owner = nullptr;
    hide_now(true);
    if (s.poll) {
        lv_timer_delete(s.poll);
        s.poll = nullptr;
    }
}

bool visible()
{
    return service().keyboard != nullptr;
}

lv_obj_t *owner()
{
    return service().owner;
}

int bottom_inset()
{
    return service().inset;
}

int add_inset_listener(std::function<void(int inset)> listener)
{
    Service &s = service();
    const int id = s.next_id++;
    s.listeners.push_back(Listener{id, std::move(listener)});
    return id;
}

void remove_inset_listener(int id)
{
    auto &listeners = service().listeners;
    listeners.erase(std::remove_if(listeners.begin(), listeners.end(), [id](const Listener &l) { return l.id == id; }),
                    listeners.end());
}

void keep_above_keyboard(lv_obj_t *obj, int top)
{
    if (!obj) return;
    auto *keeper = new Keeper;
    keeper->top = top;
    place_above(obj, top, bottom_inset());
    keeper->id = add_inset_listener([obj, top](int inset) { place_above(obj, top, inset); });
    lv_obj_add_event_cb(obj, keeper_delete_cb, LV_EVENT_DELETE, keeper);
}

} // namespace osk

} // namespace ui
} // namespace cp0
