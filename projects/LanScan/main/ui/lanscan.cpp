/*
 * SPDX-License-Identifier: MIT
 */

#include "lanscan.hpp"

#include "cp0_keyboard_navigation_contract.h"
#include "input_keys.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kBackground = 0x101214;
constexpr uint32_t kSelected = 0x2A2F35;
constexpr uint32_t kText = 0xF4F4F5;
constexpr uint32_t kMuted = 0x8A929B;
constexpr uint32_t kGold = 0xF0B400;
constexpr uint32_t kGreen = 0x33CC33;
constexpr uint32_t kBlue = 0x3B9DFF;

constexpr int kWidth = 320;
constexpr int kHeaderH = 18;
constexpr int kFooterH = 16;
constexpr int kRowH = 19;

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, "");
    return label;
}

} // namespace

UILanScanPage::UILanScanPage()
{
    set_page_title("LAN Scan");

    root_ = lv_obj_create(ui_APP_Container);
    lv_obj_remove_style_all(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(root_, kWidth, 150);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    header_ = make_label(root_, &lv_font_montserrat_14, kGold);
    lv_obj_set_pos(header_, 6, 1);
    lv_obj_set_size(header_, kWidth - 110, 16);   // fixed height: "dots" mode wraps when the height is automatic
    lv_label_set_long_mode(header_, LV_LABEL_LONG_MODE_DOTS);

    status_ = make_label(root_, &lv_font_montserrat_12, kGreen);
    lv_obj_set_width(status_, 100);
    lv_obj_set_pos(status_, kWidth - 106, 3);
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_RIGHT, 0);

    for (int i = 0; i < kRows; ++i) {
        rows_[i] = lv_obj_create(root_);
        lv_obj_remove_style_all(rows_[i]);
        lv_obj_clear_flag(rows_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(rows_[i], kWidth, kRowH);
        lv_obj_set_pos(rows_[i], 0, kHeaderH + i * kRowH);
        lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(rows_[i], lv_color_hex(kSelected), 0);
        lv_obj_set_user_data(rows_[i], reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(rows_[i], row_event_cb, LV_EVENT_CLICKED, this);

        row_left_[i] = make_label(rows_[i], &lv_font_montserrat_14, kText);
        lv_obj_set_pos(row_left_[i], 6, 1);
        row_right_[i] = make_label(rows_[i], &lv_font_montserrat_12, kMuted);
        lv_obj_set_size(row_right_[i], 194, 14);
        lv_label_set_long_mode(row_right_[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(row_right_[i], kWidth - 200, 3);
        lv_obj_set_style_text_align(row_right_[i], LV_TEXT_ALIGN_RIGHT, 0);
    }

    footer_ = make_label(root_, &lv_font_montserrat_12, kMuted);
    lv_obj_set_width(footer_, kWidth - 8);
    lv_obj_set_pos(footer_, 6, 150 - kFooterH + 1);

    lv_obj_add_event_cb(root_screen_, key_event_cb, LV_EVENT_KEY, this);
    timer_ = lv_timer_create(timer_cb, 250, this);
    start_scan();
    refresh();
}

UILanScanPage::~UILanScanPage()
{
    if (timer_) lv_timer_delete(timer_);
    scanner_.stop();
}

void UILanScanPage::start_scan()
{
    view_ = View::Hosts;
    selected_ = 0;
    offset_ = 0;
    have_network_ = lanscan::detect_network(network_);
    if (have_network_) scanner_.start_discovery(network_);
}

void UILanScanPage::open_ports(bool full_range)
{
    if (host_index_ < 0 || host_index_ >= static_cast<int>(hosts_.size())) return;
    full_range_ = full_range;
    view_ = View::Ports;
    selected_ = 0;
    offset_ = 0;
    scanner_.start_port_scan(hosts_[static_cast<size_t>(host_index_)].ip, full_range);
}

void UILanScanPage::move_selection(int delta)
{
    const int count = view_ == View::Hosts ? static_cast<int>(hosts_.size()) : static_cast<int>(ports_.size());
    if (count <= 0) return;
    selected_ = std::clamp(selected_ + delta, 0, count - 1);
    if (selected_ < offset_) offset_ = selected_;
    if (selected_ >= offset_ + kRows) offset_ = selected_ - kRows + 1;
    refresh();
}

void UILanScanPage::activate()
{
    if (view_ == View::Hosts) {
        if (hosts_.empty()) return;
        host_index_ = selected_;
        open_ports(false);
    }
}

void UILanScanPage::back()
{
    if (view_ == View::Ports) {
        scanner_.stop();
        view_ = View::Hosts;
        selected_ = std::clamp(host_index_, 0, std::max(0, static_cast<int>(hosts_.size()) - 1));
        offset_ = std::max(0, selected_ - kRows + 1);
        if (selected_ < offset_) offset_ = selected_;
        refresh();
    } else if (navigate_home) {
        navigate_home();
    }
}

void UILanScanPage::refresh()
{
    hosts_ = scanner_.hosts();
    ports_ = scanner_.ports();

    char text[96];
    const bool hosts_view = view_ == View::Hosts;
    if (hosts_view) {
        if (!have_network_) {
            std::snprintf(text, sizeof(text), "No network");
        } else {
            std::snprintf(text, sizeof(text), "%s  %s  [%zu]", network_.iface.c_str(), network_.ip.c_str(),
                          hosts_.size());
        }
        lv_label_set_text(header_, text);
        if (scanner_.discovery_running()) {
            std::snprintf(text, sizeof(text), "scanning %d%%", scanner_.discovery_progress());
            lv_label_set_text(status_, text);
        } else {
            lv_label_set_text(status_, have_network_ ? "done" : "");
        }
        lv_label_set_text(footer_, "Enter: ports   S: rescan   Esc: back");
    } else {
        const std::string target = scanner_.port_target();
        std::snprintf(text, sizeof(text), "%s  (%zu open)", target.c_str(), ports_.size());
        lv_label_set_text(header_, text);
        if (scanner_.port_scan_running()) {
            std::snprintf(text, sizeof(text), "scanning %d%%", scanner_.port_progress());
            lv_label_set_text(status_, text);
        } else {
            lv_label_set_text(status_, full_range_ ? "1-1024 done" : "done");
        }
        lv_label_set_text(footer_, "F: scan 1-1024   S: rescan   Esc: back");
    }

    const int count = hosts_view ? static_cast<int>(hosts_.size()) : static_cast<int>(ports_.size());
    if (selected_ >= count) selected_ = std::max(0, count - 1);

    for (int i = 0; i < kRows; ++i) {
        const int index = offset_ + i;
        if (index >= count) {
            lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(rows_[i], index == selected_ ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (hosts_view) {
            const lanscan::Host &h = hosts_[static_cast<size_t>(index)];
            lv_label_set_text(row_left_[i], h.ip.c_str());
            lv_obj_set_style_text_color(row_left_[i],
                                        lv_color_hex(h.is_self ? kGreen : (h.is_gateway ? kBlue : kText)), 0);
            std::string right = h.is_self ? "this device" : (h.is_gateway ? "gateway" : "");
            std::string detail = h.name;
            if (!h.vendor.empty()) detail += detail.empty() ? h.vendor : " (" + h.vendor + ")";
            if (!detail.empty()) right = right.empty() ? detail : right + "  " + detail;
            if (right.empty()) right = h.mac;
            lv_label_set_text(row_right_[i], right.c_str());
        } else {
            const lanscan::PortResult &p = ports_[static_cast<size_t>(index)];
            std::snprintf(text, sizeof(text), "%d/tcp", p.port);
            lv_label_set_text(row_left_[i], text);
            lv_obj_set_style_text_color(row_left_[i], lv_color_hex(kGreen), 0);
            lv_label_set_text(row_right_[i], p.service.c_str());
        }
    }
    if (count == 0) {
        lv_obj_remove_flag(rows_[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(rows_[0], LV_OPA_TRANSP, 0);
        const bool busy = hosts_view ? scanner_.discovery_running() : scanner_.port_scan_running();
        lv_label_set_text(row_left_[0], busy ? "Scanning..." : (hosts_view ? "No devices found" : "No open ports"));
        lv_obj_set_style_text_color(row_left_[0], lv_color_hex(kMuted), 0);
        lv_label_set_text(row_right_[0], "");
    }
}

void UILanScanPage::handle_key(lv_event_t *event)
{
    uint32_t key = lv_event_get_key(event);
    key = cp0_keyboard_navigation_alias(key);
    if (key == LV_KEY_UP || key == KEY_UP) move_selection(-1);
    else if (key == LV_KEY_DOWN || key == KEY_DOWN) move_selection(1);
    else if (key == LV_KEY_ENTER || key == KEY_ENTER) activate();
    else if (key == LV_KEY_ESC || key == KEY_ESC) back();
    else if (key == 's' || key == 'S' || key == KEY_S) {
        if (view_ == View::Hosts) start_scan();
        else open_ports(full_range_);
    } else if ((key == 'f' || key == 'F' || key == KEY_F) && view_ == View::Ports) {
        open_ports(true);
    }
    refresh();
}

void UILanScanPage::key_event_cb(lv_event_t *event)
{
    static_cast<UILanScanPage *>(lv_event_get_user_data(event))->handle_key(event);
}

void UILanScanPage::row_event_cb(lv_event_t *event)
{
    auto *self = static_cast<UILanScanPage *>(lv_event_get_user_data(event));
    const int row = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    const int count = self->view_ == View::Hosts ? static_cast<int>(self->hosts_.size())
                                                  : static_cast<int>(self->ports_.size());
    if (self->offset_ + row >= count) return;
    self->selected_ = self->offset_ + row;
    self->activate();
    self->refresh();
}

void UILanScanPage::timer_cb(lv_timer_t *timer)
{
    static_cast<UILanScanPage *>(lv_timer_get_user_data(timer))->refresh();
}
