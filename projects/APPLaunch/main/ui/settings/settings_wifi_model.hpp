/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Wi-Fi model shared by the two Settings renderers (task 014 P3b): the legacy Networks / Join Hidden pages
// (settings_wifi_page.cpp) and the native Wi-Fi page (settings_native_wifi_page.cpp).
//
//   settings_wifi_com   the Wi-Fi service as both pages call it: the cp0_wifi_* C API (cp0_signal_wifi_api), with
//                       the credential checks. Moved unchanged from settings_wifi_page.cpp. Blocking: call it from a
//                       worker thread.
//   settings_wifi::     pure rules (no LVGL, no service call; tests/test_settings_wifi_model.cpp): the signal bars of
//                       a network (the status bar's thresholds), the list order (connected, saved, signal), what
//                       Enter on a network does, the password check, the status and error texts.
#pragma once

#include "cp0_lvgl_app.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

class settings_wifi_com {
private:
    enum class WifiDefaultDef : std::size_t {
        kMaxPasswordBytes = 64,
    };

public:
    struct AccessPoint {
        std::string ssid;
        std::string security;
        int signal  = 0;
        bool in_use = false;
        bool saved  = false;
    };

    struct Status {
        bool connected = false;
        std::string ssid;
        std::string ip;
        int signal    = 0;
        bool ethernet = false;
    };

    template <std::size_t Size>
    static std::string bounded_string(const char (&value)[Size])
    {
        std::size_t length = 0;
        while (length < Size && value[length] != '\0') ++length;
        return std::string(value, length);
    }

    static bool valid_text(const std::string &value, std::size_t max_bytes, bool allow_empty)
    {
        if (!allow_empty && value.empty()) return false;
        if (value.size() >= max_bytes) return false;
        return std::all_of(value.begin(), value.end(),
                           [](unsigned char byte) { return byte >= 0x20u && byte != 0x7Fu; });
    }

    static int validate_credentials(const std::string &ssid, const std::string &password)
    {
        if (!valid_text(ssid, CP0_WIFI_SSID_MAX, false) || !valid_text(password, static_cast<std::size_t>(WifiDefaultDef::kMaxPasswordBytes) + 1, true))
            return CP0_WIFI_ERROR_INVALID;
        return 0;
    }

    static bool is_open_security(const std::string &security)
    {
        if (security.empty()) return true;
        std::string normalized = security;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char byte) { return static_cast<char>(std::toupper(byte)); });
        return normalized == "OPEN" || normalized == "NONE" || normalized == "--";
    }

    static int radio_enabled()
    {
        return cp0_wifi_radio_enabled();
    }

    static int radio_set_enabled(bool enabled)
    {
        return cp0_wifi_radio_set_enabled(enabled ? 1 : 0);
    }

    static int read_status(Status &status)
    {
        status = {};
        cp0_wifi_status_t raw{};
        const int result = cp0_wifi_status_read(&raw);
        if (result != 0) return result;

        status.connected = raw.connected != 0;
        status.ssid      = bounded_string(raw.ssid);
        status.ip        = bounded_string(raw.ip);
        status.signal    = raw.signal;
        status.ethernet  = raw.ethernet != 0;
        return 0;
    }

    static int scan(std::vector<AccessPoint> &access_points, int max_entries)
    {
        access_points.clear();
        if (max_entries <= 0) return 0;

        const int limit = std::clamp(max_entries, 1, CP0_WIFI_AP_MAX);
        std::vector<cp0_wifi_ap_t> raw(static_cast<std::size_t>(limit));
        const int result = cp0_wifi_scan(raw.data(), limit);
        if (result < 0) return result;

        const int count = std::min(result, limit);
        access_points.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const cp0_wifi_ap_t &source = raw[static_cast<std::size_t>(index)];
            AccessPoint access_point;
            access_point.ssid     = bounded_string(source.ssid);
            access_point.security = bounded_string(source.security);
            access_point.signal   = source.signal;
            access_point.in_use   = source.in_use != 0;
            access_point.saved    = source.saved != 0;
            if (!access_point.ssid.empty()) access_points.push_back(std::move(access_point));
        }
        return static_cast<int>(access_points.size());
    }

    static int connect(const std::string &ssid, const std::string &password)
    {
        const int validation = validate_credentials(ssid, password);
        if (validation != 0) return validation;
        return cp0_wifi_connect(ssid.c_str(), password.empty() ? nullptr : password.c_str());
    }

    static bool profile_exists(const std::string &ssid)
    {
        return valid_text(ssid, CP0_WIFI_SSID_MAX, false) && cp0_wifi_profile_exists(ssid.c_str()) != 0;
    }

    static int connect_hidden(const std::string &ssid, const std::string &password)
    {
        const int validation = validate_credentials(ssid, password);
        if (validation != 0) return validation;

        return cp0_wifi_connect_hidden(ssid.c_str(), password.empty() ? nullptr : password.c_str());
    }

    static int profile_forget(const std::string &ssid)
    {
        if (!valid_text(ssid, CP0_WIFI_SSID_MAX, false)) return CP0_WIFI_ERROR_INVALID;
        return cp0_wifi_profile_forget(ssid.c_str());
    }

    static int profile_disconnect_active()
    {
        return cp0_wifi_disconnect_active();
    }
};

namespace settings_wifi {

using AccessPoint = settings_wifi_com::AccessPoint;
using Status = settings_wifi_com::Status;

constexpr std::size_t kMaxSsidBytes = 32;     /* the legacy Join Hidden SSID field */
constexpr std::size_t kMaxPasswordBytes = 64; /* WPA: 8..63 characters or 64 hex digits */

/* 0..100 from the backend's signal: a strength 1..100, or an RSSI in dBm (-100 dBm -> 0, -50 dBm -> 100), as the
 * status strip reads it (native_ui.cpp). */
int signal_percent(int signal);

/* Signal bars lit, 0..4: the status bar's thresholds (cp0_statusbar.c kBarThresholds 1 / 30 / 60 / 80 %). */
int signal_bars(int signal);

/* The network the device is on: its scan entry says so, or the status names it. */
bool is_connected(const AccessPoint &ap, const Status &status);

/* List order: the connected network, then saved ones, then by signal (strongest first), then by name. */
void sort_networks(std::vector<AccessPoint> &networks, const Status &status);

/* What Enter / a tap on a network row does. */
enum class RowAction {
    Actions,     /* the connected network: Details / Forget / Disconnect */
    Connect,     /* an open or a saved network: connect at once */
    AskPassword, /* a new secured network: the password dialog */
};
RowAction row_action(const AccessPoint &ap, const Status &status);

/* The password check before connecting (the legacy page's rule): empty -> "Password required", enterprise security
 * -> not supported, WPA -> 8..63 characters or 64 hex digits. Empty string when it may be tried. */
std::string password_error(const std::string &password, const std::string &security);

/* The native page's status line: "Wi-Fi is off", the network and its IP ("No IP" while it has none), or
 * "Not connected". */
std::string status_line(const Status &status, bool radio_on);

/* Result of a connect / forget / disconnect for the user (the legacy page's texts). */
const char *connection_error_text(int result);
/* The same for a connection made from the password dialog: a refused password is "Wrong password". */
const char *password_result_text(int result);
/* A failed scan, for the status line (the native page has a Rescan row instead of the R key). */
const char *scan_error_text(int result);

} // namespace settings_wifi
