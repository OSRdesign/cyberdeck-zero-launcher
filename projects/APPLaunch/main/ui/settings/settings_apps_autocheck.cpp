/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps, the check for app updates when the page opens (see settings_apps_autocheck.hpp).
 */

#include "settings_apps_autocheck.hpp"

#include <algorithm>
#include <cctype>

namespace apps_autocheck {

namespace {

/* Howard Hinnant's days_from_civil: days since 1970-01-01 of a proleptic Gregorian date. */
long long days_from_civil(long long year, int month, int day)
{
    year -= month <= 2 ? 1 : 0;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const long long yoe = year - era * 400;
    const long long doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* `count` digits at `pos` (advanced); false when they are not all digits. */
bool digits(const std::string &text, std::size_t &pos, std::size_t count, int &value)
{
    if (pos + count > text.size()) return false;
    value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[pos + i];
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        value = value * 10 + (c - '0');
    }
    pos += count;
    return true;
}

bool expect(const std::string &text, std::size_t &pos, char c)
{
    if (pos >= text.size() || text[pos] != c) return false;
    ++pos;
    return true;
}

} // namespace

bool parse_setting(const std::string &text)
{
    std::string value;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) value.push_back(c);
    return value != "0";
}

long long parse_time(const std::string &text)
{
    std::size_t pos = 0;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!digits(text, pos, 4, year) || !expect(text, pos, '-') || !digits(text, pos, 2, month) ||
        !expect(text, pos, '-') || !digits(text, pos, 2, day))
        return -1;
    if (pos >= text.size() || (text[pos] != 'T' && text[pos] != ' ')) return -1;
    ++pos;
    if (!digits(text, pos, 2, hour) || !expect(text, pos, ':') || !digits(text, pos, 2, minute)) return -1;
    if (pos < text.size() && text[pos] == ':' && !(++pos, digits(text, pos, 2, second))) return -1;
    if (pos < text.size() && text[pos] == '.') { /* fractions of a second: ignored */
        ++pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) return -1;
    long long offset = 0; /* seconds east of UTC */
    if (pos < text.size() && (text[pos] == 'Z' || text[pos] == 'z')) {
        ++pos;
    } else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
        const int sign = text[pos] == '-' ? -1 : 1;
        ++pos;
        int zone_h = 0, zone_m = 0;
        if (!digits(text, pos, 2, zone_h)) return -1;
        if (pos < text.size() && text[pos] == ':') ++pos;
        if (!digits(text, pos, 2, zone_m) || zone_h > 23 || zone_m > 59) return -1;
        offset = sign * (zone_h * 3600LL + zone_m * 60LL);
    }
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos != text.size()) return -1;
    return days_from_civil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL + second - offset;
}

SourceState source_state(const apps_backend::Source &source)
{
    SourceState state;
    state.url = source.url;
    state.enabled = source.enabled;
    state.has_list = source.status == "ok" || source.status == "cached";
    /* an "error" record carries the time of the failed attempt there, not of a list */
    state.saved = state.has_list ? parse_time(source.synced_at) : -1;
    return state;
}

bool stale(long long saved, long long now, long long ttl)
{
    if (saved < 0) return true;
    const long long age = now - saved;
    if (age < -kClockSkewSeconds) return true; /* saved in the future: the clock moved, the age is unknown */
    return age >= ttl;
}

std::vector<std::string> due(const Input &input, const Memory &memory)
{
    std::vector<std::string> urls;
    if (!input.forced && !input.setting_on) return urls;
    for (const SourceState &source : input.sources) {
        if (!source.enabled) continue; /* never contacted */
        if (input.forced) {
            urls.push_back(source.url);
            continue;
        }
        if (!stale(source.saved, input.now, input.ttl)) continue;
        const auto tried = memory.attempted.find(source.url);
        if (tried != memory.attempted.end() && input.now >= tried->second && input.now - tried->second < input.ttl)
            continue; /* tried within the TTL (a failure is not retried at every opening) */
        urls.push_back(source.url);
    }
    return urls;
}

long long list_time(const std::vector<SourceState> &sources)
{
    long long newest = -1;
    for (const SourceState &source : sources)
        if (source.enabled && source.has_list && source.saved >= 0) newest = std::max(newest, source.saved);
    return newest;
}

Plan plan(const Input &input, const Memory &memory, bool network_up)
{
    Plan result;
    if (input.forced) {
        result.refresh = due(input, memory);
        return result;
    }
    if (!input.setting_on) return result;
    const bool any = std::any_of(input.sources.begin(), input.sources.end(),
                                 [](const SourceState &source) { return source.enabled; });
    if (!any) return result; /* no source to speak of */
    result.refresh = due(input, memory);
    if (!result.refresh.empty()) {
        if (network_up) {
            result.shown = Shown::Checking;
        } else {
            result.refresh.clear();
            result.shown = Shown::Offline;
        }
        return result;
    }
    const long long newest = list_time(input.sources);
    // the last refresh of this run failed and nothing newer came since (a forced sync would have brought a newer list)
    if (memory.last == Outcome::Offline && memory.last_at >= 0 && (newest < 0 || memory.last_at >= newest))
        result.shown = Shown::Offline;
    else if (newest >= 0)
        result.shown = Shown::Fresh;
    return result;
}

void record_attempts(Memory &memory, const std::vector<std::string> &urls, long long now)
{
    for (const std::string &url : urls) memory.attempted[url] = now;
}

Outcome outcome(int refreshed)
{
    return refreshed > 0 ? Outcome::Fresh : Outcome::Offline;
}

void record_outcome(Memory &memory, Outcome result, long long now)
{
    memory.last = result;
    memory.last_at = now;
}

Shown shown_for(Outcome result)
{
    switch (result) {
    case Outcome::Fresh: return Shown::Fresh;
    case Outcome::Offline: return Shown::Offline;
    case Outcome::None: break;
    }
    return Shown::None;
}

std::string age_text(long long seconds)
{
    const long long minutes = std::max(0LL, seconds) / 60;
    if (minutes < 60) return std::to_string(std::max(1LL, minutes)) + " min";
    const long long hours = minutes / 60;
    if (hours < 48) return std::to_string(hours) + " h";
    return std::to_string(hours / 24) + " days";
}

std::string status_text(Shown shown, long long list_time, long long now)
{
    switch (shown) {
    case Shown::None:
        return std::string();
    case Shown::Checking:
        return "Checking for updates...";
    case Shown::Fresh:
        if (list_time < 0 || now - list_time < 60) return "Up to date (just now)";
        return "Updated " + age_text(now - list_time) + " ago";
    case Shown::Offline:
        if (list_time < 0) return "Offline: showing the saved list";
        return "Offline: showing the list from " + age_text(now - list_time) + " ago";
    }
    return std::string();
}

} // namespace apps_autocheck
