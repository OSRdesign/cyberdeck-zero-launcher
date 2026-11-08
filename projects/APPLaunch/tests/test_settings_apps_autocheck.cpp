/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps, the check for app updates when the page opens (settings_apps_autocheck): the setting's stored value,
 * the backend's saved time, which sources are due (cache age against the TTL, setting Off, disabled sources, the
 * attempts of this run, a clock that moved), the opening's plan (offline, nothing due, the last result), the forced
 * sync that ignores all of it, and the status lines.
 */

#include "../main/ui/settings/settings_apps_autocheck.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace apps_autocheck;

namespace {

/* 2026-11-08T16:22:00Z */
constexpr long long kT = 1794154920LL;

SourceState source(const std::string &url, long long saved, bool enabled = true, bool has_list = true)
{
    SourceState state;
    state.url = url;
    state.saved = saved;
    state.enabled = enabled;
    state.has_list = has_list;
    return state;
}

Input input_at(long long now, std::vector<SourceState> sources, bool setting_on = true)
{
    Input input;
    input.now = now;
    input.sources = std::move(sources);
    input.setting_on = setting_on;
    return input;
}

void setting()
{
    assert(parse_setting(""));       /* absent: On (the default) */
    assert(parse_setting("1"));
    assert(!parse_setting("0"));
    assert(!parse_setting(" 0\n"));
    assert(parse_setting("2"));      /* unknown values: On */
    assert(parse_setting("off"));    /* the config service stores integers; anything else is not "0" */
    assert(std::string(kConfigKey) == "apps_auto_check");
    assert(kTtlSeconds == 600);
}

void times()
{
    /* the backend's own format (now_text: %Y-%m-%dT%H:%M:%S%z, local time with its offset) */
    assert(parse_time("2026-11-08T16:22:00+0000") == kT);
    assert(parse_time("2026-11-08T17:22:00+0100") == kT);
    assert(parse_time("2026-11-08T11:22:00-0500") == kT);
    assert(parse_time("2026-11-08T16:22:00Z") == kT);
    assert(parse_time("2026-11-08T21:52:00+05:30") == kT);
    assert(parse_time("2026-11-08 16:22") == kT);            /* no seconds, a space, no zone: UTC */
    assert(parse_time("2026-11-08T16:22:00.123Z") == kT);    /* fractions ignored */
    assert(parse_time(" 2026-11-08T16:22:00+0000\n") == kT);
    assert(parse_time("1970-01-01T00:00:00Z") == 0);
    assert(parse_time("2024-02-29T00:00:00Z") == 1709164800LL); /* a leap day */
    assert(parse_time("2026-11-09T00:00:00+0000") - parse_time("2026-11-08T00:00:00+0000") == 86400);
    /* unreadable: -1 (the source is then due) */
    assert(parse_time("") == -1);
    assert(parse_time("yesterday") == -1);
    assert(parse_time("2026-13-08T16:22:00Z") == -1);
    assert(parse_time("2026-11-08T25:22:00Z") == -1);
    assert(parse_time("2026-11-08") == -1);
    assert(parse_time("2026-11-08T16:22:00+00") == -1);
    assert(parse_time("2026-11-08T16:22:00 trailing") == -1);
}

void source_states()
{
    apps_backend::Source ok;
    ok.url = "u1";
    ok.status = "ok";
    ok.synced_at = "2026-11-08T16:22:00+0000";
    SourceState state = source_state(ok);
    assert(state.url == "u1" && state.enabled && state.has_list && state.saved == kT);

    apps_backend::Source cached = ok; /* a failed refresh keeps the last good list and its time */
    cached.status = "cached";
    assert(source_state(cached).has_list && source_state(cached).saved == kT);

    apps_backend::Source error = ok; /* never fetched: the time is the failed attempt's, not a list's */
    error.status = "error";
    assert(!source_state(error).has_list && source_state(error).saved == -1);

    apps_backend::Source never = ok;
    never.status = "not synced";
    never.synced_at.clear();
    assert(!source_state(never).has_list && source_state(never).saved == -1);

    apps_backend::Source off = ok;
    off.enabled = false;
    assert(!source_state(off).enabled);
}

void ttl()
{
    assert(!stale(kT, kT));                      /* just saved */
    assert(!stale(kT, kT + 599));                /* 9 min 59 s */
    assert(stale(kT, kT + 600));                 /* 10 min: due */
    assert(stale(kT, kT + 7 * 3600));            /* the Pi 4B case: saved at 16:22, opened 7 h later */
    assert(stale(-1, kT));                       /* unknown age */
    assert(!stale(kT + 200, kT));                /* a little ahead (clock skew): fresh */
    assert(stale(kT + 3600, kT));                /* far ahead: the clock moved, unknown */
    assert(!stale(kT, kT + 100, 120) && stale(kT, kT + 120, 120)); /* the TTL is an input */
}

void due_sources()
{
    Memory memory;
    /* the cache age decides, per source */
    Input input = input_at(kT + 700, {source("old", kT), source("fresh", kT + 500), source("never", -1, true, false)});
    std::vector<std::string> urls = due(input, memory);
    assert((urls == std::vector<std::string>{"old", "never"}));

    /* the setting Off: nothing, ever */
    input.setting_on = false;
    assert(due(input, memory).empty());
    input.setting_on = true;

    /* a disabled source is never contacted, however old */
    input = input_at(kT + 7200, {source("on", kT), source("off", kT, false), source("off-never", -1, false, false)});
    assert((due(input, memory) == std::vector<std::string>{"on"}));

    /* tried within the TTL in this run (it failed: its saved time did not move): not again */
    input = input_at(kT + 7200, {source("a", kT), source("b", kT)});
    record_attempts(memory, {"a"}, kT + 7000);
    assert((due(input, memory) == std::vector<std::string>{"b"}));
    input.now = kT + 7000 + 599;
    assert((due(input, memory) == std::vector<std::string>{"b"}));
    input.now = kT + 7000 + 600; /* the TTL passed: tried again */
    assert((due(input, memory) == std::vector<std::string>{"a", "b"}));
    input.now = kT + 6000;       /* the clock went back before the attempt: the attempt does not count */
    assert((due(input, memory) == std::vector<std::string>{"a", "b"}));
}

void forced()
{
    /* Sync my sources / S: every enabled source, whatever the setting, the TTL and the attempts */
    Memory memory;
    record_attempts(memory, {"a", "b"}, kT);
    Input input = input_at(kT + 1, {source("a", kT), source("b", kT + 1), source("off", kT, false)}, false);
    input.forced = true;
    assert((due(input, memory) == std::vector<std::string>{"a", "b"}));
    /* nor the route: the sync reports its own failures */
    Plan p = plan(input, memory, false);
    assert((p.refresh == std::vector<std::string>{"a", "b"}) && p.shown == Shown::None);
    input.setting_on = true;
    p = plan(input, memory, true);
    assert((p.refresh == std::vector<std::string>{"a", "b"}) && p.shown == Shown::None);
}

void plans()
{
    Memory memory;
    /* stale and online: refresh in the background, "Checking..." meanwhile */
    Input input = input_at(kT + 7 * 3600, {source("a", kT), source("off", kT, false)});
    Plan p = plan(input, memory, true);
    assert((p.refresh == std::vector<std::string>{"a"}) && p.shown == Shown::Checking);

    /* stale and offline (no default route): no network use, the offline line */
    p = plan(input, memory, false);
    assert(p.refresh.empty() && p.shown == Shown::Offline);

    /* the setting Off: today's behaviour, nothing at all */
    input.setting_on = false;
    p = plan(input, memory, true);
    assert(p.refresh.empty() && p.shown == Shown::None);
    input.setting_on = true;

    /* nothing due: the list's age, no network use (online or not) */
    input = input_at(kT + 180, {source("a", kT), source("b", kT - 3000)});
    p = plan(input, memory, true);
    assert(p.refresh.size() == 1 && p.refresh[0] == "b"); /* b is old */
    input = input_at(kT + 180, {source("a", kT), source("b", kT + 100)});
    p = plan(input, memory, true);
    assert(p.refresh.empty() && p.shown == Shown::Fresh);
    p = plan(input, memory, false);
    assert(p.refresh.empty() && p.shown == Shown::Fresh);

    /* no enabled source: nothing to say */
    input = input_at(kT, {source("off", kT, false)});
    p = plan(input, memory, true);
    assert(p.refresh.empty() && p.shown == Shown::None);
    input = input_at(kT, {});
    assert(plan(input, memory, true).shown == Shown::None);

    /* the last refresh of this run failed (everything tried within the TTL): the offline line, no new attempt */
    input = input_at(kT + 3600, {source("a", kT)});
    record_attempts(memory, {"a"}, kT + 3500);
    record_outcome(memory, outcome(0), kT + 3500);
    p = plan(input, memory, true);
    assert(p.refresh.empty() && p.shown == Shown::Offline);
    /* ...unless a newer list came since (a forced sync succeeded): its age */
    input = input_at(kT + 3600, {source("a", kT + 3550)});
    p = plan(input, memory, true);
    assert(p.refresh.empty() && p.shown == Shown::Fresh);
    /* a successful refresh: the age line on the next openings within the TTL */
    Memory good;
    record_attempts(good, {"a"}, kT);
    record_outcome(good, outcome(1), kT);
    input = input_at(kT + 120, {source("a", kT)});
    p = plan(input, good, true);
    assert(p.refresh.empty() && p.shown == Shown::Fresh);
    /* once per opening and not again within the TTL: the second opening a minute later does not refresh */
    Memory once;
    input = input_at(kT + 3600, {source("a", kT)});
    p = plan(input, once, true);
    assert(p.refresh.size() == 1);
    record_attempts(once, p.refresh, input.now);
    record_outcome(once, outcome(1), input.now);
    input.sources[0].saved = input.now; /* the refresh saved a new list */
    input.now += 60;
    p = plan(input, once, true);
    assert(p.refresh.empty() && p.shown == Shown::Fresh);
}

void outcomes()
{
    assert(outcome(1) == Outcome::Fresh && outcome(3) == Outcome::Fresh);
    assert(outcome(0) == Outcome::Offline);
    assert(shown_for(Outcome::Fresh) == Shown::Fresh && shown_for(Outcome::Offline) == Shown::Offline);
    assert(shown_for(Outcome::None) == Shown::None);
    Memory memory;
    record_outcome(memory, Outcome::Fresh, kT);
    assert(memory.last == Outcome::Fresh && memory.last_at == kT);

    /* the newest list among the enabled sources that have one */
    assert(list_time({source("a", kT - 100), source("b", kT), source("off", kT + 50, false),
                      source("never", -1, true, false)}) == kT);
    assert(list_time({source("never", -1, true, false)}) == -1);
    assert(list_time({}) == -1);
}

void lines()
{
    assert(age_text(0) == "1 min" && age_text(59) == "1 min" && age_text(119) == "1 min");
    assert(age_text(120) == "2 min" && age_text(59 * 60 + 59) == "59 min");
    assert(age_text(3600) == "1 h" && age_text(7 * 3600 + 1800) == "7 h" && age_text(47 * 3600) == "47 h");
    assert(age_text(48 * 3600) == "2 days" && age_text(30 * 86400) == "30 days");
    assert(age_text(-5) == "1 min");

    assert(status_text(Shown::None, kT, kT).empty());
    assert(status_text(Shown::Checking, kT, kT + 99) == "Checking for updates...");
    assert(status_text(Shown::Fresh, kT, kT) == "Up to date (just now)");
    assert(status_text(Shown::Fresh, kT, kT + 59) == "Up to date (just now)");
    assert(status_text(Shown::Fresh, kT, kT + 60) == "Updated 1 min ago");
    assert(status_text(Shown::Fresh, kT, kT + 180) == "Updated 3 min ago");
    assert(status_text(Shown::Fresh, kT, kT + 2 * 3600) == "Updated 2 h ago");
    assert(status_text(Shown::Fresh, -1, kT) == "Up to date (just now)");
    assert(status_text(Shown::Fresh, kT + 30, kT) == "Up to date (just now)"); /* a little ahead */
    assert(status_text(Shown::Offline, kT, kT + 3600) == "Offline: showing the list from 1 h ago");
    assert(status_text(Shown::Offline, kT, kT + 7 * 3600) == "Offline: showing the list from 7 h ago");
    assert(status_text(Shown::Offline, kT, kT + 25 * 60) == "Offline: showing the list from 25 min ago");
    assert(status_text(Shown::Offline, kT, kT + 3 * 86400) == "Offline: showing the list from 3 days ago");
    assert(status_text(Shown::Offline, -1, kT) == "Offline: showing the saved list");
}

} // namespace

int main()
{
    setting();
    times();
    source_states();
    ttl();
    due_sources();
    forced();
    plans();
    outcomes();
    lines();
    std::printf("test_settings_apps_autocheck: all passed\n");
    return 0;
}
