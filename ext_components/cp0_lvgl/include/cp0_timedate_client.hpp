#pragma once

#ifdef __cplusplus
extern "C" {
#endif
int cp0_timedate_get_ntp(int *enabled);
int cp0_timedate_get_time(int values[6]);
int cp0_timedate_set_ntp(int enabled);
int cp0_timedate_set_time(const char *timestamp);

/* The time zone (independent of Network Time: neither call touches the other setting). */

/* The zone in use ("Europe/Paris") into zone[size]: /etc/localtime's link, else timedated's Timezone property.
 * 0, -22 bad arguments, -34 too small, -1 unknown. Quick: no D-Bus call when /etc/localtime is a link. */
int cp0_timedate_get_timezone(char *zone, int size);
/* Set the zone through timedated (SetTimezone, non-interactive: PolicyKit decides, 51-launcher-time-power.rules allows
 * the sudo group). 0 done, -22 a name cp0::tz::valid_name() refuses or timedated does not know, -13 refused by PolicyKit
 * (the caller may try the sudo path: system admin command TimezoneSet), -1 any other failure. Blocks up to 30 s: call
 * it from a worker thread. */
int cp0_timedate_set_timezone(const char *zone);
/* The zones timedatectl list-timezones shows (timedated's ListTimezones), else the ones of
 * /usr/share/zoneinfo/tzdata.zi, zone1970.tab or zone.tab: valid names, sorted, one per line, in a malloc'd string the
 * caller free()s; NULL when none is found. Blocks (D-Bus, file reads): a worker thread. */
char *cp0_timedate_list_timezones(void);
/* After a zone change: this process's local time follows the new zone at once (tzset; glibc's localtime_r keeps the
 * zone it read first otherwise). LVGL thread. */
void cp0_timedate_zone_changed(void);
#ifdef __cplusplus
}
#endif
