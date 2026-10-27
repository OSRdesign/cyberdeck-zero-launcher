/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Screen > PDA mode (see settings_pda_page.hpp).
 */

#include "settings_pda_page.hpp"

#include "settings_session_model.hpp"

#include "../pda_mode.hpp"

#include "hal_lvgl_bsp.h"

#include <list>
#include <string>
#include <utility>

namespace {

bool call_config(std::list<std::string> arguments)
{
    bool ok = false;
    try {
        cp0_signal_config_api(std::move(arguments), [&](int code, std::string) { ok = code == 0; });
    } catch (...) {
        return false;
    }
    return ok;
}

/* the value is in config.json (the file the next launcher reads before its display opens) */
bool store(int value)
{
    return call_config({"SetInt", pda_mode::kConfigKey, std::to_string(value)}) && call_config({"Save"});
}

} // namespace

LvSettingPdaModeChoicePage3::LvSettingPdaModeChoicePage3(lv_obj_t *parent, const NodeIter &parent_node,
                                                         std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, {})
{
    const pda_mode::Startup &start = pda_mode::startup();
    values_ = pda_mode::choices(start.board, start.offered);
    for (int i = 0; i < static_cast<int>(values_.size()); i++)
        if (values_[static_cast<size_t>(i)] == start.mode) current_ = i; /* read before initialize(): its row */
    back_callback_ = std::move(back_callback);
    LeaveSelfPage = [this] { request_back(); };
    initialize(parent);
}

LvSettingPdaModeChoicePage3::~LvSettingPdaModeChoicePage3()
{
    LeaveSelfPage = nullptr;
    back_callback_ = nullptr;
}

void LvSettingPdaModeChoicePage3::choice_set_observer(SettingsChoiceObserver *observer)
{
    LvSettingValuePage3Base::choice_set_observer(observer);
    observer_ = observer;
}

int LvSettingPdaModeChoicePage3::initial_selection() const
{
    return current_;
}

SettingApiResult LvSettingPdaModeChoicePage3::activate_selected()
{
    if (selected_index < 0 || selected_index >= static_cast<int>(values_.size())) return SettingApiResult::Failure;
    if (selected_index == current_) {
        request_back(); /* nothing to change */
        return SettingApiResult::Success;
    }
    /* the native host asks (a Dialog over the list) and answers through choice_confirm_reply(); the legacy roller
     * Settings has no PDA mode entry */
    if (!observer_) return SettingApiResult::Failure;
    asked_ = selected_index;
    observer_->choice_confirm("Restart the launcher?", "This restarts the launcher and closes open apps.", "Cancel",
                              "Restart");
    return SettingApiResult::Pending;
}

void LvSettingPdaModeChoicePage3::choice_confirm_reply(bool confirmed)
{
    if (asked_ < 0) return;
    const int row = asked_;
    asked_ = -1;
    if (!confirmed) {
        keep_current();
        return;
    }
    if (!store(values_[static_cast<size_t>(row)])) {
        keep_current();
        report_status("PDA mode could not be saved", true);
        return;
    }
    current_ = row;
    report_status("Restarting the launcher", false);
    /* the orderly end of the main loop, exit status 76: the unit starts the launcher again (main.cpp) */
    launcher_session::request_restart();
}

void LvSettingPdaModeChoicePage3::keep_current()
{
    select(current_); /* the list's check stays on the value in force */
}

void LvSettingPdaModeChoicePage3::request_back()
{
    if (back_requested_) return;
    back_requested_ = true;
    if (back_callback_) back_callback_();
}

std::unique_ptr<DComponens::LvglComponensBase> settings_pda_mode_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback)
{
    return std::make_unique<LvSettingPdaModeChoicePage3>(parent, page_node, std::move(back_callback));
}

namespace settings_pda {

void append_entry(Tree &tree, const NodeIter &screen)
{
    const pda_mode::Startup &start = pda_mode::startup();
    const std::vector<int> values = pda_mode::choices(start.board, start.offered);
    if (values.empty()) return; /* no portrait canvas with the writing zone on this board */
    NodeIter pda = tree.append_child(screen, SettingEntry{"PDA mode", settings_pda_mode_choice_page_factory});
    for (const int value : values) tree.append_child(pda, SettingEntry{pda_mode::label(value)});
}

} // namespace settings_pda
