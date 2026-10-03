/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "scanner.hpp"
#include "ui_app_page.hpp"

#include <array>

// Lists the devices on the local network and scans the ports of the selected one.
// The launcher-facing class name follows the AppPage convention of the template app.
class UILanScanPage : public AppPage
{
public:
    UILanScanPage();
    ~UILanScanPage() override;

private:
    enum class View { Hosts, Ports };
    static constexpr int kRows = 6;

    void start_scan();
    void open_ports(bool full_range);
    void move_selection(int delta);
    void activate();
    void back();
    void refresh();
    void handle_key(lv_event_t *event);

    static void key_event_cb(lv_event_t *event);
    static void row_event_cb(lv_event_t *event);
    static void timer_cb(lv_timer_t *timer);

    lanscan::Scanner scanner_;
    lanscan::Network network_;
    bool have_network_ = false;
    View view_ = View::Hosts;
    int selected_ = 0;
    int offset_ = 0;
    int host_index_ = 0;   // host whose ports are shown
    std::vector<lanscan::Host> hosts_;
    std::vector<lanscan::PortResult> ports_;
    bool full_range_ = false;

    lv_obj_t *root_ = nullptr;
    lv_obj_t *header_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *footer_ = nullptr;
    std::array<lv_obj_t *, kRows> rows_{};
    std::array<lv_obj_t *, kRows> row_left_{};
    std::array<lv_obj_t *, kRows> row_right_{};
    lv_timer_t *timer_ = nullptr;
};
