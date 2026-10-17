/*
 * SPDX-License-Identifier: MIT
 *
 * Settings entry of pointer navigation (see settings_pointer_page.hpp).
 */

#include "settings_pointer_page.hpp"

#include "../launcher_pointer.hpp"

#include <utility>

// ------------------------------------------------------------------ System > Pointer navigation: Off / On

LvSettingPointerChoicePage3::LvSettingPointerChoicePage3(lv_obj_t *parent, const NodeIter &parent_node,
                                                         std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, {})
{
    current_ = launcher_pointer::mode(); /* read before initialize(): it selects this row (0 Off, 1 On) */
    back_callback_ = std::move(back_callback);
    LeaveSelfPage = [this] { request_back(); };
    initialize(parent);
}

LvSettingPointerChoicePage3::~LvSettingPointerChoicePage3()
{
    LeaveSelfPage = nullptr;
    back_callback_ = nullptr;
}

int LvSettingPointerChoicePage3::initial_selection() const
{
    return current_;
}

SettingApiResult LvSettingPointerChoicePage3::activate_selected()
{
    if (selected_index != current_ && !launcher_pointer::set_mode(selected_index)) return SettingApiResult::Failure;
    current_ = selected_index;
    request_back();
    return SettingApiResult::Success;
}

void LvSettingPointerChoicePage3::request_back()
{
    if (back_requested_) return;
    back_requested_ = true;
    if (back_callback_) back_callback_();
}

std::unique_ptr<DComponens::LvglComponensBase> settings_pointer_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback)
{
    return std::make_unique<LvSettingPointerChoicePage3>(parent, page_node, std::move(back_callback));
}

namespace settings_pointer {

void append_entries(Tree &tree, const NodeIter &system)
{
    NodeIter pointer = tree.append_child(system, SettingEntry{"Pointer navigation", settings_pointer_choice_page_factory});
    tree.append_child(pointer, SettingEntry{"Off"}); /* index 0 = launcher_pointer::kOff (the default) */
    tree.append_child(pointer, SettingEntry{"On"});  /* index 1 = launcher_pointer::kOn */
}

} // namespace settings_pointer
