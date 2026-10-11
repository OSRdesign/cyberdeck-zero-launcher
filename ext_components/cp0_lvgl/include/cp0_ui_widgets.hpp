/*
 * SPDX-License-Identifier: MIT
 *
 * Shared widgets of the responsive shell (task 014 P2a, report 022 section 3.4): the page scaffold and the roller
 * list with its row kinds. Pages built from them carry no geometry: every size comes from the layout service
 * (cp0_ui_metrics.h) through the roller page layout (cp0_ui_roller.h), and every widget handles touch and the
 * keyboard itself, so a page made only of these widgets works with a finger and with a physical keyboard.
 *
 *   Page    header (the home grid's title, a status line, an on-screen Esc button, room for the shared status
 *           strip) and the area under it. The Esc button calls the page's back action directly: it never injects a
 *           key, so it can never leave a stuck Esc behind (report 022 section 2.2).
 *   Roller  today's centre-highlight roller, full height, width capped and centred. Rows:
 *             Nav     label, current value right-aligned in green (opens something)
 *             Choice  label, blue check mark on the current value
 *             Toggle  label, switch
 *             Slider  label, slider, value text (Left / Right or a drag change it)
 *           Touch: a drag scrolls and snaps to a row; a tap on a row moves the highlight there; a tap on the
 *           highlighted row activates it. Keyboard (LV_EVENT_KEY on the page's input group): Up / Down move, Enter
 *           activates, Esc goes back, Left / Right change a slider (elsewhere Right activates and Left goes back, as
 *           in today's Settings).
 *
 * LVGL thread only. The objects are children of the parent given to the constructor; the destructor deletes them
 * (unless the parent went first).
 */
#pragma once

#include "cp0_ui_roller.h"
#include "lvgl/lvgl.h"

#include <functional>
#include <string>
#include <vector>

namespace cp0 {
namespace ui {

enum class RowKind { Nav, Choice, Toggle, Slider };

struct RowSpec {
    RowKind kind = RowKind::Nav;
    std::string label;
    std::string value;  /* Nav: current value (green, right); Slider: value text */
    bool on = false;    /* Choice: carries the check mark; Toggle: switch state */
    int slider_min = 0; /* Slider */
    int slider_max = 100;
    int slider_value = 0;
    int slider_step = 1; /* one Left / Right press */
};

struct RollerEvents {
    std::function<void(int row)> activate;          /* Enter, Right off a slider, tap on the highlighted row */
    std::function<void(int row, int value)> slide;  /* a slider moved: drag released, or Left / Right */
    std::function<void()> back;                     /* Esc, Left off a slider */
};

class Roller {
public:
    Roller(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, RollerEvents events);
    ~Roller();
    Roller(const Roller &) = delete;
    Roller &operator=(const Roller &) = delete;

    /* Replace the rows; `selected` is centred at once. wrap: Up on the first row goes to the last (lists). */
    void set_rows(const std::vector<RowSpec> &rows, int selected, bool wrap);
    /* Change one row's value / state in place (it keeps its place and the highlight). */
    void update_row(int index, const RowSpec &spec);
    void select(int index, bool animated);
    int selected() const { return selected_; }
    int count() const { return static_cast<int>(rows_.size()); }
    /* The object to add to the page's input group and focus: it receives the keys. */
    lv_obj_t *focus_object() const { return list_; }

private:
    struct Row {
        RowSpec spec;
        lv_obj_t *obj = nullptr;
        lv_obj_t *label = nullptr;
        lv_obj_t *value = nullptr;  /* Nav / Slider value text, Choice check mark */
        lv_obj_t *control = nullptr; /* switch or slider */
        int distance = -1;          /* styled for this distance from the centre (-1: not yet) */
    };

    static void list_event_cb(lv_event_t *event);
    static void row_event_cb(lv_event_t *event);
    static void slider_event_cb(lv_event_t *event);
    static void body_delete_cb(lv_event_t *event);

    void build_row(int index);
    void fill_row(Row &row);
    void style_row(int index, bool force);
    void restyle_around(int previous);
    void update_chevrons();
    void set_selected(int index);
    void on_key(uint32_t key);
    void on_scroll(lv_event_code_t code);
    void on_row_clicked(int index);
    void slide_by(int index, int steps);

    cp0_ui_roller_page_t layout_;
    RollerEvents events_;
    lv_obj_t *body_ = nullptr;
    lv_obj_t *bar_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *up_ = nullptr;   /* chevrons, drawn as lines (their points live here) */
    lv_obj_t *down_ = nullptr;
    lv_point_precise_t up_points_[3]{};
    lv_point_precise_t down_points_[3]{};
    std::vector<Row> rows_;
    int selected_ = 0;
    int target_ = -1; /* centre row a programmatic scroll is heading for (-1: none) */
    bool wrap_ = true;
    bool rebuilding_ = false; /* set_rows() is replacing the rows: scroll events are ignored */
};

class Page {
public:
    /* make_title: creates the title label at the home grid's place (native_ui::add_title in the launcher); NULL
     * draws a plain label from the same metrics. on_back: the Esc button. */
    using TitleFactory = std::function<lv_obj_t *(lv_obj_t *parent, const char *text)>;
    Page(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, TitleFactory make_title, std::function<void()> on_back);
    ~Page();
    Page(const Page &) = delete;
    Page &operator=(const Page &) = delete;

    /* The title for a path ("Settings", "Screen", "DarkTime"): the whole path joined with " > " when it fits up to
     * the Esc button, else the path without its first parts, else the last part with dots. */
    void set_title(const std::vector<std::string> &path);
    /* One status line under the title (empty hides it); error draws it red. */
    void set_caption(const std::string &text, bool error);

    lv_obj_t *root() const { return root_; }
    lv_obj_t *header() const { return header_; }
    const cp0_ui_roller_page_t &layout() const { return layout_; }

private:
    static void esc_event_cb(lv_event_t *event);
    static void root_delete_cb(lv_event_t *event);

    cp0_ui_roller_page_t layout_;
    std::function<void()> on_back_;
    lv_obj_t *root_ = nullptr;
    lv_obj_t *header_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *caption_ = nullptr;
    lv_obj_t *esc_ = nullptr;
};

} // namespace ui
} // namespace cp0
