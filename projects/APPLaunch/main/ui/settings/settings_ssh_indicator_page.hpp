/*
 * SPDX-License-Identifier: MIT
 *
 * Settings entry of the SSH indicator (the terminal glyph in the status strip while an incoming SSH session exists,
 * launcher_ssh.hpp), in the native Settings tree only, right after Pointer navigation:
 *
 *   System > SSH indicator   Off / On (a value page: a ChoicePage in the native host; persisted as config
 *                            "ssh_indicator" through the config service, written only when changed). Default On.
 *
 * Off: the launcher stops looking for sessions and status.state says ssh=0, so the apps' strips hide it too. The
 * strip's slot stays reserved either way (the header's Esc and title never move). The legacy roller Settings (stock
 * builds without the dpi-scaled backend) does not get it.
 */
#pragma once

#include "lvgl_components.hpp"

#include <functional>
#include <memory>

// System > SSH indicator: Off / On.
class LvSettingSshIndicatorChoicePage3 : public LvSettingValuePage3Base {
public:
    LvSettingSshIndicatorChoicePage3(lv_obj_t *parent, const NodeIter &parent_node,
                                     std::function<void()> back_callback);
    ~LvSettingSshIndicatorChoicePage3() override;

protected:
    int initial_selection() const override;
    SettingApiResult activate_selected() override;

private:
    void request_back();

    int current_ = 0;
    bool back_requested_ = false;
    std::function<void()> back_callback_;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_ssh_indicator_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

namespace settings_ssh_indicator {

// Appends "SSH indicator" to the System section. Call only when the native Settings host is selected.
void append_entries(Tree &tree, const NodeIter &system);

} // namespace settings_ssh_indicator
