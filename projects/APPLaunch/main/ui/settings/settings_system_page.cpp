/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_system_page.hpp"

#include "cp0_lvgl_app.h"
#include "cp0_font_service.hpp"
#include "hal_lvgl_bsp.h"
#include "settings_about_info_model.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cerrno>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>

namespace {

#define SETTINGS_SYSTEM_STRINGIFY_IMPL(value) #value
#define SETTINGS_SYSTEM_STRINGIFY(value) SETTINGS_SYSTEM_STRINGIFY_IMPL(value)

const char *launcher_version()
{
#ifdef LAUNCHER_VERSION
    return LAUNCHER_VERSION;
#elif defined(LAUNCHER_VERSION_RAW)
    return SETTINGS_SYSTEM_STRINGIFY(LAUNCHER_VERSION_RAW);
#else
    return "0.0.0-dev";
#endif
}

const char *launcher_channel()
{
#ifdef LAUNCHER_CHANNEL
    return LAUNCHER_CHANNEL;
#elif defined(LAUNCHER_CHANNEL_RAW)
    return SETTINGS_SYSTEM_STRINGIFY(LAUNCHER_CHANNEL_RAW);
#else
    return "development";
#endif
}

const char *launcher_build_date()
{
#ifdef LAUNCHER_BUILD_DATE
    return LAUNCHER_BUILD_DATE;
#elif defined(LAUNCHER_BUILD_DATE_RAW)
    return SETTINGS_SYSTEM_STRINGIFY(LAUNCHER_BUILD_DATE_RAW);
#else
    return "unknown";
#endif
}

const char *launcher_commit()
{
#ifdef LAUNCHER_GIT_COMMIT
    return LAUNCHER_GIT_COMMIT;
#elif defined(LAUNCHER_GIT_COMMIT_RAW)
    return SETTINGS_SYSTEM_STRINGIFY(LAUNCHER_GIT_COMMIT_RAW);
#else
    return "unknown";
#endif
}

const lv_font_t *title_font()
{
    return settings_fonts::sans(14, LV_FREETYPE_FONT_STYLE_BOLD);
}

const lv_font_t *body_font()
{
    return settings_fonts::sans(12);
}

const lv_font_t *hint_font()
{
    return settings_fonts::sans(12, LV_FREETYPE_FONT_STYLE_BOLD);
}

lv_obj_t *create_label(lv_obj_t *parent,
                       int x,
                       int y,
                       int width,
                       const char *text,
                       uint32_t color,
                       const lv_font_t *font,
                       bool wrap = false)
{
    if (!parent) return nullptr;
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font ? font : body_font(), LV_PART_MAIN);
    return label;
}

void configure_page(lv_obj_t *object, int width, int height)
{
    if (!object) return;
    lv_obj_set_size(object, width, height);
    lv_obj_set_pos(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 0, LV_PART_MAIN);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void set_label(lv_obj_t *label, const std::string &text)
{
    if (label) lv_label_set_text(label, text.c_str());
}

bool uses_network_table(SettingsSystemPageKind kind)
{
    return kind == SettingsSystemPageKind::Network ||
           kind == SettingsSystemPageKind::Ethernet;
}

lv_obj_t *create_network_table_row(lv_obj_t *parent,
                                   int row_index,
                                   const char *key)
{
    if (!parent) return nullptr;
    using LayoutMetric = LvSettingSystemInfoPage3::LayoutMetric;
    const auto metric = [](LayoutMetric value) {
        return LvSettingSystemInfoPage3::metric(value);
    };

    lv_obj_t *row = lv_obj_create(parent);
    if (!row) return nullptr;
    lv_obj_set_pos(row,
                   metric(LayoutMetric::TableX),
                   metric(LayoutMetric::TableY) + row_index * metric(LayoutMetric::TableRowH));
    lv_obj_set_size(row, metric(LayoutMetric::TableW), metric(LayoutMetric::TableRowH));
    lv_obj_set_style_bg_color(row,
                              lv_color_hex(row_index % 2 == 0 ? 0x151515 : 0x1B1B1B),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0x383838), LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *divider = lv_obj_create(row);
    if (divider) {
        lv_obj_set_pos(divider, metric(LayoutMetric::TableDividerX), 0);
        lv_obj_set_size(divider,
                        metric(LayoutMetric::TableDividerW),
                        metric(LayoutMetric::TableDividerH));
        lv_obj_set_style_bg_color(divider, lv_color_hex(0x505050), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(divider, 0, LV_PART_MAIN);
        lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    }

    create_label(row,
                 metric(LayoutMetric::TableKeyX),
                 4,
                 metric(LayoutMetric::TableKeyW),
                 key,
                 0xA8A8A8,
                 settings_fonts::sans(13, LV_FREETYPE_FONT_STYLE_BOLD));
    return create_label(row,
                        metric(LayoutMetric::TableValueX),
                        3,
                        metric(LayoutMetric::TableValueW),
                        "--",
                        0xF2F2F2,
                        settings_fonts::mono(14));
}

SettingsSystemPageKind resolve_kind(const NodeIter &page_node, SettingsSystemPageKind requested)
{
    if (requested != SettingsSystemPageKind::Auto) return requested;
    if (page_node.node && !page_node->label.empty()) {
        if (page_node->label == "Password") return SettingsSystemPageKind::Password;
        if (page_node->label == "OS") return SettingsSystemPageKind::OS;
        if (page_node->label == "Username" || page_node->label == "Hostname" ||
            page_node->label == "Account")
            return SettingsSystemPageKind::Account;
        if (page_node->label == "Version") return SettingsSystemPageKind::Version;
        if (page_node->label == "Build") return SettingsSystemPageKind::Build;
        if (page_node->label == "Ethernet" || page_node->label == "IP" ||
            page_node->label == "Gateway" || page_node->label == "MAC")
            return SettingsSystemPageKind::Ethernet;
    }
    return SettingsSystemPageKind::Network;
}

// Keep the OS-info transport private to this page.  The page is the only
// consumer, so exposing a separate API surface only added an unnecessary
// coupling between settings modules.
void request_osinfo(std::list<std::string> arguments,
                    std::function<void(int, std::string)> callback) noexcept
{
    if (!callback) return;

    auto delivered = std::make_shared<std::atomic_bool>(false);
    auto safe_callback = [callback = std::move(callback), delivered](int code,
                                                                       std::string data) mutable {
        bool expected = false;
        if (!delivered->compare_exchange_strong(expected, true)) return;
        try {
            callback(code, std::move(data));
        } catch (...) {
        }
    };

    try {
        cp0_signal_osinfo_api(std::move(arguments), safe_callback);
    } catch (...) {
        safe_callback(-1, "osinfo service unavailable");
    }
}

template <typename Owner>
bool start_osinfo_request(Owner *owner,
                          std::list<std::string> arguments,
                          std::function<void(int, std::string)> handler,
                          std::function<void(int, std::string)> stale_handler = {})
{
    if (!owner || !handler || !owner->ensure_async_dispatch()) return false;

    const auto token = owner->async_token();
    if (!token.valid()) return false;

    auto callback = [token,
                    handler = std::move(handler),
                    stale_handler = std::move(stale_handler)](int code,
                                                                std::string data) mutable {
        auto deliver_stale = [&stale_handler](int result_code, std::string result_data) {
            if (!stale_handler) return;
            try {
                stale_handler(result_code, std::move(result_data));
            } catch (...) {
            }
        };

        if (!token.valid()) {
            deliver_stale(code, std::move(data));
            return;
        }

        const std::string stale_data = data;
        const bool queued = SettingsAsync::Dispatch::enqueue_from_callback(
            token,
            [handler = std::move(handler), code, data = std::move(data)]() mutable {
                if (handler) handler(code, std::move(data));
            });
        if (!queued) deliver_stale(code, stale_data);
    };

    return owner->async_tasks().start(
        [arguments = std::move(arguments), callback = std::move(callback)](auto stop) mutable {
            if (stop && stop->load(std::memory_order_acquire)) return;
            request_osinfo(std::move(arguments), std::move(callback));
        });
}

void stop_timer(lv_timer_t *&timer)
{
    if (!timer) return;
    lv_timer_delete(timer);
    timer = nullptr;
}

} // namespace

struct LvSettingSystemInfoPage3::State
{
    SettingsSystemPageKind kind = SettingsSystemPageKind::Network;
    uint64_t generation = 1;
    bool request_pending = false;
    bool used_default_fallback = false;
    std::chrono::steady_clock::time_point request_deadline;
    lv_timer_t *request_timer = nullptr;
    settings_system::NetworkInfo network{"--", "--", "--"};
    settings_system::AccountInfo account{"--", "--"};
    SettingsAboutOsInfo os_info;
    std::string status;
    lv_obj_t *title = nullptr;
    lv_obj_t *line_one = nullptr;
    lv_obj_t *line_two = nullptr;
    lv_obj_t *line_three = nullptr;
    lv_obj_t *status_label = nullptr;
    lv_obj_t *hint = nullptr;
};

namespace {

constexpr std::chrono::seconds kInfoRequestTimeout{15};

void render_system_info(LvSettingSystemInfoPage3 *page);

template <typename StateT>
void stop_system_request_timer(StateT *state)
{
    if (!state) return;
    stop_timer(state->request_timer);
}

void system_info_request_timeout_cb(lv_timer_t *timer);

bool arm_system_request_timer(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->state_) return false;
    auto *state = page->state_.get();
    stop_system_request_timer(state);
    state->request_deadline = std::chrono::steady_clock::now() + kInfoRequestTimeout;
    state->request_timer = lv_timer_create(system_info_request_timeout_cb, 250, page);
    return state->request_timer != nullptr;
}

void system_info_request_timeout_cb(lv_timer_t *timer)
{
    auto *page = timer ? static_cast<LvSettingSystemInfoPage3 *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!page || !page->state_ || timer != page->state_->request_timer) return;
    auto *state = page->state_.get();
    if (!state->request_pending) {
        stop_system_request_timer(state);
        return;
    }
    if (std::chrono::steady_clock::now() < state->request_deadline) return;

    stop_system_request_timer(state);
    state->request_pending = false;
    ++state->generation;
    if (state->generation == 0) state->generation = 1;
    page->advance_async_generation();
    state->status = settings_system::backend_error_label(-ETIMEDOUT);
    render_system_info(page);
}

void render_system_info(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->Get()) return;
    auto *state = page->state_.get();
    if (!state) return;

    switch (state->kind) {
    case SettingsSystemPageKind::Network:
    case SettingsSystemPageKind::Ethernet:
        set_label(state->title, "Ethernet Info");
        set_label(state->line_one, state->network.ip);
        set_label(state->line_two, state->network.gateway);
        set_label(state->line_three, state->network.mac);
        break;
    case SettingsSystemPageKind::Account:
        set_label(state->title, "Account");
        set_label(state->line_one, "Username: " + state->account.username);
        set_label(state->line_two, "Password: " + settings_system::password_unsupported_label());
        set_label(state->line_three, "Hostname: " + state->account.hostname);
        break;
    case SettingsSystemPageKind::Password:
        set_label(state->title, "Account password");
        set_label(state->line_one, settings_system::password_unsupported_label());
        set_label(state->line_two, "The backend exposes read-only account information.");
        set_label(state->line_three, "No password is changed or stored by Settings.");
        break;
    case SettingsSystemPageKind::OS:
        set_label(state->title, "OS");
        set_label(state->line_one, "Build date: " + state->os_info.build_date);
        set_label(state->line_two, "Commit: " + state->os_info.commit);
        set_label(state->line_three, "");
        break;
    case SettingsSystemPageKind::Version:
        set_label(state->title, "Version");
        set_label(state->line_one, settings_system::version_label(launcher_version()));
        set_label(state->line_two, settings_system::build_label(
                                      launcher_build_date(), launcher_channel(), launcher_commit()));
        set_label(state->line_three, std::string("Commit: ") + launcher_commit());
        break;
    case SettingsSystemPageKind::Build:
        set_label(state->title, "Build");
        set_label(state->line_one, settings_system::build_label(
                                      launcher_build_date(), launcher_channel(), launcher_commit()));
        set_label(state->line_two, std::string("Build date: ") + launcher_build_date());
        set_label(state->line_three, std::string("Commit: ") + launcher_commit());
        break;
    case SettingsSystemPageKind::Auto:
        break;
    }

    // Keep machine-readable values (addresses, versions and commits) in a
    // fixed-width face while account and explanatory text retain CJK coverage.
    const bool network_table = uses_network_table(state->kind);
    const bool machine_values = network_table ||
                                state->kind == SettingsSystemPageKind::OS ||
                                state->kind == SettingsSystemPageKind::Version ||
                                state->kind == SettingsSystemPageKind::Build;
    const lv_font_t *line_font = network_table
        ? settings_fonts::mono(14)
        : machine_values ? settings_fonts::mono(11) : settings_fonts::cjk_sans(12);
    for (lv_obj_t *line : {state->line_one, state->line_two, state->line_three}) {
        if (!line) continue;
        lv_obj_set_style_text_font(line, line_font, LV_PART_MAIN);
        // Hashes and other machine values can be wider than the fixed text box.
        // Scroll them in place so the complete value remains readable.
        lv_label_set_long_mode(line, machine_values ? LV_LABEL_LONG_SCROLL_CIRCULAR
                                                    : LV_LABEL_LONG_CLIP);
    }

    set_label(state->status_label, state->status);
    if (state->hint) {
        const char *hint = state->request_pending ? "Loading..." : "ENTER: refresh   ESC: back";
        lv_label_set_text(state->hint, hint);
    }
}

void set_system_error(LvSettingSystemInfoPage3 *page, int code, const std::string &payload)
{
    if (!page || !page->state_) return;
    if (code == 0)
        page->state_->status = "Invalid system information payload";
    else
        page->state_->status = settings_system::backend_error_label(code, payload);
    render_system_info(page);
}

void request_network(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->state_ || page->state_->request_pending) return;
    auto *state = page->state_.get();
    page->advance_async_generation();
    ++state->generation;
    if (state->generation == 0) state->generation = 1;
    const uint64_t request_generation = state->generation;
    state->request_pending = true;
    state->status = "Loading Ethernet information...";
    state->used_default_fallback = false;
    if (!arm_system_request_timer(page)) {
        state->request_pending = false;
        state->status = "Unable to create Ethernet timeout timer";
        render_system_info(page);
        return;
    }
    render_system_info(page);

    const bool use_default = state->kind == SettingsSystemPageKind::Network;
    const std::string command = use_default ? "NetworkDefaultInfoRead" : "EthInfoRead";
    if (!start_osinfo_request(
            page,
            {command},
            [page, request_generation, use_default](int code, std::string payload) {
                auto *state = page ? page->state_.get() : nullptr;
                if (!state || request_generation != state->generation || !page->Get()) return;
                stop_system_request_timer(state);
                state->request_pending = false;
                settings_system::NetworkInfo candidate;
                if (code == 0 && settings_system::parse_network_info_strict(payload, candidate)) {
                    state->network = std::move(candidate);
                    state->status = "Ethernet information loaded";
                    render_system_info(page);
                    return;
                }

                if (!use_default && !state->used_default_fallback) {
                    state->used_default_fallback = true;
                    state->request_pending = true;
                    state->status = "Trying default network information...";
                    page->advance_async_generation();
                    const uint64_t fallback_generation = ++state->generation;
                    if (!arm_system_request_timer(page)) {
                        state->request_pending = false;
                        set_system_error(page, -1, "Unable to create Ethernet timeout timer");
                        return;
                    }
                    render_system_info(page);
                    if (start_osinfo_request(
                            page,
                            {"NetworkDefaultInfoRead"},
                            [page, fallback_generation](int fallback_code,
                                                         std::string fallback_payload) {
                                auto *fallback_state = page ? page->state_.get() : nullptr;
                                if (!fallback_state || fallback_generation != fallback_state->generation ||
                                    !page->Get())
                                    return;
                                stop_system_request_timer(fallback_state);
                                fallback_state->request_pending = false;
                                settings_system::NetworkInfo fallback_info;
                                if (fallback_code == 0 &&
                                    settings_system::parse_network_info_strict(fallback_payload, fallback_info)) {
                                    fallback_state->network = std::move(fallback_info);
                                    fallback_state->status = "Ethernet information loaded";
                                    render_system_info(page);
                                } else {
                                    set_system_error(page, fallback_code, fallback_payload);
                                }
                            }))
                        return;
                    state->request_pending = false;
                    stop_system_request_timer(state);
                }
                set_system_error(page, code, payload);
            })) {
        state->request_pending = false;
        stop_system_request_timer(state);
        state->status = "Unable to schedule Ethernet request";
        render_system_info(page);
    }
}

void request_account(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->state_ || page->state_->request_pending) return;
    auto *state = page->state_.get();
    page->advance_async_generation();
    ++state->generation;
    if (state->generation == 0) state->generation = 1;
    const uint64_t request_generation = state->generation;
    state->request_pending = true;
    state->status = "Loading account information...";
    if (!arm_system_request_timer(page)) {
        state->request_pending = false;
        state->status = "Unable to create account timeout timer";
        render_system_info(page);
        return;
    }
    render_system_info(page);

    if (!start_osinfo_request(
            page,
            {"AccountInfoRead"},
            [page, request_generation](int code, std::string payload) {
                auto *state = page ? page->state_.get() : nullptr;
                if (!state || request_generation != state->generation || !page->Get()) return;
                stop_system_request_timer(state);
                state->request_pending = false;
                settings_system::AccountInfo candidate;
                if (code == 0 && settings_system::parse_account_info_strict(payload, candidate)) {
                    state->account = std::move(candidate);
                    state->status = "Account information loaded";
                    render_system_info(page);
                } else {
                    set_system_error(page, code, payload);
                }
            })) {
        state->request_pending = false;
        stop_system_request_timer(state);
        state->status = "Unable to schedule account request";
        render_system_info(page);
    }
}

void refresh_system_info(LvSettingSystemInfoPage3 *page)
{
    if (!page || !page->state_) return;
    switch (page->state_->kind) {
    case SettingsSystemPageKind::Network:
    case SettingsSystemPageKind::Ethernet: request_network(page); break;
    case SettingsSystemPageKind::Account: request_account(page); break;
    case SettingsSystemPageKind::Password:
        page->state_->status = "Read-only account information";
        render_system_info(page);
        break;
    case SettingsSystemPageKind::OS:
        page->state_->os_info = SettingsAboutInfoModel::read_os_issue_file();
        if (page->state_->os_info.build_date == "unknown" &&
            page->state_->os_info.commit == "unknown")
            page->state_->status = "OS build information unavailable";
        else if (page->state_->os_info.build_date == "unknown" ||
                 page->state_->os_info.commit == "unknown")
            page->state_->status = "OS build information is incomplete";
        else
            page->state_->status = "OS build information loaded";
        render_system_info(page);
        break;
    case SettingsSystemPageKind::Version:
    case SettingsSystemPageKind::Build:
        page->state_->status = "Build information loaded";
        render_system_info(page);
        break;
    case SettingsSystemPageKind::Auto: break;
    }
}

void system_info_key_event(LvSettingSystemInfoPage3 *page, lv_event_t *event)
{
    if (!page || !event || lv_event_get_code(event) != LV_EVENT_KEY) return;
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (page->state_) {
            ++page->state_->generation;
            page->state_->request_pending = false;
            stop_system_request_timer(page->state_.get());
            page->advance_async_generation();
        }
        if (page->LeaveSelfPage) page->LeaveSelfPage();
    } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
        refresh_system_info(page);
    }
    lv_event_stop_processing(event);
}

} // namespace

LvSettingSystemInfoPage3::LvSettingSystemInfoPage3(lv_obj_t *parent,
                                                   const NodeIter &page_node,
                                                   std::function<void()> back_callback,
                                                   SettingsSystemPageKind kind)
    : state_(std::make_unique<State>())
{
    state_->kind = resolve_kind(page_node, kind);
    LeaveSelfPage = std::move(back_callback);
    create_ui(parent);
}

LvSettingSystemInfoPage3::~LvSettingSystemInfoPage3()
{
    if (state_) {
        ++state_->generation;
        state_->request_pending = false;
        stop_system_request_timer(state_.get());
    }
    cancel_async_tasks();
    if (ComponensObj) {
        lv_obj_delete(ComponensObj);
        ComponensObj = nullptr;
    }
    state_.reset();
}

void LvSettingSystemInfoPage3::AnimateNextIn(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingSystemInfoPage3::AnimateNextOut(std::function<void()> animate_over_func)
{
    if (animate_over_func) animate_over_func();
}

void LvSettingSystemInfoPage3::LoadNextPage()
{
}

void LvSettingSystemInfoPage3::LeaveNextPage()
{
    if (state_) {
        ++state_->generation;
        if (state_->generation == 0) state_->generation = 1;
        state_->request_pending = false;
        stop_system_request_timer(state_.get());
        advance_async_generation();
    }
    if (LeaveSelfPage) LeaveSelfPage();
}

void LvSettingSystemInfoPage3::create_ui(lv_obj_t *parent)
{
    if (!parent || !state_) return;
    using LayoutMetric = LvSettingSystemInfoPage3::LayoutMetric;
    ComponensObj = lv_obj_create(parent);
    if (!ComponensObj) return;
    configure_page(ComponensObj,
                   LvSettingSystemInfoPage3::metric(LvSettingSystemInfoPage3::LayoutMetric::ScreenW),
                   LvSettingSystemInfoPage3::metric(LvSettingSystemInfoPage3::LayoutMetric::ScreenH));
    DComponens::lvgl_bind_event(
        ComponensObj,
        LV_EVENT_KEY,
        nullptr,
        std::bind(&system_info_key_event, this, std::placeholders::_1));

    const bool network_table = uses_network_table(state_->kind);
    state_->title = create_label(
        ComponensObj,
        LvSettingSystemInfoPage3::metric(LayoutMetric::TextX),
        LvSettingSystemInfoPage3::metric(LayoutMetric::TitleY),
        LvSettingSystemInfoPage3::metric(LayoutMetric::TextW),
        "System",
        0x58A6FF,
        network_table ? settings_fonts::sans(16, LV_FREETYPE_FONT_STYLE_BOLD) : title_font());
    if (network_table) {
        state_->line_one = create_network_table_row(ComponensObj, 0, "IP");
        state_->line_two = create_network_table_row(ComponensObj, 1, "Gateway");
        state_->line_three = create_network_table_row(ComponensObj, 2, "MAC");
    } else {
        state_->line_one = create_label(ComponensObj,
                                        LvSettingSystemInfoPage3::metric(LayoutMetric::TextX),
                                        LvSettingSystemInfoPage3::metric(LayoutMetric::LineOneY),
                                        LvSettingSystemInfoPage3::metric(LayoutMetric::TextW),
                                        "", 0xECECEC, body_font());
        state_->line_two = create_label(ComponensObj,
                                        LvSettingSystemInfoPage3::metric(LayoutMetric::TextX),
                                        LvSettingSystemInfoPage3::metric(LayoutMetric::LineTwoY),
                                        LvSettingSystemInfoPage3::metric(LayoutMetric::TextW),
                                        "", 0xCCCCCC, body_font());
        state_->line_three = create_label(ComponensObj,
                                          LvSettingSystemInfoPage3::metric(LayoutMetric::TextX),
                                          LvSettingSystemInfoPage3::metric(LayoutMetric::LineThreeY),
                                          LvSettingSystemInfoPage3::metric(LayoutMetric::TextW),
                                          "", 0xAAAAAA, body_font());
    }
    state_->status_label = create_label(ComponensObj,
                                        LvSettingSystemInfoPage3::metric(LayoutMetric::TextX),
                                        network_table
                                            ? LvSettingSystemInfoPage3::metric(LayoutMetric::TableStatusY)
                                            : LvSettingSystemInfoPage3::metric(LayoutMetric::StatusY),
                                        LvSettingSystemInfoPage3::metric(LayoutMetric::TextW),
                                        "", 0xF0C850,
                                        network_table ? settings_fonts::sans(11) : body_font(),
                                        !network_table);
    state_->hint = create_label(ComponensObj,
                                LvSettingSystemInfoPage3::metric(LayoutMetric::TextX),
                                LvSettingSystemInfoPage3::metric(LayoutMetric::HintY),
                                LvSettingSystemInfoPage3::metric(LayoutMetric::TextW),
                                "", 0x46DC87,
                                network_table ? settings_fonts::sans(10, LV_FREETYPE_FONT_STYLE_BOLD)
                                              : hint_font());
    if (network_table) {
        if (state_->status_label) {
            lv_obj_set_height(
                state_->status_label,
                LvSettingSystemInfoPage3::metric(LayoutMetric::TableStatusH));
            lv_label_set_long_mode(state_->status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        }
        if (state_->hint) {
            lv_obj_set_height(
                state_->hint,
                LvSettingSystemInfoPage3::metric(LayoutMetric::TableHintH));
            lv_label_set_long_mode(state_->hint, LV_LABEL_LONG_CLIP);
        }
    }
    refresh_system_info(this);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_system_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingSystemInfoPage3>(
        parent, page_node, std::move(back_callback), SettingsSystemPageKind::Auto);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_ethernet_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingSystemInfoPage3>(
        parent, page_node, std::move(back_callback), SettingsSystemPageKind::Ethernet);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_account_page_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return std::make_unique<LvSettingSystemInfoPage3>(
        parent, page_node, std::move(back_callback), SettingsSystemPageKind::Account);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_system_info_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return settings_system_page_factory(parent, page_node, std::move(back_callback));
}

std::unique_ptr<DComponens::LvglComponensBase> settings_ethernet_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return settings_ethernet_page_factory(parent, page_node, std::move(back_callback));
}

std::unique_ptr<DComponens::LvglComponensBase> settings_account_page3_factory(
    lv_obj_t *parent,
    const NodeIter &page_node,
    std::function<void()> back_callback)
{
    return settings_account_page_factory(parent, page_node, std::move(back_callback));
}
