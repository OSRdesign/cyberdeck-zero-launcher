/*
 * Settings > Touch: choose the touch gesture behaviour of each installed app.
 */

#include "settings_touch_page.hpp"

#include "../app_registry.h"
#include "../touch_settings.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>
#include <vector>

LvSettingTouchChoicePage3::LvSettingTouchChoicePage3(lv_obj_t *parent,
                                                     const NodeIter &parent_node,
                                                     std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, {})
{
    app_name_ = parent_node->label;
    current_ = touch_settings::choice(app_name_); // read before initialize(): it selects this row
    back_callback_ = std::move(back_callback);
    LeaveSelfPage = [this] { request_back(); };
    initialize(parent);
}

LvSettingTouchChoicePage3::~LvSettingTouchChoicePage3()
{
    LeaveSelfPage = nullptr;
    back_callback_ = nullptr;
}

int LvSettingTouchChoicePage3::initial_selection() const
{
    return current_;
}

SettingApiResult LvSettingTouchChoicePage3::activate_selected()
{
    if (selected_index != current_ && !touch_settings::set_choice(app_name_, selected_index))
        return SettingApiResult::Failure;
    current_ = selected_index;
    request_back();
    return SettingApiResult::Success;
}

void LvSettingTouchChoicePage3::request_back()
{
    if (back_requested_) return;
    back_requested_ = true;
    if (back_callback_) back_callback_();
}

std::unique_ptr<DComponens::LvglComponensBase> settings_touch_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback)
{
    return std::make_unique<LvSettingTouchChoicePage3>(parent, page_node, std::move(back_callback));
}

namespace settings_t12b {

bool populate_touch_children(Tree &tree, const NodeIter &parent)
{
    tree.erase_children(parent);

    // apps that never use touch gestures: terminals, SSH, the Calculator, IP Panel and Settings itself
    static const std::set<std::string> excluded = {"Settings", "CLI", "Python", "SSH", "Calculator", "IP Panel"};

    std::size_t count = 0;
    const AppDescriptor *entries = launcher_app_registry_entries(&count);
    if (count > 0 && !entries) return false;

    std::vector<std::string> names;
    for (std::size_t index = 0; index < count; ++index) {
        if (!entries[index].label || !entries[index].label[0]) continue;
        const std::string name = entries[index].label;
        if (excluded.count(name) || std::find(names.begin(), names.end(), name) != names.end()) continue;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end(), [](const std::string &left, const std::string &right) {
        return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                            [](unsigned char a, unsigned char b) {
                                                return std::tolower(a) < std::tolower(b);
                                            });
    });

    for (const std::string &name : names) {
        NodeIter app = tree.append_child(parent, SettingEntry{name, settings_touch_choice_page_factory});
        tree.append_child(app, SettingEntry{"Off"});
        tree.append_child(app, SettingEntry{"Swipe"});
        tree.append_child(app, SettingEntry{"Swipe + Fire"});
    }
    return true;
}

} // namespace settings_t12b
