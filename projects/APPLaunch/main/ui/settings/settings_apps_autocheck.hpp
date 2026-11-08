/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps: the check for app updates when the page opens (pure: no I/O, no thread, no LVGL;
 * tests/test_settings_apps_autocheck.cpp). Before it, the page showed the lists the Store backend cached at the last sync
 * until the user ran Sources > Sync my sources (key S); a published update stayed hidden until then.
 *
 *   setting   "Auto-check updates" (Settings > Apps > Sources, value On / Off), config key kConfigKey: 0 Off, 1 On;
 *             absent = On (written only when the user changes it). Off: the page behaves as before (no check, no line).
 *   due       an enabled source whose list is older than kTtlSeconds (the backend's own saved time, the --registries
 *             synced_at field; unknown or unreadable: due), not tried automatically within kTtlSeconds in this launcher
 *             run. A disabled source is never contacted. GitHub's raw files are cached 5 min: a shorter TTL gains
 *             nothing.
 *   plan      once per opening, after the cached lists are shown: nothing due -> no network use, the line tells the list's
 *             age; something due and a default route -> those sources are refreshed in the background ("Checking for
 *             updates..."); something due and no route -> no network use, "Offline: ...".
 *   forced    Sync my sources, S and a source's own Sync: every enabled source, whatever the setting, the TTL, the
 *             attempts and the route (the sync's own progress and errors, as before).
 *   lines     "Checking for updates...", "Up to date (just now)", "Updated 3 min ago", "Updated just now (1 failed)"
 *             (some sources came back, one did not), "Offline: showing the list from 2 h ago" (none came back, or no
 *             route; "Offline: showing the saved list" when the list's age is unknown). The age is the newest saved
 *             time of the enabled sources that have a list; the page keeps it current while it stays open.
 *
 * The backend has no conditional requests (If-None-Match / If-Modified-Since): a refresh downloads the registry again
 * (with a cache-busting query) and only the icons it does not have yet.
 */
#pragma once

#include "apps_backend.hpp"

#include <map>
#include <string>
#include <vector>

namespace apps_autocheck {

constexpr const char *kConfigKey = "apps_auto_check"; /* 0 Off, 1 On; absent = On */
constexpr long long kTtlSeconds = 600;                /* 10 min */
/* One source's refresh at most (the backend's own connect timeout is 10 s; the same call fetches the icons of new apps) */
constexpr int kRefreshTimeoutSeconds = 20;
/* A saved time this far ahead of the clock is not trusted (the clock moved): the source is due. */
constexpr long long kClockSkewSeconds = 300;

/* The setting's stored text: "0" is Off; anything else (absent, "1", unreadable) is On. */
bool parse_setting(const std::string &text);

/* The backend's saved time (strftime "%Y-%m-%dT%H:%M:%S%z", e.g. "2026-11-08T16:22:00+0000"; also "Z", "+01:00", a
 * space instead of the T, no seconds; no zone: UTC) -> epoch seconds, -1 when it cannot be read. */
long long parse_time(const std::string &text);

struct SourceState {
    std::string url;
    bool enabled = true;
    bool has_list = false; /* its last good list is shown ("ok", or "cached" after a failed refresh) */
    long long saved = -1;  /* epoch of that list, -1 unknown */
};

/* From the backend's --registries record. */
SourceState source_state(const apps_backend::Source &source);

enum class Outcome { None, Fresh, Offline };

/* What this launcher run remembers between openings of the page (the page is created anew each time). */
struct Memory {
    std::map<std::string, long long> attempted; /* url -> wall time of its last automatic refresh */
    Outcome last = Outcome::None;               /* the last automatic refresh's result */
    long long last_at = -1;
    int last_failed = 0;                        /* sources that failed in it (Fresh: some came back, these did not) */
};

struct Input {
    bool setting_on = true;
    bool forced = false;
    long long now = 0; /* wall clock, epoch seconds */
    long long ttl = kTtlSeconds;
    std::vector<SourceState> sources;
};

/* A list saved at `saved` is too old at `now` (or of unknown age). */
bool stale(long long saved, long long now, long long ttl = kTtlSeconds);

/* The sources to refresh: forced -> every enabled one; setting Off -> none; else the enabled ones that are stale and were
 * not tried automatically within the TTL. In the order given. */
std::vector<std::string> due(const Input &input, const Memory &memory);

/* The newest saved time of the enabled sources that have a list, -1 when none. */
long long list_time(const std::vector<SourceState> &sources);

/* What the status line says. */
enum class Shown { None, Checking, Fresh, Offline };

struct Plan {
    std::vector<std::string> refresh; /* contact these now; empty: no network use */
    Shown shown = Shown::None;        /* the line meanwhile (Checking) or instead */
    int failed = 0;                   /* Fresh: sources that failed in this run's last refresh ("(1 failed)") */
};

/* The opening's decision (forced: the sync's sources, nothing shown). */
Plan plan(const Input &input, const Memory &memory, bool network_up);

/* The attempts are recorded when the refresh starts, the result when it ends. */
void record_attempts(Memory &memory, const std::vector<std::string> &urls, long long now);
/* refreshed: sources that came back with a new list. At least one: Fresh; none: Offline. */
Outcome outcome(int refreshed);
/* failed: the sources of the refresh that did not come back (shown with a Fresh result) */
void record_outcome(Memory &memory, Outcome result, long long now, int failed = 0);
Shown shown_for(Outcome result);

/* "1 min" (under 2 min, at least 1), "59 min", "1 h", "47 h", "2 days". */
std::string age_text(long long seconds);
/* The line for `shown`, the list's saved time (-1 unknown) and the clock. Fresh with failed > 0 (some sources came back,
 * `failed` did not): "Updated just now (1 failed)", "Updated 3 min ago (2 failed)" instead of "Up to date (just now)". */
std::string status_text(Shown shown, long long list_time, long long now, int failed = 0);

} // namespace apps_autocheck
