/*
 * SPDX-License-Identifier: MIT
 *
 * Settings entry of the SSH indicator (see settings_ssh_indicator_page.hpp).
 */

#include "settings_ssh_indicator_page.hpp"

#include "../launcher_ssh.hpp"

#include <utility>

// ------------------------------------------------------------------ System > SSH indicator: Off / On

LvSettingSshIndicatorChoicePage3::LvSettingSshIndicatorChoicePage3(lv_obj_t *parent, const NodeIter &parent_node,
                                                                   std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, {})
{
    current_ = launcher_ssh::mode(); /* read before initialize(): it selects this row (0 Off, 1 On) */
    back_callback_ = std::move(back_callback);
    LeaveSelfPage = [this] { request_back(); };
    initialize(parent);
}

LvSettingSshIndicatorChoicePage3::~LvSettingSshIndicatorChoicePage3()
{
    LeaveSelfPage = nullptr;
    back_callback_ = nullptr;
}

int LvSettingSshIndicatorChoicePage3::initial_selection() const
{
    return current_;
}

SettingApiResult LvSettingSshIndicatorChoicePage3::activate_selected()
{
    if (selected_index != current_ && !launcher_ssh::set_mode(selected_index)) return SettingApiResult::Failure;
    current_ = selected_index;
    request_back();
    return SettingApiResult::Success;
}

void LvSettingSshIndicatorChoicePage3::request_back()
{
    if (back_requested_) return;
    back_requested_ = true;
    if (back_callback_) back_callback_();
}

std::unique_ptr<DComponens::LvglComponensBase> settings_ssh_indicator_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback)
{
    return std::make_unique<LvSettingSshIndicatorChoicePage3>(parent, page_node, std::move(back_callback));
}

namespace settings_ssh_indicator {

void append_entries(Tree &tree, const NodeIter &system)
{
    NodeIter ssh = tree.append_child(system, SettingEntry{"SSH indicator", settings_ssh_indicator_choice_page_factory});
    tree.append_child(ssh, SettingEntry{"Off"}); /* index 0 = ssh_indicator::kOff */
    tree.append_child(ssh, SettingEntry{"On"});  /* index 1 = ssh_indicator::kOn (the default) */
}

} // namespace settings_ssh_indicator
