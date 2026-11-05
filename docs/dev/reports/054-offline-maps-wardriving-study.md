# 054 - Offline maps with an external GPS, and wardriving (study)

Date 2026-11-05. Read-only study: no product code, no board touched, nothing built or installed. The owner approved
starting a map viewer/explorer app with wardriving and an external GPS receiver, with map tiles stored on the board.
This report gives options and a recommendation; design and visual decisions stay with the controller and the owner.

**What I read.**
- Launcher `github/main` @ `3824b2cc`: `docs/APP-CONTRACT.md` (sections 2, 5b, 5c, 6, 10b, 13, 14),
  `docs/dev/ROADMAP.md`, reports 027 and 052 (format), `projects/APPLaunch/config_defaults.mk` (LVGL options).
- Apps `origin/main` @ `bec05f3`: `docs/APP-GUIDELINES.md`; Net Toolbox (`wifiscan`, `ble`, `walktest`,
  `netlog_wifi`); Serial Console (`ports`, `devices`, `identify`, `probe`, `serial_io`); viz1090 (`gps`, `nmea`,
  `make_world.py`, `.desktop`); Mesh Hop (`nodes`, `model`, `client`, `serial_transport`, `deck_clock`); RSS Reader
  (`docs/IMAGES.md`, `image_codecs.h`, `fetch.hpp`); Home Assistant (`curl_api.cpp`); `_common/scons/main.SConstruct`.
- The web (read 2026-11-05), listed under Sources with their licences.

**Markers.** **V** = verified in our code or a measured board fact already recorded in a report; **S** = read in a
source (Sources list); **I** = inferred or estimated, needs a spike or a board. **Cost unit:** one worker session
(ws), about 0.4 M tokens (the ratio of reports 028 and 052).

## 0. Summary

**Recommendation: build it, raster first, in four small phases, as one native `cp0_lvgl` app.**

1. **Tiles.** Never use `tile.openstreetmap.org` for this app: its policy forbids offline use and any area download
   (S, quoted in 2.1). No online source is on by default; the ones below are opt-in. Get maps onto the board in three ways:
   **import** a raster MBTiles/PMTiles file from USB/SD (prepared on a PC from legal sources), **download an area**
   only from sources whose terms allow it (the owner's own tile server on the LAN, a provider plan that allows bulk),
   and an **on-demand viewing cache** (LRU, size cap) for sources that allow on-device caching (OpenTopoMap,
   Thunderforest with the user's own key). Every source carries policy flags; the app enforces them.
2. **Rendering.** Raster tiles (PNG/JPEG) decoded with stb_image (already in RSS Reader, public domain/MIT), kept as
   RGB565, blitted by a custom LVGL map widget. A 256 px tile is designed for about 3.8 px/mm; our panels have 6.5 to
   11.3 px/mm (I), so tiles must be **magnified about 2x** (or be 512 px "@2x" tiles) or the labels are unreadable.
   Vector tiles (10-20x smaller files, crisp at any density) are a later option after a spike; MapLibre Native is out
   (needs OpenGL ES 3.0 or Vulkan, S).
3. **GPS.** Our own NMEA reader, reusing MIT code we already have: viz1090's listen-only NMEA watcher and Serial
   Console's sysfs port scan, GNSS device table and NMEA/UBX classifier (V). Not gpsd by default: Debian's gpsd
   hot-plug rules claim generic USB-serial chips such as CP210x (10c4:ea60), which LoRa boards for Mesh Hop use (S).
4. **Wardriving.** Managed-mode scans only, no root: Wi-Fi through NetworkManager like Net Toolbox (V), BLE through
   `bluetoothctl` like the Net Toolbox radar (V, off by default on the deck: the radio is shared with its Bluetooth
   keyboard). Log WiGLE CSV 1.6 (S) with **GPS time** (the boards have no RTC), local only, export by hand.
5. **Sharing.** A documented shared folder for tile archives and overlays, and a `position.state` file in the state
   directory. First the map app writes it (0.2); later the launcher may own the GPS and write it (section 6.4). Over
   the LoRa mesh only positions and waypoints travel, never tiles (bandwidth).

**Key facts.**
- Pi Zero 2 W: 512 MB (about 414 MB usable), 2.4 GHz-only Wi-Fi, Wi-Fi and Bluetooth on one chip (V). App rule:
  VmRSS under 25 MB (V). A decoded 256 px RGB565 tile is 128 KB; 2x magnified 512 KB.
- A 20 x 20 km city at zoom 0-16 is about 3,500 raster tiles (35-70 MB); zoom 0-17 about 13,300 tiles
  (130-270 MB); a country like France at zoom 0-14 about 260,000 tiles (several GB) (I, section 2.5).
- LVGL 9.5 in our toolkit already has ThorVG vector drawing, lodepng and NEON blending switched on (V).
- Mesh Hop already knows node positions from adverts and can set the radio's position (V).

**Phases (cost in ws).** P0 spike 0.5-0.75; **0.1** viewer + imported/cached tiles + GPS dot 2-2.5; **0.2** area
download, cache cap, tracks, `position.state` 1.25-1.75; **0.3** wardriving log and overlay 1.25-1.75; **0.4** Mesh Hop
link 0.75-1.25; docs 0.25. MVP (P0 + 0.1 + docs) about 2.75-3.5 ws (1.1-1.4 M tokens); everything through 0.4 about
6-8.25 ws (2.4-3.3 M tokens). Optional: vector renderer (spike + 2.5-4 ws), launcher-owned GPS (1.5-2.5 ws + release).

**Owner questions:** section 11 (12 questions, each with a proposed default).

## 1. Decisions for the owner

| # | Decision | Options | Proposed default |
|---|---|---|---|
| D1 | Scope and order | 0.1 viewer, 0.2 downloads + tracks, 0.3 wardrive, 0.4 Mesh Hop; or wardrive earlier | as listed |
| D2 | One app or two | (a) one app with a Wardrive page; (b) a map app + a separate logger app sharing the store | (a): wardriving needs the map and the GPS anyway |
| D3 | Default tile sources | (a) none online, import + own server only; (b) add OpenTopoMap viewing cache; (c) add Thunderforest with the user's key | (a) + (b) + (c) as opt-in sources the user enables; never `tile.openstreetmap.org` |
| D4 | Raster now, vector later | (a) raster only; (b) raster now, vector spike after 0.2; (c) vector first | (b) |
| D5 | Display scale | automatic from density (about 2x), a Settings choice 1x/1.5x/2x/3x | automatic with an override (the exact rule is a design pass) |
| D6 | Who owns the GPS | app (0.1), launcher later; launcher now; gpsd | app first, contract ready for the launcher (6.4) |
| D7 | Wi-Fi scanning | NetworkManager only (no root); plus an optional helper with `cap_net_admin` | NetworkManager only |
| D8 | BLE in wardriving | on, off, off on boards whose keyboard is Bluetooth | off by default, a switch with the warning |
| D9 | Logs | local only, manual WiGLE export; any upload code | local only, no upload code |
| D10 | Storage | shared folder path, cache cap, a USB stick path | section 6.2 layout; cap 512 MB or 10 % of free space, the smaller |
| D11 | Screen sizes | 640x480 minimum + `X-Portrait=480x640`; also 480x320 | 640x480 + portrait; 480x320 only if the P0 layout check says it is cheap |
| D12 | System clock from GPS | no; offer when the clock is unsynced (sudo prompt) | offer, never automatic (4.6) |

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
| Stadia Maps | on-device caching with an active subscription, 100 MB per device (S [Stadia], a tutorial, not the ToS) | not stated | paid | per their docs | not proposed |
| The owner's own tile server on the LAN (tileserver-gl, a static folder of tiles) | the owner's data | yes | free | per the data used (ODbL: "© OpenStreetMap") | **recommended** for area downloads |
| Raster MBTiles/PMTiles made on a PC | the owner's file | n/a (import) | free | per the data | **recommended** (import) |

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
| Directory tree `z/x/y.png` | plain files | none | none | only as an import source; many small files on SD |
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

Three paths, all into one store per source:

1. **On-demand cache.** Every tile fetched while the user looks at the map is kept. Shown again from the store; expired
   tiles are revalidated with `If-None-Match` / `If-Modified-Since` when online, and shown stale when offline **only
   if the source allows offline use**. Eviction: least recently used, by a size cap (D10). For the 1-tile ring around
   the view (smooth panning) only where the source allows prefetch.
2. **Download this area.** A rectangle (the current view or a drawn box) and a zoom range; the page shows the tile
   count and an estimated size before it starts; rate-limited (at most 4 parallel requests, as OpenFreeMap asks),
   resumable, cancellable; refused for a source without `bulk_ok`, and capped by the source's own limit.
3. **Import a file.** An MBTiles or PMTiles file from a USB stick or the SD card: either copied into the store or used
   in place (a stick that stays plugged). The PC side (outside the app): `pmtiles extract` (BSD-3) to cut a region from
   Protomaps or Mapterhorn; BBBike/Geofabrik downloads; raster rendering on the PC for raster files (tool choice: P0).

**Per-source policy record** (proposal, a small text file per source, shipped and user-added): name, URL template,
raster or vector, image format, tile size (256/512), min and max zoom, attribution text, `offline_ok`, `bulk_ok`,
`max_bulk_tiles`, `prefetch_ok`, minimum TTL, an API key field, the User-Agent (always
`<app-id>/<version> (+https://github.com/OSRdesign/cyberdeck-zero-apps)`).

**Store format options for the cache:**

| Option | Pros | Cons |
|---|---|---|
| (a) Directory tree `cache/<source>/z/x/y.png` + an index file | no library; trivial | thousands of small files: slow to count, evict and delete on SD; index must be kept in step |
| (b) **One MBTiles file per source** + an extra table (tile key, last use, size, ETag, expiry) | one file per source; standard (QGIS, tileserver-gl, other apps read it); atomic transactions; the shared store of section 6 | needs SQLite (link or `dlopen("libsqlite3.so.0")` like Home Assistant opens libcurl, V); write amplification if every view writes |
| (c) Own append-only pack file + index | fewest writes | our own format, compaction code, nobody else reads it |

Proposal: **(b)**. SD-friendly rules: WAL journal with `synchronous=NORMAL`, commits batched (every few seconds or
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
  4.3" DSI about 8.6, tablet 7" about 8.5, Pi 4B 5" about 7.3, zero8 3.5" about 6.5 px/mm. At 1:1 an 11 px map label
  is 1.0-1.7 mm tall: unreadable on most of them. Options:

  | Option | Look | Cost |
  |---|---|---|
  | Magnify at decode time (2x nearest or bilinear; 1.5x bilinear) | soft text, still readable; shows a quarter of the area | 4x the RAM per tile (512 KB at 2x) but fewer tiles on screen |
  | 512 px "@2x" tiles (rendered with labels for high density) | crisp | only from sources that offer them, or files rendered on a PC with a scale of 2 |
  | Draw scaled by LVGL at every frame (`lv_image` scale) | the same as magnifying | per-frame software transform on a Zero 2 W: likely too slow when panning (I) |
  | Vector rendering at the panel's density | crisp at any size | section 3.3 |

  Proposal: magnify at decode time with an automatic factor from the canvas density (about 2x on most boards) and a
  Settings override; prefer `@2x` tiles where the source has them. The exact rule is a design pass (D5).

### 3.3 Vector rendering options

| Option | Licence | Fit |
|---|---|---|
| Our own MVT renderer: vtzero + protozero (BSD-2, header libraries, S [vtzero]) to decode, ThorVG through LVGL's vector API (MIT, already linked) or our own scanline fill for polygons and lines, FreeType for labels, a small fixed style | all permissive | feasible; polygons and lines are the easy part; **labels** (placement along roads, collisions) are the bulk of the work; render each tile once into an RGB565 cache at the panel's density. Zero 2 W time per urban tile unknown (I, spike) |
| libosmscout (C++, offline rendering and routing) | LGPL (S [libosmscout]) | its own import format; LGPL is not GPL, but the library must stay dynamically linked and replaceable; heavy |
| MapLibre Native | BSD-2 | Linux needs OpenGL ES 3.0, Vulkan or WebGPU (S [MLN]); the Zero 2 W and Pi 3A+ GPUs offer OpenGL ES 2.0 only (I); our apps draw through fbdev: **out** |
| Mapsforge | LGPL-3.0, Java | **out** |

Proposal: no vector work before 0.2 ships; then a 0.5 ws spike (decode and draw one urban z14 tile on the deck,
time and RAM) decides.

### 3.4 Projection, scale bar, overlays, day and night

- **Web Mercator (EPSG:3857):** tile x from longitude, tile y from `ln(tan(lat) + sec(lat))`, latitude limited to
  +-85.0511; MBTiles stores rows in TMS order (row = 2^z - 1 - y). Ground resolution in metres per pixel =
  156,543.03 x cos(latitude) / 2^z for 256 px tiles (25.6 m at z12, 1.6 m at z16 at 48 N). All in `core/`, tested
  on known points.
- **Scale bar:** from the ground resolution at the screen centre and the display scale; rounded to 1-2-5 steps;
  metric or imperial (Settings).
- **Overlays:** GPS dot with an accuracy circle and a heading wedge; the current track (decimated with
  Douglas-Peucker per zoom); waypoints; Wi-Fi/BLE sightings (5.7); Mesh Hop nodes (6). Lines through LVGL's line or
  ThorVG path drawing; at most a few thousand points per frame, pre-projected per zoom.
- **Day/night:** raster tiles cannot be restyled; a night view = a dim/darken pass applied at decode time (cheap) or
  dark tiles from a source that has them. The app's own chrome follows the app palette.

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
`board.state` `ram_mb`, one custom LVGL map widget, MBTiles (SQLite) and PMTiles readers in `core/`, overlays drawn on
top. No WebP, no vector, no imagery in 0.1.

## 4. GPS (question 3)

### 4.1 Receivers

| Receiver | Linux sees | Notes |
|---|---|---|
| USB u-blox 7/8/9 dongles (VK-162/172 style) | `/dev/ttyACM*` (cdc_acm), USB 1546:01a7/01a8/01a9 | NMEA at 9600 by default, UBX binary too; in Serial Console's device table (V) |
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

Proposal: own reader; gpsd as an optional, user-configured source later (connect to a gpsd the user runs).

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
  listen loop and its stop within about 200 ms). The map app copies them into its own `core/` (each app owns its
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
priority), and the board test includes GPS + Wi-Fi scan + display at once. A powered hub is the advice if it resets.

### 4.6 Time from GPS (bonus)

The deck and zero8 have no RTC (V); without network the clock is wrong after boot. Wardrive and track logs therefore
use **the receiver's UTC** (RMC date + time), never the system clock (I, rule). Setting the system clock:

| Option | Notes |
|---|---|
| (a) The app offers "Set the clock from GPS" when the clock is unsynced (Mesh Hop's `deck_clock` check: `timedatectl ... NTPSynchronized`, V) | `timedatectl set-time` refuses while automatic sync is on (I); `date -s` through the sudo prompt; once per boot |
| (b) gpsd + chrony SHM | system configuration; see the gpsd risk above |
| (c) The launcher does it (with GPS ownership, 6.4) | central; the launcher already has a sudo prompt and the time zone page (V) |

Proposal: (a) in 0.2 as an offer, never automatic; (c) if the launcher takes the GPS.

## 5. Wardriving (question 4)

### 5.1 What each board can scan

| Board | Wi-Fi | Bluetooth | Note |
|---|---|---|---|
| deck (Zero 2 W) | 2.4 GHz only (V) | BLE, same chip as Wi-Fi | the keyboard is Bluetooth: BLE scanning can make it stutter (V, Net Toolbox warning) |
| zero8 (Zero 2 W) | 2.4 GHz only | BLE, same chip | below 640x480 (D11) |
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
  (S [Kismet-log]); Kismet is GPL (I, not re-checked). WiGLE CSV is the common ground: Kismet users can read it and
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

## 6. Mesh Hop link and a shared contract (question 5; later phase, sketch)

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
  index.json             written by the map app: archives, their bounds, zooms, attribution (readers ignore unknown keys)
```

`overlays/mesh-hop.csv` would carry `id, name, kind (node|repeater|room|waypoint), lat, lon, time_utc, source`; the
map app watches the folder (inotify) and draws each file as a layer. Readers open MBTiles read-only; one writer per
file. This is documented in APP-GUIDELINES (apps repo), not in the launcher contract, unless the launcher writes it.

Two ways for Mesh Hop to "share the map": (a) its nodes appear in the map app through `overlays/mesh-hop.csv`
(cheap, proposed for 0.4); (b) Mesh Hop's own empty Map tab draws the shared tile store with a copy of the map app's
widget and tile readers (more code in a second app; later, if the owner wants a map inside Mesh Hop).

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
| A. The map app owns the receiver, writes `position.state` while it runs (`X-Background=run` while recording) | no launcher change; ships in 0.2 | Mesh Hop, viz1090 and others get a position only while the map app runs; only one process can hold the receiver: viz1090's watcher opens ports without a lock (V), so viz1090 and the map app cannot both use it |
| B. **The launcher owns it** (a thread or a small supervised helper, like the `status.state` thread), publishes `position.state`, `board.state` gains `gps=`, `launcher.sock` gains a `gps` status verb, a Settings > GPS page (receiver, on/off, set the clock) | one owner for every app; works at boot; the clock fix in one place; apps need no serial code | launcher work and a release; the launcher must claim only receivers identified as GNSS (never a LoRa board's port); more code in a process that must not crash (a helper process isolates it) |
| C. gpsd owns it, everyone speaks gpsd | standard, multi-client, chrony time | the udev claim risk (4.2); root configuration; a daemon on 512 MB boards |
| D. **A now, B later** with the same file | readers written once; B only when a second app needs the GPS | the move to B is a later launcher task |

Proposal: **D**. Mesh Hop reads `position.state` from 0.4 ("Use the deck's GPS" for its advert position, rate-limited:
on a 50 m move and at most every 5 min, I); viz1090 can switch from its own watcher to the file later.

**Over the mesh:** only positions and waypoints. Proposal for a waypoint: a text message carrying a `geo:` URI
(RFC 5870, `geo:48.8566,2.3522`) that Mesh Hop shows with an "On the map" action writing the point to its overlay
file. Tiles never go over LoRa (a city tile set is tens of MB; LoRa carries a few hundred bytes per packet).

## 7. App shape (question 6)

### 7.1 Name candidates

Neutral words, no trademarks (checked against nothing formal: the owner picks): **Waymark**, **Trail Atlas**,
**Deck Atlas**, **Offline Map**, **Field Atlas**, **Map Walker**. Avoid "CardputerZero", "Graffiti",
"Minesweeper", and known product names ("Field Maps" is a GIS product, "Pathfinder", "Wayfinder" are widely used).
App id follows the name (`waymark`, `trail-atlas`, ...).

### 7.2 Screens (proposals; layout is a design pass)

| Page | Content |
|---|---|
| Map (home) | the map; status chip (fix, sats, accuracy); `+` / `-` / locate buttons; scale bar; attribution corner; a recording dot |
| Layers & downloads | sources (on/off, cache size), imported files, "Download this area" (zoom range, count, size), clear cache |
| Tracks | record / stop, list (date, length, duration), show on map, export GPX to USB |
| Wardrive (0.3) | start/stop, counters (APs new/total, BLE), scan interval, list, overlay switches, export WiGLE CSV |
| GPS | receiver, fix, satellites by constellation, DOP, UTC, "Set the clock" (0.2), "Find my GPS" |
| Settings | units, coordinate format, display scale, night view, cache cap, API keys, data licences, about |

Sketch at 640x480 (not to scale):

```
+----------------------------------------------+
| Waymark        3D 9 sats +-4 m      [Esc]    |
|                                              |
|                 (map tiles)              [+] |
|                     o<- you              [-] |
|          . . track . .                   [o] |
|                                              |
| 200 m |-----|              (c) OpenStreetMap |
+----------------------------------------------+
  Ctrl+O / hold 3 s: Layers, Tracks, Wardrive, GPS, Settings
```

Portrait (`X-Portrait=480x640`): the same map, buttons in a bottom row. The page menu by Ctrl+O and the 3 s hold box
listing every action (guidelines section 14), plus a visible menu button for touch.

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

- `.desktop`: `X-Fullscreen=true`, `X-MinSize=640x480`, `X-Portrait=480x640`, `X-Background=run` (recording and
  wardriving continue while hidden; the app stops drawing and stops scans that are not recording while hidden).
- `app.json`: `arch: ["arm64", "amd64"]`; `depends`: the base (`libfreetype6, libinput10, libxkbcommon0`) plus
  `curl, ca-certificates` (downloads through the `curl` program as RSS Reader does, V) and `libsqlite3-0` (or
  `dlopen` it, the Home Assistant pattern, to avoid touching the shared build, V); `recommends`:
  `network-manager, bluez, iw`; `permissions`: `network`, `additional_hardware`.
- Data: `~/.config/<id>/` settings; `~/.local/share/<id>/` tracks and wardrive logs; the shared maps folder (6.2).
- Group: `dialout` for the GPS (documented, with the fix in the message).

### 7.5 Libraries and licences

| Component | Licence | Use |
|---|---|---|
| Our code | MIT | everything |
| stb_image | public domain / MIT (V) | PNG/JPEG decode; its zlib inflater could also open PMTiles' gzip directories (I) |
| SQLite (system `libsqlite3-0`) | public domain | MBTiles |
| PMTiles spec / reference C++ | CC0 / BSD-3 (S) | our reader, or the header |
| LVGL 9.5, ThorVG | MIT (in the toolkit) | drawing |
| vtzero, protozero (later, vector) | BSD-2 (S) | MVT decode |
| libwebp (later, terrain) | BSD-3 (I) | WebP |
| gpsd (optional external source) | BSD-2 (S) | not linked |
| Not used: Mapsforge (LGPL-3.0), libosmscout (LGPL), OsmAnd (GPL-3.0), Kismet (GPL), nexmon (GPL-3.0) | | named only |
| Map data | ODbL (OSM), CC-BY (OpenMapTiles design), CC-BY-SA (OpenTopoMap), provider terms | attribution in the app and README |

### 7.6 Per-board feasibility

| Board | Screen, input | RAM | Fit | Notes |
|---|---|---|---|---|
| deck, Zero 2 W | 640x480, 11.3 px/mm, BT keyboard + touch | 512 MB (414 usable) | **yes** | 2x scale shows about 320x240 tile px; BLE wardrive off by default; 2.4 GHz only |
| Pi 3A+ 800x480 DSI | touch only | 512 MB | **yes** | the touch-only reference; dual-band (I) |
| Pi 3A+ 480x320 SPI | touch | 512 MB | hidden (below 640x480) | D11 |
| Pi 4B 1 GB, 480x800 portrait | touch | 1 GB | **yes** with `X-Portrait` | |
| Pi 5 + HyperPixel 800x480 | touch (+ keyboard) | 2-8 GB (I) | **yes** | fastest Pi |
| HackBerry CM5 720x720 | keyboard + touch (2 contacts) | about 1.8 GB (I) | **yes** | square: the map uses all of it |
| Panasonic FZ-M1, amd64 | 1280x800 (800x1280 PDA), touch + keyboard | 3.8 GB | **yes** (amd64 package) | Intel Wi-Fi |
| zero8, Zero 2 W 480x320 (320x480 portrait) | touch | 512 MB | hidden unless a 480x320 layout is cheap | the map page adapts naturally; the list pages are the question |

## 8. Phases, cost and tests (questions 6 and 7)

### 8.1 Phases

| Phase | Worker | Content | Cost (ws) | Owner test |
|---|---|---|---|---|
| P0 spike | savvy-medium | decode timing and RSS for 256/512 px PNG/JPEG tiles (host + an arm64 probe for the deck); MBTiles and PMTiles readers on generated fixtures; NMEA fixture replay with the viz1090 parser extended; fake tile server; `iw scan dump` as a normal user; vector tile sizes with `pmtiles extract --dry-run` | 0.5-0.75 | 5 min: run the probe on the deck (ms per tile, VmRSS) and `iw ... scan dump` |
| 0.1 viewer | savvy-heavy | map widget, Web Mercator, RAM LRU, display scale, imported MBTiles/PMTiles raster, on-demand cache store with source policy flags (OpenTopoMap, Thunderforest key, own server), GPS reader + status page + dot, scale bar, attribution, keys/touch/pointer, scenes at 640x480, 800x480, 720x720, 1280x800, 480x640, 480x800, BOARD-TESTS, README, icon, amd64 | 2-2.5 | 15 min: plug the GPS outdoors on the deck and the Pi 3A+; pan/zoom by keys and touch; Wi-Fi off: cached tiles still show; unplug/replug the GPS |
| 0.2 downloads + tracks | savvy-medium | area download (count, size, rate limit, resume), cache cap and eviction, tracks (record, list, GPX export), `position.state` writer, "Set the clock from GPS" | 1.25-1.75 | 10 min: download a small area from the owner's server, a 10 min walk recorded, GPX opened on the PC |
| 0.3 wardrive | savvy-medium | nmcli scan loop (Net Toolbox port), optional `iw` dump, BLE (Net Toolbox port, off by default on the deck), WiGLE CSV with GPS time, AP table, overlay and list, export | 1.25-1.75 | 15 min walk: log, overlay, export; the deck's BT keyboard with BLE off and on |
| 0.4 Mesh Hop link | savvy-medium | shared folder docs, Mesh Hop overlay file and `position.state` reader ("Use the deck's GPS"), `geo:` waypoint action | 0.75-1.25 | 10 min with a MeshCore node |
| Docs | docs-writer | README rows, app page, data licences, APP-GUIDELINES shared-folder paragraph | 0.25 | - |
| Optional V | savvy-heavy | vector spike 0.5, then an MVT renderer with a fixed style and labels | 3-4.5 | per phase |
| Optional L | launcher-dev | launcher-owned GPS (6.4 B), Settings > GPS, release | 1.5-2.5 + release | per release |

**Totals:** MVP (P0 + 0.1 + docs) 2.75-3.5 ws, about 1.1-1.4 M tokens; through 0.3: 5.25-7 ws, about 2.1-2.8 M
tokens; through 0.4: 6-8.25 ws, about 2.4-3.3 M tokens. The orchestrator's design pass and reviews are extra.

### 8.2 Tests (most on the PC)

- **Fixtures:** recorded NMEA files (cold start without fix, a walk, a drive, four constellations in GSV, bad
  checksums, a wrong-speed garbage stream, an unplug mid-line); replayed through a regular file (viz1090's
  `$VIZ_DEV_DIR` hook, V) and through pseudo-terminals (Serial Console's `sim` pattern, V).
- **Fake tile server:** Python stdlib serving generated tiles (each tile shows its z/x/y) with `Cache-Control`,
  `ETag`, 304 answers, 429/503, slow and dropped responses (RSS Reader's `tests/unit/local_server.py` is the precedent, V); the
  test asserts the User-Agent, the parallel request cap and that a `bulk_ok=0` source refuses an area download.
- **Archives:** a script generates tiny MBTiles and PMTiles fixtures (raster PNG/JPEG, 256 and 512 px, TMS rows).
- **Wardrive:** Net Toolbox's recorded `nmcli` and `bluetoothctl` outputs (V); a golden WiGLE CSV; a scripted walk
  that joins scans and fixes.
- **Units:** projection on known points, scale bar steps, LRU and cap, policy flags, CSV escaping (SSIDs with commas,
  quotes, UTF-8, empty hidden SSIDs).
- **Headless scenes** (`_common/headless`): every page by touch and by keys at the sizes of phase 0.1, plus a
  `lifecycle` scene for `X-Background=run`.
- **Board tests:** short owner protocols (quality bar: the main path works), one per phase, as in the table.

## 9. Risks and non-goals

- **Licence drift:** provider terms change; the source records carry the flags, the README dates them.
- **Readability:** raster labels at 2x are soft; if the owner finds them poor on the deck, `@2x` sources or the
  vector phase are the answers (D4, D5).
- **Storage:** raster archives grow by 4x per zoom; the download page must show size before starting.
- **Serial port conflicts:** never probe ports held by other apps or known LoRa boards (4.2).
- **Bluetooth keyboard:** BLE scanning on the deck (D8).
- **Power:** GPS + scans + display on a Zero 2 W (4.5).
- **Non-goals:** routing and turn-by-turn, address search (needs a geocoding index), satellite imagery, monitor mode,
  packet capture, any upload, OSMF tile servers.

## 10. Not verified

- Tile decode times and the RAM peak on a Zero 2 W (P0).
- Whether `iw dev <if> scan dump` works for the deck user, and how often NetworkManager refuses `--rescan yes`.
- Two-finger touch delivery through `cp0_lvgl` (pinch).
- Panel densities other than the deck's (nominal diagonals used).
- Vector tile sizes per area and an MVT render time on the deck.
- Thunderforest `@2x` URLs and their caching headers; OpenTopoMap headers.
- The deck's power margin with a USB GPS plugged in.
- Kismet's licence text (named only).

## 11. Questions for the owner

1. **Scope and order (D1, D2):** one app, phases 0.1 viewer, 0.2 downloads + tracks, 0.3 wardrive, 0.4 Mesh Hop?
   *Default: yes.*
2. **Name (7.1):** pick one, or give your own. *No default.*
3. **Tile sources (D3):** no OSMF servers at all; import files and your own LAN server as the main way; OpenTopoMap
   (viewing cache) and Thunderforest (your own free key) as opt-in sources? *Default: yes.*
4. **Do you have, or want, a tile server on the PC** (a folder of tiles, or tileserver-gl) for area downloads, and
   a USB stick for imports? *Needed for the 0.1/0.2 board tests.*
5. **Raster now, vector spike after 0.2 (D4)?** *Default: yes.*
6. **Display scale (D5):** automatic about 2x with a Settings override? *Default: yes; the exact rule in the design
   pass.*
7. **GPS ownership (D6):** the app now, the launcher later with the same `position.state`; gpsd not used by default?
   *Default: yes.*
8. **Your GPS receiver:** which one (u-blox USB, a bridge-chip puck, Bluetooth, a phone)? *Needed for P0 fixtures.*
9. **Wardriving (D7, D8, D9):** NetworkManager scans only (no root helper), BLE off by default with a switch, logs
   local only, WiGLE export by hand, no upload code? *Default: yes.*
10. **Storage (D10):** the shared folder `~/.local/share/cyberdeckzero/maps/`, cache cap 512 MB or 10 % of free space
    (the smaller), archives on a USB stick allowed? *Default: yes.*
11. **Screens (D11):** 640x480 minimum with `X-Portrait=480x640`; a 480x320 layout only if cheap after P0? *Default:
    yes.*
12. **Clock (D12):** offer "Set the clock from GPS" (sudo prompt) when the clock is unsynced, never automatic?
    *Default: yes.*

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

Our repositories (V):
- Apps `origin/main` `bec05f3`: `apps/net-toolbox/src/core/{wifiscan,ble,walktest,netlog_wifi}.hpp`;
  `apps/serial-console/src/core/{ports,devices,identify,probe,serial_io}.{hpp,cpp}`;
  `apps/viz1090/src/core/{gps,nmea,place}.hpp`, `apps/viz1090/build/make_world.py`;
  `apps/mesh-hop/src/core/{nodes,model,client,serial_transport,deck_clock}.{hpp,cpp}`;
  `apps/rss-reader/docs/IMAGES.md`, `apps/rss-reader/src/ui/image_codecs.h`, `apps/rss-reader/src/core/fetch.hpp`;
  `apps/home-assistant/src/core/curl_api.cpp`; `apps/_common/scons/main.SConstruct`; `docs/APP-GUIDELINES.md`.
- Launcher `github/main` `3824b2cc`: `docs/APP-CONTRACT.md`, `docs/dev/ROADMAP.md`,
  `projects/APPLaunch/config_defaults.mk`.
