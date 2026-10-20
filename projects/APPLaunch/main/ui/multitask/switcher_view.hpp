/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-d (task 015-pd): the two launcher screens of the switcher, drawn with the shell's widgets and no
 * geometry of their own beyond the approved mockup's proportions (every size from the layout service: the header's
 * height, TARGET, the text sizes, the density). LVGL thread only. Each is built when it opens and deleted when it
 * closes; neither has a timer of its own (the header's status strip keeps its clock, as on every launcher screen).
 *
 *   SwitcherScreen  "Open apps": the shared page header (title, Esc, status strip), a fixed 2x2 grid of cards (at most
 *                   4 resident apps; a free slot is a dashed "Room for one more"), a bottom bar Home | Close all.
 *                   A card: the app's icon, its name (cut with dots), a state line with a coloured dot (In front /
 *                   Running green, Paused grey, Not responding amber) and an X key in its top-right corner whose
 *                   touch target is the TARGET token square (7 mm). Tap a card = pick, tap X = close. Keys (on the
 *                   screen, LV_EVENT_KEYBOARD): arrows and Tab / Shift+Tab move the blue selection border, Enter (on its
 *                   release) activates, Esc / Backspace cancel. No animation, no picture of the apps.
 *   LegacySheet     "Close <app>?": the toolkit's Dialog (Keep, Close) alone on a screen whose only drawn part is the
 *                   dialog's box (the native clip, cp0_display.h): around it the panel keeps the frozen app's frame,
 *                   which the supervisor wrote back dimmed. Esc / Backspace = Keep.
 */
#pragma once

#include "lvgl/lvgl.h"
#include "switcher_model.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cp0 {
namespace ui {
class Dialog;
class Page;
} // namespace ui
} // namespace cp0

namespace multitask_ui {

/* The frozen app's frame behind the sheet keeps 38 % of its colours (the mockup's 62 % black veil). */
inline constexpr int kSheetBackdropPct = 38;

struct CardView {
    std::intptr_t id = 0;
    std::string name;
    std::string icon; /* the app's icon file as its home tile loads it ("" = none) */
    multitask::CardLabel label;
};

struct SwitcherEvents {
    std::function<void(std::intptr_t id)> pick;  /* a card tapped, or Enter on it */
    std::function<void(std::intptr_t id)> close; /* its X */
    std::function<void()> home;
    std::function<void()> close_all;
    std::function<void()> cancel; /* the header's Esc, Esc / Backspace */
};

class SwitcherScreen {
public:
    /* Builds the screen on the native display (not loaded). */
    explicit SwitcherScreen(SwitcherEvents events);
    /* Deletes the screen: load another one first. */
    ~SwitcherScreen();
    SwitcherScreen(const SwitcherScreen &) = delete;
    SwitcherScreen &operator=(const SwitcherScreen &) = delete;

    lv_obj_t *screen() const { return screen_; }
    /* The cards, most recently used first (only the first 4); the slots after them show "Room for one more". */
    void set_cards(const std::vector<CardView> &cards);
    /* The selection border: a card (0..3), multitask::kItemHome or multitask::kItemCloseAll (clamped to what exists). */
    void select(int item);
    int selected() const { return selected_; }
    int card_count() const { return static_cast<int>(cards_.size()); }
    /* The touch target of a card's X and the card itself, screen coordinates (the render harness checks the sizes). */
    bool close_area(int card, lv_area_t *out) const;
    bool card_area(int card, lv_area_t *out) const;
    bool bar_area(int item, lv_area_t *out) const; /* multitask::kItemHome or kItemCloseAll */

private:
    struct Slot {
        lv_obj_t *card = nullptr;   /* the card (a free slot: the dashed frame) */
        lv_obj_t *icon = nullptr;
        lv_obj_t *name = nullptr;   /* the name's first line ... */
        lv_obj_t *name2 = nullptr;  /* ... and its second (a long name; cut with dots after it) */
        lv_obj_t *dot = nullptr;
        lv_obj_t *state = nullptr;
        lv_obj_t *close = nullptr;  /* the X's touch target (TARGET square) */
        lv_obj_t *close_key = nullptr; /* the X's visible key inside it */
        lv_obj_t *room = nullptr;   /* a free slot: "Room for one more" */
        std::array<lv_obj_t *, 4> dashes{};
        std::array<std::array<lv_point_precise_t, 2>, 4> dash_points{};
        std::string icon_src;       /* what the icon shows (set again only when it changes) */
    };

    static void card_event_cb(lv_event_t *event);
    static void close_event_cb(lv_event_t *event);
    static void button_event_cb(lv_event_t *event);
    static void key_event_cb(lv_event_t *event);
    static void loaded_event_cb(lv_event_t *event);

    void build_slot(int index);
    void fill_slot(int index);
    void layout_text(Slot &slot, const std::string &name);
    void style_selection();
    void activate(int item);

    SwitcherEvents events_;
    lv_obj_t *screen_ = nullptr;
    std::unique_ptr<cp0::ui::Page> page_;
    bool strip_added_ = false;
    std::array<Slot, multitask::kSwitcherSlots> slots_{};
    lv_obj_t *home_ = nullptr;
    lv_obj_t *close_all_ = nullptr;
    std::vector<CardView> cards_;
    int selected_ = 0;
    /* geometry (screen px) */
    int pad_ = 12, gap_ = 12, card_w_ = 0, card_h_ = 0, cards_y_ = 0, bar_y_ = 0, bar_h_ = 0, target_ = 0;
    /* inside a card: the text column's left edge, line heights, the dot */
    int inset_ = 0, text_x_ = 0, name_h_ = 0, state_h_ = 0, line_gap_ = 0, dot_ = 0;
    const lv_font_t *name_font_ = nullptr;
};

class LegacySheet {
public:
    /* "Close <app_name>?", Keep (also Esc / Backspace) and Close; built on its own screen (not loaded). */
    LegacySheet(const std::string &app_name, std::function<void()> keep, std::function<void()> close);
    ~LegacySheet();
    LegacySheet(const LegacySheet &) = delete;
    LegacySheet &operator=(const LegacySheet &) = delete;

    lv_obj_t *screen() const { return screen_; }
    /* The dialog's box (screen coordinates): the native clip while the sheet is up. */
    lv_area_t box() const;
    /* The touch targets of Keep (0) and Close (1), screen coordinates (the render harness checks the sizes). */
    bool button_area(int index, lv_area_t *out) const;

private:
    static void key_event_cb(lv_event_t *event);

    std::function<void()> keep_;
    std::function<void()> close_;
    lv_obj_t *screen_ = nullptr;
    std::unique_ptr<cp0::ui::Dialog> dialog_;
};

} // namespace multitask_ui
