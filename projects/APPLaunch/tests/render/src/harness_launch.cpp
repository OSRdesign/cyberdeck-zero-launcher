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
#include "page_app/ui_app_ssh.hpp"
#include "page_app/ui_app_st.hpp"
#include "settings/settings_page.hpp"

#include "harness_launch.hpp"
#include "app_registry.h"
#include "home_order_launcher.hpp"
#include "launch.h"
#include "model/home_order.hpp"
#include "native_ui.hpp"

#include <cctype>
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
    // the grid's index: a stored home order (scene `config home_order ...`) may have moved the tile
    if (s_launch) {
        for (std::size_t i = 0; i < s_launch->app_count(); ++i)
            if (const app *item = s_launch->app_at(i); item && item->Name == name) return static_cast<int>(i);
        return -1;
    }
    for (std::size_t i = 0; i < specs().size(); ++i)
        if (specs()[i].name == name) return static_cast<int>(i);
    return -1;
}

} // namespace harness

namespace {

/* The tile's id as on the board: a built-in's config key (the registry copy in harness_stubs.cpp), else the id the
 * desktop loader would give "<name>.desktop" (app_desktop_<name>_desktop_<fnv of the file name>). */
std::string tile_id(const std::string &name)
{
    std::size_t count = 0;
    const AppDescriptor *entries = launcher_app_registry_entries(&count);
    for (std::size_t i = 0; entries && i < count; ++i)
        if (entries[i].label && name == entries[i].label) return entries[i].config_key;
    const std::string file = name + ".desktop";
    std::string id = "app_desktop_";
    for (const unsigned char c : file) id.push_back(std::isalnum(c) ? static_cast<char>(c) : '_');
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_%08x", static_cast<unsigned>(home_order::key_of(file)));
    return id + suffix;
}

} // namespace

Launch::Launch()
{
    for (const auto &spec : specs()) {
        if (spec.kind == "settings")
            app_list.emplace_back(spec.name, spec.icon, page_v<UISettingTreePage>);
        else if (spec.kind == "calculator")
            app_list.emplace_back(spec.name, spec.icon, page_v<UICalculatorPage>);
        else if (spec.kind == "ssh") // the SSH form (the native form on the native display) and its terminal
            app_list.emplace_back(spec.name, spec.icon, page_v<UISSHPage>);
        else if (spec.kind == "cli") // the terminal page as the CLI tile opens it, on the fake PTY (task 014 P4b)
            app_list.emplace_back(spec.name, spec.icon, page_v<UISTPage>);
        else // a tile only: stock apps are shown with the harness placeholder (scene "stockapp"); the Exec line
             // "tile:NAME" names it for the multitasking scenes (dots, switcher icons); launch_Exec does nothing here
            app_list.emplace_back(spec.name, spec.icon, "tile:" + spec.name, false);
        app_list.back().Id = tile_id(spec.name);
    }
    // the scene's tiles are today's order; a stored user order (study 046) applies as in Launch::applications_load()
    launcher_home_order_apply(app_list);
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
    native_ui::cancel_move_mode(); // as launch.cpp (study 046 P2)
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

void Launch::launch_Exec(const std::string &, bool, bool, const std::string &, multitask::Background, bool) {}

void Launch::launch_Exec_in_terminal(const std::string &, bool, TerminalHelpFactory) {}
