/*
 * SPDX-License-Identifier: MIT
 *
 * Pure helpers for the read-only BlueZ device facts (Icon, Class, UUIDs) the Bluetooth backend publishes in
 * cp0_bt_device_t: no GLib, no BlueZ, so they are unit-tested on the host (tests/test_bluetooth_device_facts.cpp).
 */
#pragma once

#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace cp0::bluetooth {

/* One UUID as the wire carries it: a Bluetooth-base UUID (0000XXXX-0000-1000-8000-00805f9b34fb) becomes its four
 * lowercase hex digits ("110b"); any other UUID is returned lowercased in full. "" for an empty string. */
inline std::string short_uuid(const std::string &uuid)
{
    std::string lower;
    lower.reserve(uuid.size());
    for (const char c : uuid) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    static const char kBaseTail[] = "-0000-1000-8000-00805f9b34fb";
    if (lower.size() == 36 && lower.compare(0, 4, "0000") == 0 && lower.compare(8, std::strlen(kBaseTail), kBaseTail) == 0)
        return lower.substr(4, 4);
    return lower;
}

/* The UUID list as one comma separated text of at most size-1 bytes: whole UUIDs only (never a cut one), short form
 * where possible, duplicates dropped, order kept. */
inline std::string join_uuids(const std::vector<std::string> &uuids, std::size_t size)
{
    std::string joined;
    if (size == 0) return joined;
    for (const std::string &raw : uuids) {
        const std::string item = short_uuid(raw);
        if (item.empty()) continue;
        bool seen = false;
        std::size_t at = 0;
        while (at <= joined.size() && !joined.empty()) {
            std::size_t end = joined.find(',', at);
            if (end == std::string::npos) end = joined.size();
            if (joined.compare(at, end - at, item) == 0 && end - at == item.size()) {
                seen = true;
                break;
            }
            at = end + 1;
        }
        if (seen) continue;
        const std::size_t need = joined.size() + (joined.empty() ? 0 : 1) + item.size();
        if (need >= size) break;
        if (!joined.empty()) joined.push_back(',');
        joined += item;
    }
    return joined;
}

/* True for the A2DP service classes that mean "music profile": Audio Source (110a) and Audio Sink (110b), in either
 * the short or the full form. */
inline bool is_a2dp_uuid(const std::string &uuid)
{
    const std::string s = short_uuid(uuid);
    return s == "110a" || s == "110b";
}

} // namespace cp0::bluetooth
