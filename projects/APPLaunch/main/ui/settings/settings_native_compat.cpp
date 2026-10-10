/*
 * SPDX-License-Identifier: MIT
 *
 * The compat path of the native Settings host (see settings_native_compat.hpp).
 */

#include "settings_native_compat.hpp"

#include "lvgl_components.hpp"
#include "ui_app_page.hpp"

#include "../native_ui.hpp"

#include <utility>

/* The legacy pages' parent: an AppPage (20 px top bar "Settings" + 320x150 container), created on the compat
 * display (native_ui::begin_page(false, ...) made it the default one). Kept until the Settings page closes. */
class SettingsCompatPath::Window : public AppPage {
public:
    Window()
    {
        set_page_title("Settings");
        lv_obj_set_style_bg_color(screen(), lv_color_black(), LV_PART_MAIN);
    }
};

SettingsCompatPath::SettingsCompatPath(AppPageRoot &native_page, Post post, std::function<void()> on_closed)
    : native_page_(native_page), post_(std::move(post)), on_closed_(std::move(on_closed))
{
}

SettingsCompatPath::~SettingsCompatPath()
{
    if (page_ && page_->Get() && window_ && window_->input_group()) lv_group_remove_obj(page_->Get());
    page_.reset();
    window_.reset();
}

bool SettingsCompatPath::open(const NodeIter &node)
{
    if (page_ || !node->page_factory) return false;
    // what launch.h does for the legacy Settings: compat window, toolbar, touch rows as keys (row 21 px at y 96)
    native_ui::begin_page(false, true);
    if (!window_) window_ = std::make_unique<Window>();
    if (!window_->screen() || !window_->ui_APP_Container) {
        window_.reset();
        native_ui::begin_page(true, false);
        cp0_lvgl_start_app_page(native_page_);
        return false;
    }
    cp0_lvgl_start_app_page(*window_);
    closing_ = false;
    page_ = node->page_factory(window_->ui_APP_Container, node, [this] {
        if (closing_) return;
        closing_ = true;
        post_([this] { close(); });
    });
    if (!page_ || !page_->Get()) {
        page_.reset();
        close();
        return false;
    }
    lv_obj_set_x(page_->Get(), page_->panel_x()); /* its resting column, as the legacy submenu leaves it */
    if (lv_group_t *group = window_->input_group()) {
        lv_group_add_obj(group, page_->Get());
        lv_group_focus_obj(page_->Get());
    }
    return true;
}

void SettingsCompatPath::close()
{
    if (page_ && page_->Get() && window_ && window_->input_group()) lv_group_remove_obj(page_->Get());
    page_.reset();
    closing_ = false;
    native_ui::begin_page(true, false); /* native display, plain pointer touch */
    cp0_lvgl_start_app_page(native_page_);
    if (on_closed_) on_closed_();
}
