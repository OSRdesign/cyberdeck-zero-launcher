/*
 * SPDX-License-Identifier: MIT
 *
 * Settings entries of the fallback on-screen keyboard (task 014 P2b), in the native Settings tree only:
 *
 *   System > On-screen keyboard   Auto / Off (a value page: a ChoicePage in the native host; persisted as config
 *                                 "osk_mode" through the config service, launcher_osk.hpp). Default Auto.
 *   System > Keyboard test        development builds only (settings_ui::dev_pages(): APPLAUNCH_DEV=1 or a development
 *                                 channel build): a native form with a Text, a Number and a Password field, to try the
 *                                 keyboard on a board (APPLAUNCH_OSK=force shows it even with a physical keyboard).
 *
 * The legacy roller Settings (stock builds without the dpi-scaled backend) gets neither.
 */
#pragma once

#include "lvgl_components.hpp"
#include "settings_native_panel.hpp"

#include <functional>
#include <memory>

namespace cp0 {
namespace ui {
class Page;
class TextField;
} // namespace ui
} // namespace cp0

// System > On-screen keyboard: Auto / Off.
class LvSettingKeyboardChoicePage3 : public LvSettingValuePage3Base {
public:
    LvSettingKeyboardChoicePage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback);
    ~LvSettingKeyboardChoicePage3() override;

protected:
    int initial_selection() const override;
    SettingApiResult activate_selected() override;

private:
    void request_back();

    int current_ = 0;
    bool back_requested_ = false;
    std::function<void()> back_callback_;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_keyboard_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

// The "Keyboard test" entry's factory: a marker for settings_native_page() (SettingsNativePage::KeyboardTest); the
// native host opens the page with settings_native_panel_create(). It builds nothing for the legacy renderer.
std::unique_ptr<DComponens::LvglComponensBase> settings_keyboard_test_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

// The Keyboard test form, built in page.body(), its fields in `group` (the first one focused: it edits, the keyboard
// comes). back: the page's back action (Esc on a field that is not editing).
std::unique_ptr<SettingsNativePanel> settings_keyboard_test_panel_create(cp0::ui::Page &page, lv_group_t *group,
                                                                         std::function<void()> back);

namespace settings_keyboard {

// Appends "On-screen keyboard" (and "Keyboard test" in development builds) to the System section. Call only when the
// native Settings host is selected.
void append_entries(Tree &tree, const NodeIter &system);

} // namespace settings_keyboard
