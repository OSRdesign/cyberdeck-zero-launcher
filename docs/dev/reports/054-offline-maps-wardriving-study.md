# 054 - Offline maps with an external GPS, and wardriving (study)

Date 2026-11-05. Read-only study: no product code, no board touched, nothing built or installed. The owner approved
starting a map viewer/explorer app with wardriving and an external GPS receiver, with map tiles stored on the board.
This report gives options and a recommendation; design and visual decisions stay with the controller and the owner.
**Since the owner's decisions of 2026-11-05 (section 11) the app is called Deck Atlas (`deck-atlas`) and wardriving
is deferred** (kept in section 5 for later, not planned).

**What I read.**
- Launcher `github/main` @ `3824b2cc`: `docs/APP-CONTRACT.md` (sections 2, 5b, 5c, 6, 10b, 13, 14),
  `docs/dev/ROADMAP.md`, reports 027 and 052 (format), `projects/APPLaunch/config_defaults.mk` (LVGL options).
- Apps `origin/main` @ `bec05f3`: `docs/APP-GUIDELINES.md`; Net Toolbox (`wifiscan`, `ble`, `walktest`,
  `netlog_wifi`); Serial Console (`ports`, `devices`, `identify`, `probe`, `serial_io`); viz1090 (`gps`, `nmea`,
  `make_world.py`, `.desktop`); Mesh Hop (`nodes`, `model`, `client`, `serial_transport`, `deck_clock`); RSS Reader
  (`docs/IMAGES.md`, `image_codecs.h`, `fetch.hpp`); Home Assistant (`curl_api.cpp`); `_common/scons/main.SConstruct`.
- The web (read 2026-11-05), listed under Sources with their licences.
- Addendum (section 12): prior-art repositories (READMEs, docs, source, licence files) through the GitHub API, read
  2026-11-05.

**Markers.** **V** = verified in our code or a measured board fact already recorded in a report; **S** = read in a
source (Sources list); **I** = inferred or estimated, needs a spike or a board. **Cost unit:** one worker session
(ws), about 0.4 M tokens (the ratio of reports 028 and 052).

## 0. Summary

**Recommendation: build Deck Atlas, raster first, in phases 0.1, 0.2 and 0.4 (0.3 wardriving deferred by the owner),
as one native `cp0_lvgl` app whose map engine is a reusable MIT component of the toolkit `cp0_lvgl`.**

**Owner decisions (2026-11-05, questions 2-13; details in section 11 and the D table).** Name **Deck Atlas**
(`deck-atlas`). Tiles: imports always on (MBTiles, PMTiles, a `z/x/y` tile folder converted on the board into one
MBTiles), opt-in OpenTopoMap and Thunderforest viewing caches, Geoapify (own key) the only in-app area download, never
the OSMF servers, no LAN tile server planned. Raster now, vector spike after 0.2. Display scale automatic (nearest of
1x/1.5x/2x) with an override. Deck Atlas reads the GPS (a u-blox USB puck for the tests) and writes `position.state`.
**Wardriving deferred** ("skip wardriving for now"). Storage in the shared folder, cache cap the smaller of 512 MB and
10 % of free space. **Small screens from the start:** 640x480, 480x320 and 320x480 in 0.1, larger portrait panels too.
"Set the clock from GPS" offered, never automatic. Own MIT engine; GPL/AGPL projects design references only. Sections
0-11 and 12.3/12.4 are made consistent with these decisions; section 13 (new) is the P0 spike plan. For this update I
also read the launcher's `docs/PDA-PROFILE.md`, `docs/APP-CONTRACT.md` sections 3 and 5c-6 (`X-MinSize`,
`X-Portrait`, `board.state` `ppmm`/`target`), `docs/dev/ROADMAP.md` (v0.9.4) and `projects/APPLaunch/tests/render/README.md`
(the `p320` profile) (V).

**Owner decision (2026-11-05, question 1).** The map app (Deck Atlas since question 2) does not embed Mesh Hop, and Mesh Hop does not get a copy of
the map app's widget code. The **map engine** (tile widget, Web Mercator, MBTiles/PMTiles readers, RAM tile LRU, the
shared-folder tile store and source-policy cache, display scale, overlay/marker/track layers) is built once in the
toolkit (`ext_components/cp0_lvgl` of the launcher repo) behind a small public interface (3.7). The map app is its first
user, Mesh Hop its second: Mesh Hop's own Map tab shows the shared tile store and its mesh nodes with the engine (6.2,
phase 0.4). The data contracts stay (the shared folder `~/.local/share/cyberdeckzero/maps/`, `position.state`, overlay
files, `geo:` waypoints), so any app can read positions and points without linking the engine. Sections 0, 1, 3.4, 3.6,
3.7 (new), 6, 7.4, 7.5, 8, 9, 10, 11 and one row of 12.4 are updated for it; for this update I also read the apps
repo's `apps/_common/LAUNCHER_REF`, `apps/_common/README.md` (the launcher pin) and APP-GUIDELINES section 3, and the
toolkit's `ext_components/cp0_lvgl/AGENTS.md` and `SConstruct` (V).

**Addendum (section 12, prior art).** Existing open-source map viewers and tile tools, NavBoxLib among them, were
surveyed after the first draft. They change details, not the plan: Geoapify (the user's own free key) is added as an
opt-in source that allows area downloads (D3); archives rendered at a higher DPI or as `@2x` are preferred over
magnifying on the board; the source records get a blocked-tile guard; P0 adds palette PNG and `mmap` timings. NavBoxLib
and the other GPL/AGPL map apps are design references only; no change to formats, storage, GPS reader, phases or cost.

1. **Tiles (decided 2026-11-05).** Never use the OSM Foundation servers (`tile.openstreetmap.org`, the OSMF vector
   tiles): their policy forbids offline use and any area download (S, quoted in 2.1). Imports are always on; no online
   source is on by default; the ones below are opt-in. Get maps onto the board in three ways: **import** a raster
   MBTiles or PMTiles file, or a `z/x/y` **tile folder** (for example the output of the MapTileDl tool), from a USB
   stick or the SD card (a folder is converted on the board into one MBTiles); **download an area** only from Geoapify
   with the user's own free key (about 12,000 tiles a day on the free plan, 12.4), the only in-app area download; and a
   **persistent viewing cache** for OpenTopoMap and Thunderforest (the user's own free key): kept on the SD card after
   quitting, only tiles actually viewed, never pre-fetched, expiry headers honoured, least recently used evicted at the
   cap (2.7). Every source carries policy flags; the app enforces them, with three guards (blocked-tile image,
   missing-tile cache, daily cap). A LAN tile server is no longer a planned route (an optional advanced source, no work
   planned).
2. **Rendering.** Raster tiles (PNG/JPEG) decoded with stb_image (already in RSS Reader, public domain/MIT), kept as
   RGB565, blitted by the map engine's LVGL widget, part of `cp0_lvgl` (3.7). A 256 px tile is designed for about
   3.8 px/mm; our panels have 6.3 to 11.3 px/mm (zero8's 6.3 and the deck's 11.3 V, the others I), so tiles are
   **magnified by the display scale** (decided 2026-11-05: automatic, the nearest of 1x/1.5x/2x from the panel
   density, a Settings override Auto/1x/1.5x/2x), and 512 px "@2x" tiles or files rendered at a higher DPI on a PC are
   preferred where available (12.4), or the labels are unreadable.
   Vector tiles (10-20x smaller files, crisp at any density) get a spike after 0.2 (decided 2026-11-05); MapLibre
   Native is out (needs OpenGL ES 3.0 or Vulkan, S).
3. **GPS (decided 2026-11-05).** Deck Atlas reads the receiver itself with our own NMEA reader, reusing MIT code we
   already have: viz1090's listen-only NMEA watcher and Serial Console's sysfs port scan, GNSS device table and
   NMEA/UBX classifier (V). Not gpsd by default (an optional user-run source later): Debian's gpsd hot-plug rules claim
   generic USB-serial chips such as CP210x (10c4:ea60), which LoRa boards for Mesh Hop use (S). The owner's receiver is
   a u-blox USB puck (ACM or bridge chip not known yet).
4. **Wardriving: deferred by the owner (2026-11-05), not planned.** Section 5 keeps the study (managed-mode scans
   through NetworkManager and `bluetoothctl`, WiGLE CSV 1.6 with GPS time, local only) for a later decision; phase
   0.3 stays numbered but has no cost in the totals.
5. **Sharing.** A documented shared folder for tile archives and overlays, and a `position.state` file in the state
   directory. First Deck Atlas writes it (0.2); later the launcher may own the GPS and write it (section 6.4). Mesh
   Hop reuses the toolkit's map engine for its own Map tab (0.4) instead of a copy; apps that only need positions or
   points read the files and link nothing. Over the LoRa mesh only positions and waypoints travel, never tiles
   (bandwidth).
6. **Screens (decided 2026-11-05).** Small screens from the start: 0.1 is designed for 640x480, 480x320 and 320x480
   (zero8 in portrait, the launcher's narrow portrait rule), and for the larger portrait canvases (480x640, 480x800).
   `X-MinSize=480x320` and `X-Portrait=320x480`, so Deck Atlas is not hidden on the 480x320 boards (7.2, 7.4, 7.6).

**Key facts.**
- Pi Zero 2 W: 512 MB (about 414 MB usable), 2.4 GHz-only Wi-Fi, Wi-Fi and Bluetooth on one chip (V). App rule:
  VmRSS under 25 MB (V). A decoded 256 px RGB565 tile is 128 KB; 2x magnified 512 KB.
- A 20 x 20 km city at zoom 0-16 is about 3,500 raster tiles (35-70 MB); zoom 0-17 about 13,300 tiles
  (130-270 MB); a country like France at zoom 0-14 about 260,000 tiles (several GB) (I, section 2.5).
- LVGL 9.5 in our toolkit already has ThorVG vector drawing, lodepng and NEON blending switched on (V).
- Mesh Hop already knows node positions from adverts and can set the radio's position (V).

**Phases (cost in ws, all estimates, I; recomputed for the owner decisions of 2026-11-05).** P0 spike 1-1.25 (was
0.75-1: + the small-screen legibility checks on the deck and zero8 in both orientations, the u-blox puck capture and
the folder-import timing; the `iw` dump check is dropped with wardriving); **0.1** map engine in `cp0_lvgl` (launcher
merge) + apps toolkit pin move + Deck Atlas with imports (MBTiles, PMTiles, tile folder), viewing caches and a GPS dot,
**laid out for 640x480, 480x320, 320x480 and the portrait canvases** 3.25-4.25 (was 2.5-3.25: +0.5-0.75 for the small
screens, +0.25 for the folder import and its conversion); **0.2** Geoapify area download with the daily cap, cache
cap, tracks, `position.state`, the clock offer 1.75-2.25 (was 1.5-2: +0.25 for the small-screen layouts of the new
pages); **0.3** wardriving: **deferred by the owner, not planned, not in the totals** (if resumed later: 1.25-1.75
ws); **0.4** Mesh Hop shows the map and its nodes with the engine 1.5-2.25 (unchanged); docs 0.25. **MVP (P0 + 0.1 +
docs) about 4.5-5.75 ws (1.8-2.3 M tokens; was 3.5-4.5 ws, 1.4-1.8 M); everything through 0.4 without 0.3 about
7.75-10.25 ws (3.1-4.1 M tokens; was 7.75-10.5 ws, 3.1-4.2 M, with 0.3).** Optional: vector renderer (spike + 2.5-4
ws), launcher-owned GPS (1.5-2.5 ws + release). Tokens are estimates at 0.4 M per ws; heavy-tier sessions (P0, 0.1,
0.4) may run above that ratio (I).

**Owner questions:** section 11 (13 questions; all decided 2026-11-05, question 9 as "deferred").

## 1. Decisions for the owner

All twelve decided by the owner on 2026-11-05 (D7-D9 by deferring wardriving).

| # | Decision | Options | Decision |
|---|---|---|---|
| D1 | Scope and order | 0.1 engine in the toolkit + viewer, 0.2 downloads + tracks, 0.3 wardrive, 0.4 Mesh Hop shows the map and its nodes with the engine; or wardrive earlier | **Decided 2026-11-05 (owner):** one app; phases 0.1, 0.2, 0.4 in that order; **0.3 wardriving deferred by the owner, not planned** (the id is kept, nothing renumbered); the app does not embed Mesh Hop; the map engine is a reusable MIT component of `cp0_lvgl`, Deck Atlas its first user, Mesh Hop its second (0.4) |
| D2 | One app or two; where the map code lives | (a) one app with a Wardrive page; (b) a map app + a separate logger app sharing the store. Map code: (i) inside the map app, Mesh Hop gets overlay files only or a copy; (ii) **an engine in the toolkit that both apps link**, data contracts kept for everyone else | **Decided 2026-11-05 (owner):** (a) one app, **Deck Atlas** (`deck-atlas`, 7.1), + (ii) engine reuse, no copy; the Wardrive page waits with wardriving |
| D3 | Default tile sources | (a) none online, imports only; (b) add OpenTopoMap viewing cache; (c) add Thunderforest with the user's key; (d) add Geoapify with the user's key, area download allowed (12.4); (e) a LAN tile server | **Decided 2026-11-05 (owner):** imports always on (MBTiles, PMTiles, a `z/x/y` tile folder converted on the board into one MBTiles; the import screen says the user is responsible for the terms of what they import); (b), (c) and (d) opt-in; Geoapify the only in-app area download; (e) not wanted: an optional advanced source with no work planned; the OSMF servers never; three guards (2.7) |
| D4 | Raster now, vector later | (a) raster only; (b) raster now, vector spike after 0.2; (c) vector first | **Decided 2026-11-05 (owner):** (b) |
| D5 | Display scale | automatic from density, a Settings choice | **Decided 2026-11-05 (owner):** automatic from the panel density, the nearest of 1x/1.5x/2x; Settings override Auto/1x/1.5x/2x; 512 px/`@2x` tiles or high-DPI archives made on a PC preferred where available; the exact rule in the design pass (3.2) |
| D6 | Who owns the GPS | app (0.1), launcher later; launcher now; gpsd | **Decided 2026-11-05 (owner):** Deck Atlas reads the GPS and writes `position.state` (`X-Background=run` while recording); launcher ownership later with the same file; gpsd not default (an optional user-run source later) (6.4) |
| D7 | Wi-Fi scanning | NetworkManager only (no root); plus an optional helper with `cap_net_admin` | **Deferred 2026-11-05 (owner)** with wardriving; the study's default stays on file: NetworkManager only |
| D8 | BLE in wardriving | on, off, off on boards whose keyboard is Bluetooth | **Deferred 2026-11-05 (owner)** with wardriving; study default on file: off, a switch with the warning |
| D9 | Logs | local only, manual WiGLE export; any upload code | **Deferred 2026-11-05 (owner)** with wardriving; study default on file: local only, no upload code |
| D10 | Storage | shared folder path, cache cap, a USB stick path | **Decided 2026-11-05 (owner):** `~/.local/share/cyberdeckzero/maps/`; cache cap the smaller of 512 MB and 10 % of free space, for the cache only; imported archives never evicted (Settings lists their sizes and deletes them); an archive on a USB stick is read in place, "archive missing" when the stick is gone, copying to the SD card recommended; SQLite archives with batched writes, no tiny files (2.7, 6.2) |
| D11 | Screen sizes | 640x480 minimum + portrait; also 480x320 and 320x480 | **Decided 2026-11-05 (owner):** small screens from the start: 0.1 laid out for 640x480, 480x320 and 320x480 (narrow portrait rule) and the larger portrait canvases; `X-MinSize=480x320`, `X-Portrait=320x480`; compact toolbar, thumb-sized controls, no side panels (7.2, 7.4) |
| D12 | System clock from GPS | no; offer when the clock is unsynced (sudo prompt) | **Decided 2026-11-05 (owner):** offered when the clock is unsynced, only with a valid fix carrying date and time, after a confirmation showing the GPS time; sudo prompt, `timedatectl set-time`, fallback `date -s`; never automatic; time zone untouched (4.6) |

## 2. Tile sources, formats and storage (question 1)

### 2.1 The OSM Foundation tile policy, exactly

The policy page (S [OSMF-tiles], undated, read 2026-11-05) says:

- **Prohibited:** "Bulk downloading is any pre-emptive fetching of tiles other than those a user is actively
  viewing." "**Offline use is not permitted on `tile.openstreetmap.org`.**" Listed as not permitted: features such as
  "Download city/country for offline use" or "Save area for later", "Any 'download for offline' button or background
  job that fetches tiles a user is not currently viewing", "Preloading entire towns/regions or multiple zoom stacks
  'just in case'", "Building tile archives (e.g. .zip, .mbtiles) for later distribution".
- **Allowed:** "Normal interactive viewing by a human where the client requests only the tiles needed for the current
  viewport" and "Re-visits served from your local cache honouring server caching headers (or >= 7-day TTL if you
  cannot read them)."
- **Caching:** honour `Cache-Control`, `Expires`, `ETag`; at least 7 days when the headers cannot be read; conditional
  requests (`If-None-Match`, `If-Modified-Since`) for expired tiles; never send `no-cache` by default.
- **Identification:** "a clear, unique User-Agent string that names your app and optionally includes a contact URL or
  email"; never a library default; native apps need no Referer.
- **URL:** exactly `https://tile.openstreetmap.org/{z}/{x}/{y}.png`.
- **Attribution:** visible on the map, not hidden behind toggles or off-screen.
- **Alternatives:** "If you require offline maps, use self-hosted tiles or a provider that explicitly allows
  offline/prefetching."

The OSMF vector tiles (`vector.openstreetmap.org`, Shortbread schema) have the same rule: "Bulk downloading is
prohibited", including "downloading for offline usage" (S [OSMF-vector]).

**Consequence for this app.** An app whose purpose is offline maps cannot use the OSMF servers honestly: a viewing
cache that is shown when the board is offline is "offline use". Proposal: leave both OSMF servers out completely.

### 2.2 Raster sources that can be cached

| Source | Terms for caching on the board | Area download | Cost / key | Attribution | Fit |
|---|---|---|---|---|---|
| `tile.openstreetmap.org` (OSMF) | viewing cache only, no offline use (S) | no | free, no key | "© OpenStreetMap contributors" | **excluded** |
| OpenTopoMap | apps allowed "provided the server is not overloaded, e.g. by mass downloads"; small server since 2026-01-05; licence CC-BY-SA (S [OTM]) | no (mass downloads) | free, no key | "Kartendaten: © OpenStreetMap-Mitwirkende, SRTM, Kartendarstellung: © OpenTopoMap (CC-BY-SA)" | viewing cache, opt-in |
| Thunderforest (OpenCycleMap, Outdoors, Transport...) | "Tiles may be cached in-browser and on-device for offline use"; may keep tiles past expiry (S [TF-terms]) | only on Small Business plan or higher (S [TF-price]) | free Hobby plan 150,000 tiles/month with an API key | "Thunderforest" and "OpenStreetMap contributors", with links | viewing cache with the user's own key; `@2x` tiles exist (I) |
| Geoapify (OSM Bright, Positron and other raster styles; added by the addendum, 12.4) | "you can cache, store, and redistribute the generated map tiles" (S [Geoapify-tiles]) | yes, within the plan's credits: free plan 3,000 credits a day, a tile costs 0.25 credit, so 12,000 tiles a day (S [Geoapify-price]) | free plan with an API key | "Powered by Geoapify" and OpenStreetMap; mandatory on the free plan (S [Geoapify-terms]) | **opt-in with the user's own key, area download allowed** (daily cap); `@2x` URLs exist (S) |
| Stadia Maps | on-device caching with an active subscription, 100 MB per device (S [Stadia], a tutorial, not the ToS) | not stated | paid | per their docs | not proposed |
| A tile server on the LAN (tileserver-gl, a static folder of tiles) | the owner's data | yes | free | per the data used (ODbL: "© OpenStreetMap") | **not wanted by the owner (2026-11-05):** at most an optional advanced source later, no work planned |
| Raster MBTiles/PMTiles made on a PC (QGIS at a higher DPI, `pmtiles extract`) | the owner's file | n/a (import) | free | per the data | **import, always on** (decided 2026-11-05) |
| A `z/x/y` tile folder from a PC tool, for example MapTileDl [MapTileDl] | the user's files; the tool's page names no output format (a folder sync is inferred, I) and does not state its tile sources or their terms | n/a (import) | free | unknown: the map shows "© OpenStreetMap contributors" | **import, always on** (decided 2026-11-05): converted on the board into one MBTiles (2.7); the import screen tells the user they are responsible for the terms of what they import |

Not checked in detail and not proposed: satellite imagery (Esri, MapTiler, EOX Sentinel-2 cloudless); commercial
APIs usually allow offline storage only on paid plans.

### 2.3 Vector tiles and region archives

| Source | What | Offline terms | Size / cadence | Licence, attribution |
|---|---|---|---|---|
| OpenFreeMap | OpenMapTiles-schema vector tiles, public instance, no key, no limits on views (S [OFM]) | offline download allowed up to "3,000 tiles or 50 MB", at most 4 parallel requests, a unique User-Agent; larger: contact them | weekly planet downloads (Btrfs and MBTiles) | code MIT; data OSM; "OpenFreeMap © OpenMapTiles Data from OpenStreetMap" |
| Protomaps basemap | daily planet PMTiles builds, zoom 0-15, about 120 GB (S [PM-dl]) | download and copy to your own storage; hotlinking discouraged; `pmtiles extract --bbox` or `--region` cuts a region from the remote file (S [PM-cli]) | daily | ODbL Produced Work, OpenStreetMap attribution; PMTiles code BSD-3, spec CC0 (S [PM-repo]) |
| Shortbread extracts | vector MBTiles per Geofabrik region ("experimental, non-updated") and BBBike custom regions (S [SB-dl]) | downloads for offline use | Geofabrik not updated; BBBike on request | data ODbL |
| BBBike extract service | any region up to 24 million km2, formats incl. vector MBTiles, PMTiles, Mapsforge, OsmAnd, Garmin; MBTiles 0.2-0.6x the PBF size (S [BBBike]) | free for end users, fair use | data updated daily, 2-7 min per extract | "Map data © OpenStreetMap contributors, ODbL 1.0" |
| OpenMapTiles schema | the schema many vector sources use | n/a | n/a | code BSD; design CC-BY: credit "OpenMapTiles" plus OpenStreetMap (S [OMT]) |
| Mapterhorn (terrain) | Terrarium-encoded elevation tiles, 512 px **WebP**; planet PMTiles z0-12, regional z13-17 (S [Mapterhorn]) | PMTiles downloads, extract by bbox | static releases | code BSD-3; per-source attribution page |

A vector archive needs a vector renderer on the board (section 3.3). Mapterhorn hillshade needs a WebP decoder
(libwebp, BSD-3) and a shading pass: a later layer at best.

### 2.4 Offline map formats

| Format | What | Reader we could use | Licence of format / reader | Verdict |
|---|---|---|---|---|
| **MBTiles 1.3** | SQLite file: `metadata` (name, value) and `tiles` (zoom, column, row in TMS order, blob) | SQLite (public domain), our own few queries | spec text CC BY 3.0 US, "use in products and code is entirely free" (S [MBTiles]) | **yes**: import and cache store |
| **PMTiles v3** | one file, header + compressed directories + tiles, read by offset; HTTP range friendly | our own reader from the spec, or the reference C++ header | spec CC0; reference implementations BSD-3 (S [PM-repo]) | **yes**: import (read-only) |
| Directory tree `z/x/y.png` | plain files | none | none | **import source only** (decided 2026-11-05): converted on the board into one MBTiles, never kept as many small files on the SD card |
| Mapsforge `.map` (OpenAndroMaps) | vector, rendered on the device with a theme | Mapsforge library (Java, LGPL-3.0) or our own reader from the spec | maps: private use only, no bundling without written permission, no direct links (S [OAM]) | no (renderer effort, LGPL, terms) |
| OsmAnd `.obf` | vector + POI + routing | OsmAnd core (GPL-3.0) | GPL code | no |
| Organic Maps `.mwm` | vector | Organic Maps (Apache-2.0 code; `.mwm` under a separate licence, S [Organic]) | mixed | no (GPU renderer, own toolchain) |

### 2.5 Sizes

Tile counts from Web Mercator (computed, I): a 256 px tile is 409 m wide at zoom 16 and 205 m at zoom 17 at 48 N.
Raster tile size: about 12.6 KB on average served by the OSMF CDN (derived from 1.7 PB / 135 billion requests in
August 2026, S [OSM-disk]); urban tiles are larger, rural smaller; 10-20 KB is used below. Vector tile sizes are an
estimate (I, P0 measures them with `pmtiles extract --dry-run`).

| Area | Zooms | Raster tiles | Raster size | Vector (z0-14, shown deeper by over-zoom) |
|---|---|---|---|---|
| A walk, 5 x 5 km | 0-17 | about 1,000 | 10-20 MB | 83 tiles, 2-5 MB |
| A city, 20 x 20 km | 0-16 | about 3,500 | 35-70 MB | 335 tiles, 10-25 MB |
| A city, 20 x 20 km | 0-17 | about 13,300 | 130-270 MB | the same |
| France, 550,000 km2 | 0-14 | about 260,000 | 2.6-5 GB (likely less: rural tiles) | about 1-3 GB (BBBike ratio x 4.8 GB PBF, S [Geofabrik]) |
| France | 0-16 | about 4.1 million | 40-80 GB: not practical | - |
| Planet | 0-15 | - | - | about 120 GB (Protomaps, S) |

Each extra zoom level multiplies the raster count by about 4. The practical raster limit on a 32 GB SD card is a
region or a few cities at zoom 16-17; a country fits only at zoom 13-14. Vector archives remove this limit, which is
the main argument for a later vector phase.

### 2.6 Update cadence and attribution

| Source | Cadence | Attribution the app shows |
|---|---|---|
| OSM data in general | continuous | "© OpenStreetMap" (or "© OpenStreetMap contributors"), linked or explained on an About/"Data licences" page (S [OSMF-attr]) |
| OpenFreeMap | weekly planet | "OpenFreeMap © OpenMapTiles Data from OpenStreetMap" |
| Protomaps builds | daily | OpenStreetMap |
| Geofabrik / BBBike | daily data; Shortbread on Geofabrik not updated | OpenStreetMap, ODbL |
| OpenTopoMap | re-rendered by the server, no fixed cadence stated | the OpenTopoMap line above |
| Thunderforest | the provider's | Thunderforest + OpenStreetMap contributors |

Attribution rules for apps (S [OSMF-attr]): in a map corner, or a start-up splash; may collapse after 5 s or on the
first pan, then an "(i)" or an About entry must lead to it. Proposal: a one-line credit in a corner that collapses
on the first pan, plus a "Data licences" page listing every source in use.

### 2.7 How the board stores tiles "automatically"

Three paths (decided 2026-11-05, D3 and D10), all into the shared folder (6.2):

1. **Viewing cache (OpenTopoMap, Thunderforest with the user's key).** It is **persistent on the SD card**: every
   tile fetched while the user looks at the map is kept in the source's cache store and is still there after quitting
   Deck Atlas or rebooting. It stores **only tiles actually viewed, never pre-fetched** (no area or zoom stack fetched
   ahead). Shown again from the store; the server's expiry headers (`Cache-Control`, `Expires`, `ETag`) are honoured:
   expired tiles are revalidated with `If-None-Match` / `If-Modified-Since` when online, and shown stale when offline
   **only if the source allows offline use**. Eviction: least recently used, when the cache reaches its cap (the
   smaller of 512 MB and 10 % of free space, D10). The cap applies to the cache only. A 1-tile ring around the view
   (smooth panning) only where the source record sets `prefetch_ok` (neither of these two sources by default, I).
2. **Download this area (Geoapify with the user's key only, 0.2).** A rectangle (the current view or a drawn box) and
   a zoom range; the page shows the tile count and an estimated size before it starts; rate-limited (at most 4
   parallel requests), resumable, cancellable; refused for a source without `bulk_ok`, and stopped by the daily cap
   (about 12,000 tiles a day on the free plan, S [Geoapify-price]). Downloaded tiles land in the Geoapify store and
   count against the cache cap (I, to settle in 0.2: a separate "downloaded, not evicted" mark is an option).
3. **Import (always on).** An MBTiles or PMTiles file, or a `z/x/y` tile folder, from a USB stick (exFAT) or the SD
   card. An archive file is either copied to the SD card (recommended) or read in place on the stick; when the stick
   is gone the map shows "archive missing" for it and falls back to the other sources. A **tile folder is converted on
   the board into one MBTiles** (one SQLite file, batched writes) to spare the SD card thousands of small files; the
   conversion runs at idle priority with a progress line (its speed on a Zero 2 W: P0, section 13). The import screen
   tells the user that they are responsible for the terms of what they import; the map shows "© OpenStreetMap
   contributors" for an imported folder whose source is unknown. Imported archives are **never evicted**: Settings lists
   their sizes and deletes them on request. The PC side (outside the app): MapTileDl [MapTileDl] (output assumed to be
   a `z/x/y` folder, I), `pmtiles extract` (BSD-3) to cut a region from Protomaps or Mapterhorn, BBBike/Geofabrik
   downloads, QGIS raster MBTiles at a higher DPI.

**Per-source policy record** (proposal, a small text file per source, shipped and user-added): name, URL template,
raster or vector, image format, tile size (256/512), min and max zoom, attribution text, `offline_ok`, `bulk_ok`,
`max_bulk_tiles`, `daily_cap`, `prefetch_ok`, minimum TTL, an API key field, the User-Agent (always
`<app-id>/<version> (+https://github.com/OSRdesign/cyberdeck-zero-apps)`).

**Three guards (from prior art, 12.2; decided 2026-11-05):** (1) **blocked-tile image:** a download or a viewing
fetch stops when different tiles come back with identical bytes (an error picture: the OSMF servers send an "Access
Blocked" image with HTTP 200, S [OMD]), except plain one-colour tiles such as open sea; (2) **missing-tile cache:**
absent tiles (404) are remembered per source, so a second pass does not ask again; (3) **daily cap:** a source's
`daily_cap` (Geoapify) is counted per day (UTC assumed, I) and stops further downloads until the next day.

**Store format options for the cache:**

| Option | Pros | Cons |
|---|---|---|
| (a) Directory tree `cache/<source>/z/x/y.png` + an index file | no library; trivial | thousands of small files: slow to count, evict and delete on SD; index must be kept in step |
| (b) **One MBTiles file per source** + an extra table (tile key, last use, size, ETag, expiry) | one file per source; standard (QGIS, tileserver-gl, other apps read it); atomic transactions; the shared store of section 6 | needs SQLite (link or `dlopen("libsqlite3.so.0")` like Home Assistant opens libcurl, V); write amplification if every view writes |
| (c) Own append-only pack file + index | fewest writes | our own format, compaction code, nobody else reads it |

Proposal, confirmed by the owner's storage decision (D10, 2026-11-05: SQLite archives, batched writes, no tiny
files): **(b)**. SD-friendly rules: WAL journal with `synchronous=NORMAL`, commits batched (every few seconds or
tens of tiles), the "last used" time kept in RAM and written at most once per tile per day, a checkpoint on exit.
Typical writes: 2,000 new tiles in a day of use is about 30 MB (I): small.

## 3. Rendering on LVGL with little RAM (question 2)

### 3.1 What the toolkit already has (V)

- LVGL 9.5 with `LV_USE_VECTOR_GRAPHIC`, `LV_USE_THORVG_INTERNAL` (ThorVG, MIT), `LV_USE_LODEPNG`, NEON software
  blending (`LV_DRAW_SW_ASM_NEON`), an LVGL image cache of 1 MB (`LV_CACHE_DEF_SIZE`), FreeType text
  (`projects/APPLaunch/config_defaults.mk`).
- RSS Reader decodes JPEG, PNG, GIF and BMP with stb_image 2.30 on a worker, one decode at a time, into RGB565, with an
  allocator ceiling and `malloc_trim(0)` after each decode (`apps/rss-reader/docs/IMAGES.md`).
- Host timing there: a 3 MP baseline JPEG in 34 ms; the Zero 2 W is estimated 8-12x slower (V for host, I for board).

### 3.2 The raster path

- **Decode cost (I, P0 measures it).** 256 x 256 = 0.066 MP: a JPEG tile about 1 ms on the host, so about 6-12 ms on a
  Zero 2 W; PNG (inflate + filters) perhaps 2x that. A full 640x480 view at 1x needs at most 4 x 3 = 12 tiles; cold,
  about 0.1-0.3 s on two workers. Panning decodes only the new column or row.
- **Memory per board** (decoded RGB565 tiles in an LRU, 25 MB VmRSS rule for the Zero 2 W):

  | Board class (from `board.state` `ram_mb`) | Tile RAM cache | Tiles at 1x | Tiles at 2x |
  |---|---|---|---|
  | 512 MB (deck, Pi 3A+, zero8) | 6-8 MB | 48-64 | 12-16 |
  | 1-2 GB (Pi 4B, HackBerry CM5) | 24 MB | 192 | 48 |
  | 4 GB and more (Pi 5, tablet 3.8 GB) | 48 MB | 384 | 96 |

  The app base is about 13-15 MB for a kind A app (V, guidelines section 1), so 6-8 MB of tiles keeps the deck under
  25 MB. The encoded tile bytes stay on disk (the page cache does the rest).
- **Density, the important finding.** Web map tiles are drawn for about 96 dpi (3.8 px/mm). Our panels (I, from
  nominal diagonals; the deck's 11.3 px/mm is V): deck 11.3, HackBerry about 10, HyperPixel 4" about 9.2, Pi 3A+
  4.3" DSI about 8.6, tablet 7" about 8.5, Pi 4B 5" about 7.3, zero8 3.5" 6.3 px/mm (V: 79x49 mm, the launcher's
  `p320` render profile; on the board the app reads `board.state` `ppmm`). At 1:1 an 11 px map label
  is 1.0-1.7 mm tall: unreadable on most of them. Options:

  | Option | Look | Cost |
  |---|---|---|
  | Magnify at decode time (2x nearest or bilinear; 1.5x bilinear) | soft text, still readable; shows a quarter of the area | 4x the RAM per tile (512 KB at 2x) but fewer tiles on screen |
  | 512 px "@2x" tiles (rendered with labels for high density) | crisp | only from sources that offer them, or files rendered on a PC with a scale of 2 |
  | Draw scaled by LVGL at every frame (`lv_image` scale) | the same as magnifying | per-frame software transform on a Zero 2 W: likely too slow when panning (I) |
  | Vector rendering at the panel's density | crisp at any size | section 3.3 |

  **Decided 2026-11-05 (D5):** magnify at decode time by an automatic factor from the panel density
  (`board.state` `ppmm`), the nearest of 1x, 1.5x and 2x, with a Settings override Auto/1x/1.5x/2x; prefer 512 px /
  `@2x` tiles where the source has them, and archives rendered at a higher DPI on a PC (QGIS "Generate XYZ tiles
  (MBTiles)" has a DPI setting, S [QGIS-xyz]) so the board does not have to magnify them (addendum, 12.4). Worked
  example only (I, not the rule): density / 3.8 gives about 3.0 on the deck (capped to 2x), 1.7 on zero8 (1.5x or
  2x). The exact rule (rounding, cap, the factor applied to `@2x` tiles) is a design pass; P0 checks legibility at
  each factor on the deck and zero8 (section 13).

### 3.3 Vector rendering options

| Option | Licence | Fit |
|---|---|---|
| Our own MVT renderer: vtzero + protozero (BSD-2, header libraries, S [vtzero]) to decode, ThorVG through LVGL's vector API (MIT, already linked) or our own scanline fill for polygons and lines, FreeType for labels, a small fixed style | all permissive | feasible; polygons and lines are the easy part; **labels** (placement along roads, collisions) are the bulk of the work; render each tile once into an RGB565 cache at the panel's density. Zero 2 W time per urban tile unknown (I, spike) |
| libosmscout (C++, offline rendering and routing) | LGPL (S [libosmscout]) | its own import format; LGPL is not GPL, but the library must stay dynamically linked and replaceable; heavy |
| MapLibre Native | BSD-2 | Linux needs OpenGL ES 3.0, Vulkan or WebGPU (S [MLN]); the Zero 2 W and Pi 3A+ GPUs offer OpenGL ES 2.0 only (I); our apps draw through fbdev: **out** |
| Mapsforge | LGPL-3.0, Java | **out** |

Decided 2026-11-05 (D4, option b): no vector work before 0.2 ships; then a 0.5 ws spike (decode and draw one urban z14 tile on the deck,
time and RAM) decides.

### 3.4 Projection, scale bar, overlays, day and night

- **Web Mercator (EPSG:3857):** tile x from longitude, tile y from `ln(tan(lat) + sec(lat))`, latitude limited to
  +-85.0511; MBTiles stores rows in TMS order (row = 2^z - 1 - y). Ground resolution in metres per pixel =
  156,543.03 x cos(latitude) / 2^z for 256 px tiles (25.6 m at z12, 1.6 m at z16 at 48 N). All in the map engine
  (3.7, plain C/C++ without LVGL for this part), tested on known points in the toolkit's tests.
- **Scale bar:** from the ground resolution at the screen centre and the display scale; rounded to 1-2-5 steps;
  metric or imperial (Settings).
- **Overlays:** GPS dot with an accuracy circle and a heading wedge; the current track (decimated with
  Douglas-Peucker per zoom); waypoints; Mesh Hop nodes (6); Wi-Fi/BLE sightings only if wardriving is resumed (5.7,
  deferred). Lines through LVGL's line or
  ThorVG path drawing; at most a few thousand points per frame, pre-projected per zoom.
- **Day/night:** raster tiles cannot be restyled; a night view = a dim/darken pass applied at decode time (cheap) or
  dark tiles from a source that has them; NavBoxLib's lightness-inverting lookup table (12.1) is another cheap option
  for the design pass. The app's own chrome follows the app palette.

### 3.5 Pan and zoom by touch, keys and pointer

| Input | Pan | Zoom | Other |
|---|---|---|---|
| Keyboard (the deck) | arrows: a quarter screen per press, faster when held | `+` / `-` (and Page Up / Page Down) | a follow key, a page key for each page; letters by key code (guidelines) |
| Touch (Pi 3A+, all) | drag | on-screen `+` / `-` buttons, double tap to zoom in | a locate button; no gesture-only action (guidelines) |
| Two-finger pinch | - | zoom | only where `cp0_lvgl` delivers two contacts (HackBerry and the tablet report 2, V); not checked in the toolkit (I): not needed for 0.1 |
| Trackpoint / mouse (pointer navigation) | arrives as arrow keys (APP-CONTRACT 10b, V) | wheel arrives as Up/Down; a held click is Ctrl+O, the options key | no code needed |

The map widget keeps an offset and blits the visible tiles; a pan only invalidates; LVGL partial refresh does the rest.

### 3.6 Recommended minimal path

Raster PNG/JPEG via stb_image (same build flags as RSS Reader), RGB565 at the display scale, an LRU sized from
`board.state` `ram_mb`, one LVGL map widget, MBTiles (SQLite) and PMTiles readers, overlays drawn on top: all of it
the map engine in `cp0_lvgl` (3.7), not in the app's `core/`; Deck Atlas keeps its GPS reader, pages, imports and
downloads UI in its own `core/` and `ui/` (and wardriving there too, if it is resumed). No WebP, no vector, no imagery in 0.1. From prior art (12.3): 8-bit palette PNG in the archives we prepare; the
parent tile magnified as a placeholder while a tile loads; tiles queued newest first; a loader thread that writes only
into the cache slot it was handed, with slots allocated on the LVGL thread (no lock around the LRU).

### 3.7 Where the engine lives: the toolkit (owner decision 2026-11-05)

**Place.** A new module of `ext_components/cp0_lvgl` in the launcher repo, MIT like the rest of the toolkit (SPDX
headers), with host tests in `ext_components/cp0_lvgl/tests/run_tests.sh` and its interface documented in
`docs/cp0_lvgl.en.md` (the toolkit's own rule for public interfaces, `ext_components/cp0_lvgl/AGENTS.md`, V). It is
compiled only when a Kconfig switch asks for it (proposal: `CONFIG_CP0_LVGL_MAP`, after the pattern of the
`CONFIG_CP0_LVGL_INIT_*` service switches, V), so the launcher and the apps that do not draw maps carry none of it (I:
how unused toolkit objects are dropped today is checked in P0).

**How the apps repo gets it (V, apps repo `bec05f3`).** Apps do not copy the toolkit: `apps/_common/LAUNCHER_REF` pins
one launcher commit, `build_app.sh` checks that the launcher checkout matches it and compiles that commit's `cp0_lvgl`
statically into each app (`apps/_common/scons/main.SConstruct`, `REQUIREMENTS = ['cp0_lvgl', ...]`). The guidelines'
rule: "a new reader or client in `cp0_lvgl` reaches an app only at a pin that has it" (a new widget the same way, I);
a pin is moved deliberately; only a change to the status strip's drawing forces a rebuild of every app. Pins on
launcher `main` between releases have precedent (the current `3f6579f`, main after v0.8.11). The launcher's
`docs/dev/deck/check_statusbar_drift.py` already treats a copy of toolkit files inside an app that links `cp0_lvgl` as
drift (exit 1, for the strip files); the engine follows the same principle: one source, no app copies. **Consequence,
in order:** (1) the engine is merged into launcher `main` (a launcher PR; no launcher release is needed, the launcher
itself does not use it); (2) an apps PR moves `LAUNCHER_REF` to that commit (the apps' "pin move" PR; the strip's
drawing does not change, so other apps move at their next rebuild, as with the 0.8.6 readers, V); (3) Deck Atlas 0.1
is built at that pin. Every later engine change repeats (1) and (2) before an app can use it, and Mesh Hop (0.4) needs
its own rebuild at a pin that has the engine.

**Public interface (sketch only: names, types and threading to be settled in 0.1; no implementation).**

```c
/* cp0_map.h: the map engine. Every call on the LVGL thread unless noted. */
typedef struct cp0_map_store cp0_map_store_t;     /* the shared tile store + source records (2.7, 6.2) */
typedef struct cp0_map_layer cp0_map_layer_t;

typedef struct {
    const char *store_dir;        /* NULL: $XDG_DATA_HOME/cyberdeckzero/maps/ */
    const char *sources_dir;      /* NULL: <store_dir>/sources/ ; extra records an app ships */
    const char *user_agent;       /* "<app-id>/<version> (+https://github.com/OSRdesign/cyberdeck-zero-apps)" */
    size_t      ram_cache_bytes;  /* 0: from board.state ram_mb (3.2) */
    int         read_only;        /* 1: never writes a cache store (a second app, 6.2 "one writer per file") */
    int         network;          /* 0: store and imported archives only, no fetching */
} cp0_map_store_cfg_t;

cp0_map_store_t *cp0_map_store_open(const cp0_map_store_cfg_t *cfg);
void  cp0_map_store_close(cp0_map_store_t *s);
int   cp0_map_store_sources(cp0_map_store_t *s, cp0_map_source_info_t *out, int max); /* records + archives */
int   cp0_map_store_cache_stats(cp0_map_store_t *s, cp0_map_cache_stats_t *out); /* RAM tiles, hits, misses, disk bytes, cap */
int   cp0_map_store_set_cap(cp0_map_store_t *s, uint64_t bytes);                   /* cache eviction only (D10) */
/* 0.1: import an MBTiles/PMTiles file (copied or read in place) or a z/x/y folder (converted into one MBTiles);
   imported archives are never evicted; progress via callback, off the LVGL thread */
int   cp0_map_store_import(cp0_map_store_t *s, const char *path, int copy, cp0_map_progress_cb cb, void *user);
int   cp0_map_store_remove(cp0_map_store_t *s, const char *archive_id);           /* Settings: delete an archive */
/* 0.2: area download; refused without bulk_ok, capped by max_bulk_tiles / daily_cap; progress via callback */
int   cp0_map_store_download(cp0_map_store_t *s, const char *source_id, const cp0_map_bbox_t *box,
                             int zmin, int zmax, cp0_map_progress_cb cb, void *user);

lv_obj_t *cp0_map_create(lv_obj_t *parent, cp0_map_store_t *s);
void  cp0_map_set_source(lv_obj_t *map, const char *source_id);
void  cp0_map_set_center(lv_obj_t *map, double lat, double lon);
void  cp0_map_get_center(lv_obj_t *map, double *lat, double *lon);
void  cp0_map_set_zoom(lv_obj_t *map, int zoom);
void  cp0_map_set_scale(lv_obj_t *map, float scale);           /* display scale; 0 = automatic (D5) */
void  cp0_map_pan_px(lv_obj_t *map, int dx, int dy);
void  cp0_map_set_position(lv_obj_t *map, const cp0_map_fix_t *fix); /* dot, accuracy circle, heading; NULL hides */

cp0_map_layer_t *cp0_map_add_markers(lv_obj_t *map, const char *name); /* nodes, waypoints, APs */
int   cp0_map_markers_set(cp0_map_layer_t *l, const cp0_map_marker_t *m, int n); /* lat, lon, kind, label */
cp0_map_layer_t *cp0_map_add_track(lv_obj_t *map, const char *name);
void  cp0_map_track_append(cp0_map_layer_t *l, double lat, double lon);
void  cp0_map_layer_show(cp0_map_layer_t *l, int on);

/* tile events: loaded, missing, refused by the source policy, error; delivered on the LVGL thread */
typedef void (*cp0_map_tile_cb)(void *user, const cp0_map_tile_event_t *ev);
void  cp0_map_on_tile(lv_obj_t *map, cp0_map_tile_cb cb, void *user);

/* projection helpers, no LVGL, usable from any thread */
void   cp0_map_latlon_to_world_px(double lat, double lon, int zoom, double *x, double *y);
double cp0_map_metres_per_px(double lat, int zoom, float scale);
```

What stays outside the engine, per app: the GPS reader (4.2, Deck Atlas owns the receiver), pages and menus, the
import and "Download this area" UI, tracks files, the clock offer, the `position.state` writer and reader (a small plain-C reader may
sit in `cp0_lvgl` beside `cp0_app_board_read()`, or in each app: a choice for 0.2/0.4, I). The layout and look of the
widget's own controls (buttons, attribution corner, scale bar) are a design pass (7.2).

## 4. GPS (question 3)

### 4.1 Receivers

| Receiver | Linux sees | Notes |
|---|---|---|
| USB u-blox 7/8/9 dongles (VK-162/172 style) | `/dev/ttyACM*` (cdc_acm), USB 1546:01a7/01a8/01a9 | NMEA at 9600 by default, UBX binary too; in Serial Console's device table (V). **The owner has a u-blox USB puck (2026-11-05):** whether it shows as `ttyACM*` (native USB) or `ttyUSB*` (a bridge chip) is not known yet; P0 records its USB id, port and speed (section 13) |
| USB GPS with a bridge chip (PL2303, CH340, CP210x; e.g. SiRF pucks) | `/dev/ttyUSB*` | 4800 or 9600; the bridge ids are generic: only NMEA content tells it is a GPS (V, Serial Console classifier) |
| UART HAT on GPIO 14/15 | `/dev/serial0` | competes with the Bluetooth UART setup on Zero 2 W / 3 / 4; PPS on a GPIO for precise time; a board config change: not proposed for 0.1 |
| Bluetooth GNSS (SPP) | an RFCOMM tty after pairing and binding | BlueZ 5 needs `rfcomm bind` (deprecated tool) or a D-Bus serial profile; later phase |
| A phone | NMEA or gpsd JSON over TCP/UDP on Wi-Fi | e.g. GPSd Forwarder (MIT, F-Droid) sends to a UDP port (S [GPSdFwd]); a later source type |

### 4.2 Reading: our own NMEA reader or gpsd

| | Own reader (proposed) | gpsd (BSD-2-Clause, S [gpsd-lic]) |
|---|---|---|
| Code | **exists, MIT:** viz1090 `core/gps` (listens read-only on each `ttyACM*`/`ttyUSB*` at the last good speed, then 9600, 4800, 38400, 115200; the first port that talks NMEA is the receiver, one with a fix wins; `$VIZ_DEV_DIR` test hook) and `core/nmea` (GGA, RMC, GLL, GSV, GSA, VTG with checksum), V | a system daemon; clients speak JSON on TCP 2947 (S [gpsd]); libgps is BSD too |
| Port discovery | Serial Console `scan_ports()`: sysfs ids, by-id names, skips the Bluetooth UART `/dev/serial1` (V) | hot-plug through udev rules |
| Multi-client | one owner per port (serial ports do not multiplex); others read `position.state` (6.3) | native |
| Time to the system | the app offers it (4.6) | chrony SHM refclock, the gpsd time-service HOWTO (S [gpsd]) |
| **Risk** | must not open other apps' ports: take only ports free under `flock` + `TIOCEXCL`, as Mesh Hop and Serial Console lock theirs (V) | Debian's rules match generic bridges such as CP210x 10c4:ea60 (S [gpsd-rules]) and gpsd then probes the port, changes its speed and steals data (S [suse-bug]): **LoRa boards for Mesh Hop and Serial Console devices use those chips** |
| Install | nothing | a daemon, a root config (`/etc/default/gpsd`) to disable USB auto-claim |

Decided 2026-11-05 (D6): own reader in Deck Atlas; gpsd not default, an optional, user-configured source later
(connect to a gpsd the user runs).

To extend the viz1090 parser: GSA (2D/3D, DOP), GSV per constellation (GP, GL, GA, GB/BD talkers), RMC date and
speed/course, GST when present (error estimate), and the u-blox UBX check from Serial Console's classifier.
**Rule (I, new):** never listen on a port whose USB id is a known LoRa/companion board or that another app holds;
probe unknown bridges only when the user asks ("Find my GPS"). Why even a read-only listen matters: the open itself
raises DTR (Mesh Hop's `serial_transport.hpp` says so, V), and ESP32 boards reset through DTR/RTS (Serial Console's
level 2 probe uses exactly that, V), so sniffing an idle LoRa board's port can reset it (I).

### 4.3 Discovery and hot-plug

- On start and every 2-4 s while no receiver is found: list ports (sysfs), skip locked ones, listen 1-2.5 s per
  speed, take the first that gives valid NMEA. Remember the receiver by USB identity (vid:pid/serial), never by path
  (guidelines section 14 "the device, not the port", V).
- What exists to reuse (V): Serial Console's `scan_ports()` / `resolve_port()` (sysfs walk, by-id key, follows a
  device that comes back on another `ttyUSB` number, a fake sysfs tree for tests); Mesh Hop's `list_serial_ports()`,
  `usb_identity()` and `choose_port()` (the last used vid:pid first, then known ids); viz1090's `GpsWatcher` (the
  listen loop and its stop within about 200 ms). Deck Atlas copies them into its own `core/` (each app owns its
  copy today; a shared `_common` serial module is possible but not proposed here).
- On unplug (read error, `ENODEV`): state "Receiver unplugged", back to searching; the track gets a gap, not a jump.
- Permission: `dialout` group; the message explains the fix (guidelines section 6, V; viz1090 reports `denied`).

### 4.4 Fix status display

A small strip on the map (no fix / 2D / 3D, satellites used, accuracy in metres) and a GPS page: the receiver and its
port, speed, fix type, satellites in view by constellation with signal bars (GSV), HDOP/VDOP, UTC time from the
receiver, age of the last fix, position in the user's format. Waiting states in plain words ("Searching: go outside,
first fix can take a few minutes", I).

### 4.5 Power

A USB GPS draws tens of mA (I). The deck has reset under heavy load before (Mesh Hop 0.6/0.7 warm-up, guidelines
section 14, V): the app does no heavy work when the receiver connects (indexing, cache cleanup run later at idle
priority), and the board test includes GPS + Wi-Fi tile fetching + display at once. A powered hub is the advice if it resets.

### 4.6 Time from GPS (bonus)

The deck and zero8 have no RTC (V); without network the clock is wrong after boot. Track logs (and wardrive logs, if
resumed) therefore use **the receiver's UTC** (RMC date + time), never the system clock (I, rule). Setting the system
clock:

| Option | Notes |
|---|---|
| (a) The app offers "Set the clock from GPS" when the clock is unsynced (Mesh Hop's `deck_clock` check: `timedatectl ... NTPSynchronized`, V) | `timedatectl set-time` refuses while automatic sync is on (I); `date -s` through the sudo prompt; once per boot |
| (b) gpsd + chrony SHM | system configuration; see the gpsd risk above |
| (c) The launcher does it (with GPS ownership, 6.4) | central; the launcher already has a sudo prompt and the time zone page (V) |

**Decided 2026-11-05 (D12): (a) in 0.2, an offer, never automatic.** "Set the clock from GPS" is offered only when
the clock is unsynced and only with a valid fix that carries date and time (RMC status `A` with a date, I: the exact
validity test in 0.2); a confirmation shows the GPS time before anything changes; then the sudo prompt runs
`timedatectl set-time`, with `date -s` as the fallback (when `timedatectl` refuses, for example while automatic sync
is on); the time zone is never touched. (c) later, if the launcher takes the GPS.

## 5. Wardriving (question 4) - DEFERRED

> **Deferred by the owner on 2026-11-05 ("skip wardriving for now"). Not planned.** This section is kept unchanged as
> the study for a later decision. Nothing in it is part of phases 0.1, 0.2 or 0.4, of the MVP or of the cost totals.
> Phase 0.3, decisions D7-D9, question 9, the wardriving tests (8.2) and the wardriving risks (9) are in the same
> deferred state. If wardriving is resumed later: about 1.25-1.75 ws (estimated before the small-screen decision;
> the Wardrive page would also need the 480x320 and 320x480 layouts, I).

### 5.1 What each board can scan

| Board | Wi-Fi | Bluetooth | Note |
|---|---|---|---|
| deck (Zero 2 W) | 2.4 GHz only (V) | BLE, same chip as Wi-Fi | the keyboard is Bluetooth: BLE scanning can make it stutter (V, Net Toolbox warning) |
| zero8 (Zero 2 W) | 2.4 GHz only | BLE, same chip | 480x320 / 320x480, supported since D11 (2026-11-05) |
| Pi 3A+, Pi 4B, Pi 5, HackBerry CM5 | 2.4 and 5 GHz (I) | BLE | |
| FZ-M1 tablet | Intel adapter, NetworkManager (I) | BLE | amd64 build |
| Any board + USB Wi-Fi adapter | a second interface (Net Toolbox's interface chooser, V) | - | scan on `wlan1` while `wlan0` stays connected |

### 5.2 How our apps scan today (V)

- **Wi-Fi (Net Toolbox 0.4, from Wi-Fi Survey):** `nmcli -t -e yes -f IN-USE,BSSID,SSID,MODE,CHAN,FREQ,RATE,SIGNAL,SECURITY
  device wifi list ifname <if> --rescan <mode>`, no root, no shell, its own process group. First list `--rescan yes`,
  then every 5 s `--rescan auto` (NetworkManager rescans only when its list is older than about 30 s); a refused fresh
  scan falls back to the cached list. Signal only as a percent; dBm estimated as -40 - 0.6 x (100 - pct).
- **Link level:** `/proc/net/wireless` and `/usr/sbin/iw dev <if> link` (walk test).
- **BLE (radar):** `bluetoothctl` driven on stdin (`menu scan`, `duplicate-data on`, `transport le`, `scan on`):
  address, type, name, RSSI, TX power, company id; reasons in plain words when Bluetooth is off or missing.

**Gaps for wardriving.** A fresh list only about every 30 s through `auto` (slow when moving); percent instead of
dBm; no first/last-seen per AP from the driver. Options:

| Option | Root | Notes |
|---|---|---|
| (a) nmcli `--rescan yes` every 10-15 s, accept refusals | no | the simplest; NetworkManager decides (I: refusal rate on the boards) |
| (b) + `iw dev <if> scan dump` to read the kernel's cached results (dBm, capabilities, "last seen" age) | probably no for dump (S [iw-dump], secondary; I) | triggering a scan needs `CAP_NET_ADMIN` (S [iwinfo]) |
| (c) A tiny helper with `cap_net_admin` set in postinst (as iwinfo's helper does, S [iwinfo]) | capability | the most control; a security-relevant install step: owner decision D7 |

Proposal: (a), with (b) if P0 shows the dump works for a normal user.

### 5.3 Monitor mode: out of scope

Monitor mode on the onboard chips needs patched firmware: nexmon (GPL-3.0) lists the Zero 2 W chip only for an old
5.10 kernel (S [nexmon]); our boards run Debian 13 kernels. External adapters with in-kernel monitor drivers exist,
but monitor mode needs root, takes the adapter from NetworkManager and is not needed for AP metadata. Never
deauthentication or injection.

### 5.4 Log formats

- **WiGLE CSV 1.6** (S [WiGLE-CSV]): UTF-8, RFC 4180; a pre-header line (`WigleWifi-1.6`, appRelease, model, release,
  device, display, board, brand, star, body, subBody), then the header
  `MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type`.
  `FirstSeen` is `YYYY-MM-DD hh:mm:ss` UTC; `AuthMode` bracketed tokens (example `[WPA2-EAP-CCMP][ESS]`); `Type` is
  `WIFI`, `BT`, `BLE` (and cell types we do not log); for BLE `Channel` is 0 and `MfgrId` the company id.
  One row per sighting (each helps WiGLE place the AP).
- **Kismet compatibility:** Kismet's own log is an SQLite `.kismet` file, and Kismet writes WiGLE CSV for upload
  (S [Kismet-log]); Kismet is GPL-2.0 unless a file says otherwise (its licence file, read for the addendum, 12.2). WiGLE CSV is the common ground: Kismet users can read it and
  WiGLE takes it. No `.kismet` writer is proposed.
- **Track:** an append-only line file (UTC, lat, lon, alt, speed, fix, accuracy), crash-safe; GPX 1.1 written on
  export (GPX is XML with closing tags: bad for a file that may be cut by a power loss).
- **AP summary (in the app):** one record per BSSID with first/last seen, best RSSI and the position of the best
  reading, kept in an SQLite table next to the CSV, for the overlay and the list.

### 5.5 Storage

A WiGLE row is about 150 bytes (I). A one-hour city drive with a scan every 10 s and 30 APs per scan: 10,800 rows,
about 1.6 MB; a one-hour walk with 15 APs per scan every 10 s: about 0.8 MB. Rows are buffered and appended every
10-30 s (one write, `fdatasync` at most once a minute): a few MB a day.

### 5.6 Scan interval and speed

At 5 km/h a 10 s cycle is 14 m between scans; at 50 km/h 139 m; a 2.4 GHz AP is typically heard over tens to a
hundred-odd metres (I). So: walking and cycling are fully covered, city driving is fine, fast roads leave gaps. Each
row takes the GPS fix closest in time to the end of its scan; the page shows "speed too high for the scan rate" above
a threshold (I).

### 5.7 Map overlay

- Dots per AP at its strongest reading, coloured by security (open / WEP / WPA / WPA2-3), never colour alone
  (guidelines); a density grid at low zoom (count per screen cell, a colour ramp); BLE as a separate layer.
- A list sorted by last seen or by RSSI with the AP's detail (BSSID, SSID, channel, security, vendor from the OUI
  table Net Toolbox already reads in parts, V).

### 5.8 Privacy and legal (brief)

- Collect only metadata a normal scan returns: BSSID, SSID, RSSI, channel/frequency, security, position, time. No
  payloads, no association, no probe-request capture, no deauthentication, no injection.
- **Local only by default; no upload code;** the user exports WiGLE CSV to a USB stick and uploads by hand if they
  choose.
- In the EU, the Article 29 Working Party (Opinion 13/2011, WP185) treated the combination of an AP's MAC address and
  its location as personal data (S [WP29], secondary summaries); GDPR applies to such data. Laws differ by country:
  the README says so in one line and points the user to local rules. WiGLE takes removal requests by BSSID
  (S [WiGLE-CSV]).
- A clear "Recording" indicator while logging; logs in the user's home only.

## 6. Mesh Hop reuses the map engine, and a shared contract (question 5; later phase, sketch)

### 6.1 What Mesh Hop has today (V)

- Nearby nodes with `has_pos`, `lat`, `lon` from adverts (`core/model.hpp`, at most 150), contacts with positions.
- `set_position(lat, lon)` (`SET_ADVERT_LATLON`) and the board GPS switch when the firmware lists `gps`
  (`core/client.hpp`).
- Its serial port locked with `flock` + `TIOCEXCL` (`core/serial_transport.cpp`).
- A `Tab::Map` that is never shown: "Map is not on Home and never shown: empty until it exists"
  (`src/ui/mesh_app.cpp`).

### 6.2 Shared store (proposal)

Guidelines forbid writing into another app's folder (V), so the shared place must be a documented neutral folder:

```
$XDG_DATA_HOME/cyberdeckzero/maps/        (default ~/.local/share/cyberdeckzero/maps/)
  sources/<id>.conf      tile source records (2.7); apps may add their own
  tiles/<source>.mbtiles cache stores; imported archives (or symlinks to a USB stick)
  overlays/<app-id>.csv  points an app publishes (one file per app, written atomically by that app only)
  index.json             written by Deck Atlas: archives, their bounds, zooms, attribution (readers ignore unknown keys)
```

`overlays/mesh-hop.csv` would carry `id, name, kind (node|repeater|room|waypoint), lat, lon, time_utc, source`; the
map app watches the folder (inotify) and draws each file as a layer. Readers open MBTiles read-only; one writer per
file. This is documented in APP-GUIDELINES (apps repo), not in the launcher contract, unless the launcher writes it;
the engine's interface page (`docs/cp0_lvgl.en.md`, 3.7) points to the same layout, since the engine reads it.

How Mesh Hop "shares the map" (updated for the owner decision of 2026-11-05):

- **Proposed for 0.4: engine reuse.** Mesh Hop links the toolkit's map engine (3.7) at a pin that has it. Its own
  empty Map tab (6.1) opens the shared store **read-only** (`read_only=1`: it shows what Deck Atlas imported, cached or
  downloaded; it writes no cache store, so "one writer per file" holds), draws its nodes, repeaters and rooms from its
  model as a marker layer, the deck's position from `position.state` as the dot, and `geo:` waypoints as another marker
  layer. Whether Mesh Hop may also fetch tiles online itself (with its own User-Agent, through the same source policy)
  is left open (I; proposal: not in 0.4). No widget or reader code is copied into Mesh Hop.
- **Kept as the minimal fallback and the interop path: (a) overlay file only.** Mesh Hop also writes
  `overlays/mesh-hop.csv`, so its nodes appear in Deck Atlas (and in any other reader) without either app linking
  anything more. If the engine cannot be linked into Mesh Hop for a measured reason (RAM or binary size on the deck,
  P0), 0.4 falls back to (a) alone.
- The earlier option "(b) a copy of Deck Atlas's widget and tile readers inside Mesh Hop" is dropped: two copies would
  drift (the reason `check_statusbar_drift.py` exists, 3.7).

The data contracts stay independent of the engine: the folder layout above, `position.state` (6.3), the overlay files
and `geo:` waypoints (6.4) can be read by any app, in any language, without linking `cp0_lvgl`'s map module.

### 6.3 `position.state` (draft)

In the state directory (`$XDG_RUNTIME_DIR/applaunch/`, a tmpfs on systemd boards (I): no SD writes), atomic
rename, at most 1 Hz, the same trust rule as `status.state` (`pid` alive):

```
v=1
gen=812
pid=2310
writer=<app id or launcher>
fix=3                   0 none, 2 2D, 3 3D
lat=48.856613
lon=2.352222
alt_m=35.2
speed_mps=1.4
course_deg=270.0
acc_m=4.5               estimated horizontal error
sats_used=8
utc=2026-11-05T10:15:30Z   time of the fix, from the receiver
mono_ms=123456789       CLOCK_MONOTONIC at the fix (age without trusting the wall clock)
source=usb:1546:01a8    or tcp:..., manual
```

An absent key means unknown (the `board.state` rule). A reader that sees `fix=0` or an old `mono_ms` shows "no GPS".

### 6.4 Who owns the GPS

| Option | Pros | Cons |
|---|---|---|
| A. Deck Atlas owns the receiver, writes `position.state` while it runs (`X-Background=run` while recording) | no launcher change; ships in 0.2 | Mesh Hop, viz1090 and others get a position only while Deck Atlas runs; only one process can hold the receiver: viz1090's watcher opens ports without a lock (V), so viz1090 and Deck Atlas cannot both use it |
| B. **The launcher owns it** (a thread or a small supervised helper, like the `status.state` thread), publishes `position.state`, `board.state` gains `gps=`, `launcher.sock` gains a `gps` status verb, a Settings > GPS page (receiver, on/off, set the clock) | one owner for every app; works at boot; the clock fix in one place; apps need no serial code | launcher work and a release; the launcher must claim only receivers identified as GNSS (never a LoRa board's port); more code in a process that must not crash (a helper process isolates it) |
| C. gpsd owns it, everyone speaks gpsd | standard, multi-client, chrony time | the udev claim risk (4.2); root configuration; a daemon on 512 MB boards |
| D. **A now, B later** with the same file | readers written once; B only when a second app needs the GPS | the move to B is a later launcher task |

**Decided 2026-11-05 (D6): D.** Deck Atlas reads the receiver and writes `position.state` (`X-Background=run` while
recording); the launcher may take ownership later with the same file; gpsd is not default. Mesh Hop reads `position.state` from 0.4 ("Use the deck's GPS" for its advert position, rate-limited:
on a 50 m move and at most every 5 min, I); viz1090 can switch from its own watcher to the file later.

**Over the mesh:** only positions and waypoints. Proposal for a waypoint: a text message carrying a `geo:` URI
(RFC 5870, `geo:48.8566,2.3522`) that Mesh Hop shows with an "On the map" action: the point is drawn on Mesh Hop's own
Map tab (the engine's waypoint layer) and written to its overlay file, so Deck Atlas shows it too. Tiles never go over LoRa (a city tile set is tens of MB; LoRa carries a few hundred bytes per packet).

## 7. App shape (question 6)

### 7.1 Name

**Decided 2026-11-05 (owner): Deck Atlas**, app id and package `deck-atlas` (settings in `~/.config/deck-atlas/`,
own data in `~/.local/share/deck-atlas/`, User-Agent `deck-atlas/<version> (+https://github.com/OSRdesign/cyberdeck-zero-apps)`).

### 7.2 Screens (proposals; layout is a design pass)

| Page | Content |
|---|---|
| Map (home) | the map; status chip (fix, sats, accuracy); `+` / `-` / locate buttons; scale bar; attribution corner; a recording dot |
| Layers & downloads | sources (on/off, cache size), imported archives (size, delete, "archive missing"), "Import" (file or tile folder, with the terms notice, 2.7), "Download this area" (Geoapify only, 0.2: zoom range, count, size, daily cap left), clear cache |
| Tracks | record / stop, list (date, length, duration), show on map, export GPX to USB |
| Wardrive (0.3, **deferred**) | not built; the study's page (start/stop, counters, list, overlay switches, WiGLE CSV export) waits with section 5 |
| GPS | receiver, fix, satellites by constellation, DOP, UTC, "Set the clock from GPS" (0.2, 4.6), "Find my GPS" |
| Settings | units, coordinate format, display scale (Auto/1x/1.5x/2x), night view, cache cap, API keys (Thunderforest, Geoapify), data licences, about |

**Sizes (decided 2026-11-05, D11: small screens from the start).** The owner's layout rules for every size: a compact
toolbar, thumb-sized controls (at least the `board.state` `target` token, 7 mm, I), no side panels. The rows below
are the sizes 0.1 lays out and tests; the drawings are a design pass.

| Canvas | Boards | Layout notes (proposals, design pass) | Added cost in 0.1 (I) |
|---|---|---|---|
| 640x480 | deck (11.3 px/mm), a 640x480 3.5" panel | the reference layout; map buttons in a column at the edge | in the base |
| 800x480, 720x720, 1280x800 | Pi 3A+ DSI, Pi 5 HyperPixel, HackBerry, tablet | the same layout with more map; no side panels either | in the base |
| 480x320 (landscape, X-MinSize) | zero8, Pi 3A+ 3.5" SPI (6.3 px/mm) | compact toolbar, fewer buttons on the map (zoom and locate), status chip shortened; list pages one column; dialogs at full width | +0.25-0.35 ws |
| 320x480 (narrow portrait, X-Portrait) | zero8 / a 480x320 panel with PDA mode on (upright, no writing band, `APPLAUNCH_PDA=0`) | the launcher's narrow portrait rule (v0.9.4: 40 px top bar without a title, Esc 44x30, smaller dialogs and text); map buttons in a bottom row; one column everywhere | +0.25-0.4 ws (shared with 480x320: one compact layout family) |
| 480x640, 480x800 (portrait) | Pi 3A+ / Pi 4B DSI portrait, tablet 800x1280 | buttons in a bottom row; with PDA mode on the app draws its own writing band (APP-CONTRACT section 3) | in the base (it was planned before) |

Together the two small sizes add +0.5-0.75 ws to 0.1 (layouts, scenes, a second compact page family, I).

Sketch at 640x480 (not to scale):

```
+----------------------------------------------+
| Deck Atlas     3D 9 sats +-4 m      [Esc]    |
|                                              |
|                 (map tiles)              [+] |
|                     o<- you              [-] |
|          . . track . .                   [o] |
|                                              |
| 200 m |-----|              (c) OpenStreetMap |
+----------------------------------------------+
  Ctrl+O / hold 3 s: Layers, Tracks, GPS, Settings
```

Portrait (480x640 and larger, and 320x480): the same map, buttons in a bottom row. The page menu by Ctrl+O and the
3 s hold box listing every action (guidelines section 14), plus a visible menu button for touch.

### 7.3 Controls

| Action | Keyboard (deck) | Touch | Pointer |
|---|---|---|---|
| Pan | arrows | drag | motion (arrows) |
| Zoom | `+` / `-`, Page Up / Page Down | buttons, double tap | wheel only if mapped (I) |
| Follow me | `F` (key code) or Enter on the locate button | locate button | click on it |
| Pages | Ctrl+O box, then Up/Down/Enter | menu button or 3 s hold | held click (Ctrl+O) |
| Back / quit | Esc (root: quit, guidelines) | header Esc | - |

Text input is rare (a source URL, an API key, a waypoint name): the shared on-screen keyboard rule applies (V).

### 7.4 Packaging (draft)

- `.desktop` (`deck-atlas.desktop`): `X-Fullscreen=true`, `X-MinSize=480x320`, `X-Portrait=320x480`,
  `X-Background=run` (track recording and the `position.state` writer continue while hidden; the app stops drawing
  and stops fetching tiles while hidden). Why these values (APP-CONTRACT section 6, V): `X-MinSize` is the landscape
  minimum and hides the tile on any smaller screen, so `480x320` keeps Deck Atlas on the 480x320 boards (and on a
  portrait launcher the check is against the turned landscape canvas, which is 480x320 there too); `X-Portrait` is the
  smallest portrait canvas the app supports, so one value `320x480` gives the upright canvas on zero8 in PDA mode and
  also on the larger portrait panels (480x640, 480x800, 800x1280). The contract asks for `X-Portrait` only in a build
  whose layout passes on the portrait canvases (480x640, 480x800): with 320x480 added, the 0.1 scenes cover all three.
  An older launcher ignores both keys, so the app still checks its canvas at start and says "needs a 480x320 screen"
  below that.
- `app.json`: `arch: ["arm64", "amd64"]`; `depends`: the base (`libfreetype6, libinput10, libxkbcommon0`) plus
  `curl, ca-certificates` (downloads through the `curl` program as RSS Reader does, V) and `libsqlite3-0` (or
  `dlopen` it, the Home Assistant pattern, to avoid touching the shared build, V). With the engine in `cp0_lvgl`
  (3.7) this choice is made once in the toolkit: either the engine `dlopen`s `libsqlite3.so.0` (no build change for
  any app; every app that uses the engine lists `libsqlite3-0` in `depends`) or the toolkit links `-lsqlite3` behind
  `CONFIG_CP0_LVGL_MAP` (the apps' SCons build, `apps/_common/scons/main.SConstruct`, and the cross sysroot then need
  the SQLite development files for arm64 and amd64). Proposal: `dlopen`, decided in P0; `recommends`: none for
  0.1-0.4 (`network-manager, bluez, iw` only if wardriving is resumed); `permissions`: `network`,
  `additional_hardware`.
- Data: `~/.config/deck-atlas/` settings; `~/.local/share/deck-atlas/` tracks; the shared maps folder (6.2) for
  caches and imported archives.
- Group: `dialout` for the GPS (documented, with the fix in the message).

### 7.5 Libraries and licences

| Component | Licence | Use |
|---|---|---|
| Our code | MIT | everything: Deck Atlas, and our own map engine in `cp0_lvgl` (the toolkit is MIT, SPDX headers, V); decided 2026-11-05 (question 13) |
| stb_image | public domain / MIT (V) | PNG/JPEG decode, inside the engine; its zlib inflater could also open PMTiles' gzip directories (I) |
| lodepng (already in LVGL, `LV_USE_LODEPNG`, V) | zlib (I) | an alternative PNG decoder P0 may compare; no new licence |
| SQLite (system `libsqlite3-0`) | public domain | MBTiles, used by the engine (`dlopen` or linked, 7.4) |
| PMTiles spec / reference C++ | CC0 / BSD-3 (S) | our reader, or the header |
| LVGL 9.5, ThorVG | MIT (in the toolkit) | drawing |
| vtzero, protozero (later, vector) | BSD-2 (S) | MVT decode |
| libwebp (later, terrain) | BSD-3 (I) | WebP |
| gpsd (optional external source) | BSD-2 (S) | not linked |
| Not used: Mapsforge (LGPL-3.0), libosmscout (LGPL), OsmAnd (GPL-3.0), Kismet (GPL-2.0), nexmon (GPL-3.0); from the addendum (12.2): NavBoxLib (GPL-3.0), Meshtastic device-ui (GPL-3.0), Trail Mate (AGPL-3.0), IceNav (GPL-3.0), osm-gps-map and Viking (GPL-2.0) | | named only, design references; nothing copied |
| Map data | ODbL (OSM), CC-BY (OpenMapTiles design), CC-BY-SA (OpenTopoMap), provider terms | attribution in the app and README |

### 7.6 Per-board feasibility

Updated for the owner decision of 2026-11-05 (D11: small screens from the start). The small-screen cost is in 0.1
(+0.5-0.75 ws, 7.2); the column says which layout of 7.2 each board gets.

| Board | Screen, input | RAM | Fit | Layout (7.2) | Notes |
|---|---|---|---|---|---|
| deck, Zero 2 W | 640x480, 11.3 px/mm, BT keyboard + touch | 512 MB (414 usable) | **yes** | 640x480 | 2x scale shows about 320x240 tile px; 2.4 GHz only |
| Pi 3A+ 800x480 DSI | touch only | 512 MB | **yes** | 800x480 (480x800 with PDA mode) | the touch-only reference |
| Pi 3A+ 480x320 SPI | touch, 6.3 px/mm | 512 MB | **yes** (was hidden) | 480x320 compact; 320x480 with PDA mode | `X-MinSize=480x320` (7.4) |
| Pi 4B 1 GB, 480x800 portrait | touch | 1 GB | **yes** with `X-Portrait` | 480x800 | |
| Pi 5 + HyperPixel 800x480 | touch (+ keyboard) | 2-8 GB (I) | **yes** | 800x480 | fastest Pi |
| HackBerry CM5 720x720 | keyboard + touch (2 contacts) | about 1.8 GB (I) | **yes** | 720x720 | square: the map uses all of it |
| Panasonic FZ-M1, amd64 | 1280x800 (800x1280 PDA), touch + keyboard | 3.8 GB | **yes** (amd64 package) | 1280x800 / 800x1280 | Intel Wi-Fi |
| zero8, Zero 2 W 480x320 (320x480 portrait with PDA mode) | touch, 6.3 px/mm (V, `p320` profile) | 512 MB | **yes** (was "hidden unless cheap") | 480x320 compact and 320x480 narrow portrait | the P0 legibility test board for the low density (section 13); display scale likely 1.5x (I) |

## 8. Phases, cost and tests (questions 6 and 7)

### 8.1 Phases

Recomputed for the owner decisions of 2026-11-05. All costs are estimates (I), in worker sessions (ws) of about
0.4 M tokens; heavy-tier sessions may use more tokens per session than that ratio (I).

| Phase | Worker | Content | Cost (ws) | Owner test |
|---|---|---|---|---|
| P0 spike | savvy-heavy (was savvy-medium) | the plan in section 13: decode timing and RSS for 256/512 px PNG, palette PNG and JPEG tiles (PC host + the deck + zero8); MBTiles (SQLite) vs PMTiles (`mmap` vs `pread`) reads on generated fixtures; a 64- and 128-tile RAM LRU; NMEA fixture replay with the viz1090 parser extended and a live capture from the owner's u-blox puck; fake tile server with the blocked-tile fixture; vector tile sizes with `pmtiles extract --dry-run`; the weight of SQLite + PNG/JPEG decode in a second app on the deck (binary size, VmRSS of a stub built like Mesh Hop with the engine linked and a 3-4 MB tile cache vs 6-8 MB), `dlopen` vs link for SQLite (7.4), whether unused toolkit objects stay out of apps that do not enable `CONFIG_CP0_LVGL_MAP`, a symbol check for stb_image (`STB_IMAGE_STATIC`); **new (2026-11-05):** tile-text legibility at 1x/1.5x/2x on the deck and on zero8 at 480x320 and 320x480 with a real tile set, panel px/mm per board from `board.state`, the tile-folder-to-MBTiles conversion rate. Dropped with wardriving: `iw ... scan dump` | 1-1.25 (was 0.75-1) | 15 min: the probe on the deck and zero8 (ms per tile, VmRSS, the second-app stub), the legibility screens read at arm's length, the puck plugged in outdoors |
| 0.1 engine + viewer | savvy-heavy | **engine in `cp0_lvgl`** (launcher PR, 3.7): map widget, Web Mercator, RAM LRU, display scale (Auto/1x/1.5x/2x), imported MBTiles/PMTiles raster and the tile-folder import converted into one MBTiles, the persistent viewing cache store with source policy flags (OpenTopoMap, Thunderforest key) and the blocked-tile and missing-tile guards, marker/track/position layers, tile callbacks and cache stats, the Kconfig switch, toolkit tests, `docs/cp0_lvgl.en.md`; **apps pin move PR** (`LAUNCHER_REF`); **Deck Atlas 0.1** at that pin: GPS reader + status page + dot, scale bar, attribution, the import screen with the terms notice, archive list with sizes and delete, keys/touch/pointer, **layouts and scenes at 640x480, 480x320, 320x480**, 800x480, 720x720, 1280x800, 480x640, 480x800, BOARD-TESTS, README, icon, amd64 | 3.25-4.25 (was 2.5-3.25: +0.5-0.75 small screens, +0.25 folder import and conversion) | 20 min: plug the GPS outdoors on the deck and on zero8 (480x320 and 320x480); pan/zoom by keys and touch; import a tile folder from the USB stick; Wi-Fi off: cached and imported tiles still show; unplug/replug the GPS; pull the stick: "archive missing" |
| 0.2 downloads + tracks | savvy-medium | Geoapify area download (count, size, rate limit, resume, daily cap) and the cache cap and eviction in the engine (one more launcher PR + pin move), its UI in the app at all sizes; tracks (record, list, GPX export), `position.state` writer, "Set the clock from GPS" (4.6) | 1.75-2.25 (was 1.5-2: +0.25 small-screen layouts of the new pages) | 10 min: download a small area with a Geoapify key, a 10 min walk recorded, GPX opened on the PC, the clock offer on a board without network |
| 0.3 wardrive | - | **deferred by the owner (2026-11-05), not planned.** The study's content (nmcli scan loop, optional `iw` dump, BLE off by default on the deck, WiGLE CSV with GPS time, AP table, overlay, export) stays in section 5 | not in the totals; if resumed later: 1.25-1.75 | - |
| 0.4 Mesh Hop shows the map and its nodes with the engine | savvy-heavy | Mesh Hop rebuilt at a pin with the engine (both architectures); its Map tab shown: the shared store read-only, nodes/repeaters/rooms as a marker layer, the deck's position from a `position.state` reader ("Use the deck's GPS" for the advert position too), `geo:` waypoint action (Map tab + overlay file), Mesh Hop's overlay file for Deck Atlas; scenes at Mesh Hop's sizes, VmRSS check on the deck; shared folder docs. Fallback if P0 says the engine is too heavy for Mesh Hop: overlay file + reader only (the old sketch, 0.75-1.25) | 1.5-2.25 (unchanged) | 10 min with a MeshCore node: nodes on the Map tab over cached tiles, a `geo:` message shown on both maps |
| Docs | docs-writer | README rows, app page, data licences, the import terms notice, APP-GUIDELINES shared-folder paragraph | 0.25 | - |
| Optional V | savvy-heavy | vector spike 0.5 (after 0.2, D4), then an MVT renderer with a fixed style and labels | 3-4.5 | per phase |
| Optional L | launcher-dev | launcher-owned GPS (6.4 B), Settings > GPS, release | 1.5-2.5 + release | per release |

**Totals (estimates, I; recomputed 2026-11-05 for the owner decisions, wardriving excluded):**

| Total | ws (new) | tokens (new, estimate) | before (engine update, with 0.3) |
|---|---|---|---|
| MVP: P0 + 0.1 + docs | 4.5-5.75 | about 1.8-2.3 M | 3.5-4.5 ws, 1.4-1.8 M |
| through 0.2 | 6.25-8 | about 2.5-3.2 M | (through 0.3: 6.25-8.25 ws, 2.5-3.3 M) |
| through 0.4 (0.3 deferred) | 7.75-10.25 | about 3.1-4.1 M | 7.75-10.5 ws, 3.1-4.2 M |
| through 0.4 with the 0.4 fallback (overlay file only) | 7-9.25 | about 2.8-3.7 M | 7-9.5 ws |
| wardriving, if resumed later (not in any total) | 1.25-1.75 | about 0.5-0.7 M | the same |

The through-0.4 total is about the same as before although wardriving left it: the small screens (+0.75-1 ws in 0.1
and 0.2), the folder import (+0.25) and the bigger spike (+0.25) take the place of 0.3. Each engine change
after 0.2 that an app needs costs about 0.25 ws more for the launcher PR and the pin move (I). The orchestrator's
design pass (including the small-screen layouts) and reviews are extra.

### 8.2 Tests (most on the PC)

- **Fixtures:** recorded NMEA files (cold start without fix, a walk, a drive, four constellations in GSV, bad
  checksums, a wrong-speed garbage stream, an unplug mid-line, an RMC with a valid date and time and one without, for
  the clock offer); one of them recorded from the owner's u-blox puck in P0; replayed through a regular file
  (viz1090's `$VIZ_DEV_DIR` hook, V) and through pseudo-terminals (Serial Console's `sim` pattern, V).
- **Fake tile server:** Python stdlib serving generated tiles (each tile shows its z/x/y) with `Cache-Control`,
  `ETag`, 304 answers, 429/503, slow and dropped responses (RSS Reader's `tests/unit/local_server.py` is the precedent, V); the
  test asserts the User-Agent, the parallel request cap and that a `bulk_ok=0` source refuses an area download; a mode
  that returns one identical "blocked" image for every tile (the download must stop), 404s that are cached as
  absent, and a daily cap that stops a download (the three guards, 2.7); the viewing cache never fetches a tile that
  is not on screen.
- **Archives:** a script generates tiny MBTiles and PMTiles fixtures (raster PNG/JPEG, 256 and 512 px, TMS rows) and a
  `z/x/y` tile folder for the import and its conversion into one MBTiles; an archive whose file disappears ("archive
  missing"); eviction never touches an imported archive.
- **Units:** projection on known points, scale bar steps, display scale choice from `ppmm` (Auto/1x/1.5x/2x), LRU and
  cap (the smaller of 512 MB and 10 % of free space), policy flags. The engine's units (projection, LRU, cap, policy
  flags, readers, folder conversion) live in `ext_components/cp0_lvgl/tests` and run with the toolkit's tests; the
  apps keep their own.
- **Headless scenes** (`_common/headless`): every page by touch and by keys at the sizes of phase 0.1, plus a
  `lifecycle` scene for `X-Background=run`.
- **Small-screen render tests (new, D11):** `p320`-style scenes of every Deck Atlas page at **480x320** (the
  3.5" panel landscape, 79x49 mm = 6.3 px/mm, the launcher's `pi3a` render profile) and at **320x480** (the narrow
  portrait, `p320` profile: no rotation, `APPLAUNCH_PDA` 0 for the app), after the launcher's render goldens
  (`projects/APPLaunch/tests/render/README.md`, sizes `480x320` and `320x480`, V); each checks that nothing is cut, the
  Esc and the map buttons reach the `target` size, and no side panel appears; plus 480x640 and 480x800 portrait scenes
  (the contract's condition for `X-Portrait`, 7.4).
- **Wardrive (deferred, 5):** Net Toolbox's recorded `nmcli` and `bluetoothctl` outputs (V); a golden WiGLE CSV with
  CSV escaping (SSIDs with commas, quotes, UTF-8, empty hidden SSIDs); a scripted walk that joins scans and fixes. Not
  planned while wardriving is deferred.
- **Board tests:** short owner protocols (quality bar: the main path works), one per phase, as in the table.

## 9. Risks and non-goals

- **Licence drift:** provider terms change; the source records carry the flags, the README dates them.
- **Imported data terms (new, D3):** a tile folder or archive from a PC tool (MapTileDl, others) may come from a
  server whose terms forbid it; the app cannot check. The import screen says the user is responsible for the terms of
  what they import, and the map shows "© OpenStreetMap contributors"; the README repeats it.
- **Copyleft prior art:** the closest map apps are GPL/AGPL (12.2); they are read as design references only, and no
  code from them goes into the app (decided 2026-11-05, question 13).
- **Readability:** raster labels at 1.5x/2x are soft; if the owner finds them poor on the deck or zero8, `@2x` sources,
  high-DPI archives or the vector phase are the answers (D4, D5); P0 measures it first (section 13).
- **Small screens (new, D11):** 480x320 and 320x480 leave little room for the map with a toolbar and thumb-sized
  buttons; the compact layout family is a design pass before 0.1 code, and the render tests guard it; cost in 0.1.
- **Folder import on the board (new, D3):** converting a large tile folder into MBTiles on a Zero 2 W takes time and SD
  writes once; P0 measures the rate, the app shows progress and runs at idle priority; a very large folder is better
  converted on the PC (`mbutil`, `pmtiles convert`).
- **Storage:** raster archives grow by 4x per zoom; the download page must show size before starting; imported
  archives are never evicted, so Settings shows their sizes.
- **Serial port conflicts:** never probe ports held by other apps or known LoRa boards (4.2).
- **Power:** GPS + Wi-Fi fetching + display on a Zero 2 W (4.5).
- **Engine in a second app (from the 2026-11-05 decision):** SQLite + PNG/JPEG decode + a tile cache in Mesh Hop on
  the deck (about 414 MB usable, VmRSS rule 25 MB): RAM and binary size measured in P0; Mesh Hop gets a smaller tile
  cache (`ram_cache_bytes`), and falls back to the overlay file only if it does not fit (6.2).
- **Toolkit pin and version coupling:** Deck Atlas and Mesh Hop use the engine only at a `LAUNCHER_REF` that has it;
  every engine change is a launcher PR plus an apps pin move before an app sees it, and the two apps may sit at
  different pins for a while (each app compiles the toolkit statically, so they never break each other at run time).
- **API stability:** two apps call the engine; an interface change means changing both at the next pin move. Rule
  proposed: keep the 3.7 interface small, add calls rather than change them, document it in `docs/cp0_lvgl.en.md`, and
  cover it with toolkit tests. There is no shared library, so no binary ABI to keep stable across apps (static link,
  V); only the store's on-disk format and the state files must stay compatible between apps at different pins
  (versioned keys: `v=1` in `position.state`, an extra table in MBTiles).
- **Build changes:** linking SQLite into the toolkit would touch the shared SCons build and the cross sysroots (arm64,
  amd64); `dlopen` avoids it (7.4). Licences stay permissive: SQLite public domain, stb_image public domain/MIT,
  lodepng zlib (I), our engine MIT; no GPL code goes into the toolkit or the apps (NavBoxLib and the others in 12 are
  references only).
- **Deferred with wardriving (not current risks):** BLE scanning and the deck's Bluetooth keyboard (D8); scan load on
  power (4.5); the privacy and legal points of 5.8.
- **Non-goals:** routing and turn-by-turn, address search (needs a geocoding index), satellite imagery, monitor mode,
  packet capture, any upload, OSMF tile servers, a LAN tile server as a planned route (owner, 2026-11-05).

## 10. Not verified

- Tile decode times and the RAM peak on a Zero 2 W (P0).
- Two-finger touch delivery through `cp0_lvgl` (pinch).
- Panel densities other than the deck's and zero8's (nominal diagonals used; P0 reads `board.state` `ppmm` per board).
- Tile-text legibility at 1x/1.5x/2x on the deck and on zero8 (P0).
- Vector tile sizes per area and an MVT render time on the deck.
- Thunderforest `@2x` URLs and their caching headers; OpenTopoMap headers.
- The deck's power margin with a USB GPS plugged in.
- The owner's u-blox puck: native USB (`ttyACM*`) or a bridge chip (`ttyUSB*`), its speed and time to first fix (P0).
- MapTileDl's output: its page names no format; a `z/x/y` folder sync is inferred (I); its tile sources and terms are
  not stated. I did not read the page myself; this follows the owner's brief of 2026-11-05.
- The tile-folder-to-MBTiles conversion rate on a Zero 2 W (P0).
- Whether `timedatectl set-time` refuses on the boards while automatic sync is on (the `date -s` fallback covers it).
- The engine's weight in a second app (binary size, VmRSS of Mesh Hop with the engine), `dlopen` vs link for SQLite,
  and whether apps that do not enable the map module stay unchanged in size (P0).
- Deferred with wardriving: whether `iw dev <if> scan dump` works for the deck user, and how often NetworkManager
  refuses `--rescan yes`.
- From the addendum: the items listed in 12.5. (Kismet's licence text, listed here before, was read for the
  addendum: GPL-2.0 unless a file says otherwise, 12.2.)

## 11. Questions for the owner

All thirteen answered by the owner on 2026-11-05; question 9 by deferring wardriving.

1. **Scope and order (D1, D2):** one app, phases 0.1 map engine in the toolkit + viewer, 0.2 downloads + tracks,
   0.3 wardrive, 0.4 Mesh Hop shows the map and its nodes with the engine? **Decided 2026-11-05 (owner):** one app;
   the map app must not embed Mesh Hop; the map engine is built once as a reusable MIT component of `cp0_lvgl` with a
   small public interface (3.7); Deck Atlas is its first user and Mesh Hop, in 0.4, its second (its own Map tab shows
   the shared tile store and its nodes, no copy of the widget code); the data contracts (shared folder,
   `position.state`, overlay files, `geo:` waypoints) stay so any app can read positions without linking the engine.
   Phases 0.1, 0.2, 0.4; **0.3 deferred by the owner, not planned** (ids kept, nothing renumbered).
2. **Name (7.1):** **Decided 2026-11-05 (owner): Deck Atlas**, id and package `deck-atlas`.
3. **Tile sources (D3):** **Decided 2026-11-05 (owner):** option B without the LAN server. Imports always on
   (MBTiles, PMTiles, a `z/x/y` tile folder such as MapTileDl's output, converted on the board into one MBTiles; the
   import screen says the user is responsible for the terms; the map shows "© OpenStreetMap contributors"); opt-in
   OpenTopoMap (viewing cache), Thunderforest (own free key, viewing cache), Geoapify (own free key, area download,
   about 12,000 tiles a day on the free plan); the OSMF servers never; no LAN tile server (an optional advanced source
   at most, no work planned); Geoapify the only in-app area download, imported files the other offline route. The
   viewing cache is persistent on the SD card (2.7). The three guards: blocked-tile image, missing-tile cache, daily
   cap.
4. **A tile server on the PC, a USB stick:** **Decided 2026-11-05 (owner):** no PC tile server; a USB stick (exFAT)
   for imports. Test data for P0 and 0.1: section 13 (a synthetic tile set, a real raster set of a small area from the
   owner, the u-blox puck).
5. **Raster now, vector spike after 0.2 (D4):** **Decided 2026-11-05 (owner):** yes (option A of the question, D4 (b)).
6. **Display scale (D5):** **Decided 2026-11-05 (owner):** automatic from the panel density, the nearest of
   1x/1.5x/2x; Settings override Auto/1x/1.5x/2x; 512 px/`@2x` or PC-rendered high-DPI archives preferred where
   available; the exact rule in the design pass.
7. **GPS ownership (D6):** **Decided 2026-11-05 (owner):** Deck Atlas reads the GPS itself and writes
   `position.state` (`X-Background=run` while recording); launcher ownership later with the same file; gpsd not
   default (an optional user-run source later).
8. **Your GPS receiver:** **Decided 2026-11-05 (owner):** a u-blox USB puck (native ACM or a bridge chip: not known
   yet); used for the P0 fixtures, plus a recorded NMEA file for the PC tests.
9. **Wardriving (D7, D8, D9):** **Deferred 2026-11-05 (owner): "skip wardriving for now".** Section 5, phase 0.3,
   D7-D9 and the related tests and risks are kept in a deferred state, out of the MVP and the totals; if resumed
   later: 1.25-1.75 ws. The study's defaults stay on file (NetworkManager scans only, BLE off by default with a
   switch, logs local only, WiGLE export by hand, no upload code).
10. **Storage (D10):** **Decided 2026-11-05 (owner):** the shared folder `~/.local/share/cyberdeckzero/maps/`; cache
    cap the smaller of 512 MB and 10 % of free space, for the cache only; imported archives never evicted (Settings
    lists sizes and deletes); an archive on a USB stick read in place with an "archive missing" fallback, copying to
    the SD card recommended; SQLite archives with batched writes, no tiny files.
11. **Screens (D11):** **Decided 2026-11-05 (owner):** option B, small screens from the start: 0.1 designed for
    640x480, 480x320 and 320x480 (zero8 in portrait, the narrow portrait rule: 40 px bar, Esc 44x30), and the larger
    portrait canvases; `X-MinSize=480x320` and `X-Portrait=320x480` so the app is not hidden on the 480x320 boards
    (7.4); compact toolbar, thumb-sized controls, no side panels; the added cost is in 0.1 (7.2, 8.1).
12. **Clock (D12):** **Decided 2026-11-05 (owner):** "Set the clock from GPS" offered when the clock is unsynced, only
    with a valid fix carrying date and time, after a confirmation showing the GPS time; sudo prompt,
    `timedatectl set-time`, fallback `date -s`; never automatic; time zone untouched (4.6).
13. **Prior art (12):** **Decided 2026-11-05 (owner):** our own MIT engine in `cp0_lvgl`; NavBoxLib and the other
    GPL/AGPL projects (Meshtastic device-ui, Trail Mate, IceNav, osm-gps-map, Viking) design references only, nothing
    copied.

## 12. Prior art (addendum)

Added 2026-11-05. The owner pointed out a gap: sections 2-4 compared providers and formats, but not the open-source
projects that already store OSM tiles and show maps on small devices (the owner's pointer: NavBoxLib). Read on 2026-11-05
through the GitHub API (READMEs, docs, source files, licence files, last push dates) and the web. **In this section
V = read in the project's own repository (README, docs or source), S = read in another source, I = inferred.**
Nothing was copied from any project; GPL/AGPL projects are named as references only.

### 12.1 NavBoxLib (the owner's pointer)

- **What (V):** "Maps for embedded devices, tiling + markers + tracks + GPX for LVGL", by t413. C++, LVGL 8 and 9,
  PlatformIO; about 45 KB of source in `src/navboxlib` (map renderer, pixel buffer, layers, track log, geo point). It
  also runs on a desktop through a POSIX copy of the Arduino `SD` file API, which its unit tests use. Created
  2026-05-28, 58 commits, last push 2026-06-12, 3 stars. **Licence GPL-3.0**; the README says the author may move to
  LGPL later.
- **Where it is used (V):** the author's NavBoxESP firmware (GPL-3.0, last push 2026-06-13) for the M5 Cardputer
  (ESP32-S3) and the LilyGo T-Deck, with the M5 GPS unit or a LoRa cap that carries a GNSS receiver.
- **Fetching (V):** none; the library has no network code. NavBoxESP's README lists three ways to fill the SD card: a
  "global basemap" zip from its releases, the author's AlphaMap web downloader (its page does not say which tile
  server it uses: not verified), and the author's `osm2edgetx` script. The script fetches
  `https://tile.openstreetmap.org/{z}/{x}/{y}.png` by default (`--url` points elsewhere), one tile at a time with a
  100 ms delay and a User-Agent carrying the author's e-mail; re-runs skip existing files; there is no licence file.
  Fetching a radius over a zoom range from the OSMF server is the bulk download the OSMF policy forbids (2.1): **the
  default ignores the policy** (I).
- **Storage (V):** plain files, `<dir>/z/x/y.png`, standard 256 px OSM PNGs on the SD card (NavBoxESP: `/maps/osm`);
  the map picks the deepest zoom that has a tile at the centre with `SD.exists()`. The map writes nothing: no cache
  store, no eviction. NavBoxESP also embeds a small base tile set in its firmware and writes it to the card at first
  boot (count not checked).
- **Decode and render (V):** PNGdec (Apache-2.0) with a per-row callback into RGB565. It can **crop while decoding**
  (rows outside the visible part are skipped, only the needed columns copied); that is how the README claims 80-180 KB
  of tile RAM instead of 524 KB on boards without PSRAM. A RAM cache of `TILECACHE_SIZE` tiles (default 4) evicts the
  least recently used tile that is not on screen. One `lv_image` per slot points at the decoded buffer (no copy),
  placed with `lv_obj_set_pos` and scaled with `lv_img_set_zoom`. While a tile loads, **the parent tile (up to three
  levels up) is shown magnified**; queued tiles load newest first. Images wider than 256 px are refused. A "smart
  invert" night view inverts HSL lightness, boosts saturation and caches the result in a 4,096-entry RGB444 lookup
  table (8 KB).
- **GPS (V):** not in the library. NavBoxESP uses TinyGPS++ (LGPL-2.1) on a UART and probes each candidate pin pair
  for up to 1.2 s for a `$`; when the GPS date is invalid, its timestamps fall back to seconds since boot.
- **Verdict: design reference only.** GPL-3.0 code cannot go into our MIT apps, neither copied nor linked (7.5).
  Even under LGPL it would fit poorly: Arduino-style file API, a 4-tile cache, a 256 px limit (no `@2x`), no network,
  no cache store, no MBTiles or PMTiles (I). Ideas worth writing again in our own code: the magnified parent tile as a
  placeholder, newest-first loading, a lookup table for a night view. Crop-while-decoding matters little on our
  boards, which keep 6-8 MB for tiles (3.2) (I).

### 12.2 Comparison

| Project | What | Licence; last push | Tiles: where from, policy | Storage | Reusable? | Lessons for us |
|---|---|---|---|---|---|---|
| NavBoxLib + NavBoxESP (t413) | LVGL map library; GPS firmware for Cardputer and T-Deck | GPL-3.0; 2026-06-12 / 2026-06-13 | PC and web downloaders; `osm2edgetx` defaults to bulk from OSMF: **ignores the policy** (V, I) | `z/x/y.png` on the SD card | no: reference | 12.1 |
| Meshtastic device-ui ("MUI") | LVGL UI for Meshtastic devices with a map panel; also a Linux build that downloads with libcurl | GPL-3.0; 2026-11-04 | no default server: the user enters a URL template (a field, or a `.url` file per style) and every fetched tile is saved; no `ETag`/`Cache-Control` handling, no eviction; User-Agent `MUI/2.8 (+https://meshtastic.org)` (V). Starter zips (zoom 1-6) credited to Thunderforest, Geoapify and OSM (V); points users to a community tile service with its own server (S [MUI]) | `/maps/<style>/z/x/y.png` on FAT32/exFAT (exFAT recommended), plus an optional raster `<style>.pmtiles` read-only through the reference `pmtiles.hpp`, one cached directory per depth; tiles fetched later go beside it (V) | no: reference | PMTiles works even on an ESP32-S3 with the reference reader; 8-bit palette PNG to cut size (ImageMagick `-colors 256`); grey decoding on 2 MB PSRAM boards; a "save every tile" cache with no policy flags is what we avoid |
| Trail Mate (vicliu624) | offline-first handheld: maps, LoRa, GPS; ESP32 boards plus Linux builds, among them an APPLaunch-packaged Cardputer Zero app and a uConsole build | AGPL-3.0; 2026-11-03 (PC packer Trail Mate Center: AGPL-3.0) | devices: its own TMAP packages made by Trail Mate Center; Linux: on-demand download with defaults `tile.openstreetmap.org`, OpenTopoMap and Esri World Imagery, User-Agent `TrailMate-uConsole/0.1` without a contact, no caching-header code found (V): **offline use of OSMF tiles, against 2.1** (I) | Linux: files under `<sd>/maps/base` plus an SQLite index (`map_tile_cache`: status, HTTP status, bytes) (V) | no: AGPL | a map app already exists for the same launcher family; GPS and LoRa run as resident services that pages only read (like 6.4 B) |
| IceNav-v3 (jgauchia) | ESP32-S3/P4 GPS navigator, LVGL + LovyanGFX | GPL-3.0; 2026-11-01 | raster tiles **rendered on the PC from OSM data** with Maperitive, no tile server; vector `.nav` files per zoom from Geofabrik extracts with Tile-Generator (GPL-3.0) (V) | `MAP/z/x/y.png`, `NAVMAP/Z<zoom>.nav`; advice: 32 KB clusters and an rsync tool that writes Z, X, Y in order against fragmentation (V) | no: reference | rendering locally is the clean legal path; many small files are slow on SD cards (hence their tooling); a GPX logger with walk/bike/car sampling profiles |
| 0015 map_tiles + OfflineMapDownloader | ESP-IDF component for LVGL 9 (`map_view` widget, LRU tile cache, routes) and a Flask packager for the PC | MIT; 2026-09-27 | the packager (v2) **refuses the OSMF servers**; per-source records with `bulk_ok`, `bulk_limit_bytes` and the terms in words (checked 2026-09): self-hosted server, local MBTiles or folder, Geoapify, Stadia (100 MB cap), Thunderforest (bulk only from Small Business), MapTiler (no bulk); stops when identical bytes come back for different tiles; caches absent tiles; User-Agent with a contact (V) | `{base}/{style}/z/x/y.bin`: LVGL 9 RGB565, 12-byte header + 128 KB per tile, nothing to decode on the device (V) | design, and small parts under MIT with attribution; the code is ESP-IDF/FreeRTOS-specific (I) | the closest to our plan: it confirms the policy record (2.7); its loader task writes only the slot it was handed and slots are allocated only on the LVGL thread, so the cache needs no lock; placeholder on a miss; a prefetch ring. Raw RGB565 tiles are not for us: about 10x the PNG size (I) |
| Cyclenerd offline-map-tile-downloader | Go PC tool for MUI and MeshCore devices | MIT; 2026-09-15 | sources from a JSON file, no provider built in; 10 tiles/s and 4 workers by default, an honest User-Agent, "respect the tile server's Terms of Service" (V) | `maps/<style>/z/x/y.png`, optional 8-bit conversion (V) | a PC tool | leaves the policy to the user |
| Hogesyx lvgl_map_tiles_downloader | Python packager "for LVGL" | MIT; 2025-05-29 | **Google Maps tiles** (V); against Google's terms (I) | zip of `z/x/y.png`, 8-bit PNG (V) | no | what not to do |
| osm-gps-map | GTK map widget used by Linux GPS apps | GPL-2.0; 2026-10-11 | on-demand cache; built-in sources include OSM, OpenTopoMap, Google and Bing URLs; a bulk call `osm_gps_map_download_maps()` capped at 10,000 tiles per zoom; User-Agent `libosmgpsmap/<version>` (V); the policy is not enforced (I) | `<base>/<source name or md5>/z/x/y`, no eviction (V) | no: reference | the classic Linux design, and its gaps |
| Viking | GTK GPS data editor with maps | GPL-2.0; 2026-10-03 | per-source `use-etag` and a tile-age setting; "Download Maps in Zoom Levels" with built-in limits and a warning that links the OSM policy (V) | its own tile cache; MBTiles layers (V) | no: reference | per-source caching flags, as in our records |
| MOBAC; JTileDownloader | PC bulk downloaders and atlas makers | GPL-2.0 (MOBAC 2.4.0, updated 2026-10-19); GPL-3.0 (JTD 0.6.2, 2020) | both were blocked by the OSMF tile servers for bulk downloading (S [MOBAC], [JTD]) | many atlas formats | no | the OSMF enforces its policy against tools |
| Navit; QMapShack; Pure Maps + OSM Scout Server | navigation, GIS and offline phone maps on Linux | GPL-2.0 with LGPL parts; GPL-3.0; GPL-3.0 | Navit draws its own vector files made from OSM extracts (I); the others are Qt applications (I) | their own formats | no | vector drawing in plain C runs on small Linux devices (Navit, I); Qt stacks are too heavy for a Zero 2 W (I) |
| PC and server tools: go-pmtiles, mbutil, VersaTiles, mbtileserver, tileserver-gl, tilemaker, QGIS | convert, cut, serve, render | BSD-3 (2026-07-22); BSD-3 (archived); MIT (2026-11-04); ISC (2025-05-21); BSD-2 (2026-11-01); FTWPL (2026-11-03); QGIS is a GPL program used as a tool | the owner's own files: `pmtiles serve` (used in MUI's guide) and mbtileserver serve `z/x/y` from one archive; QGIS "Generate XYZ tiles (MBTiles)" renders any layer to raster MBTiles with a **DPI** setting (S [QGIS-xyz]) | MBTiles, PMTiles, folders | as programs, never linked | the PC side of "Import" and "own server" (2.7) |
| Kismet | Wi-Fi/Bluetooth capture and wardriving server with a web UI | GPL-2.0 unless a file says otherwise; 2026-11-05 | its web map loads online tiles (default OSMF) only after the user agrees to that tile server: viewing in a browser, no offline store (V) | `.kismet` (SQLite) and WiGLE CSV (S, 5.4) | no | its own serial GPS driver (automatic baud, NMEA, UBX, SiRF), a listen-only `gps=nmea` mode that "never sends anything to the GPS", raw NMEA over TCP, gpsd, the browser's location; "Only one process can open a serial or USB device" (V, `kismet.conf`) |
| Pwnagotchi (jayofelony fork) | Pi Zero handshake collector (bettercap, monitor mode) | GPL-3.0; 2026-09-28 | GPS map plugin: Leaflet in a browser (V) | a `.gps.json` per capture (V) | no | GPS through bettercap on a serial port; monitor mode stays out of scope (5.3) |
| Sparrow-wifi | Wi-Fi/Bluetooth analyser for Linux (Qt) | GPL-3.0; 2026-10-21 | map export is a Google Maps HTML page that needs an API key (V) | - | no | nothing for the map side |
| WiGLE Android; ESP32 wardrivers: ESP32 Marauder, wardriver rev3, Bruce, Evil-M5Project | phone app; ESP32 firmwares | BSD-3 (2026-11-02); MIT (2026-10-29); GPL-3.0 (2026-05-25); AGPL-3.0 (2026-11-04); no licence (2026-11-04) | no maps on the device found in the firmwares (I) | Marauder (`WigleWifi-1.6`), wardriver rev3 and Bruce write WiGLE CSV; Evil-M5Project ships WiGLE merge tools (V, code search) | format only | WiGLE CSV 1.6 is the common ground, as proposed (5.4) |

Searched without a result: an official LVGL map widget or an `lv_lib_*` map library (GitHub searches "lvgl map
tiles" and "lvgl openstreetmap", one web search, 2026-11-05); the only LVGL map components found are NavBoxLib and
0015 map_tiles. MapProxy (Apache-2.0) warns about the OSM policy in its docs but does not enforce it (V).

### 12.3 The four questions

**(a) How they get tiles onto the device, legally and automatically.**

- Three patterns: (1) **prepared on a PC and copied** (NavBox, IceNav, map_tiles, MUI starter sets, Trail Mate
  packages); (2) **an on-demand cache on the device** (MUI, Trail Mate on Linux, osm-gps-map, Viking); (3) **bulk
  download** on a PC or the device (osm2edgetx, MOBAC, JTileDownloader, osm-gps-map's call, Viking's dialog).
- Servers they point to: the OSMF server by default (osm2edgetx, Trail Mate on Linux, Kismet's browser map, the source
  lists of osm-gps-map and Viking); OpenTopoMap and Esri imagery (Trail Mate); Google tiles (Hogesyx, osm-gps-map's
  list); keyed providers (MUI's starter sets, 0015); self-rendered data (IceNav with Maperitive, 0015's first choice
  "self-hosted", the community service MUI points to).
- Policy: **only 0015's packager enforces the source terms** (refuses OSMF, per-source bulk flags, caps, the
  blocked-image check). Viking warns and limits; Cyclenerd and MUI leave it to the user; osm2edgetx and Trail Mate on
  Linux default to uses the OSMF policy forbids (bulk, offline). Of the projects read, only Viking uses `ETag` (per
  source). Our policy-first design (2.1, 2.7) is stricter than most prior art; 0015 shows it is practical.
- Legal ways that need no server of our own: render on a PC from OSM data (IceNav); keyed providers whose terms allow
  storing (0015 lists Geoapify and Stadia's 100 MB; Geoapify's own page says "you can cache, store, and redistribute
  the generated map tiles", S [Geoapify-tiles]); community bundles made from OSM data on their own server (the one MUI
  points to: free, rebuilt monthly, raster PMTiles or zip, zoom 0-15 per European country and US state, World zoom
  0-9 in 509 MB, France 10 GB; the page states no terms and no licence, S [Oxed]: check before we recommend it).

**(b) Storage layout on the SD card, and why.**

- Almost every microcontroller project uses `z/x/y` folders: FAT32/exFAT cards, no SQLite on an ESP32, simple PC
  tools (I). The cost shows in their own docs: IceNav asks for 32 KB clusters and ships an ordered rsync tool; MUI's
  guide warns that large regions mean very many files (a community post reports over 800,000 files for one country
  set, S).
- MUI added a read-only **PMTiles** archive per style (from 2.8.1) and keeps `z/x/y` only for tiles fetched later; it
  never writes the archive (V). Trail Mate on Linux keeps files plus an **SQLite index**. No device project writes
  tiles into MBTiles (no SQLite on microcontrollers, I).
- For our boards (ext4 on the SD card, SQLite available): **no change**. One MBTiles file per source for the cache,
  PMTiles/MBTiles imported read-only (2.4, 2.7), batched commits (2.7). Prior art confirms the reasons: one file instead
  of hundreds of thousands, eviction by one SQL statement, no per-file block slack (a 12 KB tile takes 12-16 KB as a
  file on ext4, I). Two additions: a `z/x/y` folder from another tool is converted on the PC (`pmtiles convert`,
  VersaTiles, mbutil) or read in place from a stick, never copied tile by tile onto the SD card; sticks use exFAT,
  because FAT32 stops at 4 GB per file (I). **Superseded by the owner decision of 2026-11-05 (D3, D10):** Deck Atlas
  imports a `z/x/y` folder itself and converts it on the board into one MBTiles (2.7), so the SD card still never holds
  the many small files; converting on the PC stays the advice for very large folders.

**(c) Decode and render tricks for little RAM.**

- **Raw RGB565 tiles** (0015): nothing to decode, but 128 KB per 256 px tile, about 10x the PNG (I), and on our boards
  reading 128 KB from the SD card likely costs about as much as decoding a 12 KB PNG (I, P0). Not for archives; RGB565
  stays our in-RAM format (3.2).
- **8-bit palette PNG** (MUI starter sets, Cyclenerd, Hogesyx): smaller files, decoded by stb_image like any PNG (I).
  Adopt for the archives we prepare or document; P0 times it.
- **Crop while decoding and a 4-tile cache** (NavBoxLib): made for boards with a few hundred KB; we keep 6-8 MB for
  tiles (I). Not needed.
- **Parent tile magnified as a placeholder** (NavBoxLib; map_tiles draws a placeholder), **newest-first loading**, a
  **prefetch ring** where the source allows it (map_tiles): adopt in the 0.1 widget.
- **A loader that writes only into the slot it was handed**, slots allocated on the LVGL thread (map_tiles): adopt as
  the threading rule; no lock around the LRU.
- Grey decoding on low-memory boards (MUI) and a lookup-table night view (NavBoxLib): options for the design pass,
  not decisions.
- **Memory-mapped files:** no project uses `mmap` (microcontrollers cannot); MUI reads PMTiles by offset with one
  cached directory per depth (V). On Linux a read-only `mmap` of a PMTiles or MBTiles file lets the page cache do the
  caching (I); P0 compares it with `pread`.

**(d) Changes to our recommendation:** listed in 12.4.

### 12.4 Changes to the recommendation and to the owner questions

| Item | Change |
|---|---|
| D3 tile sources | **change:** add (d) Geoapify with the user's own key as an opt-in source **that allows "Download this area"**. Its tile page says "you can cache, store, and redistribute the generated map tiles"; the free plan gives 3,000 credits a day and a tile costs 0.25 credit, so 12,000 tiles a day (a city at zoom 0-16 in one day, I); `@2x` raster URLs exist; attribution ("Powered by Geoapify" and OpenStreetMap) is mandatory on the free plan (S). The record gets `bulk_ok=1` and a daily cap. Why: the only free, legal area download found that needs neither a PC nor a server. OSMF servers stay excluded; the owner's own server stays recommended. (**Owner decision 2026-11-05:** Geoapify is the only in-app area download; no LAN tile server is planned; imports include `z/x/y` folders; 2.2, 2.7.) Sections 0, 1, 2.2, 2.7 and 11 are updated. |
| Source records (2.7) | **change:** two guards from 0015's packager: stop a download when different tiles return identical bytes (an error picture such as the OSMF "Access Blocked" image, sent with HTTP 200), except plain one-colour tiles; remember absent tiles (404) per source. Plus a `daily_cap` field. Tests in 8.2. |
| Display scale (D5, 3.2) | **change of preference, not of principle:** files made on a PC are rendered at a higher DPI (QGIS DPI setting) or as `@2x` (tileserver-gl), so labels are crisp without magnification; decode-time magnification stays for sources without `@2x`. |
| Format inside prepared archives | **change:** 8-bit palette PNG in the raster MBTiles/PMTiles we prepare or document (smaller files); JPEG only for imagery. The formats themselves (MBTiles cache, PMTiles/MBTiles import, 2.4): **no change**. |
| P0 spike (8.1) | **change of scope, cost unchanged at the time (0.5-0.75 ws, I; since then 0.75-1 for the engine and 1-1.25 for the owner decisions of 2026-11-05, 8.1, section 13):** add palette PNG vs RGB PNG vs JPEG decode times on the same tiles, PMTiles read through `mmap` vs `pread`, the blocked-tile fixture. |
| 0.1 map widget (3.6) | **change (details):** parent tile magnified as the placeholder, newest-first loading, a prefetch ring only where `prefetch_ok`, the loader writes only its own slot. Cost unchanged (I). |
| Storage (D10, 2.7, 6.2) | **no change.** Added: convert `z/x/y` folders on the PC; exFAT sticks (FAT32's 4 GB per-file limit). (Owner decision 2026-11-05, D10: the app converts an imported folder on the board; see 12.3 (b).) |
| GPS reader (4.2) | **no change.** Prior art agrees: Kismet has its own serial NMEA driver, a listen-only NMEA mode, and warns that only one process can hold a serial port; NavBoxESP probes its UARTs for NMEA like viz1090's watcher; Trail Mate runs GPS as a resident service, our option B for later (6.4). |
| Licences (7.5) | **no change in substance:** no code from NavBoxLib, Meshtastic device-ui, Trail Mate, IceNav, osm-gps-map or Viking (GPL/AGPL); 0015's MIT code may be adapted with attribution if it helps (I). |
| Phases and cost (8.1) | **no change** from the addendum. (Later, the owner decision of 2026-11-05 on question 1 moved the map engine into `cp0_lvgl` and changed 0.1, 0.2, 0.4 and the totals: see sections 0, 3.7 and 8.1. The owner decisions on questions 2-13 of the same day deferred 0.3 and added the small screens: 8.1.) |

**Owner questions (section 11):** question 3 now includes Geoapify; question 4 lists lighter server choices, QGIS at a
higher DPI and the community bundles; question 13 is new (NavBoxLib and the other GPL/AGPL apps as references only).

### 12.5 Not verified (addendum)

- The tile server behind the AlphaMap web downloader (its page does not say); the content of NavBoxESP's "global
  basemap" zip.
- The terms and licence of the community raster bundles MUI points to (none stated on the page).
- Geoapify's caching headers, `@2x` tile sizes and the free plan's request rate (the pricing page says tiles may use
  4x the plan's request rate, without the number for the free plan).
- Whether the OSMF "Access Blocked" image is still sent with HTTP 200 (0015's README says so; not tested).
- ESP32_Offline_GPS_MAPS (no licence) and FoxtrotGPS were not read; Navit, QMapShack, Pure Maps and OSM Scout Server
  were judged from their descriptions only.
- The estimates in 12.3 (RGB565 read vs PNG decode time, palette PNG decode cost, `mmap` benefit): P0.

## 13. P0 spike plan (plan only)

Added 2026-11-05 after the owner decisions. **A plan, nothing run:** no board touched, nothing built. Cost 1-1.25 ws
(estimate, I; 8.1), worker tier **savvy-heavy** (unfamiliar formats, cross-builds, board measurements and a go/no-go
call). The spike writes throwaway probes and a short report with numbers; no product code, no PR to `main`.

**Goals (numbers to measure).**

| # | Measurement | Where | Why |
|---|---|---|---|
| M1 | decode time per tile, median and 95th percentile: PNG 256 px, PNG 512 px, 8-bit palette PNG 256 px, JPEG 256 px (stb_image, RSS Reader's flags; lodepng for comparison) | PC host, deck, zero8 | the tile budget of 0.1 (3.2) |
| M2 | tile read time: PMTiles through `mmap` vs `pread`, vs SQLite MBTiles (`SELECT` by z/x/y, prepared statement), cold and warm page cache | PC host, deck | archive format inside the engine (2.4, 12.3) |
| M3 | RAM: VmRSS of a probe with a decoded-tile LRU of 64 and of 128 tiles (8 and 16 MB of RGB565 at 1x), and the 6-8 MB deck budget at 2x | deck, zero8, PC host | the 25 MB VmRSS rule (3.2) |
| M4 | SQLite + decode weight when linked into a second app: binary size and VmRSS of a stub built like Mesh Hop with the engine (3-4 MB tile cache vs 6-8 MB); `dlopen` vs link; unused toolkit objects absent when `CONFIG_CP0_LVGL_MAP` is off; `STB_IMAGE_STATIC` symbol check | deck, PC host | 0.4 engine reuse or its fallback (6.2) |
| M5 | legibility of tile text at 1x, 1.5x and 2x: street names and place labels read at arm's length by the owner, from the real tile set (below), on the deck (640x480) and on zero8 at 480x320 and 320x480 | deck, zero8 | the display scale rule (D5) and the small-screen layouts (D11) |
| M6 | panel px/mm per board from `board.state` `ppmm` and `density` | deck, zero8 (and any board the owner names) | the automatic scale (D5) |
| M7 | u-blox puck: port enumeration (`ttyACM*` or `ttyUSB*`, USB vid:pid, by-id name), the speed the viz1090 watcher finds and how long it takes, time to first fix outdoors (cold and warm), sentence mix (GGA/RMC/GSA/GSV, UBX); a 10-20 min NMEA capture saved as a fixture | the board the puck is plugged into | the GPS reader (4.2) and the PC fixtures (8.2) |
| M8 | tile-folder-to-MBTiles conversion rate on a Zero 2 W (tiles per second, SD writes), from the synthetic folder | deck | the folder import (2.7) |
| M9 | vector tile sizes per area with `pmtiles extract --dry-run` | PC host | the vector spike after 0.2 (D4) |

**What runs where.**
- **PC host:** the synthetic tile generator, M1/M2/M3 baselines, the fake tile server with the blocked-tile, 404 and
  daily-cap modes, M9, the cross-builds (arm64) of the probes and the second-app stub.
- **Deck `zero7.local`** (640x480, 11.3 px/mm, Zero 2 W): M1-M4, M5 at 640x480, M6, M8; M7 if the puck is plugged
  in there.
- **zero8 `zero8.local`** (480x320, 6.3 px/mm, Zero 2 W): M1 and M3 as a second Zero 2 W, M5 and M6 at **480x320**,
  then with PDA mode on at **320x480** (switching restarts the launcher and closes open apps, PDA-PROFILE). The
  legibility screens are static probe pages, not the app layout.

**Test data needed.**
- A **synthetic generated tile set** (made by the spike: each tile shows its z/x/y and typical map-like content;
  256 and 512 px; PNG, palette PNG and JPEG; as MBTiles, PMTiles and a `z/x/y` folder) for timing only.
- A **real raster tile set of a small area** (for example 5 x 5 km at zoom 12-17) for M5, **supplied by the owner**
  in one of three ways: MapTileDl's output folder, a QGIS "Generate XYZ tiles (MBTiles)" file (ideally once at
  96 DPI and once at a higher DPI), or a Geoapify key so the spike fetches the area legally (256 px and `@2x`).
- The **u-blox puck** for a live NMEA capture (M7); the capture becomes a PC fixture.
- A USB stick (exFAT) to carry the real set to the boards.

**Exit criteria (proposed thresholds; go/no-go for 0.1).**

| Check | Go | No-go consequence |
|---|---|---|
| M1 decode on a Zero 2 W | 256 px PNG median <= 15 ms, JPEG <= 10 ms, 512 px PNG <= 60 ms; a cold 640x480 view at 2x on screen in <= 0.5 s with two workers | above 2x the threshold: prefer JPEG or palette archives, lower the top display factor, lean on the parent-tile placeholder; re-plan 0.1 if all formats fail |
| M2 reads | one tile lookup <= 2 ms warm for the chosen reader | pick the faster reader; both above 10 ms: rethink the store |
| M3 RAM | the deck probe with its 2x budget stays under 25 MB VmRSS | smaller LRU per `ram_mb`, or 1.5x as the deck's top factor |
| M4 second app | the engine adds <= 1.5 MB binary and <= 6 MB VmRSS to the Mesh Hop stub with a 3-4 MB cache | 0.4 falls back to the overlay file only (0.75-1.25 ws) |
| M5 legibility | at least one factor readable on each of deck, zero8 480x320, zero8 320x480 with the real set | require `@2x` / high-DPI archives for those boards, or move the vector spike earlier (an owner decision) |
| M7 GPS | the puck is found without a manual port choice in <= 10 s and gives a fix outdoors | record its ids for the device table; if it needs a bridge-chip probe, it stays behind "Find my GPS" (4.2) |
| M8 folder import | >= 50 tiles/s on the deck (I) | keep the import but recommend converting large folders on the PC |

**Worker tiers.** P0 savvy-heavy (this plan); the controller reviews the numbers and proposes the go/no-go to the
owner; the owner runs the board readings (M5 by eye, M7 outdoors) from a short protocol (controller-led testing, no
deck-verifier by default).

**What the controller needs from the owner.**
1. The boards named for the probe installs: proposed deck `zero7.local` and zero8 `zero8.local` (zero8 switched to
   PDA mode for the 320x480 readings and back after).
2. The real tile set of a small area: a MapTileDl folder, a QGIS MBTiles file, or a Geoapify key.
3. Which board the u-blox puck is plugged into (proposed: the deck) and a time to take it outdoors for the fix.
4. A USB stick (exFAT) for the real set.

## Sources

Read 2026-11-05. Licence of each item in brackets where known.

Tile policies and providers:
- [OSMF-tiles] OSMF Tile Usage Policy (undated page): https://operations.osmfoundation.org/policies/tiles/
- [OSMF-vector] OSMF Vector Tile Usage Policy: https://operations.osmfoundation.org/policies/vector/
- [OSMF-attr] OSMF Attribution Guidelines: https://osmfoundation.org/wiki/Licence/Attribution_Guidelines
- [OSM-disk] OSM wiki, Tile disk usage (CDN traffic August 2026, edited 2026-09-22): https://wiki.openstreetmap.org/wiki/Tile_disk_usage
- [OTM] OpenTopoMap, about (map CC-BY-SA): https://opentopomap.org/about
- [TF-terms] Thunderforest terms: https://www.thunderforest.com/terms/
- [TF-price] Thunderforest pricing: https://www.thunderforest.com/pricing/
- [Stadia] Stadia Maps tutorial on offline maps (not the ToS): https://docs.stadiamaps.com/tutorials/offline-maps-with-flutter-maplibre-gl/
- [OFM] OpenFreeMap (code MIT, data OSM): https://openfreemap.org/
- [PM-dl] Protomaps basemap downloads (ODbL Produced Work): https://docs.protomaps.com/basemaps/downloads
- [PM-cli] PMTiles CLI: https://docs.protomaps.com/pmtiles/cli
- [PM-repo] PMTiles repository (implementations BSD-3, spec CC0): https://github.com/protomaps/PMTiles
- [SB-dl] Shortbread downloads (data ODbL): https://shortbread-tiles.org/download/
- [BBBike] BBBike extract service (data ODbL 1.0): https://extract.bbbike.org/extract.html
- [Geofabrik] Geofabrik Europe downloads (data ODbL): https://download.geofabrik.de/europe.html
- [OMT] OpenMapTiles (code BSD, design CC-BY): https://github.com/openmaptiles/openmaptiles
- [Mapterhorn] Mapterhorn (code BSD-3, per-source data attribution): https://mapterhorn.com/ and https://mapterhorn.com/data-access/

Formats and renderers:
- [MBTiles] MBTiles specification (text CC BY 3.0 US, free use): https://github.com/mapbox/mbtiles-spec
- [OAM] OpenAndroMaps licence (private use; no bundling without permission): https://www.openandromaps.org/en/legend/license
- Mapsforge (LGPL-3.0): https://github.com/mapsforge/mapsforge
- [libosmscout] libosmscout (LGPL): https://github.com/Framstag/libosmscout
- [vtzero] vtzero (BSD-2-Clause): https://github.com/mapbox/vtzero
- [MLN] MapLibre Native platform support (OpenGL ES 3.0, Vulkan, WebGPU on Linux): https://maplibre.org/maplibre-native/docs/book/platforms/index.html
- [Organic] Organic Maps (code Apache-2.0, map files under a separate licence): https://github.com/organicmaps/organicmaps

GPS:
- [gpsd] gpsd home page: https://gpsd.io/
- [gpsd-lic] gpsd COPYING (BSD-2-Clause): https://gitlab.com/gpsd/gpsd/-/raw/master/COPYING
- [gpsd-rules] Debian gpsd udev rules (10c4:ea60 and other bridges): https://sources.debian.org/src/gpsd/3.16-1%7Ebpo8+1/gpsd.rules.in/
- [suse-bug] openSUSE bug 432420, gpsd rules break non-GPS USB-serial devices: https://lists.opensuse.org/archives/list/bugs@lists.opensuse.org/thread/OP6NKKAGP45KUCNSMJOIXZBPBXRGLPQJ
- [GPSdFwd] GPSd Forwarder (MIT per IzzyOnDroid): https://f-droid.org/packages/io.github.tiagoshibata.gpsdclient/ and https://apt.izzysoft.de/fdroid/index/apk/io.github.tiagoshibata.gpsdclient

Wardriving:
- [WiGLE-CSV] WiGLE CSV format v1.6: https://api.wigle.net/csvFormat.html
- [Kismet-log] Kismet logging: https://www.kismetwireless.net/docs/readme/logging/logging/
- [nexmon] nexmon (GPL-3.0): https://github.com/seemoo-lab/nexmon
- [iw-dump] Scan dump readable without root (secondary): https://www.lieberbiber.de/?p=193
- [iwinfo] iwinfo helper with network capabilities: https://www.linuxlinks.com/iwinfo-provides-wifi-information/
- [WP29] Article 29 WP Opinion 13/2011 (WP185) summary: https://www.hunton.com/privacy-and-cybersecurity-law-blog/article-29-working-party-opines-on-geolocation-services ; primary PDF (not opened): https://lists.oasis-open.org/archives/pmrm/201106/pdf00000.pdf

Prior art (section 12; licence from each repository's licence file or metadata, last push from the GitHub API,
read 2026-11-05):
- [NavBoxLib] t413/NavBoxLib (GPL-3.0; last push 2026-06-12), `readme.md`, `src/navboxlib/{MapRenderer,PixelBuffer,fileclass}`: https://github.com/t413/NavBoxLib
- [NavBoxESP] t413/NavBoxESP (GPL-3.0; 2026-06-13), `readme.md`, `platformio.ini`, `lib/corelib/{GpsManager,views/MapView}.cpp`, `lib/staticfiles/basetilewriter.cpp`: https://github.com/t413/NavBoxESP
- [osm2edgetx] t413/osm2edgetx (no licence file; 2026-05-29), `osm2edgetx.py`: https://github.com/t413/osm2edgetx
- [AlphaMap] AlphaMap web tile downloader (tile server not stated): https://t413.com/go/alphamaptool ; widget t413/AlphaMapLua (GPL-3.0; 2026-05-13): https://github.com/t413/AlphaMapLua
- [PNGdec] bitbank2/PNGdec (Apache-2.0): https://github.com/bitbank2/PNGdec
- [TinyGPS++] mikalhart/TinyGPSPlus (LGPL-2.1 per the header): https://github.com/mikalhart/TinyGPSPlus
- [MUI] meshtastic/device-ui (GPL-3.0; 2026-11-04), `maps/README.md`, `source/graphics/map/CURLService.cpp`, `include/graphics/map/{PMTileService,MapTileSettings}.h`: https://github.com/meshtastic/device-ui ; Meshtastic UI docs: https://meshtastic.org/docs/configuration/device-uis/meshtasticui/
- [Oxed] community raster tile bundles (free, monthly, no terms stated): https://download.tiles.coalition.space/bundles
- [TrailMate] vicliu624/trail-mate (AGPL-3.0; 2026-11-03), `platform/linux/common/src/platform/linux/map_tile_cache.cpp`, `apps/linux_cardputer_zero/README.md`: https://github.com/vicliu624/trail-mate ; Trail Mate Center (AGPL-3.0; 2026-10-22): https://github.com/vicliu624/trail-mate-center
- [IceNav] jgauchia/IceNav-v3 (GPL-3.0; 2026-11-01), `docs/sd-card-file-structure.md`, `docs/mass-copy.md`: https://github.com/jgauchia/IceNav-v3 ; Tile-Generator (GPL-3.0; 2026-11-01): https://github.com/jgauchia/Tile-Generator
- [map_tiles] 0015/map_tiles (MIT; 2026-09-27): https://github.com/0015/map_tiles ; component page: https://components.espressif.com/components/0015/map_tiles
- [OMD] 0015/OfflineMapDownloader (MIT; 2026-09-27), `README.md`, `tile_sources.py`: https://github.com/0015/OfflineMapDownloader
- [Cyclenerd] Cyclenerd/offline-map-tile-downloader (MIT; 2026-09-15): https://github.com/cyclenerd/offline-map-tile-downloader
- [Hogesyx] Hogesyx/lvgl_map_tiles_downloader (MIT; 2025-05-29): https://github.com/Hogesyx/lvgl_map_tiles_downloader
- [osm-gps-map] nzjrs/osm-gps-map (GPL-2.0; 2026-10-11), `src/osm-gps-map-widget.c`, `src/osm-gps-map-source.c`: https://github.com/nzjrs/osm-gps-map
- [Viking] viking-gps/viking (GPL-2.0; 2026-10-03), `src/osm.c`, `help/C/index.docbook`: https://github.com/viking-gps/viking
- [MOBAC] Mobile Atlas Creator (GPL-2.0; 2.4.0, updated 2026-10-19): https://sourceforge.net/projects/mobac/ ; OSMF block (2011): https://help.openstreetmap.org/questions/5715/access-blocked-when-using-mobile-atlas-creator/
- [JTD] JTileDownloader (GPLv3; 0.6.2 of 2020; blocked by the OSMF servers): https://wiki.openstreetmap.org/wiki/JTileDownloader
- Navit (GPL-2.0 with LGPL parts; 2026-11-03): https://github.com/navit-gps/navit ; QMapShack (GPL-3.0; 2026-11-02): https://github.com/Maproom/qmapshack ; Pure Maps (GPL-3.0; 2026-09-16): https://github.com/rinigus/pure-maps ; OSM Scout Server (GPL-3.0; 2026-08-26): https://github.com/rinigus/osmscout-server
- PC and server tools: go-pmtiles (BSD-3; 2026-07-22) https://github.com/protomaps/go-pmtiles ; mbutil (BSD-3; archived) https://github.com/mapbox/mbutil ; VersaTiles (MIT; 2026-11-04) https://github.com/versatiles-org/versatiles-rs ; mbtileserver (ISC; 2025-05-21) https://github.com/consbio/mbtileserver ; tileserver-gl (BSD-2; 2026-11-01) https://github.com/maptiler/tileserver-gl ; tilemaker (FTWPL; 2026-11-03) https://github.com/systemed/tilemaker ; MapProxy (Apache-2.0; `doc/configuration_examples.rst`) https://github.com/mapproxy/mapproxy
- [MapTileDl] MapTileDl by t413 (the page names no output format; tile sources and terms not stated; named by the owner on 2026-11-05, not read by me): https://t413.com/p/projects/MapTileDl/
- [QGIS-xyz] QGIS "Generate XYZ tiles (MBTiles)" (`native:tilesxyzmbtiles`, DPI parameter): https://docs.qgis.org/latest/en/docs/user_manual/processing_algs/qgis/rastertools.html
- [Geoapify-tiles] Geoapify map tiles (caching, storing, redistribution; `@2x` URLs): https://www.geoapify.com/map-tiles/
- [Geoapify-price] Geoapify pricing (free plan 3,000 credits a day): https://www.geoapify.com/pricing/ ; pricing details (a tile costs 0.25 credit): https://www.geoapify.com/pricing-details/
- [Geoapify-terms] Geoapify terms (attribution mandatory on the free plan): https://www.geoapify.com/terms-and-conditions/
- [Kismet] kismetwireless/kismet (GPL-2.0 unless a file says otherwise; 2026-11-05), `conf/kismet.conf` (GPS types), `http_data/js/widgets/map.js` (tile consent): https://github.com/kismetwireless/kismet ; GPS docs: https://www.kismetwireless.net/docs/readme/gps/
- Pwnagotchi (GPL-3.0; jayofelony fork 2026-09-28), `pwnagotchi/plugins/default/{gps.py,webgpsmap.html}`: https://github.com/jayofelony/pwnagotchi ; Sparrow-wifi (GPL-3.0; 2026-10-21), `sparrowmap.py`: https://github.com/ghostop14/sparrow-wifi ; WiGLE Android (BSD-3; 2026-11-02): https://github.com/wiglenet/wigle-wifi-wardriving
- ESP32 wardrivers with WiGLE CSV: ESP32Marauder (MIT; 2026-10-29) https://github.com/justcallmekoko/ESP32Marauder ; wardriver_rev3 (GPL-3.0; 2026-05-25) https://github.com/JosephHewitt/wardriver_rev3 ; Bruce (AGPL-3.0; 2026-11-04) https://github.com/BruceDevices/firmware ; Evil-M5Project (no licence; 2026-11-04) https://github.com/7h30th3r0n3/Evil-M5Project

Our repositories (V):
- Apps `origin/main` `bec05f3`: `apps/net-toolbox/src/core/{wifiscan,ble,walktest,netlog_wifi}.hpp`;
  `apps/serial-console/src/core/{ports,devices,identify,probe,serial_io}.{hpp,cpp}`;
  `apps/viz1090/src/core/{gps,nmea,place}.hpp`, `apps/viz1090/build/make_world.py`;
  `apps/mesh-hop/src/core/{nodes,model,client,serial_transport,deck_clock}.{hpp,cpp}`;
  `apps/rss-reader/docs/IMAGES.md`, `apps/rss-reader/src/ui/image_codecs.h`, `apps/rss-reader/src/core/fetch.hpp`;
  `apps/home-assistant/src/core/curl_api.cpp`; `apps/_common/scons/main.SConstruct`; `docs/APP-GUIDELINES.md`.
- Launcher `github/main` `3824b2cc`: `docs/APP-CONTRACT.md`, `docs/dev/ROADMAP.md`,
  `projects/APPLaunch/config_defaults.mk`.
