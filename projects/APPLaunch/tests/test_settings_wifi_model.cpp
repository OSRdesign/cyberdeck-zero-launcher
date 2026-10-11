/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// The Wi-Fi rules of the native Settings page (task 014 P3b, settings_wifi_model.hpp): signal bars with the status
// bar's thresholds, the list order, what Enter does on a network, the password check and the texts.

#include "../main/ui/settings/settings_wifi_model.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using settings_wifi::AccessPoint;
using settings_wifi::RowAction;
using settings_wifi::Status;

AccessPoint ap(const char *ssid, int signal, const char *security, bool in_use = false, bool saved = false)
{
    AccessPoint result;
    result.ssid = ssid;
    result.signal = signal;
    result.security = security;
    result.in_use = in_use;
    result.saved = saved;
    return result;
}

std::vector<std::string> names(const std::vector<AccessPoint> &networks)
{
    std::vector<std::string> result;
    for (const auto &network : networks) result.push_back(network.ssid);
    return result;
}

void test_signal_percent_and_bars()
{
    // a strength (nmcli SIGNAL) is kept; an RSSI in dBm maps -100 -> 0, -50 -> 100 (the status strip's rule)
    assert(settings_wifi::signal_percent(70) == 70);
    assert(settings_wifi::signal_percent(150) == 100);
    assert(settings_wifi::signal_percent(0) == 0);
    assert(settings_wifi::signal_percent(-50) == 100);
    assert(settings_wifi::signal_percent(-65) == 70);
    assert(settings_wifi::signal_percent(-100) == 0);
    assert(settings_wifi::signal_percent(-120) == 0);

    // bars: >= 1 / 30 / 60 / 80 % (cp0_statusbar.c kBarThresholds)
    assert(settings_wifi::signal_bars(0) == 0);
    assert(settings_wifi::signal_bars(1) == 1);
    assert(settings_wifi::signal_bars(29) == 1);
    assert(settings_wifi::signal_bars(30) == 2);
    assert(settings_wifi::signal_bars(59) == 2);
    assert(settings_wifi::signal_bars(60) == 3);
    assert(settings_wifi::signal_bars(79) == 3);
    assert(settings_wifi::signal_bars(80) == 4);
    assert(settings_wifi::signal_bars(100) == 4);
    assert(settings_wifi::signal_bars(-55) == 4); /* 90 % */
    assert(settings_wifi::signal_bars(-90) == 1); /* 20 % */
    assert(settings_wifi::signal_bars(-100) == 0);
}

void test_sort_order()
{
    Status status;
    status.connected = true;
    status.ssid = "Home";
    std::vector<AccessPoint> networks = {
        ap("Cafe", 90, ""),                     /* open, strong */
        ap("Weak", 15, "WPA2"),                 /* secured, weak */
        ap("Office", 40, "WPA2", false, true),  /* saved */
        ap("Lab", 75, "WPA2", false, true),     /* saved, stronger */
        ap("Home", 50, "WPA2", false, true),    /* connected (named by the status) */
        ap("Neighbour", 90, "WPA2"),            /* same signal as Cafe: by name */
    };
    settings_wifi::sort_networks(networks, status);
    assert((names(networks) == std::vector<std::string>{"Home", "Lab", "Office", "Cafe", "Neighbour", "Weak"}));

    // the scan's in-use flag marks the connected one too; dBm and percent compare on one scale
    Status none;
    std::vector<AccessPoint> mixed = {ap("A", -80, "WPA2"), ap("B", 70, "WPA2"), ap("C", -50, "WPA2"),
                                      ap("D", 10, "", true)};
    settings_wifi::sort_networks(mixed, none);
    assert((names(mixed) == std::vector<std::string>{"D", "C", "B", "A"}));
}

void test_row_action()
{
    Status status;
    status.connected = true;
    status.ssid = "Home";
    assert(settings_wifi::row_action(ap("Home", 50, "WPA2", false, true), status) == RowAction::Actions);
    assert(settings_wifi::row_action(ap("Other", 50, "WPA2", true), Status{}) == RowAction::Actions);
    assert(settings_wifi::row_action(ap("Cafe", 50, ""), status) == RowAction::Connect);
    assert(settings_wifi::row_action(ap("Cafe", 50, "--"), status) == RowAction::Connect);
    assert(settings_wifi::row_action(ap("Lab", 50, "WPA2", false, true), status) == RowAction::Connect);
    assert(settings_wifi::row_action(ap("New", 50, "WPA2"), status) == RowAction::AskPassword);
    assert(settings_wifi::row_action(ap("New", 50, "WPA1 WPA2"), status) == RowAction::AskPassword);
}

void test_password_error()
{
    assert(settings_wifi::password_error("", "WPA2") == "Password required");
    assert(settings_wifi::password_error("short", "WPA2") == "WPA password: 8-63 chars or 64 hex");
    assert(settings_wifi::password_error("eight-ch", "WPA2").empty());
    assert(settings_wifi::password_error(std::string(63, 'x'), "WPA2").empty());
    assert(settings_wifi::password_error(std::string(64, 'x'), "WPA2") == "WPA password: 8-63 chars or 64 hex");
    assert(settings_wifi::password_error(std::string(64, 'a'), "wpa2").empty()); /* 64 hex digits */
    assert(settings_wifi::password_error("whatever", "WPA2 802.1X") == "Enterprise Wi-Fi is not supported");
    assert(settings_wifi::password_error("whatever", "EAP") == "Enterprise Wi-Fi is not supported");
    assert(settings_wifi::password_error("wep", "WEP").empty()); /* no WPA rule */
}

void test_texts()
{
    Status status;
    assert(settings_wifi::status_line(status, false) == "Wi-Fi is off");
    assert(settings_wifi::status_line(status, true) == "Not connected");
    status.connected = true;
    status.ssid = "Home";
    assert(settings_wifi::status_line(status, true) == "Home  No IP");
    status.ip = "192.168.1.20";
    assert(settings_wifi::status_line(status, true) == "Home  192.168.1.20");

    assert(std::string(settings_wifi::connection_error_text(CP0_WIFI_ERROR_AUTH)) == "Incorrect password");
    assert(std::string(settings_wifi::password_result_text(CP0_WIFI_ERROR_AUTH)) == "Wrong password");
    assert(std::string(settings_wifi::password_result_text(CP0_WIFI_ERROR_NOT_FOUND)) ==
           "Network is no longer available");
    assert(std::string(settings_wifi::connection_error_text(CP0_WIFI_ERROR_SERVICE)) == "Wi-Fi service unavailable");
    assert(std::string(settings_wifi::scan_error_text(CP0_WIFI_ERROR_TIMEOUT)) == "Wi-Fi scan timed out");
    assert(std::string(settings_wifi::scan_error_text(-99)) == "Wi-Fi scan failed");
}

void test_credentials()
{
    // the backend wrapper's checks (unchanged from the legacy page)
    assert(settings_wifi_com::validate_credentials("Home", "") == 0);
    assert(settings_wifi_com::validate_credentials("", "x") == CP0_WIFI_ERROR_INVALID);
    assert(settings_wifi_com::validate_credentials("Home", std::string(65, 'x')) == CP0_WIFI_ERROR_INVALID);
    assert(settings_wifi_com::validate_credentials("Home", std::string(64, 'x')) == 0);
    assert(settings_wifi_com::validate_credentials("Ho\x01me", "") == CP0_WIFI_ERROR_INVALID);
    assert(settings_wifi_com::is_open_security(""));
    assert(settings_wifi_com::is_open_security("none"));
    assert(!settings_wifi_com::is_open_security("WPA2"));
}

} // namespace

int main()
{
    test_signal_percent_and_bars();
    test_sort_order();
    test_row_action();
    test_password_error();
    test_texts();
    test_credentials();
    std::puts("test_settings_wifi_model: ok");
    return 0;
}
