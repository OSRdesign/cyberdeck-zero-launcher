/* SPDX-License-Identifier: MIT
 *
 * Render harness: the launcher object (class Launch, launch.h) without launch.cpp.
 *
 * launch.cpp builds the app list from the built-in registry and the installed .desktop files,
 * watches the applications directory, arms the Esc watchdog and starts processes. The harness
 * needs none of that: a scene names the tiles, and launching a page app goes through the real
 * app::launch lambda (launch.h + launcher_app.cpp), i.e. native_ui::begin_page, ui_loading,
 * the page constructor and cp0_lvgl_start_app_page, exactly as on the deck.
 */
#define APP_PAGE_IMPLEMENTATION_UNIT /* as in the page sources: ui.h must not pull launch.h first */
#include "page_app/ui_app_calculator.hpp"
#include "settings/settings_page.hpp"

#include "harness_launch.hpp"
#include "launch.h"
#include "native_ui.hpp"

#include <cstdio>

namespace {

std::vector<harness::AppSpec> &specs()
{
    static std::vector<harness::AppSpec> list;
    return list;
}

Launch *s_launch = nullptr;

} // namespace

namespace harness {

void set_apps(const std::vector<AppSpec> &apps)
{
    specs() = apps;
    // A new Launch picks the list up; the old one is leaked on purpose (a scene process is short).
    s_launch = new Launch();
    native_ui::attach(s_launch);
}

Launch *launcher()
{
    return s_launch;
}

int app_index(const std::string &name)
{
    for (std::size_t i = 0; i < specs().size(); ++i)
        if (specs()[i].name == name) return static_cast<int>(i);
    return -1;
}

} // namespace harness

Launch::Launch()
{
    for (const auto &spec : specs()) {
        if (spec.kind == "settings")
            app_list.emplace_back(spec.name, spec.icon, page_v<UISettingTreePage>);
        else if (spec.kind == "calculator")
            app_list.emplace_back(spec.name, spec.icon, page_v<UICalculatorPage>);
        else // a tile only: stock apps are shown with the harness placeholder (scene "stockapp")
            app_list.emplace_back(spec.name, spec.icon, std::string(), false);
    }
}

std::size_t Launch::app_count() const
{
    return app_list.size();
}

const app *Launch::app_at_index(int index) const
{
    if (index < 0) return nullptr;
    int i = 0;
    for (const auto &item : app_list)
        if (i++ == index) return &item;
    return nullptr;
}

int Launch::normalized_app_index(int index) const
{
    const int count = static_cast<int>(app_list.size());
    return count == 0 ? -1 : ((index % count) + count) % count;
}

void Launch::launch_index(std::size_t index)
{
    const int normalized = normalized_app_index(static_cast<int>(index));
    if (normalized < 0) return;
    current_app = normalized;
    launch_app();
}

void Launch::launch_app()
{
    const app *selected = app_at_index(current_app);
    if (!selected) return;
    native_ui::set_launching_app(selected->Name);
    try {
        selected->launch(this);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[harness] launching %s failed: %s\n", selected->Name.c_str(), error.what());
    }
}

// Same transitions as launch.cpp without the Esc watchdog thread and the hold-Esc controller.
bool Launch::begin_page_launch()
{
    return page_lifecycle_.begin_app();
}

void Launch::lv_go_back_home(void *arg) noexcept
{
    auto *self = static_cast<Launch *>(arg);
    if (!self || !self->page_lifecycle_.complete_home()) return;
    self->show_home();
    lv_refr_now(nullptr);
    self->app_Page.reset();
}

void Launch::go_back_home()
{
    if (!page_lifecycle_.request_home()) return;
    if (lv_async_call(lv_go_back_home, this) != LV_RESULT_OK) page_lifecycle_.cancel_home_request();
}

void Launch::show_home()
{
    native_ui::show_home();
}

void Launch::launch_Exec(const std::string &, bool, bool) {}

void Launch::launch_Exec_in_terminal(const std::string &, bool, TerminalHelpFactory) {}
