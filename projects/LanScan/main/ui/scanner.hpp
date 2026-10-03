/*
 * SPDX-License-Identifier: MIT
 *
 * LAN scanner backend: host discovery (ARP) and TCP connect port scan of the
 * local subnet. Needs no root and no external tools. It only ever targets
 * addresses inside the subnet of one of this device's own interfaces.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lanscan {

struct Host {
    std::string ip;
    std::string mac;
    std::string name;    // reverse DNS, may be empty
    std::string vendor;  // from a small built-in table, may be empty
    bool is_self = false;
    bool is_gateway = false;
};

struct PortResult {
    int port = 0;
    std::string service;
};

struct Network {
    std::string iface;
    std::string ip;
    uint32_t base = 0;   // host byte order, first usable address of the scanned range
    int count = 0;       // number of usable addresses (<= 254: never more than a /24)
};

/* The IPv4 network of a non-loopback interface that is up (wlan/eth preferred). */
bool detect_network(Network &out);

class Scanner {
public:
    struct State;

    Scanner();
    ~Scanner();

    /* Host discovery on `net`; results appear in hosts() while it runs. */
    void start_discovery(const Network &net);
    /* TCP connect scan of one host: the common ports, or 1-1024 when full_range. */
    void start_port_scan(const std::string &ip, bool full_range);
    void stop();

    bool discovery_running() const;
    bool port_scan_running() const;
    int discovery_progress() const;  // 0..100
    int port_progress() const;       // 0..100

    std::vector<Host> hosts() const;
    std::vector<PortResult> ports() const;
    std::string port_target() const;

private:
    std::shared_ptr<State> s_;   // shared with the (detached) worker threads
};

} // namespace lanscan
