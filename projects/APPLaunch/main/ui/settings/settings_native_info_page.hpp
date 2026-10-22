/*
 * SPDX-License-Identifier: MIT
 *
 * The read-only pages of the native Settings (task 014 P3a): System > About / Storage / Licenses, User and Date & Time >
 * Info, in the roller look. Same data as the legacy pages, from the same functions: settings_about_content(),
 * settings_storage_content(), settings_credit_content() (settings_static_info_page.hpp), settings_rtc_info_content()
 * and settings_rtc_info_lines() (settings_rtc_page.hpp), the account through the OS info service's AccountInfoRead
 * parsed by settings_system::parse_account_info_strict (settings_system_model.hpp). Also Battery (our boards with a
 * power_supply battery, settings_battery_model.hpp).
 *
 *   About, Storage, User, Date & Time Info   an InfoTable: each "Key: value" line is a row (key left, value right),
 *                                            a line without ": " a key alone; Date & Time refreshes every second;
 *                                            User loads in the background (its status in the page's status line)
 *   Licenses                                 a TextBlock: the whole text, drag or Up / Down to scroll
 *   Battery                                  an InfoTable of settings_battery::info_lines() (a power_supply battery,
 *                                            decision X12), read again every 2 s
 *
 * Esc / Left / the header's Esc go back to the section.
 */
#pragma once

#include "settings_native_panel.hpp"
#include "settings_page.hpp"

#include "cp0_ui_widgets.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace DComponens {
class LvglComponensBase;
}

std::unique_ptr<SettingsNativePanel> settings_info_panel_create(SettingsNativePage page,
                                                                const SettingsNativePanelContext &context);

/* The Battery entry's factory: identifies the native page (settings_native_page()); there is no legacy page
 * (nullptr). */
std::unique_ptr<DComponens::LvglComponensBase> settings_battery_page_factory(lv_obj_t *parent, const NodeIter &node,
                                                                             std::function<void()> back);

namespace settings_battery {
/* Settings > Battery (after Bluetooth) when the board has a power_supply battery now (settings_battery::present());
 * native Settings only */
void append_entry(Tree &tree, const NodeIter &root);
} // namespace settings_battery

namespace settings_info {

/* "Key: value" -> {key, value} at the first ": "; a line without one -> the key alone. */
cp0::ui::InfoItem item_of(const std::string &line);
std::vector<cp0::ui::InfoItem> items_of(const std::vector<std::string> &lines);

} // namespace settings_info
