/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <list>
#include <string>

#include "lvgl_components.hpp"

class LvSettingBrightnessPage3 : public LvSettingValuePage3Base {
public:
    using Arguments = std::list<std::string>;
    using Callback = std::function<void(int, std::string)>;
    using SettingsInvoker = std::function<void(Arguments, Callback)>;
    using ConfigInvoker = std::function<void(Arguments, Callback)>;

    enum class BrightnessReadStatus : uint8_t { Ok, Defaulted, BackendError, InvalidPayload };
    struct BrightnessReadResult {
        BrightnessReadStatus status = BrightnessReadStatus::BackendError;
        int maximum = 100;
        int value = 75;
        int index = 0;
        std::string message;
        bool usable() const noexcept;
        bool defaulted() const noexcept;
    };
    enum class BrightnessWriteStatus : uint8_t {
        Ok, InvalidTarget, ConfigReadFailed, BacklightWriteFailed,
        BacklightPayloadInvalid, ConfigWriteFailed, SaveFailed, RollbackFailed
    };
    struct BrightnessWriteResult {
        BrightnessWriteStatus status = BrightnessWriteStatus::ConfigReadFailed;
        int previous_value = 0;
        int previous_config = 0;
        int applied_value = 0;
        int applied_index = 0;
        bool rollback_attempted = false;
        bool rollback_succeeded = false;
        std::string message;
        bool succeeded() const noexcept;
    };

    enum class LayoutMetric : int {
        StatusLabelW = 104,
        StatusLabelX = 4,
        StatusLabelY = 4,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    /* On/off backlight (board profile gpio:<dir>): the page lists "On" and "Off <n> s". Off
     * switches the panel dark for kOffSeconds only and then back on, with a countdown in the
     * status label; Enter/tap or leaving the page switches it back on at once. Nothing is
     * persisted in this mode. */
    static constexpr int kOffSeconds = 10;

    LvSettingBrightnessPage3();
    LvSettingBrightnessPage3(lv_obj_t *parent, const NodeIter &parent_node);
    LvSettingBrightnessPage3(lv_obj_t *parent,
                             const NodeIter &parent_node,
                             std::function<void()> back_callback);
    ~LvSettingBrightnessPage3() override;

    bool choice_ready() const override { return loaded_; }

protected:
    int initial_selection() const override;
    SettingApiResult activate_selected() override;

private:
    static SettingsInvoker settings_invoker();
    static ConfigInvoker config_invoker();

    int option_count() const;
    void initialize_page(lv_obj_t *parent, std::function<void()> back_callback);
    void request_back();
    void restore_focus();
    bool request_is_current(std::uint64_t generation) const;
    void create_status_label();
    void set_status(const std::string &text, bool error);
    void finish_load(const BrightnessReadResult &result);
    void finish_load_failure();
    void begin_load();
    void finish_write(const BrightnessWriteResult &result);
    void finish_write_failure();
    bool begin_write();
    SettingApiResult activate_onoff();
    void stop_off_countdown(bool switch_on);
    void show_off_countdown();
    static void off_countdown_cb(lv_timer_t *timer);

    bool onoff_ = false;
    lv_timer_t *off_timer_ = nullptr;
    int off_remaining_ = 0;
    bool destroying_ = false;
    bool back_requested_ = false;
    bool page_alive_ = true;
    bool pending_ = false;
    bool loaded_ = false;
    std::uint64_t generation_ = 0;
    int maximum_ = 100;
    int value_ = 75;
    int saved_index_ = 0;
    lv_obj_t *status_label_ = nullptr;
    std::function<void()> back_callback_;
};
