/*
 * SPDX-License-Identifier: MIT
 *
 * Map engine, online sources without any network (cp0_map_source.h, cp0_map_store.h; task 3 of Deck Atlas 0.1):
 *   - the URL rules: HTTPS only, plain http only to 127.0.0.1 / localhost / [::1], no user info, the OSM Foundation's
 *     hosts refused in every spelling (subdomains, case, a trailing dot, a port, behind user info), placeholders;
 *   - URL filling and the "***" form for messages, log redaction;
 *   - the User-Agent rule and its builder;
 *   - the shipped records (listed, off, their flags and notes) and the record rules: a user file only tightens a
 *     shipped record, a record on a shipped host gets its ceilings, a bad record is refused;
 *   - API keys: mode 0600 whatever the umask, never in a listing, a loose file closed first, bad keys refused;
 *   - switches (enabled, daily_cap lowered only), area download refused / not in this version;
 *   - HTTP header parsing, dates, freshness floor and ceiling, Retry-After;
 *   - the cap rule (the smaller of 512 MiB and 10 % of free space), the local day.
 */
#define _GNU_SOURCE /* memmem */
#include "map_test_util.h"
#include "cp0_map_online.h"

#include <sys/stat.h>

static void test_url_rules(void)
{
    CHECK_RC(cp0_map_source_check_url("https://tile.example.com/{z}/{x}/{y}.png"), CP0_MAP_OK);
    CHECK_RC(cp0_map_source_check_url("https://tile.example.com:8443/a/{z}/{x}/{y}@2x.png?k={key}"), CP0_MAP_OK);
    CHECK_RC(cp0_map_source_check_url("http://tile.example.com/{z}/{x}/{y}.png"), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_source_check_url("HTTP://tile.example.com/{z}/{x}/{y}.png"), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_source_check_url("ftp://tile.example.com/{z}/{x}/{y}.png"), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_source_check_url("file:///tiles/{z}/{x}/{y}.png"), CP0_MAP_E_INVALID);
    /* the local-host exception (the fake tile server of the tests) */
    CHECK_RC(cp0_map_source_check_url("http://127.0.0.1:8080/{z}/{x}/{y}.png"), CP0_MAP_OK);
    CHECK_RC(cp0_map_source_check_url("http://localhost/{z}/{x}/{y}.png"), CP0_MAP_OK);
    CHECK_RC(cp0_map_source_check_url("http://[::1]:9/{z}/{x}/{y}.png"), CP0_MAP_OK);
    CHECK_RC(cp0_map_source_check_url("http://127.0.0.2/{z}/{x}/{y}.png"), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_source_check_url("http://192.168.1.10/{z}/{x}/{y}.png"), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_source_check_url("http://localhost.example.com/{z}/{x}/{y}.png"), CP0_MAP_E_REFUSED);
    /* user info: never credentials, never a disguised host */
    CHECK_RC(cp0_map_source_check_url("https://user:pw@tile.example.com/{z}/{x}/{y}.png"), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_source_check_url("http://127.0.0.1@tile.example.com/{z}/{x}/{y}.png"), CP0_MAP_E_REFUSED);
    /* the OSM Foundation's servers, in every spelling */
    static const char *const osmf[] = {
        "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
        "https://a.tile.openstreetmap.org/{z}/{x}/{y}.png",
        "https://TILE.OpenStreetMap.ORG/{z}/{x}/{y}.png",
        "https://tile.openstreetmap.org./{z}/{x}/{y}.png",
        "https://tile.openstreetmap.org:443/{z}/{x}/{y}.png",
        "https://vector.openstreetmap.org/shortbread_v1/{z}/{x}/{y}.mvt",
        "https://openstreetmap.org/{z}/{x}/{y}.png",
        "https://tile.osm.org/{z}/{x}/{y}.png",
        "https://osm.org/{z}/{x}/{y}.png",
        "https://example.com@tile.openstreetmap.org/{z}/{x}/{y}.png",
    };
    for (size_t i = 0; i < sizeof osmf / sizeof osmf[0]; i++) CHECK_RC(cp0_map_source_check_url(osmf[i]), CP0_MAP_E_REFUSED);
    CHECK(cp0_map_source_osmf_host("tile.openstreetmap.org") && cp0_map_source_osmf_host("OSM.ORG."));
    CHECK(!cp0_map_source_osmf_host("openstreetmap.org.example.com") && !cp0_map_source_osmf_host("notosm.org"));
    CHECK(!cp0_map_source_osmf_host("tile.openstreetmap.fr") && !cp0_map_source_osmf_host("tile.opentopomap.org"));
    /* a concrete URL (a redirect target) */
    CHECK_RC(cp0_map__url_check_concrete("https://tile.openstreetmap.org/1/0/0.png"), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map__url_check_concrete("http://example.invalid/1/0/0.png"), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map__url_check_concrete("http://127.0.0.1:5/normal/1/0/0.png"), CP0_MAP_OK);
    CHECK_RC(cp0_map__url_check_concrete("https://maps.example.com/1/0/0.png?apiKey=abc"), CP0_MAP_OK);
    /* placeholders and shape */
    CHECK_RC(cp0_map_source_check_url("https://t.example.com/{z}/{x}.png"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_check_url("https://t.example.com/{z}/{x}/{y}/{s}.png"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_check_url("https://{key}.example.com/{z}/{x}/{y}.png"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_check_url("https://t.example.com/{z}/{x}/{y}.png }"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_check_url("https://t.ex%61mple.com/{z}/{x}/{y}.png"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_check_url("https:///{z}/{x}/{y}.png"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_check_url(NULL), CP0_MAP_E_INVALID);
    char longurl[400];
    memset(longurl, 'a', sizeof longurl - 1);
    longurl[sizeof longurl - 1] = '\0';
    memcpy(longurl, "https://", 8);
    CHECK_RC(cp0_map_source_check_url(longurl), CP0_MAP_E_INVALID);
    /* filling, and the form for messages */
    char out[300];
    CHECK_RC(cp0_map__url_expand("https://t.example.com/{z}/{x}/{y}.png?apikey={key}", 12, 2100, 1400, "SECRETkey123",
                                 out, sizeof out),
             CP0_MAP_OK);
    CHECK(!strcmp(out, "https://t.example.com/12/2100/1400.png?apikey=SECRETkey123"));
    CHECK_RC(cp0_map__url_expand("https://t.example.com/{z}/{x}/{y}.png?apikey={key}", 3, 1, 2, NULL, out, sizeof out),
             CP0_MAP_OK);
    CHECK(!strcmp(out, "https://t.example.com/3/1/2.png?apikey=***"));
    CHECK_RC(cp0_map__url_expand("https://t.example.com/{z}/{x}/{y}.png", 3, 1, 2, NULL, out, 10), CP0_MAP_E_INVALID);
    char line[128] = "GET https://t/1/2/3.png?k=SECRETkey123 failed (SECRETkey123)";
    cp0_map__redact(line, sizeof line, "SECRETkey123");
    CHECK(!strcmp(line, "GET https://t/1/2/3.png?k=*** failed (***)"));
    printf("map sources: HTTPS only (local-host exception), OSMF hosts refused, placeholders, filling, redaction\n");
}

static void test_user_agent(void)
{
    char ua[256];
    CHECK_RC(cp0_map_user_agent(ua, sizeof ua, "deck-atlas", "0.1.0"), CP0_MAP_OK);
    CHECK(!strcmp(ua, "deck-atlas/0.1.0 (+https://github.com/OSRdesign/cyberdeck-zero-apps)"));
    CHECK(cp0_map_user_agent_ok(ua));
    CHECK_RC(cp0_map_user_agent(ua, 20, "deck-atlas", "0.1.0"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_user_agent(ua, sizeof ua, "Deck Atlas", "0.1.0"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_user_agent(ua, sizeof ua, "deck-atlas", "0.1 beta"), CP0_MAP_E_INVALID);
    CHECK(!cp0_map_user_agent_ok(NULL) && !cp0_map_user_agent_ok("") && !cp0_map_user_agent_ok("deck"));
    CHECK(!cp0_map_user_agent_ok("curl/8.14.1") && !cp0_map_user_agent_ok("libcurl-agent/1.0"));
    CHECK(!cp0_map_user_agent_ok("Wget/1.21") && !cp0_map_user_agent_ok("python-requests/2.31"));
    CHECK(!cp0_map_user_agent_ok("deck-atlas/0.1\r\nX-Evil: 1") && !cp0_map_user_agent_ok("no slash at all here"));
    CHECK(cp0_map_user_agent_ok("mesh-hop/0.6.0 (+https://github.com/OSRdesign/cyberdeck-zero-apps)"));
    printf("map sources: the User-Agent rule and its builder\n");
}

static cp0_map_store_t *open_store_at(const char *dir, int ro, int network, const char *ua)
{
    cp0_map_store_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.store_dir = dir;
    cfg.read_only = ro;
    cfg.network = network;
    cfg.user_agent = ua;
    cfg.ram_cache_bytes = 1u << 20;
    cp0_map_store_t *s = NULL;
    CHECK_RC(cp0_map_store_open(&cfg, &s), CP0_MAP_OK);
    return s;
}

static int find_entry(cp0_map_store_t *s, const char *id, cp0_map_source_info_t *out)
{
    return cp0_map_store_find(s, id, out) == CP0_MAP_OK;
}

static void write_rec(const char *store, const char *id, const char *text)
{
    char p[700];
    snprintf(p, sizeof p, "%s/sources/%s.conf", store, id);
    write_file(p, text, strlen(text));
}

static void test_shipped_and_rules(void)
{
    char store[600];
    tpath(store, sizeof store, "rules");
    cp0_map_store_t *s = open_store_at(store, 0, 0, NULL);
    cp0_map_source_info_t list[16];
    const int n = cp0_map_store_sources(s, list, 16);
    CHECK(n == 3);
    CHECK(!strcmp(list[0].id, "geoapify") && !strcmp(list[1].id, "opentopomap") && !strcmp(list[2].id, "thunderforest"));
    for (int i = 0; i < n; i++) {
        const cp0_map_source_info_t *e = &list[i];
        CHECK(e->kind == CP0_MAP_ENTRY_SOURCE && e->shipped && !e->enabled && e->state == CP0_MAP_E_DISABLED);
        CHECK(e->policy.viewing_cache == 1 && e->policy.max_parallel == 2 && e->policy.user_agent_required == 1);
        CHECK(strstr(e->note, "unverified: check before enabling") != NULL);
        CHECK(!strncmp(e->url, "https://", 8) && cp0_map_source_check_url(e->url) == CP0_MAP_OK);
        CHECK(e->attribution[0] && e->licence[0] && e->tile_px == 256 && e->format == CP0_MAP_FMT_PNG);
        CHECK(!e->has_key && e->fetches_today == 0 && e->cache_tiles == 0);
    }
    /* OpenTopoMap: no key, viewing cache only, the CC-BY-SA style line with the OSM contributors and SRTM */
    CHECK(!list[1].policy.needs_key && !list[1].policy.bulk_ok && list[1].policy.daily_cap == 2000 && list[1].maxzoom == 17);
    CHECK(strstr(list[1].attribution, "OpenStreetMap contributors, SRTM") && strstr(list[1].attribution, "OpenTopoMap (CC-BY-SA)"));
    /* Thunderforest: the user's key, viewing cache only, 150,000 a month = 5,000 a day */
    CHECK(list[2].policy.needs_key && !list[2].policy.bulk_ok && list[2].policy.daily_cap == 5000);
    CHECK(strstr(list[2].url, "{key}") && strstr(list[2].attribution, "Thunderforest"));
    /* Geoapify: the user's key, area download allowed by its terms (not in this version), 12,000 a day */
    CHECK(list[0].policy.needs_key && list[0].policy.bulk_ok && list[0].policy.daily_cap == 12000);
    CHECK(strstr(list[0].attribution, "Powered by Geoapify") && strstr(list[0].attribution, "OpenStreetMap"));
    cp0_map_store_close(s);

    /* a user file for a shipped id: only stricter. Looser values, another URL, another attribution are ignored */
    write_rec(store, "opentopomap",
              "v=1\nenabled=1\nurl=https://tile.openstreetmap.org/{z}/{x}/{y}.png\ntitle=Mine\nattribution=none\n"
              "daily_cap=999999\nmax_parallel=4\nbulk_ok=1\nmin_request_interval_ms=50\nmaxzoom=19\n");
    write_rec(store, "thunderforest", "v=1\nenabled=1\ndaily_cap=100\nmax_parallel=1\nmin_request_interval_ms=900\n"
                                      "minzoom=3\nmaxzoom=15\nviewing_cache=0\nneeds_key=0\n");
    /* a user record on Thunderforest's host: the shipped flags are its ceiling, its attribution is shown */
    write_rec(store, "tf-cycle", "v=1\ntitle=Cycle\nurl=https://tile.thunderforest.com/cycle/{z}/{x}/{y}.png?apikey={key}\n"
                                 "daily_cap=90000\nmax_parallel=4\nbulk_ok=1\nattribution=mine\n");
    /* user records of their own: defaults, a key forced by {key}, refused ones still listed */
    write_rec(store, "mine", "v=1\nurl=https://tiles.example.net/{z}/{x}/{y}.jpg?token={key}\nformat=jpg\nmaxzoom=16\n");
    write_rec(store, "plain-http", "v=1\nurl=http://tiles.example.net/{z}/{x}/{y}.png\n");
    write_rec(store, "osmf", "v=1\nurl=https://tile.openstreetmap.org/{z}/{x}/{y}.png\nenabled=1\n");
    write_rec(store, "nokey-tpl", "v=1\nurl=https://tiles.example.net/{z}/{x}/{y}.png\nneeds_key=1\n");
    s = open_store_at(store, 0, 0, NULL);
    cp0_map_source_info_t e;
    CHECK(find_entry(s, "opentopomap", &e));
    CHECK(e.enabled && e.shipped && !strcmp(e.url, "https://tile.opentopomap.org/{z}/{x}/{y}.png"));
    CHECK(!strcmp(e.title, "OpenTopoMap") && strstr(e.attribution, "OpenTopoMap (CC-BY-SA)"));
    CHECK(e.policy.daily_cap == 2000 && e.policy.max_parallel == 2 && !e.policy.bulk_ok);
    CHECK(e.policy.min_request_interval_ms == 200 && e.maxzoom == 17);
    CHECK(e.state == CP0_MAP_E_OFFLINE); /* switched on, network off */
    CHECK(find_entry(s, "thunderforest", &e));
    CHECK(e.policy.daily_cap == 100 && e.policy.max_parallel == 1 && e.policy.min_request_interval_ms == 900);
    CHECK(e.policy.viewing_cache == 0 && e.policy.needs_key == 1 && e.minzoom == 3 && e.maxzoom == 15);
    CHECK(e.state == CP0_MAP_E_NO_KEY);
    CHECK(find_entry(s, "tf-cycle", &e));
    CHECK(!e.shipped && e.enabled && e.policy.daily_cap == 5000 && e.policy.max_parallel == 2 && !e.policy.bulk_ok);
    CHECK(strstr(e.attribution, "Thunderforest") && !strstr(e.attribution, "mine") && e.policy.needs_key);
    CHECK(strstr(e.note, "unverified") != NULL && e.state == CP0_MAP_E_NO_KEY);
    CHECK(find_entry(s, "mine", &e));
    CHECK(e.enabled && e.policy.needs_key && e.policy.daily_cap == CP0_MAP_DAILY_CAP_DEFAULT && e.policy.viewing_cache);
    CHECK(e.format == CP0_MAP_FMT_JPEG && e.maxzoom == 16 && e.state == CP0_MAP_E_NO_KEY);
    CHECK(find_entry(s, "plain-http", &e) && e.state == CP0_MAP_E_REFUSED);
    CHECK(find_entry(s, "osmf", &e) && e.state == CP0_MAP_E_REFUSED);
    CHECK(find_entry(s, "nokey-tpl", &e) && e.state == CP0_MAP_E_REFUSED);
    /* switches: off and on again (the file's other lines stay), the cap only lower */
    CHECK_RC(cp0_map_source_set_enabled(s, "opentopomap", 0), CP0_MAP_OK);
    CHECK(find_entry(s, "opentopomap", &e) && !e.enabled && e.state == CP0_MAP_E_DISABLED);
    CHECK_RC(cp0_map_source_set_daily_cap(s, "opentopomap", 50), 50);
    CHECK_RC(cp0_map_source_set_daily_cap(s, "opentopomap", 100000), 2000);
    CHECK_RC(cp0_map_source_set_daily_cap(s, "geoapify", 30000), 12000);
    CHECK_RC(cp0_map_source_set_enabled(s, "opentopomap", 1), CP0_MAP_OK);
    CHECK(find_entry(s, "opentopomap", &e) && e.enabled && e.policy.min_request_interval_ms == 200);
    char p[700];
    snprintf(p, sizeof p, "%s/sources/opentopomap.conf", store);
    size_t len;
    char *text = (char *)read_file(p, &len);
    CHECK(text && strstr(text, "title=Mine") && strstr(text, "enabled=1") && strstr(text, "daily_cap=100000"));
    free(text);
    snprintf(p, sizeof p, "%s/sources/geoapify.conf", store);
    text = (char *)read_file(p, &len);
    CHECK(text && !strncmp(text, "v=1\n", 4) && strstr(text, "daily_cap=30000"));
    free(text);
    CHECK_RC(cp0_map_source_set_enabled(s, "no-such", 1), CP0_MAP_E_NOT_FOUND);
    CHECK_RC(cp0_map_source_set_enabled(s, "../escape", 1), CP0_MAP_E_NOT_FOUND);
    /* area download: refused without bulk_ok, not in this version with it, never for an archive or unknown id */
    const cp0_map_bbox_t box = {2.0, 48.0, 2.1, 48.1};
    CHECK_RC(cp0_map_store_download(s, "opentopomap", &box, 10, 14, NULL, NULL), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_store_download(s, "thunderforest", &box, 10, 14, NULL, NULL), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_store_download(s, "tf-cycle", &box, 10, 14, NULL, NULL), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_store_download(s, "geoapify", &box, 10, 14, NULL, NULL), CP0_MAP_E_NOT_IN_VERSION);
    CHECK_RC(cp0_map_store_download(s, "osmf", &box, 10, 14, NULL, NULL), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_store_download(s, "nothing", &box, 10, 14, NULL, NULL), CP0_MAP_E_REFUSED);
    CHECK_RC(cp0_map_store_download(s, "geoapify", &box, 14, 10, NULL, NULL), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_store_download(s, "geoapify", NULL, 10, 14, NULL, NULL), CP0_MAP_E_INVALID);
    cp0_map_store_close(s);
    /* read-only: switches refused, nothing written */
    s = open_store_at(store, 1, 0, NULL);
    CHECK_RC(cp0_map_source_set_enabled(s, "opentopomap", 0), CP0_MAP_E_READ_ONLY);
    CHECK_RC(cp0_map_source_set_key(s, "thunderforest", "abcdef0123456789"), CP0_MAP_E_READ_ONLY);
    CHECK_RC(cp0_map_store_set_cap(s, 1u << 20), CP0_MAP_E_READ_ONLY);
    CHECK_RC(cp0_map_store_clear_cache(s, NULL), CP0_MAP_E_READ_ONLY);
    cp0_map_store_close(s);
    printf("map sources: shipped records (off, flags, notes), user files only stricter, host ceilings, refused records\n");
}

static void test_keys(void)
{
    char store[600], p[700];
    tpath(store, sizeof store, "keys");
    cp0_map_store_t *s = open_store_at(store, 0, 0, NULL);
    const char *key = "0123abcdEF456789cafe";
    const mode_t old = umask(0); /* a careless umask must not loosen the file */
    CHECK_RC(cp0_map_source_set_key(s, "thunderforest", key), CP0_MAP_OK);
    umask(old);
    snprintf(p, sizeof p, "%s/sources/thunderforest.key", store);
    struct stat st;
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0777) == 0600);
    cp0_map_source_info_t e;
    CHECK(find_entry(s, "thunderforest", &e) && e.has_key && e.state == CP0_MAP_E_DISABLED);
    CHECK(!memmem(&e, sizeof e, key, strlen(key))); /* the key is never in a listing */
    cp0_map_source_info_t all[8];
    const int n = cp0_map_store_sources(s, all, 8);
    CHECK(n == 3 && !memmem(all, sizeof all, key, strlen(key)));
    /* a user record on the same host without a key of its own uses the provider's */
    write_rec(store, "tf-cycle", "v=1\nurl=https://tile.thunderforest.com/cycle/{z}/{x}/{y}.png?apikey={key}\n");
    CHECK_RC(cp0_map_store_refresh(s), CP0_MAP_OK);
    CHECK(find_entry(s, "tf-cycle", &e) && e.has_key && e.state == CP0_MAP_E_OFFLINE);
    /* bad keys: refused, the old one kept */
    CHECK_RC(cp0_map_source_set_key(s, "thunderforest", "short"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_set_key(s, "thunderforest", "has space in it"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_set_key(s, "thunderforest", "abc&def=0123456789"), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_set_key(s, "thunderforest", NULL), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_set_key(s, "nope", key), CP0_MAP_E_NOT_FOUND);
    char got[CP0_MAP_KEY_MAX];
    CHECK_RC(cp0_map__key_read(store, "thunderforest", got, sizeof got), CP0_MAP_OK);
    CHECK(!strcmp(got, key));
    /* a key file others can read (copied by hand) is closed to 0600 before it is used */
    CHECK(chmod(p, 0644) == 0);
    CHECK_RC(cp0_map__key_read(store, "thunderforest", got, sizeof got), CP0_MAP_OK);
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0777) == 0600);
    /* a damaged key file is not used */
    write_file(p, "not a key!\n", 11);
    CHECK_RC(cp0_map__key_read(store, "thunderforest", got, sizeof got), CP0_MAP_E_INVALID);
    CHECK_RC(cp0_map_source_set_key(s, "thunderforest", key), CP0_MAP_OK);
    /* cleared */
    CHECK_RC(cp0_map_source_clear_key(s, "thunderforest"), CP0_MAP_OK);
    CHECK(!file_exists(p));
    CHECK(find_entry(s, "thunderforest", &e) && !e.has_key);
    CHECK_RC(cp0_map_source_clear_key(s, "thunderforest"), CP0_MAP_OK); /* again: nothing to do */
    cp0_map_store_close(s);
    printf("map sources: keys 0600 whatever the umask, never listed, shared by a provider's records, cleared\n");
}

static void test_http_parsing(void)
{
    CHECK(cp0_map__http_date("Sun, 06 Nov 1994 08:49:37 GMT") == 784111777);
    CHECK(cp0_map__http_date("Sunday, 06-Nov-94 08:49:37 GMT") == 784111777);
    CHECK(cp0_map__http_date("Sun Nov  6 08:49:37 1994") == 784111777);
    CHECK(cp0_map__http_date("yesterday") == -1 && cp0_map__http_date("") == -1 && cp0_map__http_date(NULL) == -1);
    CHECK(cp0_map__http_date("Sun, 32 Nov 1994 08:49:37 GMT") == -1);
    cp0_map_http_resp_t r;
    memset(&r, 0, sizeof r);
    cp0_map__http_resp_clear(&r);
    const char *lines[] = {"HTTP/1.1 100 Continue\r\n", "HTTP/1.1 200 OK\r\n", "Content-Type: Image/PNG; charset=x\r\n",
                           "cache-control: public, max-age=3600, must-revalidate\r\n", "ETag: W/\"abc-1\"\r\n",
                           "Last-Modified: Sun, 01 Nov 2026 10:00:00 GMT\r\n", "Age: 600\r\n",
                           "Content-Length: 1234\r\n", "X-Junk\r\n"};
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) cp0_map__http_header(&r, lines[i], strlen(lines[i]));
    CHECK(r.status == 200 && !strcmp(r.content_type, "image/png") && r.max_age == 3600 && r.age == 600);
    CHECK(!strcmp(r.etag, "W/\"abc-1\"") && !strcmp(r.last_modified, "Sun, 01 Nov 2026 10:00:00 GMT"));
    CHECK(r.content_length == 1234 && !r.no_store && !r.no_cache);
    CHECK(cp0_map__http_freshness(&r) == 3600); /* max-age - Age = 3000, under the 1 h floor */
    r.max_age = 86400;
    CHECK(cp0_map__http_freshness(&r) == 86400 - 600);
    r.max_age = 400 * 86400;
    CHECK(cp0_map__http_freshness(&r) == CP0_MAP_FRESH_CEIL_S);
    cp0_map__http_resp_clear(&r);
    CHECK(cp0_map__http_freshness(&r) == CP0_MAP_FRESH_DEFAULT_S); /* no header: 7 days */
    cp0_map__http_header(&r, "Date: Sun, 01 Nov 2026 10:00:00 GMT", 35);
    cp0_map__http_header(&r, "Expires: Sun, 01 Nov 2026 16:00:00 GMT", 38);
    CHECK(cp0_map__http_freshness(&r) == 6 * 3600);
    cp0_map__http_header(&r, "Expires: 0", 10); /* an invalid date: already expired, the floor */
    CHECK(r.expires == 0 && cp0_map__http_freshness(&r) == CP0_MAP_FRESH_FLOOR_S);
    cp0_map__http_header(&r, "Cache-Control: no-cache", 23);
    CHECK(r.no_cache && cp0_map__http_freshness(&r) == CP0_MAP_FRESH_FLOOR_S);
    cp0_map__http_header(&r, "Cache-Control: no-store", 23);
    CHECK(r.no_store);
    cp0_map__http_header(&r, "Retry-After: 120", 16);
    CHECK(cp0_map__http_retry_after(&r, 1000) == 120);
    cp0_map__http_header(&r, "Retry-After: Sun, 01 Nov 2026 10:00:30 GMT", 42);
    CHECK(cp0_map__http_retry_after(&r, cp0_map__http_date("Sun, 01 Nov 2026 10:00:00 GMT")) == 30);
    cp0_map__http_resp_clear(&r);
    CHECK(cp0_map__http_retry_after(&r, 0) == -1);
    printf("map sources: HTTP headers, dates, freshness floor / ceiling / default, Retry-After\n");
}

static void test_cap_rule_and_day(void)
{
    CHECK(cp0_map_store_default_cap(0) == 0);
    CHECK(cp0_map_store_default_cap(1000ull << 20) == 100ull << 20);    /* 10 % of 1000 MiB */
    CHECK(cp0_map_store_default_cap(5120ull << 20) == 512ull << 20);    /* exactly at the turn */
    CHECK(cp0_map_store_default_cap(64000ull << 20) == 512ull << 20);   /* a big card: 512 MiB */
    setenv("TZ", "Europe/Paris", 1);
    tzset();
    const int64_t t = 1792969200; /* 2026-10-26 00:00 Paris (UTC+1, the day after the clocks went back) */
    CHECK(cp0_map__local_day(t) == 20261026 && cp0_map__local_day(t - 1) == 20261025);
    CHECK(cp0_map__next_local_midnight(t - 3600) == t && cp0_map__next_local_midnight(t) == t + 86400);
    setenv("TZ", "UTC", 1);
    tzset();
    printf("map sources: cap rule (min of 512 MiB and 10 %% of free space), the local day turns at midnight\n");
}

int main(void)
{
    tmp_init("cp0map-src");
    test_url_rules();
    test_user_agent();
    test_shipped_and_rules();
    test_keys();
    test_http_parsing();
    test_cap_rule_and_day();
    tmp_done();
    printf("test_map_source: %ld checks passed\n", g_checks);
    return 0;
}
