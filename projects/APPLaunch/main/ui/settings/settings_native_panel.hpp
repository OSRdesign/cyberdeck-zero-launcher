/*
 * SPDX-License-Identifier: MIT
 *
 * Native pages of the native Settings host (task 014 P2b / P3a / P3b / P3c / P3d): a page built with the shell's widgets
 * on the host's scaffold (cp0::ui::Page) instead of the host's roller, for a SettingsNodeKind::Native node. Which page a
 * node gets is settings_native_page() (settings_page.hpp); this file only builds it.
 *
 *   KeyboardTest  the development form (settings_keyboard_page.cpp)
 *   About, User, Storage, Licenses, DateTimeInfo, Battery   the read-only pages (settings_native_info_page.cpp)
 *   Wifi          the Wi-Fi section (settings_native_wifi_page.cpp)
 *   Bluetooth     Paired Devices, Scan, Alias (settings_native_bluetooth_page.cpp)
 *   Apps          Settings > Apps, its Apps and Sources tabs (settings_native_apps_page.cpp)
 *   TimeZone      Date & Time > Time zone, the two-level zone picker (settings_native_timezone_page.cpp)
 *   StrokesTraining  System > Strokes training, the PDA writing zone's training (settings_strokes_page.cpp)
 *   Updates       Updates, the launcher's release check and notes (settings_native_updates_page.cpp, report 047 P1)
 *
 * The host hides its roller and takes it out of the input group while a panel is up; the panel puts its own widgets
 * in the group. Esc on the panel's widgets calls `back` (the host's back, posted), and the host first asks the panel
 * (SettingsNativePanel::back) so a panel with levels of its own (a dialog, a sub-list) goes back one of them.
 *
 * LVGL thread only.
 */
#pragma once

#include "settings_tree_types.hpp"

#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cp0 {
namespace ui {
class Page;
} // namespace ui
} // namespace cp0

class SettingsNativePanel {
public:
    virtual ~SettingsNativePanel() = default;
    /* Esc (a key, the header's Esc): true when the panel went back one of its own levels, false when the host should
     * close it. */
    virtual bool back() { return false; }
};

struct SettingsNativePanelContext {
    cp0::ui::Page &page;
    lv_group_t *group;              /* the page's input group (NULL: none) */
    std::vector<std::string> title; /* the host's title path to this page: "Settings", "System", "About" */
    std::function<void()> back;     /* the host's back action (posted) */
};

std::unique_ptr<SettingsNativePanel> settings_native_panel_create(const NodeIter &node,
                                                                  const SettingsNativePanelContext &context);
