/*
 * SPDX-License-Identifier: MIT
 *
 * Settings entry of pointer navigation (cp0_pointer_nav.h), in the native Settings tree only, right before the
 * on-screen keyboard's entries:
 *
 *   System > Pointer navigation   Off / On (a value page: a ChoicePage in the native host; persisted as config
 *                                 "pointer_nav" through the config service, launcher_pointer.hpp). Default Off.
 *
 * The legacy roller Settings (stock builds without the dpi-scaled backend) does not get it.
 */
#pragma once

#include "lvgl_components.hpp"

#include <functional>
#include <memory>

// System > Pointer navigation: Off / On.
class LvSettingPointerChoicePage3 : public LvSettingValuePage3Base {
public:
    LvSettingPointerChoicePage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback);
    ~LvSettingPointerChoicePage3() override;

protected:
    int initial_selection() const override;
    SettingApiResult activate_selected() override;

private:
    void request_back();

    int current_ = 0;
    bool back_requested_ = false;
    std::function<void()> back_callback_;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_pointer_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

namespace settings_pointer {

// Appends "Pointer navigation" to the System section. Call only when the native Settings host is selected.
void append_entries(Tree &tree, const NodeIter &system);

} // namespace settings_pointer
