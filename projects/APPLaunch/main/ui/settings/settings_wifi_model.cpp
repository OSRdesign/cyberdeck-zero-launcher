/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_wifi_model.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace settings_wifi {

int signal_percent(int signal)
{
    if (signal > 0) return std::min(signal, 100);
    if (signal == 0) return 0;
    return std::clamp(2 * (signal + 100), 0, 100);
}

int signal_bars(int signal)
{
    static constexpr int kThresholds[4] = {1, 30, 60, 80}; /* cp0_statusbar.c kBarThresholds */
    const int percent = signal_percent(signal);
    int bars = 0;
    for (int threshold : kThresholds)
        if (percent >= threshold) ++bars;
    return bars;
}

bool is_connected(const AccessPoint &ap, const Status &status)
{
    return ap.in_use || (status.connected && !status.ssid.empty() && status.ssid == ap.ssid);
}

void sort_networks(std::vector<AccessPoint> &networks, const Status &status)
{
    std::stable_sort(networks.begin(), networks.end(), [&status](const AccessPoint &a, const AccessPoint &b) {
        const bool a_on = is_connected(a, status);
        const bool b_on = is_connected(b, status);
        // connected first, saved next, then the strongest, then by name
        return std::make_tuple(!a_on, !a.saved, -signal_percent(a.signal), a.ssid) <
               std::make_tuple(!b_on, !b.saved, -signal_percent(b.signal), b.ssid);
    });
}

RowAction row_action(const AccessPoint &ap, const Status &status)
{
    if (is_connected(ap, status)) return RowAction::Actions;
    if (settings_wifi_com::is_open_security(ap.security) || ap.saved) return RowAction::Connect;
    return RowAction::AskPassword;
}

std::string password_error(const std::string &password, const std::string &security)
{
    if (password.empty()) return "Password required";

    std::string upper = security;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (upper.find("802.1X") != std::string::npos || upper.find("EAP") != std::string::npos ||
        upper.find("ENTERPRISE") != std::string::npos)
        return "Enterprise Wi-Fi is not supported";
    if (upper.find("WPA") == std::string::npos) return {};
    if (password.size() >= 8 && password.size() <= 63) return {};
    if (password.size() == 64 &&
        std::all_of(password.begin(), password.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; }))
        return {};
    return "WPA password: 8-63 chars or 64 hex";
}

std::string status_line(const Status &status, bool radio_on)
{
    if (!radio_on) return "Wi-Fi is off";
    if (status.connected && !status.ssid.empty())
        return status.ssid + "  " + (status.ip.empty() ? std::string("No IP") : status.ip);
    return "Not connected";
}

const char *connection_error_text(int result)
{
    switch (result) {
    case CP0_WIFI_ERROR_RADIO_OFF:
        return "Wi-Fi is off";
    case CP0_WIFI_ERROR_AUTH:
        return "Incorrect password";
    case CP0_WIFI_ERROR_NOT_FOUND:
        return "Network is no longer available";
    case CP0_WIFI_ERROR_IP_CONFIG:
        return "Connected, but IP setup failed";
    case CP0_WIFI_ERROR_TIMEOUT:
        return "Network operation timed out";
    case CP0_WIFI_ERROR_INVALID:
        return "Invalid Wi-Fi credentials";
    default:
        return "Wi-Fi service unavailable";
    }
}

const char *password_result_text(int result)
{
    return result == CP0_WIFI_ERROR_AUTH ? "Wrong password" : connection_error_text(result);
}

const char *scan_error_text(int result)
{
    switch (result) {
    case CP0_WIFI_ERROR_RADIO_OFF:
        return "Wi-Fi is off";
    case CP0_WIFI_ERROR_TIMEOUT:
        return "Wi-Fi scan timed out";
    case CP0_WIFI_ERROR_SERVICE:
        return "Wi-Fi service unavailable";
    default:
        return "Wi-Fi scan failed";
    }
}

} // namespace settings_wifi
