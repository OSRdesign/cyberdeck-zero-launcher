/*
 * SPDX-License-Identifier: MIT
 *
 * Time zone names (the tz database's "Area/Location" identifiers, e.g. Europe/Paris): what may be handed to
 * timedated / timedatectl, and the lists they come from. Pure C++, no I/O, unit-tested on the PC
 * (tests/test_timezone.cpp). The service calls are cp0_timedate_client.hpp's.
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cp0 {
namespace tz {

constexpr std::size_t kMaxNameBytes = 64; /* the longest tz database name is 32 bytes */

/* A name that may go to timedated (SetTimezone) or as an argument of `timedatectl set-timezone`: 1..kMaxNameBytes
 * bytes of [A-Za-z0-9_+/-], components separated by one '/', each starting with a letter. So never an option ("-x"),
 * never a path step ("." / ".."), never an empty component, a space or a shell character. */
bool valid_name(std::string_view name) noexcept;

/* `timedatectl list-timezones` output (D-Bus ListTimezones joined with '\n'): one name per line. The valid names, sorted,
 * each once; anything else (blank lines, junk) is dropped. */
std::vector<std::string> parse_list(std::string_view text);

/* /usr/share/zoneinfo/tzdata.zi (what timedatectl reads itself): the zones ("Z name ..." / "Zone name ...") and the links'
 * names ("L target name" / "Link target name"). Valid names, sorted, each once. */
std::vector<std::string> parse_tzdata_zi(std::string_view text);

/* /usr/share/zoneinfo/zone1970.tab or zone.tab: the third tab-separated field of every line that is not a comment.
 * Valid names, sorted, each once. */
std::vector<std::string> parse_zone_tab(std::string_view text);

/* The zone named by /etc/localtime's link target ("/usr/share/zoneinfo/Europe/Paris", "../usr/share/zoneinfo/Etc/UTC"):
 * the part after "zoneinfo/" when it is a valid name, else "". */
std::string zone_from_link(std::string_view target);

} // namespace tz
} // namespace cp0
