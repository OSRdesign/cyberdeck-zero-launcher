/*
 * Settings > Touch: choose the touch gesture behaviour of each installed app.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "lvgl_components.hpp"

// One app's page: Off / Swipe / Swipe + Fire. The page node's label is the app name.
class LvSettingTouchChoicePage3 : public LvSettingValuePage3Base {
public:
    LvSettingTouchChoicePage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback);
    ~LvSettingTouchChoicePage3() override;

protected:
    int initial_selection() const override;
    SettingApiResult activate_selected() override;

private:
    void request_back();

    std::string app_name_;
    int current_ = 0;
    bool back_requested_ = false;
    std::function<void()> back_callback_;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_touch_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

namespace settings_t12b {

// Rebuilds the children of the "Touch" entry: every registered app except the ones that never use touch
// gestures (terminals, SSH, Calculator, IP Panel, Settings), sorted alphabetically.
bool populate_touch_children(Tree &tree, const NodeIter &parent);

} // namespace settings_t12b
