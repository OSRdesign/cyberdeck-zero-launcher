/*
 * SPDX-License-Identifier: MIT
 *
 * The native Settings host (task 014 P2a, decisions D1-D3): the existing Settings tree (settings_page.cpp) drawn
 * full screen on the native display with the shared widgets (cp0_ui_widgets.hpp), touch native and keyboard
 * complete, at every screen size. Runs when APPLAUNCH_SETTINGS_UI=native (settings_native_mode.hpp).
 *
 * Single column, drill-in, one level at a time:
 *   level 1  the sections (the root's children, exactly the entries the tree has on this board)
 *   level 2  a section's entries: Nav rows (current value right-aligned in green), toggles, sliders
 *   level 3  a value page's options with a check mark on the current one; Enter / a tap applies and goes back
 * The header is the home grid's (title at its place and size, "Settings > Screen" when it fits, the shared status
 * strip at exactly its place) plus an on-screen Esc that calls back() directly. Esc goes back one level, and from
 * level 1 home.
 *
 * How the tree is walked: every entry is classified by settings_node_kind() (its own page factory and API):
 *   Section  rows of its children (settings_prepare_section() refreshes it first, as the legacy factory did)
 *   Choice   the node's own factory builds its value page on a binding host (logic only, settings_choice_binding.hpp);
 *            the row shows the binding's current value; a value list of percentages becomes a slider row
 *   Toggle   a switch; status read with SettingApiReadFlagTimeStart on a worker (Async) or polled (Direct), toggled
 *            with SettingApiActivate, exactly as the legacy submenu does
 *   Action   SettingApiActivate on Enter
 *   Custom   no native widget yet: the compat path in development builds (settings_native_compat.hpp), hidden in a
 *            release build (D3)
 * Activation gates (Set Manually while Network Time is on) are honoured; the refusal shows in the status line.
 *
 * LVGL thread only.
 */
#pragma once

#include "settings_choice_binding.hpp"
#include "settings_tree_types.hpp"

#include "cp0_ui_widgets.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class AppPageRoot;
class SettingsCompatPath;
namespace DComponens {
class LvglComponensBase;
}

class SettingsNativeHost {
public:
    SettingsNativeHost(AppPageRoot &page, lv_obj_t *parent, Tree &tree, std::function<void()> go_home);
    ~SettingsNativeHost();
    SettingsNativeHost(const SettingsNativeHost &) = delete;
    SettingsNativeHost &operator=(const SettingsNativeHost &) = delete;

    /* Esc (key, header button): one level up; home from level 1 */
    void back();

private:
    enum class RowType { Section, Choice, Slider, Toggle, Action, Custom, Plain };
    struct Row;
    class Observer;
    class StatusTasks;

    void open_section(const NodeIter &node);
    void build_rows(int selected);
    void release_rows();
    bool create_binding(Row &row, int index);
    void show_section(int selected);
    void show_choice(int row);
    cp0::ui::RowSpec row_spec(const Row &row) const;
    cp0::ui::RowSpec option_spec(const Row &row, int option) const;
    std::vector<std::string> title_path() const;
    void activate(int index);
    void activate_entry(int index);
    void activate_choice(int option);
    void slide(int index, int value);
    void toggle(int index);
    void read_toggle(int index);
    void poll_direct_toggles();
    void open_custom(int index);
    void binding_done(std::uint64_t generation, const SettingEntry *entry);
    void choice_selected(int row, int index);
    void choice_status(int row, const std::string &text, bool error);
    void clear_caption();
    int known_value(const Row &row) const;
    void post(std::function<void()> callback);

    static void run_posted_cb(void *data);
    static void screen_loaded_cb(lv_event_t *event);
    static void direct_poll_cb(lv_timer_t *timer);

    AppPageRoot &page_root_;
    Tree &tree_;
    std::function<void()> go_home_;
    cp0_ui_roller_page_t layout_{};
    std::unique_ptr<cp0::ui::Page> page_;
    std::unique_ptr<cp0::ui::Roller> roller_;
    lv_obj_t *binding_host_ = nullptr;
    std::unique_ptr<SettingsCompatPath> compat_;

    std::vector<NodeIter> path_;     /* root .. the section shown */
    std::vector<int> path_selected_; /* the row each parent level had when its child was opened */
    std::vector<std::unique_ptr<Row>> rows_;
    std::unique_ptr<StatusTasks> status_tasks_; /* the section's status reads (joined before the tree changes) */
    std::map<const SettingEntry *, int> values_; /* last value a binding reported ready, shown while a fresh one loads */
    int choice_row_ = -1;                      /* >= 0: level 3, that row's options */
    int checked_ = -1;                         /* level 3: the option carrying the check mark */
    int caption_row_ = -1;                     /* the row whose message the status line shows (-1: none / other) */
    std::uint64_t generation_ = 0;             /* bumped whenever rows_ is rebuilt */
    lv_timer_t *direct_poll_ = nullptr;
    std::vector<std::function<void()>> posted_;
    bool post_armed_ = false;
    bool strip_added_ = false;
};
