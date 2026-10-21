/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Date & Time > Time zone: the picker's rules (see settings_timezone_model.hpp).
 */

#include "settings_timezone_model.hpp"

#include "cp0_timezone.hpp"

#include <algorithm>

namespace settings_tz {

const std::vector<std::string> &region_names()
{
    static const std::vector<std::string> names = {"Africa",   "America",  "Antarctica", "Arctic", "Asia",
                                                   "Atlantic", "Australia", "Europe",    "Indian", "Pacific"};
    return names;
}

bool is_utc(std::string_view zone)
{
    if (zone.substr(0, 4) == "Etc/") zone.remove_prefix(4);
    return zone == "UTC" || zone == "UCT" || zone == "Universal" || zone == "Zulu";
}

std::string region_of(std::string_view zone)
{
    if (is_utc(zone)) return kUtc;
    const std::size_t slash = zone.find('/');
    if (slash == std::string_view::npos || slash + 1 >= zone.size()) return {};
    const std::string_view region = zone.substr(0, slash);
    for (const std::string &name : region_names())
        if (name == region) return name;
    return {};
}

std::string city_label(std::string_view zone, std::string_view region)
{
    if (!region.empty() && zone.size() > region.size() + 1 && zone.substr(0, region.size()) == region &&
        zone[region.size()] == '/')
        return std::string(zone.substr(region.size() + 1));
    return std::string(zone);
}

std::string row_value(std::string_view zone)
{
    return zone.empty() ? std::string("Unknown") : std::string(zone);
}

Catalog::Catalog(std::vector<std::string> zones)
{
    zones.erase(std::remove_if(zones.begin(), zones.end(),
                               [](const std::string &zone) { return !cp0::tz::valid_name(zone); }),
                zones.end());
    std::sort(zones.begin(), zones.end());
    zones.erase(std::unique(zones.begin(), zones.end()), zones.end());
    zones_ = std::move(zones);
}

bool Catalog::contains(std::string_view zone) const
{
    if (zone == utc_zone()) return true;
    return std::binary_search(zones_.begin(), zones_.end(), zone,
                              [](std::string_view a, std::string_view b) { return a < b; });
}

std::vector<std::string> Catalog::regions() const
{
    std::vector<std::string> out;
    for (const std::string &region : region_names())
        if (!zones_of(region).empty()) out.push_back(region);
    out.emplace_back(kUtc);
    return out;
}

std::vector<std::string> Catalog::zones_of(std::string_view region) const
{
    std::vector<std::string> out;
    if (region.empty() || region == kUtc) return out;
    for (const std::string &zone : zones_)
        if (zone.size() > region.size() + 1 && zone.compare(0, region.size(), region) == 0 &&
            zone[region.size()] == '/')
            out.push_back(zone);
    return out;
}

std::string Catalog::utc_zone() const
{
    const auto listed = [this](const char *zone) { return std::binary_search(zones_.begin(), zones_.end(), zone); };
    if (listed("UTC")) return "UTC";
    if (listed("Etc/UTC")) return "Etc/UTC";
    return "UTC";
}

int index_of(const std::vector<std::string> &items, std::string_view item)
{
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i] == item) return static_cast<int>(i);
    return -1;
}

Outcome outcome_of_dbus(int code)
{
    switch (code) {
    case 0: return Outcome::Done;
    case -13: return Outcome::Refused;
    case -22: return Outcome::Invalid;
    default: return Outcome::Failed;
    }
}

Outcome outcome_of_sudo(int result, int exit_code)
{
    switch (result) { /* cp0_sudo_result_t */
    case 0: return exit_code == 0 ? Outcome::Done : Outcome::Failed;
    case 1: return Outcome::AuthFailed;
    case 3: return Outcome::Cancelled;
    case 4: return Outcome::TimedOut;
    default: return Outcome::Failed;
    }
}

std::string outcome_text(Outcome outcome)
{
    switch (outcome) {
    case Outcome::Done: return {};
    case Outcome::Refused: return "Not allowed to change the time zone";
    case Outcome::AuthFailed: return "Authentication failed";
    case Outcome::Cancelled: return "Time zone not changed";
    case Outcome::TimedOut: return "Time zone change timed out";
    case Outcome::Invalid: return "Unknown time zone";
    case Outcome::Failed: break;
    }
    return "Could not set the time zone";
}

} // namespace settings_tz
