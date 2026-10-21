/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Date & Time > Time zone: the picker's rules (pure C++, no LVGL, no service call; unit test
 * projects/APPLaunch/tests/test_settings_timezone_model.cpp). The page is settings_native_timezone_page.cpp.
 *
 *   level 1  the regions of the tz database that have a zone in the list (Africa, America, Antarctica, Arctic, Asia,
 *            Atlantic, Australia, Europe, Indian, Pacific), then "UTC", a zone picked directly
 *   level 2  a region's zones, shown without the region ("America/Argentina/Buenos_Aires" -> "Argentina/Buenos_Aires")
 * Other names of the list (Etc/GMT+5, CET, US/Eastern, ...) are not offered. The zone in use is highlighted: its
 * region on level 1 (UTC and its aliases: the UTC entry), the zone itself on level 2.
 * The time zone and Network Time are independent: nothing here reads or changes Network Time.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace settings_tz {

constexpr const char *kUtc = "UTC";

/* The level 1 regions, in their order (UTC comes after them). */
const std::vector<std::string> &region_names();

/* UTC under one of its names: UTC, UCT, Universal, Zulu, with or without "Etc/". */
bool is_utc(std::string_view zone);

/* "Europe" for "Europe/Paris", kUtc for a UTC name, "" for a zone outside the regions (Etc/GMT+5, CET, ...). */
std::string region_of(std::string_view zone);

/* A zone's level 2 label: the zone without "region/". */
std::string city_label(std::string_view zone, std::string_view region);

/* The value of the Date & Time row: the zone, "Unknown" when it could not be read. */
std::string row_value(std::string_view zone);

/* The zones of the list (cp0::tz::parse_list of cp0_timedate_list_timezones()), and what the two levels show. */
class Catalog {
public:
    Catalog() = default;
    /* keeps the valid names (cp0::tz::valid_name), sorted, each once */
    explicit Catalog(std::vector<std::string> zones);

    bool empty() const { return zones_.empty(); }
    /* the zone may be applied: in the list, or the UTC entry's zone */
    bool contains(std::string_view zone) const;
    /* level 1: the regions with at least one zone, in region_names() order, then kUtc */
    std::vector<std::string> regions() const;
    /* level 2: the region's zones (full names, sorted) */
    std::vector<std::string> zones_of(std::string_view region) const;
    /* the zone the UTC entry applies: "UTC" when listed, else "Etc/UTC" when listed, else "UTC" */
    std::string utc_zone() const;

private:
    std::vector<std::string> zones_;
};

/* Position of item in items, -1 when absent. */
int index_of(const std::vector<std::string> &items, std::string_view item);

/* How a change ended, from either path: timedated over D-Bus (cp0_timedate_set_timezone's code) or the sudo admin
 * command TimezoneSet (its result and exit codes, cp0_sudo_result_t). */
enum class Outcome { Done, Refused, AuthFailed, Cancelled, TimedOut, Invalid, Failed };
Outcome outcome_of_dbus(int code);
Outcome outcome_of_sudo(int result, int exit_code);
/* The status line of a change that did not happen ("" for Done). */
std::string outcome_text(Outcome outcome);

} // namespace settings_tz
