/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launch.h"

#include "app_launch_env.hpp"
#include "app_registry.h"
#include "builtin_app_registry.hpp"
#include "desktop_app_loader.hpp"
#include "esc_hold_hint_controller.h"
#include "launcher_zone.hpp"
#include "native_ui.hpp"
#include "multitask/display_handoff.hpp"
#include "multitask/multitask_ui.hpp"
#include "cp0_display.h"
#include "ui.h"
#include "generated/page_app.h"
#include "ui_launch_page.h"
#include "ui_screensaver.h"
#include "ui_loading.h"
#include "cp0_lvgl_app.h"
#include "sample_log.h"

#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
constexpr size_t kHomeCarouselSlotCount = 5;
constexpr int kHomeCarouselCenterSlot = 2;
/* The directory watcher only sees the applications dir; an icon file replaced elsewhere (app upgrade, manual copy)
 * is noticed by comparing mtime + size of the listed icons on this period. */
constexpr uint32_t kIconPollPeriodMs = 3000;
}

// ============================================================
// Launch
// ============================================================
void Launch::bind_ui()
{
    if (bound_) {
        refresh_home_carousel();
        return;
    }
    bound_ = true;

    launcher_app_registry_set_changed_callback(app_registry_changed_cb, this);
    rebuild_builtin_apps();
    applications_load();
    reload_home_icons();

    app_directory_watcher_.start([this] { applications_reload(); });
    if (!icon_poll_timer_) icon_poll_timer_ = lv_timer_create(icon_poll_cb, kIconPollPeriodMs, this);
    esc_ui_watchdog_.start();
    esc_hold_hint_controller().set_force_home_callback(esc_force_home_cb, this);
    // Multitasking P-b (task 015): only with multitasking on; otherwise nothing of it exists
    if (multitask_ui::enabled()) {
        multitask_ui::Hooks hooks;
        hooks.app_front = [this] { mt_app_front(); };
        hooks.launcher_front = [this](bool after_exit) { mt_launcher_front(after_exit); };
        hooks.show_home = [this] { show_home(); };
        multitask_ui::start(std::move(hooks));
    }
}

void Launch::launch_index(std::size_t index)
{
    const int normalized = normalized_app_index(static_cast<int>(index));
    if (normalized < 0) return;
    current_app = normalized;
    launch_app();
}

void Launch::show_home()
{
    if (native_ui::enabled()) {
        native_ui::show_home();
        return;
    }
    if (auto page = launch_page_.lock()) page->show_home_screen();
}

void Launch::launch_app()
{
    const app *selected = app_at_index(current_app);
    if (!selected) return;

    native_ui::set_launching_app(selected->Name); // touch behaviour follows Settings > Touch
    try {
        selected->launch(this);
    } catch (const std::exception &error) {
        SLOGE("Failed to launch app %s: %s", selected->Name.c_str(), error.what());
        abort_page_launch();
    } catch (...) {
        SLOGE("Failed to launch app %s: unknown exception", selected->Name.c_str());
        abort_page_launch();
    }
}

void Launch::lv_go_back_home(void *arg) noexcept
{
    auto *self = static_cast<Launch *>(arg);
    if (!self || !self->page_lifecycle_.complete_home()) return;
    try {
        esc_hold_hint_controller().set_return_home_enabled(false);
        SLOGI("[HOME] lv_go_back_home executing (page=%p)", self->app_Page.get());
        lv_timer_enable(true);
        self->show_home();
        lv_refr_now(nullptr);
        self->app_Page.reset();
        self->esc_ui_watchdog_.disarm();
        SLOGI("[HOME] lv_go_back_home done, on launcher home");
    } catch (...) {
        self->app_Page.reset();
        self->esc_ui_watchdog_.disarm();
    }
}

void Launch::go_back_home()
{
    if (!page_lifecycle_.request_home()) return;
    esc_hold_hint_controller().set_return_home_enabled(false);
    SLOGI("[HOME] go_back_home() requested, scheduling async call (page=%p)", app_Page.get());
    if (lv_async_call(lv_go_back_home, this) != LV_RESULT_OK) {
        page_lifecycle_.cancel_home_request();
        esc_hold_hint_controller().set_return_home_enabled(true);
    }
}

bool Launch::begin_page_launch()
{
    if (!page_lifecycle_.begin_app()) return false;
    esc_hold_hint_controller().set_return_home_enabled(true);
    esc_ui_watchdog_.arm();
    return true;
}

void Launch::abort_page_launch() noexcept
{
    try {
        esc_ui_watchdog_.disarm();
    } catch (...) {
        SLOGW("[HOME] failed to disarm launch watchdog during rollback");
    }
    if (!page_lifecycle_.abort_app()) return;

    try {
        esc_hold_hint_controller().set_return_home_enabled(false);
        show_home();
        app_Page.reset();
    } catch (...) {
        app_Page.reset();
    }

    try {
        ui_loading::hide();
        lv_refr_now(nullptr);
    } catch (...) {
    }
}

void Launch::launch_Exec_in_terminal(const std::string &exec, bool sysplause,
                                     TerminalHelpFactory help_factory)
{
    if (!begin_page_launch()) return;
    native_ui::begin_page(true); // the terminal fills the whole panel
    SLOGI("Launching terminal app: %s", exec.c_str());
    ui_loading::show("Loading...");
    lv_refr_now(nullptr);
    auto p = std::make_shared<UISTPage>(help_factory);
    if (!p->screen())
        throw std::runtime_error("terminal page creation failed");
    app_Page = p;
    p->navigate_home = std::bind(&Launch::go_back_home, this);
    p->terminal_sysplause = sysplause;
    ui_loading::hide();
    cp0_lvgl_start_app_page(*p);
    p->exec(exec);
}

void Launch::launch_Exec(const std::string &exec, bool keep_root, bool fullscreen, const std::string &name,
                         multitask::Background background, bool turned)
{
    // Pi port: run the stock (320x170 framebuffer) app in the scaled compat window. The UI stays
    // alive while it runs; this returns immediately and the callback brings the home grid back.
    if (native_ui::enabled() && !fullscreen) {
        ui_screensaver_set_foreground(0);
        const bool started = native_ui::run_external(exec, keep_root, [this] {
            show_home();
            ui_screensaver_set_foreground(1);
        });
        if (!started) {
            ui_screensaver_set_foreground(1);
            show_home();
        }
        return;
    }
    // Multitasking P-b (multitasking on): a full-screen app runs under the supervisor and this returns at once
    if (fullscreen && multitask_ui::active()) {
        launch_fullscreen_supervised(exec, keep_root, name, background, turned);
        return;
    }
    // external apps own the framebuffer; keep the compat chrome (a full-screen app on the Pi port
    // simply takes over the whole panel: nothing of ours draws while it runs)
    if (!fullscreen) native_ui::begin_page(false);
    SLOGI("Launching external app: %s (keep_root=%d)", exec.c_str(), keep_root);
    ui_loading::show("Loading...");
    lv_disp_t *disp = lv_disp_get_default();
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    ui_screensaver_set_foreground(0);
    LVGL_RUN_FLAGE = 0;
    if (indev) lv_indev_set_group(indev, nullptr);
    lv_refr_now(disp);
    // External apps own the framebuffer exclusively. Keep process supervision
    // active in the backend, but stop every Launcher timer/flush until the
    // child exits; drawing an LVGL toast here would overwrite the child UI.
    lv_timer_enable(false);
    // The app contract (cp0_app_contract.h): a fullscreen app learns its mode, the whole logical screen as window and
    // canvas, and where the state files are. Without layout metrics (stock build, SDL) the command is unchanged.
    // Turned (a landscape app on a portrait launcher, report 038 S2): the panel's landscape canvas and its rotation.
    if (turned) SLOGI("Launching %s turned: the panel's landscape canvas", exec.c_str());
    const std::string command =
        fullscreen ? app_launch_env::export_for_current(app_launch_env::Mode::Fullscreen, turned) + exec : exec;
    if (fullscreen) launcher_zone::set_app_front(true); // the app's own keyboard rule while it is in front (vkb)
    int result = -1;
    try {
        cp0_signal_process_api(
            {"ExecBlocking", command, keep_root ? "1" : "0"},
            [&](int code, std::string) { result = code; });
    } catch (...) {
        result = -1;
    }
    SLOGI("External app exited with code %d", result);
    // the launcher's description of the panel comes back if the app replaced it (report 023 F1)
    (void)cp0_ui_state_republish();
    if (fullscreen) {
        launcher_zone::set_app_front(false);
        launcher_zone::repaint(); // the app drew over the writing zone's band too
    }

    lv_timer_enable(true);
    if (indev) lv_indev_set_group(indev, UILaunchPage::home_input_group());
    // a full-screen app is closed with a touch (its X button): that touch must not click anything here
    if (fullscreen && native_ui::enabled()) cp0_display_set_touch_swallow(2);
    show_home();
    ui_loading::hide();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    LVGL_RUN_FLAGE = 1;
    ui_screensaver_set_foreground(1);
}

void Launch::select_next_app()
{
    int next = normalized_app_index(current_app + 1);
    if (next >= 0) current_app = next;
}

void Launch::select_previous_app()
{
    int previous = normalized_app_index(current_app - 1);
    if (previous >= 0) current_app = previous;
}

std::size_t Launch::app_count() const
{
    return app_list.size();
}

std::size_t Launch::current_app_index() const
{
    const int normalized = normalized_app_index(current_app);
    return normalized < 0 ? 0 : static_cast<std::size_t>(normalized);
}

const app *Launch::carousel_slot_app(size_t slot) const
{
    if (slot >= kHomeCarouselSlotCount)
        return nullptr;
    return app_at_index(current_app + static_cast<int>(slot) - kHomeCarouselCenterSlot);
}

void Launch::applications_load()
{
    launcher_append_desktop_apps(app_list);
    launcher_sort_app_display_order(app_list);
}

void Launch::refresh_home_carousel()
{
    int normalized = normalized_app_index(current_app);
    if (normalized < 0) return;
    current_app = normalized;
    if (auto page = launch_page_.lock()) page->refresh_carousel();
    native_ui::refresh_apps();
}

void Launch::reload_home_icons()
{
    const int normalized = normalized_app_index(current_app);
    if (normalized >= 0) current_app = normalized;

    std::vector<std::string> icon_paths;
    icon_paths.reserve(app_list.size());
    for (const app &item : app_list)
        icon_paths.push_back(item.Icon);
    /* taken before the decode: a file replaced in between only costs one extra reload on the next poll */
    icon_signatures_.capture(icon_paths);
    if (auto page = launch_page_.lock()) page->reload_home_icons(icon_paths);
    native_ui::refresh_apps();
}

void Launch::applications_reload()
{
    rebuild_builtin_apps();
    applications_load();
    reload_home_icons();
}

int Launch::normalized_app_index(int index) const
{
    int size = static_cast<int>(app_list.size());
    if (size == 0)
        return -1;

    index %= size;
    return index < 0 ? index + size : index;
}

const app *Launch::app_at_index(int index) const
{
    int normalized = normalized_app_index(index);
    if (normalized < 0)
        return nullptr;

    return &*std::next(app_list.begin(), normalized);
}

void Launch::esc_force_home_cb(void *user_data) noexcept
{
    auto *self = static_cast<Launch *>(user_data);
    if (!self || !self->app_Page || LVGL_RUN_FLAGE == 0) return;
    try {
        SLOGW("[HOME] ESC held for 3000 ms, forcing built-in page home");
        self->go_back_home();
    } catch (...) {
    }
}


void Launch::icon_poll_cb(lv_timer_t *timer) noexcept
{
    auto *self = static_cast<Launch *>(lv_timer_get_user_data(timer));
    if (!self || self->app_Page) return;
    try {
        if (self->icon_signatures_.changed()) {
            SLOGI("[HOME] an icon file changed, reloading the home icons");
            self->reload_home_icons();
        }
    } catch (...) {
    }
}

Launch::~Launch()
{
    multitask_ui::stop();
    if (icon_poll_timer_) {
        lv_timer_delete(icon_poll_timer_);
        icon_poll_timer_ = nullptr;
    }
    launcher_app_registry_clear_changed_callback(app_registry_changed_cb, this);
    esc_hold_hint_controller().set_force_home_callback(nullptr, nullptr);
    esc_hold_hint_controller().set_return_home_enabled(false);
    esc_ui_watchdog_.shutdown();
    app_directory_watcher_.stop();
    page_lifecycle_.stop();
    lv_async_call_cancel(lv_go_back_home, this);
}

Launch::Launch() = default;

void Launch::set_launch_page(std::shared_ptr<UILaunchPage> launch_page)
{
    launch_page_ = std::move(launch_page);
}

void Launch::rebuild_builtin_apps()
{
    app_list.clear();

    launcher_append_enabled_builtin_apps(app_list);

    fixed_count = app_list.size();
    current_app = normalized_app_index(current_app);
}

void Launch::app_registry_changed_cb(void *user_data) noexcept
{
    auto *self = static_cast<Launch *>(user_data);
    if (!self) return;
    try {
        self->applications_reload();
    } catch (...) {
    }
}

// ============================================================
// Multitasking P-b (task 015): the non-blocking fullscreen path
// ============================================================

// The legacy path above, up to the hand-over, without blocking: the loading overlay is painted, then the supervisor
// suspends the display (no launcher pixel after that), starts the app (or resumes it: one instance per Exec) and
// this returns. mt_app_front() / mt_launcher_front() run on this thread when the panel changes hands.
void Launch::launch_fullscreen_supervised(const std::string &exec, bool keep_root, const std::string &name,
                                          multitask::Background background, bool turned)
{
    SLOGI("Launching external app under the supervisor: %s (keep_root=%d, X-Background=%s%s)", exec.c_str(),
          keep_root, multitask::background_name(background), turned ? ", turned" : "");
    const bool resume = multitask_ui::is_resident(exec);
    if (!resume) ui_loading::show("Loading..."); // a resumed app gets its own last frame back at once
    lv_disp_t *disp = lv_disp_get_default();
    ui_screensaver_set_foreground(0);
    LVGL_RUN_FLAGE = 0;
    mt_release_input_group();
    lv_refr_now(disp);
    // The app contract (cp0_app_contract.h) as in the legacy path (turned included); the lifecycle variables, when the
    // app declares X-Background, are added by the supervisor.
    const std::string command = app_launch_env::export_for_current(app_launch_env::Mode::Fullscreen, turned) + exec;
    multitask_ui::launch(name.empty() ? exec : name, exec, command, keep_root, background);
}

void Launch::mt_release_input_group()
{
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    if (!indev) return;
    if (lv_group_t *group = lv_indev_get_group(indev)) mt_saved_group_ = group;
    lv_indev_set_group(indev, nullptr);
}

// An app takes the panel (a launch, a resume from Alt+Tab): the launcher stops drawing and goes quiet.
void Launch::mt_app_front()
{
    launcher_zone::set_app_front(true); // the app's own keyboard rule while it is in front (vkb, report 041)
    ui_screensaver_set_foreground(0);
    LVGL_RUN_FLAGE = 0;
    mt_release_input_group();
    display_handoff::enter_app_front(multitask_ui::bridge_timer());
}

// The launcher takes the panel back (the app in front exited, or was hidden): the legacy path's return sequence,
// with the display handed back instead of the timers re-enabled. A built-in page that was open (Alt+Tab from a page)
// comes back as it was; otherwise the home grid. P-d: the switcher screen or the legacy sheet, when the supervisor
// sent one with this hand-over, is already the active screen and stays (the lock screen waits while the sheet is up:
// only the sheet's box is drawn over the frozen app's frame).
void Launch::mt_launcher_front(bool after_exit)
{
    launcher_zone::set_app_front(false);
    if (after_exit) (void)cp0_ui_state_republish();
    display_handoff::leave_app_front();
    lv_disp_t *disp = lv_disp_get_default();
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    lv_group_t *group = (app_Page && mt_saved_group_) ? mt_saved_group_ : UILaunchPage::home_input_group();
    if (indev) lv_indev_set_group(indev, group);
    mt_saved_group_ = nullptr;
    // the closing touch of the app must not click anything here
    cp0_display_set_touch_swallow(2);
    if (!app_Page && !multitask_ui::screen_up()) show_home();
    ui_loading::hide();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    LVGL_RUN_FLAGE = 1;
    ui_screensaver_set_foreground(multitask_ui::sheet_up() ? 0 : 1);
}
