/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_network_policy.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include <sys/stat.h>

using cp0::network::AccessPoint;
using cp0::network::Status;

static void test_terse_fields_decode_escaped_delimiters()
{
    const auto fields = cp0::network::split_terse_fields(R"(office\:east:WPA2\\WPA3:*)");
    assert(fields.size() == 3);
    assert(fields[0] == "office:east");
    assert(fields[1] == "WPA2\\WPA3");
    assert(fields[2] == "*");
}

static void test_scan_deduplicates_by_decoded_ssid()
{
    const auto access_points = cp0::network::parse_scan_output(
        "office\\:east:31:WPA2: *\r\n"
        "office\\:east:72:WPA2:  \n"
        ":99:OPEN:  \n"
        "junk:not-a-number:OPEN:  \n"
        "too-high:101:OPEN:  \n"
        "guest:54:OPEN:  \n");

    assert(access_points.size() == 2);
    assert(access_points[0].ssid == "office:east");
    assert(access_points[0].signal == 72);
    assert(access_points[0].in_use);
    assert(access_points[1].ssid == "guest");
}

static void test_device_status_preserves_network_semantics()
{
    const Status status = cp0::network::parse_device_status(
        "eth0:ethernet:connected:Wired connection 1\n"
        "wlan0:wifi:connected (externally):--\n");
    assert(status.ethernet);
    assert(status.connected);
    assert(status.wifi_interface == "wlan0");
    assert(status.ssid.empty());

    const Status netplan = cp0::network::parse_device_status(
        "wlan0:wifi:connected:netplan-wlan0-home\\:lab\n");
    assert(netplan.connected);
    assert(netplan.ssid == "home:lab");
}

static void test_active_wifi_fills_signal_and_missing_ssid()
{
    Status status;
    cp0::network::apply_active_wifi(" :80:other\n*:67:home\\:lab\n", status);
    assert(status.signal == 67);
    assert(status.ssid == "home:lab");

    status.ssid = "profile name";
    cp0::network::apply_active_wifi("*:52:ignored\n", status);
    assert(status.ssid == "profile name");
    cp0::network::apply_active_wifi("*:5junk:broken\n*:101:also-broken\n", status);
    assert(status.signal == 52);
}

static void test_named_profile_does_not_imply_connected()
{
    for (const char *state : {"connecting (prepare)", "connecting (configuring)",
                              "connecting (need authentication)", "connecting (getting IP configuration)",
                              "disconnecting", "disconnected", "failed", "unavailable"}) {
        const Status status = cp0::network::parse_device_status(
            std::string("eth0:ethernet:connected:Wired connection 1\nwlan0:wifi:") +
            state + ":NewNetwork\n");
        assert(status.ethernet);
        assert(!status.connected);
        assert(status.ssid.empty());
        assert(status.wifi_interface.empty());
    }
}

static void test_simple_command_output_parsers()
{
    assert(cp0::network::parse_ipv4_address("3: wlan0 inet 192.168.1.42/24 brd 192.168.1.255\n") ==
           "192.168.1.42");
    assert(cp0::network::parse_ipv4_address("no address here").empty());
    assert(cp0::network::parse_radio_enabled(" \tenabled\r\n"));
    assert(!cp0::network::parse_radio_enabled("disabled\n"));
    assert(cp0::network::first_active_connection("lo\ncoffee\\:shop\r\nbackup\n") == "coffee:shop");
}

static void test_find_wifi_interface()
{
    char root[] = "/tmp/cp0-network-policy-XXXXXX";
    assert(mkdtemp(root) != nullptr);
    const std::string top = root;
    auto make = [&](const std::string &rel) { mkdir((top + rel).c_str(), 0755); };
    auto state = [&](const std::string &iface, const char *text) {
        std::ofstream((top + "/sys/class/net/" + iface + "/operstate").c_str()) << text << '\n';
    };
    make("/sys");
    make("/sys/class");
    make("/sys/class/net");

    // no sysfs tree at all, then Ethernet only: no Wi-Fi interface
    assert(cp0::network::find_wifi_interface("/nonexistent").empty());
    make("/sys/class/net/enp1s0");
    state("enp1s0", "up");
    assert(cp0::network::find_wifi_interface(top).empty());

    // the tablet's name, and the Pi's
    make("/sys/class/net/wlp2s0");
    make("/sys/class/net/wlp2s0/wireless");
    state("wlp2s0", "up");
    assert(cp0::network::find_wifi_interface(top) == "wlp2s0");

    // an up interface wins over a down one that sorts first
    make("/sys/class/net/wlan0");
    make("/sys/class/net/wlan0/wireless");
    state("wlan0", "down");
    assert(cp0::network::find_wifi_interface(top) == "wlp2s0");

    // none up: still the first by name (the caller only wants an interface to ask about)
    state("wlp2s0", "down");
    assert(cp0::network::find_wifi_interface(top) == "wlan0");

    const std::string cleanup = "rm -rf '" + top + "'";
    assert(system(cleanup.c_str()) == 0);
}

int main()
{
    test_terse_fields_decode_escaped_delimiters();
    test_scan_deduplicates_by_decoded_ssid();
    test_device_status_preserves_network_semantics();
    test_named_profile_does_not_imply_connected();
    test_active_wifi_fills_signal_and_missing_ssid();
    test_simple_command_output_parsers();
    test_find_wifi_interface();
    std::cout << "network policy tests passed\n";
    return 0;
}
