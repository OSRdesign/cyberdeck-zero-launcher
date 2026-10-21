/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Date & Time > Time zone: the picker's rules (settings_timezone_model.hpp) on a list as timedatectl
 * list-timezones prints it.
 */

#include "settings_timezone_model.hpp"

#include "cp0_timezone.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

using Names = std::vector<std::string>;

/* a short timedatectl list-timezones: regions, Etc, legacy names, a link, junk */
const char *kList = "Africa/Abidjan\nAmerica/Argentina/Buenos_Aires\nAmerica/New_York\nAsia/Tokyo\n"
                    "Australia/Sydney\nCET\nEST5EDT\nEtc/GMT+5\nEtc/UTC\nEurope/Berlin\nEurope/Paris\n"
                    "Factory\nPacific/Auckland\nUS/Eastern\nUTC\n-h\nEurope/../x\n";

settings_tz::Catalog catalog()
{
    return settings_tz::Catalog(cp0::tz::parse_list(kList));
}

void test_regions()
{
    /* only the regions that have zones, in the fixed order, UTC last; Etc, CET, US/... are not offered */
    CHECK((catalog().regions() == Names{"Africa", "America", "Asia", "Australia", "Europe", "Pacific", "UTC"}));
    /* an empty list still offers UTC */
    CHECK((settings_tz::Catalog().regions() == Names{"UTC"}));
}

void test_zones_of()
{
    const settings_tz::Catalog c = catalog();
    CHECK((c.zones_of("America") == Names{"America/Argentina/Buenos_Aires", "America/New_York"}));
    CHECK((c.zones_of("Europe") == Names{"Europe/Berlin", "Europe/Paris"}));
    CHECK(c.zones_of("UTC").empty());
    CHECK(c.zones_of("").empty());
    CHECK(c.zones_of("Eur").empty()); /* a whole region name only */
    CHECK(settings_tz::city_label("America/Argentina/Buenos_Aires", "America") == "Argentina/Buenos_Aires");
    CHECK(settings_tz::city_label("Europe/Paris", "Europe") == "Paris");
    CHECK(settings_tz::city_label("UTC", "Europe") == "UTC");
}

void test_current_zone()
{
    CHECK(settings_tz::region_of("Europe/Paris") == "Europe");
    CHECK(settings_tz::region_of("America/Argentina/Buenos_Aires") == "America");
    for (const char *utc : {"UTC", "Etc/UTC", "Etc/UCT", "UCT", "Etc/Universal", "Universal", "Etc/Zulu", "Zulu"}) {
        CHECK(settings_tz::is_utc(utc));
        CHECK(settings_tz::region_of(utc) == "UTC");
    }
    CHECK(!settings_tz::is_utc("Etc/GMT"));
    CHECK(!settings_tz::is_utc("Europe/London"));
    CHECK(settings_tz::region_of("Etc/GMT+5").empty());
    CHECK(settings_tz::region_of("US/Eastern").empty());
    CHECK(settings_tz::region_of("CET").empty());
    CHECK(settings_tz::region_of("Europe").empty());
    CHECK(settings_tz::region_of("Europe/").empty());
    CHECK(settings_tz::region_of("").empty());
    /* the highlighted rows */
    const settings_tz::Catalog c = catalog();
    CHECK(settings_tz::index_of(c.regions(), settings_tz::region_of("Europe/Paris")) == 4);
    CHECK(settings_tz::index_of(c.regions(), settings_tz::region_of("Etc/UTC")) == 6);
    CHECK(settings_tz::index_of(c.regions(), settings_tz::region_of("US/Eastern")) == -1);
    CHECK(settings_tz::index_of(c.zones_of("Europe"), "Europe/Paris") == 1);
    CHECK(settings_tz::row_value("Europe/Paris") == "Europe/Paris");
    CHECK(settings_tz::row_value("") == "Unknown");
}

void test_what_may_be_applied()
{
    const settings_tz::Catalog c = catalog();
    CHECK(c.contains("Europe/Paris"));
    CHECK(c.contains("UTC"));
    CHECK(!c.contains("Europe/Lisbon")); /* not in the list */
    CHECK(!c.contains("-h"));
    CHECK(!c.contains("Europe/../x"));
    CHECK(c.utc_zone() == "UTC");
    const settings_tz::Catalog etc_only(Names{"Etc/UTC", "Europe/Paris"});
    CHECK(etc_only.utc_zone() == "Etc/UTC");
    CHECK(etc_only.contains("Etc/UTC"));
    const settings_tz::Catalog none(Names{"Europe/Paris"});
    CHECK(none.utc_zone() == "UTC"); /* UTC always exists */
    CHECK(none.contains("UTC"));
    /* invalid names never enter the catalog */
    const settings_tz::Catalog dirty(Names{"Europe/Paris", "Europe/Paris", "bad name", "/etc/passwd", ""});
    CHECK((dirty.zones_of("Europe") == Names{"Europe/Paris"}));
}

void test_outcomes()
{
    using settings_tz::Outcome;
    CHECK(settings_tz::outcome_of_dbus(0) == Outcome::Done);
    CHECK(settings_tz::outcome_of_dbus(-13) == Outcome::Refused);
    CHECK(settings_tz::outcome_of_dbus(-22) == Outcome::Invalid);
    CHECK(settings_tz::outcome_of_dbus(-1) == Outcome::Failed);
    CHECK(settings_tz::outcome_of_sudo(0, 0) == Outcome::Done);
    CHECK(settings_tz::outcome_of_sudo(0, 1) == Outcome::Failed); /* timedatectl refused the zone */
    CHECK(settings_tz::outcome_of_sudo(1, 0) == Outcome::AuthFailed);
    CHECK(settings_tz::outcome_of_sudo(2, -5) == Outcome::Failed);
    CHECK(settings_tz::outcome_of_sudo(3, 0) == Outcome::Cancelled);
    CHECK(settings_tz::outcome_of_sudo(4, 0) == Outcome::TimedOut);
    CHECK(settings_tz::outcome_text(Outcome::Done).empty());
    for (Outcome o : {Outcome::Refused, Outcome::AuthFailed, Outcome::Cancelled, Outcome::TimedOut, Outcome::Invalid,
                      Outcome::Failed})
        CHECK(!settings_tz::outcome_text(o).empty());
}

} // namespace

int main()
{
    test_regions();
    test_zones_of();
    test_current_zone();
    test_what_may_be_applied();
    test_outcomes();
    if (failures) {
        std::fprintf(stderr, "test_settings_timezone_model: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_settings_timezone_model: ok\n");
    return EXIT_SUCCESS;
}
