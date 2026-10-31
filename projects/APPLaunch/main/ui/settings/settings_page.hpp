/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "settings_tree_types.hpp"
#include "ui_app_page.hpp"

class LvSettingRoller;
class SettingsNativeHost;

class UISettingTreePage : public AppPage {
public:
    // Key-only list pages: on a touch display, drags/taps are turned into Up/Down/Enter.
    static constexpr bool kTouchList = true;
    // Asked by the launcher before the page is created (launch.h page_native_choice): true when the native
    // Settings host runs (the dpi-scaled backend exists, settings_native_mode.hpp): the page then gets the native
    // display, full screen, with plain pointer touch, instead of the compat window.
    static bool native_display_now();

    UISettingTreePage();
    ~UISettingTreePage() override;

private:
    void create_page_detail();
    void build_native();
    static void back_home(void *data);
    void LeaveNextPage();
    void AnimateNextIn(std::function<void()> animate_over_func);
    void AnimateNextOut(std::function<void()> animate_over_func);
    void LoadNextPage();

    Tree mode_tree_;
    std::unique_ptr<LvSettingRoller> roller_;
    std::unique_ptr<SettingsNativeHost> native_host_;
};

// ---- shared by the two Settings renderers (legacy roller pages and the native host, task 014 P2a) ----

// How the native host shows a node of the tree; decided here, next to the tree, from the node's own page factory
// and API.
enum class SettingsNodeKind {
    Section, // a list of entries: Screen, Wi-Fi, Bluetooth, Launcher, Touch, Date & Time, Set Manually, System
    Choice,  // a value page (LvSettingValuePage3Base, a SettingsChoiceBinding): options, one of them current
    Toggle,  // an on / off entry with a status read (icon_enabled + API)
    Action,  // an entry with an API and no status
    Custom,  // a legacy page of another board with no native widget: hidden by the native host
    Native,  // a page built natively (settings_native_panel_create, settings_native_page() says which)
    Plain,   // nothing to open
};
SettingsNodeKind settings_node_kind(const NodeIter &node);

// The native page of a Native node (task 014 P2b / P3a / P3b / P3c / P3d), from the node's own page factory: the
// development Keyboard test, the read-only pages (About, User, Storage, Licenses, Date & Time > Info), the Wi-Fi section
// (the section whose entries hold the Networks page: one native page with the Enable toggle, the networks and Join
// Hidden Network), Bluetooth > Paired Devices, Scan and Alias, and Apps. None for every other node.
enum class SettingsNativePage {
    None,
    KeyboardTest,
    Wifi,
    About,
    User,
    Storage,
    Licenses,
    DateTimeInfo,
    BluetoothPaired,
    BluetoothScan,
    BluetoothAlias,
    Apps,
    TimeZone, // Date & Time > Time zone: the zone picker (settings_native_timezone_page.hpp)
    Battery,  // Battery: a power_supply battery's read-only page (settings_native_info_page.hpp, decision X12)
    StrokesTraining, // System > Strokes training: the PDA writing zone's training (settings_strokes_page.hpp, report 042)
    Updates,  // Updates: the launcher's release check and notes (settings_native_updates_page.hpp, report 047 P1)
};
SettingsNativePage settings_native_page(const NodeIter &node);

// What a Native row shows at its right, decided here next to the tree: Date & Time > Time zone shows the zone in use
// and a chevron (it opens a picker); Updates its state ("0.8.13 available" in amber, "Up to date", ...) and a chevron;
// every other Native row nothing (as before).
struct SettingsNativeRowLook {
    std::string value;
    bool chevron = false;
    int value_rgb = -1; /* the value's colour (0xRRGGBB), -1: the roller's green */
};
SettingsNativeRowLook settings_native_row_look(const NodeIter &node);

// The entries of the Wi-Fi section the native Wi-Fi page uses: the Enable toggle (its API: the radio state) and Join
// Hidden Network (present on this board: the page offers the hidden-network form, with the entry's label).
struct SettingsWifiSection {
    bool has_enable = false;
    NodeIter enable;
    bool has_hidden = false;
    std::string hidden_label;
};
SettingsWifiSection settings_wifi_section(const NodeIter &wifi);

// Refresh a section right before it is shown: the Launcher toggles and the Touch apps follow the installed apps,
// Date & Time re-reads Network Time, Bluetooth unblocks the radio and reads its alias.
void settings_prepare_section(const NodeIter &node);

// Leaving a section: Date & Time drops unsaved manual time edits.
void settings_leave_section(const NodeIter &node);
