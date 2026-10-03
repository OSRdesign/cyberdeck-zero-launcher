/*
 * Settings > Apps: install and remove apps from GitHub-hosted sources, and manage those sources.
 *
 * Two tabs: "Apps" (the catalogue of all enabled sources) and "Sources" (add / enable / remove a GitHub
 * repository). It drives the Store's native backend, so any registry the Store understands works.
 * Hosting your own: see docs/HOSTING-APPS.md.
 */

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apps_backend.hpp"
#include "cp0_lvgl_app.h"
#include "lvgl_components.hpp"
#include "settings_tree_types.hpp"

class LvSettingAppsPage3 : public DComponens::LvglComponensBase {
public:
    struct Shared;

    LvSettingAppsPage3(lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);
    ~LvSettingAppsPage3() override;

    void AnimateNextIn(std::function<void()> callback) override;
    void AnimateNextOut(std::function<void()> callback) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;
    void create_ui(lv_obj_t *parent) override;

private:
    enum class Tab { Apps, Sources };
    enum class Pending { None, Remove, RemoveSource };
    static constexpr int kRows = 6;

    int row_count() const;
    void move_selection(int delta);
    void set_tab(Tab tab);
    void activate();
    void toggle_source();
    void remove_selected_source();
    void begin_edit();
    void end_edit(bool submit);
    void edit_key(const struct key_item *item);
    void render();
    void set_status(const std::string &text);
    void handle_key(lv_event_t *event);
    void poll();
    void start_sudo();
    void enter_text_mode();
    void leave_text_mode();

    static void keyboard_event_cb(lv_event_t *event);
    static void poll_cb(lv_timer_t *timer);
    static void sudo_output_cb(const char *data, size_t size, void *user);
    static void sudo_complete_cb(cp0_sudo_result_t result, int exit_code, void *user);

    std::shared_ptr<Shared> shared_;
    std::vector<apps_backend::App> apps_;
    std::vector<apps_backend::Source> sources_;
    Tab tab_ = Tab::Apps;
    Pending pending_ = Pending::None;
    int selected_ = 0;
    int offset_ = 0;
    bool editing_ = false;
    std::string edit_text_;
    std::string edit_error_;
    bool text_mode_saved_ = false;
    int previous_context_ = 0;
    int previous_intercept_ = 0;
    bool backend_missing_ = false;

    lv_obj_t *tab_apps_ = nullptr;
    lv_obj_t *tab_sources_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *footer_ = nullptr;
    lv_obj_t *edit_panel_ = nullptr;
    lv_obj_t *edit_title_ = nullptr;
    lv_obj_t *edit_value_ = nullptr;
    lv_obj_t *edit_hint_ = nullptr;
    std::array<lv_obj_t *, kRows> rows_{};
    std::array<lv_obj_t *, kRows> left_{};
    std::array<lv_obj_t *, kRows> right_{};
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_dsc_ = nullptr;
    lv_timer_t *timer_ = nullptr;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_apps_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);
