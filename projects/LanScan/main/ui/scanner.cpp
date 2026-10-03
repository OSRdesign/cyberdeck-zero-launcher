/*
 * SPDX-License-Identifier: MIT
 */

#include "scanner.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ifaddrs.h>
#include <mutex>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

namespace lanscan {

namespace {

struct Service { int port; const char *name; };

// Common TCP ports and their usual services.
const Service kCommonPorts[] = {
    {21, "ftp"}, {22, "ssh"}, {23, "telnet"}, {25, "smtp"}, {53, "dns"}, {80, "http"},
    {81, "http-alt"}, {88, "kerberos"}, {110, "pop3"}, {111, "rpcbind"}, {113, "ident"}, {119, "nntp"},
    {123, "ntp"}, {135, "msrpc"}, {139, "netbios-ssn"}, {143, "imap"}, {161, "snmp"},
    {179, "bgp"}, {389, "ldap"}, {427, "slp"}, {443, "https"}, {445, "smb"}, {465, "smtps"},
    {515, "printer"}, {548, "afp"}, {554, "rtsp"}, {587, "submission"}, {631, "ipp"}, {636, "ldaps"},
    {873, "rsync"}, {902, "vmware"}, {993, "imaps"}, {995, "pop3s"}, {1080, "socks"}, {1194, "openvpn"},
    {1433, "mssql"}, {1521, "oracle"}, {1723, "pptp"}, {1883, "mqtt"}, {1900, "upnp"}, {2049, "nfs"},
    {2222, "ssh-alt"}, {2375, "docker"}, {2376, "docker-tls"}, {3000, "dev-web"}, {3128, "squid"},
    {3306, "mysql"}, {3389, "rdp"}, {3689, "daap"}, {4443, "https-alt"}, {5000, "upnp/web"},
    {5001, "synology"}, {5060, "sip"}, {5353, "mdns"}, {5432, "postgres"}, {5555, "adb"},
    {5601, "kibana"}, {5672, "amqp"}, {5900, "vnc"}, {5901, "vnc"}, {6379, "redis"},
    {6443, "k8s-api"}, {6667, "irc"}, {7000, "airplay"}, {7547, "tr-069"}, {8000, "http-alt"},
    {8006, "proxmox"}, {8008, "http-alt"}, {8080, "http-proxy"}, {8081, "http-alt"}, {8086, "influxdb"},
    {8123, "homeassistant"}, {8200, "dlna"}, {8443, "https-alt"}, {8554, "rtsp-alt"}, {8728, "mikrotik"},
    {8888, "http-alt"}, {9000, "alt"}, {9090, "cockpit/web"}, {9100, "jetdirect"}, {9200, "elastic"},
    {10000, "webmin"}, {27017, "mongodb"}, {32400, "plex"}, {49152, "upnp"}, {51820, "wireguard"},
    {62078, "iphone-sync"},
};

const char *service_name(int port)
{
    for (const Service &s : kCommonPorts)
        if (s.port == port) return s.name;
    return "";
}

// A few OUI prefixes (first three bytes of the MAC) I am sure of; anything else is left blank.
const struct { const char *oui; const char *vendor; } kVendors[] = {
    {"b8:27:eb", "Raspberry Pi"}, {"dc:a6:32", "Raspberry Pi"}, {"e4:5f:01", "Raspberry Pi"},
    {"d8:3a:dd", "Raspberry Pi"}, {"28:cd:c1", "Raspberry Pi"}, {"2c:cf:67", "Raspberry Pi"},
    {"24:0a:c4", "Espressif"}, {"30:ae:a4", "Espressif"}, {"7c:9e:bd", "Espressif"},
    {"a4:cf:12", "Espressif"}, {"84:f3:eb", "Espressif"}, {"ec:fa:bc", "Espressif"},
    {"24:6f:28", "Espressif"},
};

// The full IEEE registry (prefix<TAB>vendor per line), installed next to the app; loaded on first use.
const std::unordered_map<uint32_t, std::string> &oui_table()
{
    static std::unordered_map<uint32_t, std::string> table;
    static std::once_flag once;
    std::call_once(once, [] {
        const char *env = std::getenv("LANSCAN_OUI");
        std::ifstream file(env ? env : "/usr/share/APPLaunch/share/oui.tsv");
        std::string line;
        table.reserve(45000);
        while (std::getline(file, line)) {
            if (line.size() < 8 || line[6] != '\t') continue;
            table[static_cast<uint32_t>(std::strtoul(line.substr(0, 6).c_str(), nullptr, 16))] = line.substr(7);
        }
    });
    return table;
}

std::string vendor_for(const std::string &mac)
{
    if (mac.size() < 8) return "";
    // Bit 1 of the first byte set: locally administered (random / private address), no vendor.
    const long first = std::strtol(mac.substr(0, 2).c_str(), nullptr, 16);
    if (first & 2) return "Private MAC";
    const std::string hex = mac.substr(0, 2) + mac.substr(3, 2) + mac.substr(6, 2);
    const auto &table = oui_table();
    const auto found = table.find(static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16)));
    if (found != table.end()) return found->second;
    for (const auto &v : kVendors)
        if (mac.compare(0, 8, v.oui) == 0) return v.vendor;
    return "";
}

std::string ip_string(uint32_t host_order)
{
    in_addr a{};
    a.s_addr = htonl(host_order);
    char buffer[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a, buffer, sizeof(buffer));
    return buffer;
}

bool parse_ip(const std::string &text, uint32_t &host_order)
{
    in_addr a{};
    if (inet_pton(AF_INET, text.c_str(), &a) != 1) return false;
    host_order = ntohl(a.s_addr);
    return true;
}

std::string read_first_line(const std::string &path)
{
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

uint32_t default_gateway()
{
    std::ifstream file("/proc/net/route");
    std::string line;
    std::getline(file, line); // header
    while (std::getline(file, line)) {
        char iface[32];
        unsigned destination = 0, gateway = 0, flags = 0;
        if (std::sscanf(line.c_str(), "%31s %x %x %x", iface, &destination, &gateway, &flags) == 4 &&
            destination == 0 && (flags & 2))
            return ntohl(gateway); // /proc/net/route stores it in network byte order
    }
    return 0;
}

// Whether a TCP connect to ip:port succeeds (open) within timeout_ms.
bool tcp_open(uint32_t ip_host_order, int port, int timeout_ms)
{
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(ip_host_order);
    bool open = false;
    int rc = ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (rc == 0) {
        open = true;
    } else if (errno == EINPROGRESS) {
        pollfd p{fd, POLLOUT, 0, };
        if (::poll(&p, 1, timeout_ms) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            open = err == 0;
        }
    }
    ::close(fd);
    return open;
}

// ---- device names over the local network: mDNS (Apple, Linux, ESP, printers) and NetBIOS (Windows, Samba)

// Reads a DNS name (with compression pointers) starting at `pos`; returns the position after it in the
// original stream, or -1 on error.
int dns_name(const unsigned char *buf, int len, int pos, std::string &out)
{
    out.clear();
    int end = -1;
    int hops = 0;
    while (pos >= 0 && pos < len) {
        const int n = buf[pos];
        if (n == 0) return end >= 0 ? end : pos + 1;
        if ((n & 0xC0) == 0xC0) {
            if (pos + 1 >= len || ++hops > 16) return -1;
            if (end < 0) end = pos + 2;
            pos = ((n & 0x3F) << 8) | buf[pos + 1];
            continue;
        }
        if (pos + 1 + n > len) return -1;
        if (!out.empty()) out += '.';
        out.append(reinterpret_cast<const char *>(buf + pos + 1), static_cast<size_t>(n));
        pos += 1 + n;
    }
    return -1;
}

std::string trim_name(std::string name)
{
    for (const char *suffix : {".local", ".lan", ".home", ".fritz.box"}) {
        const size_t n = std::strlen(suffix);
        if (name.size() > n && name.compare(name.size() - n, n, suffix) == 0) name.erase(name.size() - n);
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    return name;
}

std::string parse_mdns_ptr(const unsigned char *buf, int len)
{
    if (len < 12) return "";
    const int qd = (buf[4] << 8) | buf[5];
    const int an = (buf[6] << 8) | buf[7];
    int pos = 12;
    std::string name;
    for (int i = 0; i < qd; ++i) {
        pos = dns_name(buf, len, pos, name);
        if (pos < 0) return "";
        pos += 4;
    }
    for (int i = 0; i < an && pos >= 0 && pos + 10 <= len; ++i) {
        pos = dns_name(buf, len, pos, name);
        if (pos < 0 || pos + 10 > len) return "";
        const int type = (buf[pos] << 8) | buf[pos + 1];
        const int rdlen = (buf[pos + 8] << 8) | buf[pos + 9];
        pos += 10;
        if (type == 12 && pos + rdlen <= len) {
            std::string target;
            if (dns_name(buf, len, pos, target) >= 0 && !target.empty()) return trim_name(target);
        }
        pos += rdlen;
    }
    return "";
}

std::string parse_nbstat(const unsigned char *buf, int len)
{
    if (len < 12) return "";
    const int qd = (buf[4] << 8) | buf[5];
    const int an = (buf[6] << 8) | buf[7];
    if (an < 1) return "";
    int pos = 12;
    std::string name;
    for (int i = 0; i < qd; ++i) {
        pos = dns_name(buf, len, pos, name);
        if (pos < 0) return "";
        pos += 4;
    }
    pos = dns_name(buf, len, pos, name);
    if (pos < 0 || pos + 11 > len) return "";
    pos += 10;                      // type, class, ttl, rdlength
    const int count = buf[pos++];
    for (int i = 0; i < count && pos + 18 <= len; ++i, pos += 18) {
        const int suffix = buf[pos + 15];
        const int flags = (buf[pos + 16] << 8) | buf[pos + 17];
        if (suffix == 0x00 && !(flags & 0x8000)) {            // unique workstation name
            std::string host(reinterpret_cast<const char *>(buf + pos), 15);
            while (!host.empty() && (host.back() == ' ' || host.back() == '\0')) host.pop_back();
            return host;
        }
    }
    return "";
}

// Sends the two queries to every address and collects the answers for `wait_ms`. Returns ip -> name.
std::unordered_map<std::string, std::string> query_names(const std::vector<std::string> &ips, int wait_ms)
{
    std::unordered_map<std::string, std::string> found;
    int mdns = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    int nbns = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (mdns < 0 || nbns < 0) {
        if (mdns >= 0) ::close(mdns);
        if (nbns >= 0) ::close(nbns);
        return found;
    }
    // NetBIOS node status request for "*": header + encoded name + type NBSTAT + class IN.
    unsigned char nb[50] = {0x12, 0x34, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0x20, 'C', 'K'};
    std::memset(nb + 15, 'A', 30);
    nb[45] = 0; nb[46] = 0; nb[47] = 0x21; nb[48] = 0; nb[49] = 1;

    for (const std::string &ip : ips) {
        in_addr a{};
        if (inet_pton(AF_INET, ip.c_str(), &a) != 1) continue;
        const unsigned char *o = reinterpret_cast<const unsigned char *>(&a.s_addr);
        // mDNS: PTR query for the reverse name, asking for a unicast answer (QU bit).
        unsigned char q[96] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
        int n = 12;
        for (int i = 3; i >= 0; --i) {
            const std::string label = std::to_string(o[i]);
            q[n++] = static_cast<unsigned char>(label.size());
            std::memcpy(q + n, label.data(), label.size());
            n += static_cast<int>(label.size());
        }
        const char tail[] = "\7in-addr\4arpa";
        std::memcpy(q + n, tail, sizeof(tail) - 1);
        n += static_cast<int>(sizeof(tail) - 1);
        q[n++] = 0;
        q[n++] = 0; q[n++] = 12;        // PTR
        q[n++] = 0x80; q[n++] = 1;      // IN + unicast-response
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_addr = a;
        to.sin_port = htons(5353);
        ::sendto(mdns, q, static_cast<size_t>(n), 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
        to.sin_port = htons(137);
        ::sendto(nbns, nb, sizeof(nb), 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    pollfd fds[2] = {{mdns, POLLIN, 0}, {nbns, POLLIN, 0}};
    while (std::chrono::steady_clock::now() < deadline) {
        if (::poll(fds, 2, 100) <= 0) continue;
        for (int k = 0; k < 2; ++k) {
            if (!(fds[k].revents & POLLIN)) continue;
            unsigned char buf[1500];
            sockaddr_in from{};
            socklen_t flen = sizeof(from);
            const ssize_t got = ::recvfrom(fds[k].fd, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &flen);
            if (got <= 0) continue;
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            const std::string name = k == 0 ? parse_mdns_ptr(buf, static_cast<int>(got))
                                            : parse_nbstat(buf, static_cast<int>(got));
            if (!name.empty() && found.find(ip) == found.end()) found[ip] = name;   // the first answer wins
        }
    }
    ::close(mdns);
    ::close(nbns);
    return found;
}

} // namespace

bool detect_network(Network &out)
{
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return false;
    int best_score = -1;
    for (ifaddrs *a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        const std::string name = a->ifa_name;
        int score = 1;
        if (name.rfind("wlan", 0) == 0 || name.rfind("eth", 0) == 0 || name.rfind("en", 0) == 0) score = 2;
        if (score <= best_score) continue;
        const uint32_t ip = ntohl(reinterpret_cast<sockaddr_in *>(a->ifa_addr)->sin_addr.s_addr);
        uint32_t mask = a->ifa_netmask ? ntohl(reinterpret_cast<sockaddr_in *>(a->ifa_netmask)->sin_addr.s_addr)
                                       : 0xFFFFFF00u;
        if (mask < 0xFFFFFF00u) mask = 0xFFFFFF00u;   // never sweep more than a /24
        const uint32_t network = ip & mask;
        const uint32_t broadcast = network | ~mask;
        if (broadcast - network < 2) continue;
        out.iface = name;
        out.ip = ip_string(ip);
        out.base = network + 1;
        out.count = static_cast<int>(broadcast - network - 1);
        best_score = score;
    }
    freeifaddrs(list);
    return best_score >= 0;
}

struct Scanner::State {
    mutable std::mutex mutex;
    std::vector<Host> hosts;
    std::vector<PortResult> ports;
    std::string port_target;
    std::atomic<int> discovery_gen{0};
    std::atomic<int> port_gen{0};
    std::atomic<bool> discovery_running{false};
    std::atomic<int> port_workers{0};
    std::atomic<int> discovery_progress{0};
    std::atomic<int> port_progress{0};
};

Scanner::Scanner() : s_(std::make_shared<State>()) {}

Scanner::~Scanner() { stop(); }

void Scanner::stop()
{
    ++s_->discovery_gen;
    ++s_->port_gen;
}

bool Scanner::discovery_running() const { return s_->discovery_running; }
bool Scanner::port_scan_running() const { return s_->port_workers > 0; }
int Scanner::discovery_progress() const { return s_->discovery_progress; }
int Scanner::port_progress() const { return s_->port_progress; }

std::vector<Host> Scanner::hosts() const
{
    std::lock_guard<std::mutex> lock(s_->mutex);
    return s_->hosts;
}

std::vector<PortResult> Scanner::ports() const
{
    std::lock_guard<std::mutex> lock(s_->mutex);
    return s_->ports;
}

std::string Scanner::port_target() const
{
    std::lock_guard<std::mutex> lock(s_->mutex);
    return s_->port_target;
}

namespace {

void upsert_host(std::vector<Host> &hosts, const Host &host)
{
    for (Host &existing : hosts) {
        if (existing.ip == host.ip) {
            existing.mac = host.mac;
            existing.vendor = host.vendor;
            existing.is_self = host.is_self;
            existing.is_gateway = host.is_gateway;
            return;
        }
    }
    hosts.push_back(host);
}

} // namespace

void Scanner::start_discovery(const Network &net)
{
    auto state = s_;
    const int gen = ++state->discovery_gen;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->hosts.clear();
    }
    state->discovery_progress = 0;
    state->discovery_running = true;

    std::thread([state, gen, net] {
        auto cancelled = [&] { return state->discovery_gen != gen; };
        uint32_t self_ip = 0;
        parse_ip(net.ip, self_ip);
        const uint32_t gateway = default_gateway();

        int udp = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        auto poke_all = [&](int from_percent, int to_percent) {
            for (int i = 0; i < net.count && !cancelled(); ++i) {
                const uint32_t ip = net.base + static_cast<uint32_t>(i);
                if (ip == self_ip || udp < 0) continue;
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(9); // discard: only meant to make the kernel ARP for the address
                addr.sin_addr.s_addr = htonl(ip);
                const char byte = 0;
                ::sendto(udp, &byte, 1, 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
                if (i % 16 == 15) std::this_thread::sleep_for(std::chrono::milliseconds(15));
                state->discovery_progress = from_percent + (to_percent - from_percent) * i / net.count;
            }
        };
        auto read_arp = [&] {
            std::ifstream file("/proc/net/arp");
            std::string line;
            std::getline(file, line);
            while (std::getline(file, line)) {
                char ip[32], hw[32], mask[32], dev[32];
                unsigned type = 0, flags = 0;
                if (std::sscanf(line.c_str(), "%31s 0x%x 0x%x %31s %31s %31s", ip, &type, &flags, hw, mask, dev) != 6)
                    continue;
                if (!(flags & 2) || net.iface != dev || std::strcmp(hw, "00:00:00:00:00:00") == 0) continue;
                uint32_t host_ip = 0;
                if (!parse_ip(ip, host_ip) || host_ip < net.base ||
                    host_ip >= net.base + static_cast<uint32_t>(net.count))
                    continue;
                Host host;
                host.ip = ip;
                host.mac = hw;
                host.vendor = vendor_for(host.mac);
                host.is_gateway = host_ip == gateway;
                std::lock_guard<std::mutex> lock(state->mutex);
                upsert_host(state->hosts, host);
            }
        };

        // Our own entry first.
        {
            Host self;
            self.ip = net.ip;
            self.mac = read_first_line("/sys/class/net/" + net.iface + "/address");
            self.vendor = vendor_for(self.mac);
            self.is_self = true;
            std::lock_guard<std::mutex> lock(state->mutex);
            upsert_host(state->hosts, self);
        }

        poke_all(0, 40);
        for (int round = 0; round < 4 && !cancelled(); ++round) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            read_arp();
            state->discovery_progress = 40 + round * 5;
            if (round == 1) poke_all(60, 70); // a second pass catches hosts that were slow to answer
        }
        if (udp >= 0) ::close(udp);
        state->discovery_progress = 70;

        // Names, first from the devices themselves (mDNS, NetBIOS), then reverse DNS for the rest.
        {
            std::vector<std::string> targets;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                for (const Host &h : state->hosts) targets.push_back(h.ip);
            }
            const auto names = query_names(targets, 1800);
            std::lock_guard<std::mutex> lock(state->mutex);
            for (Host &h : state->hosts) {
                const auto it = names.find(h.ip);
                if (it != names.end()) h.name = it->second;
            }
        }
        state->discovery_progress = 80;

        // Reverse DNS for the hosts still without a name, a few lookups at a time. A slow resolver must not hold anything up.
        struct Lookup {
            std::vector<std::string> ips;
            std::atomic<size_t> next{0};
            std::atomic<int> workers{0};
        };
        auto lookup = std::make_shared<Lookup>();
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            for (const Host &h : state->hosts)
                if (h.name.empty()) lookup->ips.push_back(h.ip);
        }
        for (int w = 0; w < 4; ++w) {
            ++lookup->workers;
            std::thread([state, gen, lookup] {
                for (;;) {
                    const size_t i = lookup->next++;
                    if (i >= lookup->ips.size() || state->discovery_gen != gen) break;
                    sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    inet_pton(AF_INET, lookup->ips[i].c_str(), &addr.sin_addr);
                    char name[256] = {};
                    if (getnameinfo(reinterpret_cast<sockaddr *>(&addr), sizeof(addr), name, sizeof(name), nullptr, 0,
                                    NI_NAMEREQD) == 0) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        for (Host &h : state->hosts)
                            if (h.ip == lookup->ips[i]) { h.name = trim_name(name); break; }
                    }
                }
                --lookup->workers;
            }).detach();
        }
        // Wait for the lookups, but never longer than 6 s: a slow resolver must not hold the scan up.
        for (int waited = 0; lookup->workers > 0 && waited < 60 && !cancelled(); ++waited) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const size_t done = std::min(lookup->next.load(), lookup->ips.size());
            state->discovery_progress = 80 + static_cast<int>(20 * done / std::max<size_t>(1, lookup->ips.size()));
        }

        if (state->discovery_gen == gen) {
            state->discovery_progress = 100;
            state->discovery_running = false;
        }
    }).detach();
}

void Scanner::start_port_scan(const std::string &ip, bool full_range)
{
    auto state = s_;
    const int gen = ++state->port_gen;
    uint32_t target = 0;
    if (!parse_ip(ip, target)) return;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->ports.clear();
        state->port_target = ip;
    }
    std::vector<int> list;
    if (full_range) {
        for (int p = 1; p <= 1024; ++p) list.push_back(p);
    } else {
        for (const Service &s : kCommonPorts) list.push_back(s.port);
    }
    auto ports = std::make_shared<std::vector<int>>(std::move(list));
    auto next = std::make_shared<std::atomic<size_t>>(0);
    state->port_progress = 0;
    constexpr int kWorkers = 24;
    state->port_workers += kWorkers;
    for (int w = 0; w < kWorkers; ++w) {
        std::thread([state, gen, target, ports, next] {
            for (;;) {
                const size_t i = (*next)++;
                if (i >= ports->size() || state->port_gen != gen) break;
                const int port = (*ports)[i];
                if (tcp_open(target, port, 400)) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->port_gen == gen) {
                        state->ports.push_back({port, service_name(port)});
                        std::sort(state->ports.begin(), state->ports.end(),
                                  [](const PortResult &a, const PortResult &b) { return a.port < b.port; });
                    }
                }
                if (state->port_gen == gen)
                    state->port_progress = static_cast<int>(100 * std::min(i + 1, ports->size()) / ports->size());
            }
            --state->port_workers;
        }).detach();
    }
}

} // namespace lanscan
