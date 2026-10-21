#include "cp0_timedate_client.hpp"
#include "cp0_timezone.hpp"
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>
#if !defined(HAL_PLATFORM_SDL)
#include <gio/gio.h>
#endif

namespace {
/* The zone files timedatectl itself falls back to, in its order. */
std::vector<std::string> zones_from_files() {
    struct Source { const char *path; std::vector<std::string> (*parse)(std::string_view); };
    static const Source sources[] = {{"/usr/share/zoneinfo/tzdata.zi", cp0::tz::parse_tzdata_zi},
                                     {"/usr/share/zoneinfo/zone1970.tab", cp0::tz::parse_zone_tab},
                                     {"/usr/share/zoneinfo/zone.tab", cp0::tz::parse_zone_tab}};
    for (const Source &source : sources) {
        std::ifstream file(source.path, std::ios::binary);
        if (!file) continue;
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::vector<std::string> zones = source.parse(text);
        if (!zones.empty()) return zones;
    }
    return {};
}
char *joined(const std::vector<std::string> &zones) {
    if (zones.empty()) return nullptr;
    std::string text;
    for (const std::string &zone : zones) { text += zone; text.push_back('\n'); }
    char *out = static_cast<char *>(std::malloc(text.size() + 1));
    if (out) std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}
std::string zone_from_localtime() {
    char target[512];
    const ssize_t n = readlink("/etc/localtime", target, sizeof(target) - 1);
    if (n <= 0) return {};
    target[n] = '\0';
    return cp0::tz::zone_from_link(target);
}
int copy_zone(const std::string &name, char *zone, int size) {
    if (name.empty() || !cp0::tz::valid_name(name)) return -1;
    if (name.size() + 1 > static_cast<std::size_t>(size)) return -34;
    std::memcpy(zone, name.c_str(), name.size() + 1);
    return 0;
}
}

void cp0_timedate_zone_changed(void) { tzset(); }

#if defined(HAL_PLATFORM_SDL)
int cp0_timedate_get_ntp(int *) { return -1; }
int cp0_timedate_get_time(int[6]) { return -1; }
int cp0_timedate_set_ntp(int) { return -1; }
int cp0_timedate_set_time(const char *) { return -1; }
int cp0_timedate_get_timezone(char *zone, int size) {
    if (!zone || size <= 0) return -22;
    zone[0] = '\0';
    return copy_zone(zone_from_localtime(), zone, size);
}
int cp0_timedate_set_timezone(const char *) { return -1; }
char *cp0_timedate_list_timezones(void) { return joined(zones_from_files()); }
#else
namespace {
constexpr const char *bus = "org.freedesktop.timedate1";
constexpr const char *path = "/org/freedesktop/timedate1";
constexpr const char *iface = "org.freedesktop.timedate1";
GDBusConnection *connection(GError **e) { return g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, e); }
GVariant *get_property(GDBusConnection *c, const char *name, GError **e) {
    return g_dbus_connection_call_sync(c, bus, path, "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", iface, name), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, e);
}
/* A failed timedated call: -13 when PolicyKit refused it (non-interactive call, no agent), -22 an argument timedated
 * refuses (a zone it does not know), -1 anything else. */
int call_error(GError *e) {
    if (!e) return -1;
    int rc = -1;
    if (g_dbus_error_is_remote_error(e)) {
        gchar *name = g_dbus_error_get_remote_error(e);
        if (name && (std::strcmp(name, "org.freedesktop.DBus.Error.AccessDenied") == 0 ||
                     std::strcmp(name, "org.freedesktop.DBus.Error.InteractiveAuthorizationRequired") == 0))
            rc = -13;
        else if (name && std::strcmp(name, "org.freedesktop.DBus.Error.InvalidArgs") == 0)
            rc = -22;
        g_free(name);
    } else if (g_error_matches(e, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED)) {
        rc = -13;
    }
    return rc;
}
}
int cp0_timedate_get_ntp(int *enabled) {
    if (!enabled) return -22; GError *e = nullptr; auto *c = connection(&e); if (!c) { g_clear_error(&e); return -1; }
    auto *r = get_property(c, "NTP", &e); GVariant *v = nullptr;
    if (r) g_variant_get(r, "(v)", &v);
    const bool ok = v && g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN);
    if (ok) *enabled = g_variant_get_boolean(v);
    if (v) g_variant_unref(v); if (r) g_variant_unref(r); g_clear_error(&e); g_object_unref(c); return ok ? 0 : -1;
}
int cp0_timedate_get_time(int values[6]) {
    if (!values) return -22; GError *e = nullptr; auto *c = connection(&e); if (!c) { g_clear_error(&e); return -1; }
    auto *r = get_property(c, "TimeUSec", &e); GVariant *v = nullptr; guint64 us = 0;
    if (r) g_variant_get(r, "(v)", &v);
    const bool ok = v && g_variant_is_of_type(v, G_VARIANT_TYPE_UINT64);
    if (ok) us = g_variant_get_uint64(v);
    if (v) g_variant_unref(v); if (r) g_variant_unref(r); g_clear_error(&e); g_object_unref(c);
    if (!ok) return -1; std::time_t t = static_cast<std::time_t>(us / 1000000ULL); std::tm tm{};
    if (!localtime_r(&t, &tm)) return -1;
    values[0] = tm.tm_year + 1900; values[1] = tm.tm_mon + 1; values[2] = tm.tm_mday; values[3] = tm.tm_hour; values[4] = tm.tm_min; values[5] = tm.tm_sec; return 0;
}
int cp0_timedate_set_ntp(int enabled) {
    GError *e = nullptr; auto *c = connection(&e); if (!c) { g_clear_error(&e); return -1; }
    auto *r = g_dbus_connection_call_sync(c, bus, path, iface, "SetNTP", g_variant_new("(bb)", enabled, false), nullptr, G_DBUS_CALL_FLAGS_NONE, 30000, nullptr, &e);
    const bool ok = r != nullptr; if (r) g_variant_unref(r); g_clear_error(&e); g_object_unref(c); return ok ? 0 : -1;
}
int cp0_timedate_set_time(const char *timestamp) {
    if (!timestamp) return -22; const std::string s(timestamp);
    std::tm tm{}; if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return -22;
    tm.tm_year -= 1900; --tm.tm_mon; tm.tm_isdst = -1; const auto t = std::mktime(&tm); if (t < 0) return -22;
    GError *e = nullptr; auto *c = connection(&e); if (!c) { g_clear_error(&e); return -1; }
    auto *r = g_dbus_connection_call_sync(c, bus, path, iface, "SetTime", g_variant_new("(xbb)", static_cast<gint64>(t) * 1000000, false, false), nullptr, G_DBUS_CALL_FLAGS_NONE, 30000, nullptr, &e);
    const bool ok = r != nullptr; if (r) g_variant_unref(r); g_clear_error(&e); g_object_unref(c); return ok ? 0 : -1;
}
int cp0_timedate_get_timezone(char *zone, int size) {
    if (!zone || size <= 0) return -22;
    zone[0] = '\0';
    std::string name = zone_from_localtime();
    if (name.empty()) {
        GError *e = nullptr; auto *c = connection(&e);
        if (c) {
            auto *r = get_property(c, "Timezone", &e); GVariant *v = nullptr;
            if (r) g_variant_get(r, "(v)", &v);
            if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) name = g_variant_get_string(v, nullptr);
            if (v) g_variant_unref(v); if (r) g_variant_unref(r); g_object_unref(c);
        }
        g_clear_error(&e);
    }
    return copy_zone(name, zone, size);
}
int cp0_timedate_set_timezone(const char *zone) {
    if (!zone || !cp0::tz::valid_name(zone)) return -22;
    GError *e = nullptr; auto *c = connection(&e); if (!c) { g_clear_error(&e); return -1; }
    auto *r = g_dbus_connection_call_sync(c, bus, path, iface, "SetTimezone", g_variant_new("(sb)", zone, false), nullptr, G_DBUS_CALL_FLAGS_NONE, 30000, nullptr, &e);
    const int rc = r ? 0 : call_error(e);
    if (r) g_variant_unref(r); g_clear_error(&e); g_object_unref(c); return rc;
}
char *cp0_timedate_list_timezones(void) {
    std::vector<std::string> zones;
    GError *e = nullptr; auto *c = connection(&e);
    if (c) {
        auto *r = g_dbus_connection_call_sync(c, bus, path, iface, "ListTimezones", nullptr, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &e);
        if (r) {
            GVariant *list = g_variant_get_child_value(r, 0);
            gsize count = 0;
            const gchar **names = g_variant_get_strv(list, &count);
            std::string text;
            for (gsize i = 0; names && i < count; ++i) { text += names[i]; text.push_back('\n'); }
            g_free(names); g_variant_unref(list); g_variant_unref(r);
            zones = cp0::tz::parse_list(text);
        }
        g_object_unref(c);
    }
    g_clear_error(&e);
    if (zones.empty()) zones = zones_from_files(); /* an older timedated without ListTimezones, or none running */
    return joined(zones);
}
#endif
