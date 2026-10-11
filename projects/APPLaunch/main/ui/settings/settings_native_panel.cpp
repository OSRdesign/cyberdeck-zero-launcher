/*
 * SPDX-License-Identifier: MIT
 *
 * Native pages of the native Settings host (see settings_native_panel.hpp): which builder a Native node gets.
 */

#include "settings_native_panel.hpp"

#include "settings_keyboard_page.hpp"
#include "settings_native_bluetooth_page.hpp"
#include "settings_native_info_page.hpp"
#include "settings_native_wifi_page.hpp"
#include "settings_page.hpp"

std::unique_ptr<SettingsNativePanel> settings_native_panel_create(const NodeIter &node,
                                                                  const SettingsNativePanelContext &context)
{
    switch (settings_native_page(node)) {
    case SettingsNativePage::KeyboardTest:
        return settings_keyboard_test_panel_create(context.page, context.group, context.back);
    case SettingsNativePage::Wifi:
        return settings_wifi_panel_create(node, context);
    case SettingsNativePage::About:
    case SettingsNativePage::User:
    case SettingsNativePage::Storage:
    case SettingsNativePage::Licenses:
    case SettingsNativePage::DateTimeInfo:
        return settings_info_panel_create(settings_native_page(node), context);
    case SettingsNativePage::BluetoothPaired:
    case SettingsNativePage::BluetoothScan:
    case SettingsNativePage::BluetoothAlias:
        return settings_bluetooth_panel_create(settings_native_page(node), node, context);
    case SettingsNativePage::None:
        break;
    }
    return nullptr;
}
