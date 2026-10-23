/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

namespace cp0::network {

struct AccessPoint
{
    std::string ssid;
    int signal = 0;
    std::string security;
    bool in_use = false;
};

struct Status
{
    bool connected = false;
    bool ethernet = false;
    std::string wifi_interface;
    std::string ssid;
    std::string ipv4;
    int signal = 0;
};

std::vector<std::string> split_terse_fields(const std::string &line);
std::vector<AccessPoint> parse_scan_output(const std::string &output);
Status parse_device_status(const std::string &output);
void apply_active_wifi(const std::string &output, Status &status);
std::string parse_ipv4_address(const std::string &output);
bool parse_radio_enabled(const std::string &output);
std::string first_active_connection(const std::string &output);
// The Wi-Fi interface by its kernel name (wlan0, wlp2s0, wlx...): the /sys/class/net entries that have a wireless
// directory, an up one first; "" when there is none. sysfs_root prefixes the path (host tests); "" = the real one.
std::string find_wifi_interface(const std::string &sysfs_root = std::string());

} // namespace cp0::network
