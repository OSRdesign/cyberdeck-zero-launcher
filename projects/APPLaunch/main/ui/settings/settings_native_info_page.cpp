/*
 * SPDX-License-Identifier: MIT
 *
 * The read-only pages of the native Settings (see settings_native_info_page.hpp).
 */

#include "settings_native_info_page.hpp"

#include "lvgl_components.hpp"
#include "settings_battery_model.hpp"
#include "settings_fonts.hpp"
#include "settings_rtc_page.hpp"
#include "settings_static_info_page.hpp"
#include "settings_system_model.hpp"

#include "cp0_lvgl_app.h"
#include "cp0_ui_metrics_lvgl.h"
#include "hal_lvgl_bsp.h"

#include <cerrno>
#include <chrono>
#include <memory>
#include <utility>

namespace settings_info {

cp0::ui::InfoItem item_of(const std::string &line)
{
    const std::size_t colon = line.find(": ");
    if (colon == std::string::npos || colon == 0) return {line, {}};
    return {line.substr(0, colon), line.substr(colon + 2)};
}

std::vector<cp0::ui::InfoItem> items_of(const std::vector<std::string> &lines)
{
    std::vector<cp0::ui::InfoItem> items;
    items.reserve(lines.size());
    for (const std::string &line : lines) items.push_back(item_of(line));
    return items;
}

} // namespace settings_info

namespace {

constexpr std::chrono::milliseconds kAccountTimeout{15000}; /* the legacy User page's request timeout */
constexpr uint32_t kClockRefreshMs = 1000;                   /* the legacy Date & Time Info page's refresh */
constexpr uint32_t kBatteryRefreshMs = 2000;                 /* the status strip's */

std::vector<cp0::ui::InfoItem> battery_items()
{
    cp0_power_battery_t battery{};
    const bool found = cp0_power_supply_battery(&battery) != 0;
    return settings_info::items_of(settings_battery::info_lines(found, battery));
}

/* A worker for the account read (the legacy component base's async tasks: a worker thread, the result on the LVGL
 * thread, cancelled and joined when the page goes). */
class Tasks : public DComponens::LvglComponensBase {
public:
    void create_ui(lv_obj_t *) override {}
    void LoadNextPage() override {}
    void LeaveNextPage() override {}
};

struct AccountReply {
    int code = -1;
    std::string payload;
};

std::vector<cp0::ui::InfoItem> account_items(const settings_system::AccountInfo &account)
{
    // the rows of the legacy User page (LvSettingSystemInfoPage3, Account)
    return {{"Username", account.username},
            {"Password", settings_system::password_unsupported_label()},
            {"Hostname", account.hostname}};
}

std::string joined(const std::vector<std::string> &lines)
{
    std::string text;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) text.push_back('\n');
        text += lines[i];
    }
    return text;
}

class InfoPanel : public SettingsNativePanel {
public:
    InfoPanel(SettingsNativePage kind, const SettingsNativePanelContext &context) : page_(context.page)
    {
        const cp0_ui_roller_page_t &layout = page_.layout();
        lv_obj_t *focus = nullptr;
        if (kind == SettingsNativePage::Licenses) {
            const int px = cp0_ui_metrics_get()->text_px[CP0_TEXT_BODY];
            text_ = std::make_unique<cp0::ui::TextBlock>(page_.root(), layout, joined(settings_credit_content().lines),
                                                         settings_fonts::cjk_sans(static_cast<uint16_t>(px)),
                                                         context.back);
            focus = text_->focus_object();
        } else {
            table_ = std::make_unique<cp0::ui::InfoTable>(page_.root(), layout, context.back);
            focus = table_->focus_object();
            switch (kind) {
            case SettingsNativePage::About:
                table_->set_items(settings_info::items_of(settings_about_content().lines));
                break;
            case SettingsNativePage::Storage:
                table_->set_items(settings_info::items_of(settings_storage_content().lines));
                break;
            case SettingsNativePage::DateTimeInfo:
                clock_lines_ = settings_rtc_info_content().lines;
                table_->set_items(settings_info::items_of(clock_lines_));
                clock_timer_ = lv_timer_create(clock_cb, kClockRefreshMs, this);
                break;
            case SettingsNativePage::User:
                table_->set_items(account_items({"--", "--"}));
                load_account();
                break;
            case SettingsNativePage::Battery:
                table_->set_items(battery_items());
                battery_timer_ = lv_timer_create(battery_cb, kBatteryRefreshMs, this);
                break;
            default:
                break;
            }
        }
        if (context.group && focus) {
            lv_group_add_obj(context.group, focus);
            lv_group_focus_obj(focus);
        }
    }

    ~InfoPanel() override
    {
        if (clock_timer_) lv_timer_delete(clock_timer_);
        clock_timer_ = nullptr;
        if (battery_timer_) lv_timer_delete(battery_timer_);
        battery_timer_ = nullptr;
        tasks_.reset(); /* cancels and joins the account read before the table goes */
        if (caption_) page_.set_caption("", false);
    }

private:
    static void clock_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<InfoPanel *>(lv_timer_get_user_data(timer));
        if (!self || !self->table_) return;
        std::vector<std::string> lines = self->clock_lines_;
        if (settings_rtc_info_lines(lines)) self->table_->set_items(settings_info::items_of(lines));
    }

    static void battery_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<InfoPanel *>(lv_timer_get_user_data(timer));
        if (self && self->table_) self->table_->set_items(battery_items());
    }

    void caption(const std::string &text, bool error)
    {
        caption_ = !text.empty();
        page_.set_caption(text, error);
    }

    void load_account()
    {
        tasks_ = std::make_unique<Tasks>();
        caption("Loading account information...", false);
        DComponens::LvglComponensBase::AsyncTaskCallbacks<AccountReply> callbacks;
        callbacks.execute = [] {
            // the OS info service answers on the caller's thread (cp0_lvgl_osinfo.cpp): this worker
            auto reply = std::make_shared<AccountReply>();
            try {
                cp0_signal_osinfo_api({"AccountInfoRead"}, [reply](int code, std::string data) {
                    reply->code = code;
                    reply->payload = std::move(data);
                });
            } catch (...) {
            }
            return *reply;
        };
        callbacks.on_complete = [this](DComponens::LvglComponensBase::AsyncTaskContext &, const AccountReply &reply) {
            settings_system::AccountInfo account;
            if (reply.code == 0 && settings_system::parse_account_info_strict(reply.payload, account)) {
                table_->set_items(account_items(account));
                caption("", false);
            } else {
                caption(reply.code == 0 ? "Invalid system information payload"
                                        : settings_system::backend_error_label(reply.code, reply.payload),
                        true);
            }
        };
        callbacks.on_timeout = [this](DComponens::LvglComponensBase::AsyncTaskContext &) {
            caption(settings_system::backend_error_label(-ETIMEDOUT), true);
        };
        callbacks.on_schedule_failed = [this](DComponens::LvglComponensBase::AsyncTaskContext &) {
            caption("Unable to schedule account request", true);
        };
        DComponens::LvglComponensBase::AsyncTaskOptions options;
        options.timeout = kAccountTimeout;
        tasks_->run_async_task(std::move(callbacks), options);
    }

    cp0::ui::Page &page_;
    std::unique_ptr<cp0::ui::InfoTable> table_;
    std::unique_ptr<cp0::ui::TextBlock> text_;
    std::unique_ptr<Tasks> tasks_;
    std::vector<std::string> clock_lines_;
    lv_timer_t *clock_timer_ = nullptr;
    lv_timer_t *battery_timer_ = nullptr;
    bool caption_ = false;
};

} // namespace

std::unique_ptr<SettingsNativePanel> settings_info_panel_create(SettingsNativePage page,
                                                                const SettingsNativePanelContext &context)
{
    return std::make_unique<InfoPanel>(page, context);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_battery_page_factory(lv_obj_t *, const NodeIter &,
                                                                             std::function<void()>)
{
    return nullptr; /* native only: settings_native_panel_create() */
}

namespace settings_battery {

void append_entry(Tree &tree, const NodeIter &root)
{
    if (present()) tree.append_child(root, SettingEntry{"Battery", settings_battery_page_factory, PageType::Normal});
}

} // namespace settings_battery
