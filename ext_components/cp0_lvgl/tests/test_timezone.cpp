/*
 * SPDX-License-Identifier: MIT
 *
 * Time zone names and lists (cp0_timezone.hpp): what may reach timedated / `timedatectl set-timezone` (the sudo
 * admin command TimezoneSet validates with valid_name too), and the three list formats.
 */

#include "cp0_timezone.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

using Names = std::vector<std::string>;

void test_valid_names()
{
    for (const char *name : {"UTC", "Etc/UTC", "Europe/Paris", "America/Argentina/Buenos_Aires", "Etc/GMT+5",
                             "Etc/GMT-14", "America/Port-au-Prince", "EST5EDT", "Factory", "posixrules",
                             "America/Indiana/Knox", "Antarctica/DumontDUrville"})
        CHECK(cp0::tz::valid_name(name));
}

void test_refused_names()
{
    for (const char *name : {"", "/", "/Europe/Paris", "Europe/Paris/", "Europe//Paris", "-h", "--help",
                             "Europe/-x", "+0100", "1Zone", "Europe/../etc/shadow", "../UTC", ".", "Europe/Paris ",
                             " Europe/Paris", "Europe/Pa ris", "Europe/Paris;reboot", "Europe/Paris\nUTC", "Europe/$(id)",
                             "Europe/`id`", "Europe/Paris'", "Europe/Paris\"", "Europe\\Paris", "Europe/Par*s",
                             "Europe/Paris&", "Europe/Paris|x", "Europe/Paris>x", "Europe/Pari\xC5\x9B"})
        CHECK(!cp0::tz::valid_name(name));
    const std::string longest(cp0::tz::kMaxNameBytes, 'A');
    CHECK(cp0::tz::valid_name(longest));
    CHECK(!cp0::tz::valid_name(longest + "A"));
    CHECK(!cp0::tz::valid_name(std::string("Europe/Paris\0x", 14))); /* an embedded NUL */
}

void test_parse_list()
{
    /* timedatectl list-timezones: one per line; CR LF, blanks around, junk and duplicates go */
    const Names names = cp0::tz::parse_list("Europe/Paris\nAfrica/Abidjan\r\n  UTC  \n\nnot a zone\n-h\n"
                                            "Europe/Paris\nAmerica/Argentina/Buenos_Aires");
    CHECK((names == Names{"Africa/Abidjan", "America/Argentina/Buenos_Aires", "Europe/Paris", "UTC"}));
    CHECK(cp0::tz::parse_list("").empty());
    CHECK(cp0::tz::parse_list("\n\n").empty());
}

void test_parse_tzdata_zi()
{
    const char *text = "# version 2025b\n"
                       "R E 1981 ma - Mar lastSu 1u 1 S\n"
                       "Z Europe/Paris 0:9:21 - LMT 1891 Mar 16\n"
                       "0:9:21 - PMT 1911 Mar 11\n"
                       "Zone Africa/Abidjan -0:16:8 - LMT 1912\n"
                       "L Etc/UTC UTC\n"
                       "Link Europe/Paris Europe/Monaco\n"
                       "L Etc/UTC\n" /* too short: no name */
                       "Z\n"
                       "Z ../../etc/passwd 0 - X\n";
    const Names names = cp0::tz::parse_tzdata_zi(text);
    CHECK((names == Names{"Africa/Abidjan", "Europe/Monaco", "Europe/Paris", "UTC"}));
}

void test_parse_zone_tab()
{
    const char *text = "# tzdb timezone descriptions\n"
                       "#codes\tcoordinates\tTZ\tcomments\n"
                       "FR,MC\t+4852+00220\tEurope/Paris\n"
                       "AR\t-3436-05827\tAmerica/Argentina/Buenos_Aires\tBuenos Aires (BA, CF)\n"
                       "CI,BF\t+0519-00402\tAfrica/Abidjan\n"
                       "XX\tbad line\n"
                       "YY\t+0000+00000\t-rf\n";
    const Names names = cp0::tz::parse_zone_tab(text);
    CHECK((names == Names{"Africa/Abidjan", "America/Argentina/Buenos_Aires", "Europe/Paris"}));
}

void test_zone_from_link()
{
    CHECK(cp0::tz::zone_from_link("/usr/share/zoneinfo/Europe/Paris") == "Europe/Paris");
    CHECK(cp0::tz::zone_from_link("../usr/share/zoneinfo/Etc/UTC") == "Etc/UTC");
    CHECK(cp0::tz::zone_from_link("/usr/share/zoneinfo/America/Argentina/Buenos_Aires") ==
          "America/Argentina/Buenos_Aires");
    CHECK(cp0::tz::zone_from_link("/etc/somewhere").empty());
    CHECK(cp0::tz::zone_from_link("/usr/share/zoneinfo/").empty());
    CHECK(cp0::tz::zone_from_link("/usr/share/zoneinfo/../../etc/x").empty());
}

} // namespace

int main()
{
    test_valid_names();
    test_refused_names();
    test_parse_list();
    test_parse_tzdata_zi();
    test_parse_zone_tab();
    test_zone_from_link();
    if (failures) {
        std::fprintf(stderr, "test_timezone: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_timezone: ok\n");
    return EXIT_SUCCESS;
}
