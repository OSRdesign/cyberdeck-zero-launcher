/*
 * SPDX-License-Identifier: MIT
 *
 * The built-in "Quick controls" page: four big buttons, Shutdown, Reboot, Wi-Fi and Bluetooth, on the native display
 * (the responsive shell's page header, no geometry of its own: page_app/quick_controls_layout.hpp from the layout
 * tokens). The logic is model/quick_controls_model.hpp; the services are quick_controls_backend.hpp (the ones Settings
 * uses). No confirmation dialog: a tap or Enter acts at once (owner decision, 2026-11-23).
 */

#pragma once

#include "../launcher_ui_app_page.hpp"
#include "../model/quick_controls_model.hpp"
#include "quick_controls_layout.hpp"

#include <array>
#include <memory>

namespace cp0 {
namespace ui {
class Page;
}
} // namespace cp0

class UIQuickControlsPage : public AppPage
{
public:
    // Lays itself out on the native (full-panel) display.
    static constexpr bool kNativeDisplay = true;

    UIQuickControlsPage();
    ~UIQuickControlsPage();
    UIQuickControlsPage(const UIQuickControlsPage &) = delete;
    UIQuickControlsPage &operator=(const UIQuickControlsPage &) = delete;

private:
    struct Tile {
        UIQuickControlsPage *page = nullptr;
        quick_controls::Control control = quick_controls::Control::Shutdown;
        lv_obj_t *button = nullptr;
        lv_obj_t *icon = nullptr;
        lv_obj_t *name = nullptr;
        lv_obj_t *sub = nullptr;
        lv_obj_t *dot = nullptr;
        bool armed = false; // the press of this tap / Enter began on this button (a stray release never acts)
    };

    void build(lv_obj_t *parent);
    void refresh_view();
    void place_tile(Tile &tile, const std::string &text);
    void activate(Tile &tile);
    void move_focus(const Tile &from, quick_controls::Move move);
    void ensure_polling();

    static void button_cb(lv_event_t *event);
    static void screen_loaded_cb(lv_event_t *event);
    static void tab_cb(lv_event_t *event);
    static void poll_cb(lv_timer_t *timer);

    quick_controls::Controller controller_;
    quick_controls_layout::Layout layout_;
    std::unique_ptr<cp0::ui::Page> page_;
    std::array<Tile, quick_controls_layout::kButtons> tiles_{};
    lv_timer_t *timer_ = nullptr;
    bool strip_added_ = false;
};
