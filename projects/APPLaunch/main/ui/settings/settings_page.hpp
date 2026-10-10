/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
#include <memory>

#include "settings_tree_types.hpp"
#include "ui_app_page.hpp"

class LvSettingRoller;
class SettingsNativeHost;

class UISettingTreePage : public AppPage {
public:
    // Key-only list pages: on a touch display, drags/taps are turned into Up/Down/Enter.
    static constexpr bool kTouchList = true;
    // Asked by the launcher before the page is created (launch.h page_native_choice): true when the native
    // Settings host runs (APPLAUNCH_SETTINGS_UI=native, settings_native_mode.hpp): the page then gets the native
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
    Custom,  // a page of its own the native host has no widget for yet (decision D3: compat path in dev builds)
    Plain,   // nothing to open
};
SettingsNodeKind settings_node_kind(const NodeIter &node);

// Refresh a section right before it is shown: the Launcher toggles and the Touch apps follow the installed apps,
// Date & Time re-reads Network Time, Bluetooth unblocks the radio and reads its alias.
void settings_prepare_section(const NodeIter &node);

// Leaving a section: Date & Time drops unsaved manual time edits.
void settings_leave_section(const NodeIter &node);
