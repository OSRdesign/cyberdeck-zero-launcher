/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include "../launcher_ui_app_page.hpp"
#include "../model/async_operation_lifecycle.hpp"
#include "../model/ssh_connection_model.hpp"
#include "../model/ssh_view_build_contract.hpp"
#include "keyboard_input.h"
#include <array>
#include <memory>
#include <string>

class UISTPage;

namespace cp0 {
namespace ui {
class Page;
class TextField;
class TextBlock;
} // namespace ui
} // namespace cp0

// ============================================================
//  SSH Client  UISSHPage
//  Screen: 320 x 170  (top bar 20px, ui_APP_Container 320x150)
//
//  Views:
//    VIEW_INPUT    -- Host/Port/User input fields
//    VIEW_HELP     -- SSH controls and shortcut reference
//    VIEW_TERMINAL -- Embedded UISTPage running ssh
//
//  On the native display (the dpi-scaled backend: the Pi boards, task 014 P4) the
//  form is a native page instead (ui_app_ssh_native.cpp): the Settings header,
//  Host / Port / User as TextFields, Connect and Help buttons, a status line,
//  Help as a TextBlock. Builds without that backend keep the 320x150 form above.
// ============================================================

class UISSHPage : public AppPage
{
    enum class ViewState { INPUT, HELP, TERMINAL };

public:
    UISSHPage();

    ~UISSHPage();

    // The launcher asks at launch time (launch.h page_native_choice): the native form whenever the native display
    // exists.
    static bool native_display_now();

private:
    // ==================== data members ====================
    SshConnectionModel model_;
    lv_obj_t *background_ = nullptr;
    lv_obj_t *form_container_ = nullptr;
    lv_obj_t *help_container_ = nullptr;
    ViewState view_state_ = ViewState::INPUT;
    std::shared_ptr<UISTPage> terminal_page_;
    bool terminal_return_pending_ = false;
    bool input_context_saved_ = false;
    cp0_keyboard_input_context_t previous_input_context_ = KBD_INPUT_CONTEXT_NAVIGATION;
    int previous_keypad_intercept_ = 0;
    setting::AsyncOperationLifecycle restore_operation_;
    setting::AsyncOperationLifecycle::Token restore_token_;
    std::string status_message_;
    bool status_error_ = false;

    // ==================== UI construction (input view) ====================
    void create_ui();

    bool build_input_fields();
    bool build_help_view();
    void rebuild_or_restore(SshConnectionModel previous) noexcept;
    void show_help();
    void close_help();

    // ==================== connect via SSH ====================
    void do_connect();
    bool persist_profile();
    void save_profile();
    void load_profile();
    void set_status(std::string message, bool error);

    void restore_input_view();
    void enter_text_input_mode();
    void restore_text_input_mode();

    // ==================== event binding ====================
    void event_handler_init();

    static void static_lvgl_handler(lv_event_t *e) noexcept;
    static void static_owned_obj_delete_cb(lv_event_t *event) noexcept;

    void event_handler(lv_event_t *e);
    void detach_delete_callbacks();

    // ==================== native form (ui_app_ssh_native.cpp) ====================
    bool native_ = false;
    bool native_strip_added_ = false;
    std::unique_ptr<cp0::ui::Page> native_page_;
    std::array<std::unique_ptr<cp0::ui::TextField>, SshConnectionModel::FIELD_COUNT> native_fields_;
    std::unique_ptr<cp0::ui::TextBlock> native_help_;
    lv_obj_t *native_connect_ = nullptr;
    lv_obj_t *native_help_button_ = nullptr;
    lv_obj_t *native_status_ = nullptr;
    lv_obj_t *native_buttons_row_ = nullptr;
    int native_inset_listener_ = 0;
    lv_obj_t *native_help_return_ = nullptr; // focused before the help opened
    bool native_help_close_posted_ = false;

    void build_native();
    void destroy_native();
    void native_set_status();
    void native_relayout();
    static void native_field_focus_cb(lv_event_t *event);
    void native_sync_model();
    void native_stop_editing();
    void native_show_help();
    void native_close_help();
    void native_fill_group();
    void native_on_field_submit(size_t index);
    void native_focus_step(lv_obj_t *from, bool next);
    static void native_screen_loaded_cb(lv_event_t *event);
    static void native_keyboard_cb(lv_event_t *event);
    static void native_button_cb(lv_event_t *event);
    static void native_close_help_cb(void *user);
};
