/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Screen > PDA mode (report 043, phase S7b; the rules are in pda_mode.hpp), in the native Settings tree only:
 *
 *   Screen > PDA mode   Off / On rotate left / On rotate right (a value page: a ChoicePage in the native host). Listed
 *                       only where pda_mode::startup() says the board offers it (a portrait canvas with the writing
 *                       zone fits: short side at least 480 px, not square, a board profile). The current value is what
 *                       this launcher started with. Picking another one asks first ("Restart the launcher?": this
 *                       restarts the launcher and closes open apps); Restart stores it (config "pda_mode" through the
 *                       config service, SetInt + Save) and ends the launcher with the restart exit status
 *                       (launcher_session::request_restart(), 76): its unit starts it again, and the new launcher
 *                       turns the canvas before its display opens. Cancel (or Esc) keeps the current value.
 *
 * The legacy roller Settings (stock builds without the dpi-scaled backend) does not get it.
 */
#pragma once

#include "lvgl_components.hpp"

#include <functional>
#include <memory>
#include <vector>

// Screen > PDA mode: Off / On rotate left / On rotate right.
class LvSettingPdaModeChoicePage3 : public LvSettingValuePage3Base {
public:
    LvSettingPdaModeChoicePage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback);
    ~LvSettingPdaModeChoicePage3() override;

    void choice_set_observer(SettingsChoiceObserver *observer) override;
    void choice_confirm_reply(bool confirmed) override;

protected:
    int initial_selection() const override;
    SettingApiResult activate_selected() override;

private:
    void request_back();
    void keep_current();

    std::vector<int> values_; /* pda_mode::choices(), the rows' values */
    int current_ = 0;         /* the row of the value this launcher started with */
    int asked_ = -1;          /* the row waiting for the confirmation, -1: none */
    bool back_requested_ = false;
    SettingsChoiceObserver *observer_ = nullptr;
    std::function<void()> back_callback_;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_pda_mode_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

namespace settings_pda {

// Appends "PDA mode" to the Screen section where the board offers it. Native Settings only.
void append_entry(Tree &tree, const NodeIter &screen);

} // namespace settings_pda
