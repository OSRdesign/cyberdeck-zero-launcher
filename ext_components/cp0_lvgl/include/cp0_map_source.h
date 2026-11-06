/*
 * SPDX-License-Identifier: MIT
 *
 * cp0_map_source.h: online tile sources of the map engine, their policy record and its rules (report 054 sections
 * 2.1, 2.2, 2.7; owner decisions D3 and D10; Deck Atlas 0.1 task 3). Plain data and pure functions: the store calls
 * that use them are in cp0_map_store.h, the viewers' fetch requests in cp0_map_fetch.h.
 *
 * RECORD (sources/<id>.conf in the store, or an app's cfg.sources_dir; key=value lines like the archive records,
 * readers ignore unknown keys; the file name is the id, [a-z0-9._-]):
 *   v=1  title=  url=https://host/path/{z}/{x}/{y}.png?key={key}  format=png|jpg  tile_px=256|512
 *   minzoom=  maxzoom=  attribution=  licence=  note=  enabled=0|1
 *   viewing_cache=0|1  bulk_ok=0|1  max_parallel=1..4  daily_cap=  needs_key=0|1  user_agent_required=0|1
 *   min_request_interval_ms=  blocked_hashes=<16 hex>,...  blocked_sizes=<bytes>,...
 *
 * URL TEMPLATE. {z}, {x} and {y} are required, {key} is the source's API key (the template must have it when
 * needs_key=1, and a template with {key} needs a key). HTTPS only; plain http only to 127.0.0.1, localhost or [::1]
 * (the fake tile server of the tests, a server on the same board). No user name or password in the URL. The OSM
 * Foundation's servers are NEVER used (054 2.1, owner decision): a template or a redirect to openstreetmap.org or
 * osm.org, or any of their subdomains (tile.openstreetmap.org, vector.openstreetmap.org...), is refused with
 * CP0_MAP_E_REFUSED, whatever a record says.
 *
 * SHIPPED RECORDS. The toolkit ships three records (built in, listed even without a file; all OFF until the user
 * switches them on, and the keyed ones until a key is set): "opentopomap" (OpenTopoMap, viewing cache only),
 * "thunderforest" (Thunderforest Outdoors, the user's own free key, viewing cache only) and "geoapify" (Geoapify OSM
 * Bright, the user's own free key; area download allowed by its terms, phase 0.2). Their URLs, attributions, terms and
 * limits are in docs/cp0_lvgl.en.md ("Online sources"). A user file with a shipped id may switch it on or off and make
 * its limits STRICTER (lower daily_cap and max_parallel, a longer min_request_interval_ms, fewer zooms, viewing_cache
 * or bulk_ok off); it cannot change the URL, the attribution or loosen a flag. A user record on the HOST of a shipped
 * record (another Thunderforest style, say) gets that record's flags as ceilings, its attribution, and shares its key
 * (when it has none of its own), its daily count and its pauses: one provider, one account, one cap.
 *
 * POLICY FLAGS (cp0_map_source_policy_t below; the defaults of a user-added record in brackets):
 *   viewing_cache [1]  tiles the user views may be kept in the persistent viewing cache (tiles/<id>.mbtiles); 0: they
 *                      are shown from RAM only (a few dozen) and fetched again later
 *   bulk_ok [0]        area download allowed (cp0_map_store_download; not in this version)
 *   max_parallel [2]   requests at once to the provider (1..4; the engine's own cap is 4 for all sources)
 *   daily_cap [5000]   requests per local day (any answer counts: 200, 304, 404...), counted in a state file under
 *                      the store so a restart does not reset it; the day turns at local midnight
 *   needs_key [0]      an API key is needed (forced to 1 by {key} in the URL)
 *   user_agent_required [1]  the provider asks for an identifying User-Agent; the engine sends cfg.user_agent on every
 *                      request anyway and never fetches without one
 *   min_request_interval_ms [0]  at least this long between two requests to the provider
 *   blocked_hashes, blocked_sizes  pictures the provider is known to send instead of a tile (the "blocked" guard)
 */
#ifndef CP0_MAP_SOURCE_H
#define CP0_MAP_SOURCE_H

#include "cp0_map_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_MAP_URL_MAX 256          /* a URL template, with the terminating 0 */
#define CP0_MAP_KEY_MAX 128          /* an API key, with the terminating 0 */
#define CP0_MAP_MAX_PARALLEL 4       /* requests at once, all sources together */
#define CP0_MAP_DAILY_CAP_DEFAULT 5000u /* a user-added record without daily_cap */

typedef struct {
    int viewing_cache;
    int bulk_ok;
    int max_parallel;
    uint32_t daily_cap;
    int needs_key;
    int user_agent_required;
    int min_request_interval_ms;
} cp0_map_source_policy_t;

/* Checks a URL template: 0, CP0_MAP_E_REFUSED (not https and not the local-host exception, an OSMF host, a user name
 * or password in the URL, another scheme), CP0_MAP_E_INVALID (NULL, too long, no host, {z} / {x} / {y} missing, an
 * unknown {placeholder}). Pure, any thread. */
int cp0_map_source_check_url(const char *url_template);
/* 1 when `host` (no port, any case, a trailing dot allowed) is an OSM Foundation tile host: openstreetmap.org,
 * osm.org or any subdomain of them. */
int cp0_map_source_osmf_host(const char *host);
/* The User-Agent the engine expects: "<app_id>/<version> (+https://github.com/OSRdesign/cyberdeck-zero-apps)".
 * 0, or CP0_MAP_E_INVALID (bad arguments, buf too small). */
int cp0_map_user_agent(char *buf, size_t size, const char *app_id, const char *version);
/* 1 when `ua` is a usable User-Agent: printable ASCII, 8..255 bytes, a product/version token, not a library
 * default ("curl/...", "libcurl", "Wget", "python-..."). */
int cp0_map_user_agent_ok(const char *ua);

#ifdef __cplusplus
}
#endif

#endif /* CP0_MAP_SOURCE_H */
