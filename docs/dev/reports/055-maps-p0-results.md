# 055 - Deck Atlas P0 spike: results (generated tiles)

Dates 2026-11-05 and 2026-11-06. The P0 spike of report 054 (section 13), run with **generated tiles only** (no real
tile set yet), as the owner approved. Throwaway probes, no product code, nothing installed on any board. Boards named
for P0: the deck (`zero7.local`) and zero8 (`zero8.local`, 192.168.68.184). The live GPS test (M7) and the real-tile
legibility read (M5) were out of scope.

**Markers.** **V** = measured in this spike; **R** = read on a board or in our docs; **I** = estimated or computed, not
measured.

## 0. Summary

**Two runs.** First run (2026-11-05, 21:37-22:52): the PC did everything; zero8 did the 256 px decode, then **reset by
itself** during the 512 px RGB PNG decode (a single `nice` process, memory and `throttled` fine at the last check); I
stopped there; the deck did not resolve. Second run (2026-11-05 23:27 to 2026-11-06 00:07, the owner had both boards
online): **every board probe ran on the deck and on zero8**, one probe per ssh session under `nice -n 19` with a 25 s
pause and a health line before and after each; **no reset, `throttled` 0x0 at all 118 checks**, MemAvailable never
under 251 MB. The 512 px RGB PNG decode that was running at zero8's first reset ran twice on zero8 this time (200
tiles, then all 2,107) without trouble; the cause of the first reset is unknown (section 2).

**Headline numbers (Zero 2 W: the deck, with zero8 where it differs; the two agree within 3-7 %).**

| Item | Result |
|---|---|
| Decode 256 px, median / town-tile median (stb_image built like RSS Reader) | **palette PNG 1.37 / 2.67 ms**; RGB PNG 2.34 / 6.09 ms; JPEG q75 4.47 / 5.49 ms (deck, V); zero8 1.28 / 2.54, 2.20 / 5.89, 4.35 / 5.41 ms |
| Decode 512 px (`@2x` tiles), median / town tiles | **palette PNG 4.52 / 7.08 ms**; RGB PNG 9.34 / 19.7 ms (p95 24.0); JPEG 16.8 / 19.7 ms (deck, V); zero8 within 5 % |
| stb `STBI_NEON` (JPEG only) | JPEG 256 px 4.47 -> **3.18 ms** on the deck (1.4x), zero8 4.35 -> 3.08 ms (V) |
| lodepng instead of stb | 1.6x (RGB PNG) to 2.7x (palette PNG) slower on both boards (V) |
| RGB888 -> RGB565 | 0.68 ms per 256 px tile, 2.8 ms per 512 px tile (V) |
| Magnify a 256 px tile | 2x nearest **0.77-0.80 ms**; generic bilinear 10.1 ms (1.5x) and 17.9 ms (2x); fixed-phase 1.5x **6.25 ms** (V) |
| Cold 640x480 view at 2x, read + decode + RGB565 + magnify, two workers | **11.8 ms median, 19.6 ms p95** (palette, deck); RGB PNG 14.4 / 24.7 ms; zero8 12.6 / 16.2 ms (V); threshold 500 ms |
| Read one tile on the SD card, warm median / cold mean / cold p95 | PMTiles `pread` **5.6 us / 0.40 ms / 2.05 ms**; MBTiles SQLite 53 us / 0.48 ms / 2.52 ms; `mmap` 2.7 us / 0.30 ms / 2.06 ms (deck, V) |
| Read path winner | **`pread` for PMTiles, plain SQLite for MBTiles, never `mmap`**: VmRSS after one pass over a 12.9 MB archive is 1.1 MB with `pread`, 4.0 MB with SQLite, **13.4 MB with `mmap`** (both boards, V) |
| Tile LRU, VmRSS of the probe (both boards, identical) | RGB565 64 tiles **9.1 MB**, 128 tiles 17.1 MB; ARGB8888 64 tiles 17.5 MB; 2x budget 16 x 512 KB **9.3 MB** (V) |
| Engine in a second app, arm64 binary (text+data over an empty stub) | SQLite `dlopen`ed + stb_image **+50 KB**; static trimmed SQLite +0.71 MB; static default +1.04 MB; Mesh Hop 0.8.2 on the deck is 4.72 MB (V) |
| Engine in a second app, VmRSS on the boards | no cache +1.0 (static trimmed) to +1.75 MB (`dlopen`); 4 MB cache **+5.2 to +5.95 MB**; `dlopen` with 3 MB cache +4.95 MB (deck and zero8, V) |
| Folder of 2,107 tiles to one MBTiles on the SD card | deck **784 tiles/s** (rollback journal) / 643 (WAL); zero8 558 / 511; SD writes 14.7 MB vs 28.7 MB for a 14.1 MB file (V) |
| Panel density | deck **11.3 px/mm** (`screen.state` `ppmm=1130`, `density=default`: the launcher's assumed value, no `APPLAUNCH_PANEL_MM` on the deck, R); zero8 **6.3 px/mm** (`ppmm=630`, `density=panel_mm`, 79x49 mm, R) |
| Label size, 12 px labels of a 256 px tile | deck 2x: font 2.12 mm (the launcher's smallest text there 1.59 mm); zero8 1.5x: 2.86 mm (smallest launcher text 2.22 mm); zero8 1x: 1.90 mm, below it (I, arithmetic) |

**Go/no-go (054 section 13).** M1 decode **PASS** (all measured, wide margins); M2 reads **PASS**; M3 RAM **PASS** for an
RGB565 LRU of 6-8 MB, FAIL for 128 tiles or ARGB8888 (with the app base of the guidelines); M4 **PASS** (binary and
VmRSS, at 3 and at 4 MB of cache on the boards); M6 done; M8 **PASS** (784 tiles/s on the deck); M5 and M7 open
(real tiles, the GPS puck). Details in section 5.

**Recommendation (section 6).** No change to the plan, the engine interface, the phases or the costs of 0.1-0.4. For
0.1: 8-bit palette PNG for the archives we prepare; an RGB565 LRU of 6-8 MB on the 512 MB boards; stb_image with RSS
Reader's flags plus `STB_IMAGE_STATIC`; 2x by nearest (or `@2x` tiles), 1.5x by a fixed-phase path; `pread` for
PMTiles and plain SQLite for MBTiles, never `mmap`; SQLite through `dlopen` (as 054 7.4 proposed); a small SQLite page
cache; the folder import into a new file with a rollback journal and 256-tile transactions; Mesh Hop's tile cache 3 MB.
Display scale: deck 2x, zero8 1.5x in both orientations, which is what the automatic rule of D5 gives. **P0 is complete
except M5 and M7**, which wait for the owner's real tile set and the u-blox puck.

## 1. What was built

All probe code is our own, throwaway, MIT; it lives in `C:\CLAUDE\zero7\maps-p0-work` (outside the repos) and, since it
is small, as plain files in `055-assets/` next to this report (Appendix B).

| Piece | What it does |
|---|---|
| `gen/gen_tiles.py` | Draws an imaginary area in world coordinates (3 towns, 9 villages, street grids, main roads, 2 motorways, a river, lakes, woods, about 48,000 buildings, street-name labels at 12 px on the 256 px scale, place names, each tile's z/x/y in a corner), so tiles join up. Zoom 0-14 over a 39 x 39 tile square at zoom 14 (about 64 x 64 km at 48.6 N): **2,107 tiles per set**. 256 px and 512 px (`@2x`: the same picture at twice the resolution, labels 24 px). Formats: RGB PNG (zlib level 6), 8-bit palette PNG (median cut, no dither), JPEG q75. Each set as a `z/x/y` folder, an MBTiles 1.3 file (flat `tiles` table, TMS rows) and a PMTiles v3 file (the official BSD-3 `pmtiles` 3.8.1 Python writer: gzip directories, tiles stored as they are). DejaVu Sans from the launcher repo draws the text. 22 s on the PC with 8 processes. |
| `probe/pmt.c` | A minimal PMTiles v3 reader written from the spec (CC0): header, varint directories, leaf directories with a small cache, Hilbert tile ids; gzip directories inflated with stb_image's own zlib (`stbi_zlib_decode_noheader_buffer`), so PMTiles needs no zlib. Reads by `pread` or `mmap`. |
| `probe/p0probe.c` | One static binary with the subcommands `decode` (M1; `--max N` takes an evenly spaced sample), `view` (a full screen of tiles with N workers: read + decode + RGB565 + magnify, page cache of the archive emptied first), `read` (M2), `lru` (M3), `convert` (M8), `verify` (PMTiles and MBTiles hold the same bytes). SQLite 3.50.4 amalgamation compiled in. `p0probe_neon` is the same with `STBI_NEON`. |
| `probe/stub.cpp` | M4: a C++ stub linked like the apps (`-O2`, dynamic glibc/libstdc++, `-Wl,--as-needed`), built empty, with SQLite linked statically (default and trimmed options) and with SQLite through `dlopen("libsqlite3.so.0")`; it opens an MBTiles file and fills a tile cache of 3, 4 or 8 MB. |
| `probe/build.sh`, `probe/run_p0.sh` | WSL build (fresh dirs in `~/maps-p0/build-*`, under `/tmp/wsl-build.lock`, 3 jobs; x86_64 native and arm64 with `aarch64-linux-gnu-gcc`); the run script of the PC and of the first board run. |
| `probe/one.sh`, `probe/board_seq.py` | The second board run: `one.sh` runs one probe under `timeout` and `nice -n 19` between two health lines (uptime, MemAvailable, `throttled`, temperature, core voltage, load); `board_seq.py` (on the PC) sends one probe per ssh session, waits 25 s between probes and stops at MemAvailable under 80 MB (zero8) or 100 MB (deck), a non-zero `throttled`, an uptime that goes backwards, or a lost connection (no retry). |
| `gen/legibility.py` | Renders what each panel shows of a town centre at 1x, 1.5x, 2x, `@2x` and `@2x` scaled to 1.5x, and prints the physical label sizes from the panel density. |

Third-party code compiled into the probes (none of it copied into the repos): stb_image 2.30 (public domain / MIT,
the copy of RSS Reader), SQLite 3.50.4 amalgamation (public domain, sqlite.org), lodepng 20261102 (zlib licence,
upstream GitHub; LVGL's own copy decodes into LVGL draw buffers and needs LVGL, so it was not used).

**The generated set.** Mean tile sizes: RGB PNG 12.1 KB (256 px) / 26.5 KB (512 px), palette PNG 6.0 / 12.4 KB, JPEG
4.6 / 12.6 KB; town-centre tiles 38-46 KB (RGB PNG 256 px), rural ones 1-2 KB. The 256 px RGB PNG mean is close to the
OSMF CDN average of 12.6 KB quoted in 054 2.5 (a coincidence of the drawing, but a fair load). Container overhead:
MBTiles +5 % (27.7 MB for 26.2 MB of tiles), PMTiles +0.02 % (a 4.4 KB root directory, no leaf directories at this
size). `verify`: all 2,107 tiles identical in both containers, for all six sets (PC; zero8 for two sets).

## 2. Hosts and board health

| Host | Hardware, OS | Ran |
|---|---|---|
| PC | i5-12450H, WSL2 Ubuntu 24.04 (glibc 2.39), ext4 on a virtual disk | all probes |
| deck `zero7.local` (192.168.68.159) | Pi Zero 2 W, Debian 13 (glibc 2.41), launcher 0.9.3, 640x480, ext4 on a 29 GB SD card | first run: unreachable (the name did not resolve at 21:37, 21:38, 22:43); second run: all 31 probes |
| zero8 192.168.68.184 | Pi Zero 2 W Rev 1.0, Debian 13.7, kernel 6.18.50+rpt-rpi-v8, glibc 2.41, launcher 0.9.4, PDA mode on (320x480), ext4 on the SD card | first run: 256 px decode, then the reset; second run: all 28 probes |

**zero8, first run (2026-11-05, R):**

| Time | Load | MemAvailable | `throttled` | Note |
|---|---|---|---|---|
| 22:44 (first contact) | 0.01 | 261 MB | 0x0 | up 17 min, 41.9 C; `cyberdeckzero` 21.8 MB RSS; 11 GB free on `/`; `/tmp` a 208 MB tmpfs, so the scratch dir went to `~/p0-probe` on the SD card |
| 22:47:07 (run start) | 0.81 | 248 MB | 0x0 | after the 244 MB upload |
| 22:47:12 (decode start) | 0.82 | 267 MB | 0x0 | last health line before the reset |
| about 22:50 | - | - | - | **reset** while decoding the 512 px RGB PNG set (one `nice -n 10` process, after the three 256 px sets); no persistent journal on zero8, so the cause cannot be read; `throttled` is cleared by a reboot |
| 22:52 | 0.46 | 260 MB | 0x0 | up 1-2 min, 45.6 C, core 1.256 V, the launcher service active again (restarted by the reboot, not by me) |

**Second run (R, 2026-11-05 23:27 to 2026-11-06 00:07):**

| Board | First check | During (every probe, before and after) | After cleanup |
|---|---|---|---|
| zero8 | 23:27: up 37 min (the reboot of about 22:50 and no other), `throttled` 0x0, core 1.256 V, 41.3 C, MemAvailable 254 MB, load 0.08 | 56 checks 23:31-23:47: MemAvailable 252-269 MB, 40.8-47.2 C, `throttled` 0x0, uptime always rising: **no reset** | 23:48: up 57 min, 0x0, 41.3 C, 1.256 V, 263 MB, launcher service active |
| deck | 23:27: up 2 min (just started by the owner), `throttled` 0x0, core 1.325 V, 33.2 C, MemAvailable 264 MB, load 0.63; 23:30 load 0.07, nothing but the launcher busy (`cyberdeckzero` 20.7 MB RSS) | 62 checks 23:48-00:06: MemAvailable 251-273 MB, 43.5-50.5 C, `throttled` 0x0, uptime always rising | 00:06: up 40 min, 0x0, 46.2 C, 1.325 V, 270 MB, launcher service active |

So zero8's first reset did not come back under the same probe, with more care (`nice -n 19`, pauses) but the same kind
of load; it may have been the power supply at that moment or a reboot by hand. Unexplained; worth a look if zero8
resets again under other loads.

**Cleanup.** After each run `~/p0-probe` (binaries, six PMTiles, one or two MBTiles, the tile folders: 190-246 MB) was
deleted and checked absent on each board that had it; `df` back to the size before (zero8 2.6 GB used, deck 7.0 GB);
no probe process left. Nothing was installed; no service, launcher, app or `/etc` file touched; the launcher was never
started with a flag. On the PC the WSL tree `~/maps-p0` (about 0.4 GB) and `C:\CLAUDE\zero7\maps-p0-work` stay.

## 3. Results per host and format

### 3.1 M1 decode (stb_image 2.30, PNG + JPEG only, no SIMD: RSS Reader's flags)

Per tile, ms, all 2,107 tiles, the median of the second pass ("warm"; the first pass in a fresh process was within 5 %
of it, so cold and warm decode are the same here). "Dense" = the quarter of the tiles with the largest files (town
tiles).

| Set | Mean file | PC median / dense | deck median / p95 | deck dense median / p95 | zero8 median / p95 | zero8 dense median / p95 |
|---|---|---|---|---|---|---|
| RGB PNG 256 | 12.1 KB | 0.140 / 0.579 | **2.34** / 7.73 | 6.09 / 10.1 | 2.20 / 7.44 | 5.89 / 9.73 |
| palette PNG 256 | 6.0 KB | 0.081 / 0.241 | **1.37** / 3.14 | 2.67 / 3.90 | 1.28 / 3.05 | 2.54 / 3.77 |
| JPEG q75 256 | 4.6 KB | 0.291 / 0.441 | **4.47** / 5.81 | 5.49 / 6.14 | 4.35 / 5.69 | 5.41 / 6.02 |
| RGB PNG 512 | 26.5 KB | 0.542 / 1.706 | **9.34** / 24.0 | 19.7 / 29.8 | 8.86 / 23.2 | 19.1 / 28.6 |
| palette PNG 512 | 12.4 KB | 0.208 / 0.525 | **4.52** / 8.42 | 7.08 / 10.7 | 4.39 / 8.16 | 6.94 / 10.4 |
| JPEG q75 512 | 12.6 KB | 1.044 / 1.466 | **16.8** / 20.5 | 19.7 / 21.8 | 16.3 / 20.2 | 19.2 / 21.4 |

zero8 ran the 512 px RGB PNG set first on an evenly spaced sample of 200 tiles (8.87 ms median), then on all of them.
The board/PC ratio is 15-17x on the median, 10-16x on dense tiles. Other paths (median, ms):

| Step | PC | deck | zero8 |
|---|---|---|---|
| RGB888 -> RGB565, 256 px / 512 px | 0.039 / 0.158 | 0.68 / 2.81 | 0.67 / 2.80 |
| 2x nearest on RGB565, 256 -> 512 | 0.030 | **0.77-0.80** | 0.72-0.75 |
| 1.5x generic bilinear (fixed point, scalar), RGB888 -> RGB565 | 0.65-0.70 | 10.1-10.2 | 10.0 |
| 2x generic bilinear | 1.15-1.25 | 17.9-18.0 | 17.7 |
| 1.5x fixed phase (every 2 px give 3, weights 1/3-2/3) | 0.32 | **6.25-6.29** | 6.15 |
| stb with `STBI_NEON`, JPEG 256 px (median / dense) | - | **3.18** / 4.08 | 3.08 / 4.01 |
| lodepng, first-pass median on a 600-tile sample: palette 256 / RGB PNG 256 (stb on the same: 1.44 / 2.39) | 0.194 / 0.250 (warm) | 3.72 / 3.77 | 3.67 / 3.63 |

The fixed-phase 1.5x output was compared with the bilinear one by eye on a town tile: no visible difference. On the
board it is 1.6x faster than the generic bilinear (2.2x on the PC), still about four palette-PNG decodes.

### 3.2 A full view (read + decode + RGB565 + magnify, two workers, archive page cache emptied, 12 random views)

| Screen, scale, tiles | Tiles per view | PC median | deck median / p95 / max | zero8 median / p95 / max |
|---|---|---|---|---|
| 640x480, 2x (nearest), palette 256 | 3.9 | 1.1 ms | **11.8 / 19.6 / 19.7 ms** | 12.6 / 16.2 / 18.7 ms |
| 640x480, 2x, RGB PNG 256 | 3.9 | 1.3 ms | 14.4 / 24.7 / 31.5 ms | 15.2 / 30.6 / 31.4 ms |
| 640x480, 1x, palette 256 | 9.7 | 1.7 ms | 15.9 / 31.0 / 41.6 ms | 17.3 / 28.1 / 49.8 ms |
| 480x320, 1.5x (fixed phase), palette 256 | 3.6 | 1.4 ms | 19.3 / 30.0 / 31.2 ms | 20.1 / 27.1 / 31.7 ms |
| 320x480, 1.5x (fixed phase), palette 256 | 4.3 | 1.8 ms | 26.4 / 29.7 / 30.2 ms | 27.7 / 36.2 / 42.9 ms |
| 640x480, 1x, palette 512 (`@2x` tiles) | 3.9 | 1.5 ms | 20.8 / 36.7 / 40.1 ms | 22.3 / 34.8 / 37.8 ms |

On the PC more combinations ran (JPEG, RGB PNG at every size, 1 and 4 workers: 4 workers 0.7-0.8 ms against 1.1-1.2
ms with 2 on the 2x view); the PC's "cold" is not cold (Windows caches the WSL virtual disk). On the boards it is: the
probe drops the archive from the page cache before each view.

### 3.3 M2 read paths (2,107 tiles in random order, two passes)

| Host, method | Open | Cold median / mean / p95 | Warm median / p95 | VmRSS after the cold pass (anon + file) |
|---|---|---|---|---|
| deck, PMTiles `pread`, palette 256 (12.9 MB) | 2.3 ms | 8 us / 0.40 ms / 2.05 ms | **5.6 / 27 us** | **1.1 MB** (0.2 + 0.9) |
| deck, MBTiles SQLite, palette 256 (14.1 MB) | 32.9 ms | 80 us / 0.48 ms / 2.52 ms | 53 / 111 us | 4.0 MB (2.3 + 1.7) |
| deck, PMTiles `mmap` | 7.2 ms | 4 us / 0.30 ms / 2.06 ms | 2.7 / 22 us | **13.4 MB** (0.2 + 13.2) |
| zero8, PMTiles `pread` | 4.9 ms | 7 us / 0.42 ms / 2.05 ms | 4.9 / 20 us | 1.1 MB |
| zero8, MBTiles SQLite | 33.7 ms | 58 us / 0.53 ms / 2.94 ms | 51 / 100 us | 4.0 MB |
| zero8, PMTiles `mmap` | 7.2 ms | 3 us / 0.30 ms / 1.96 ms | 2.2 / 14 us | 13.4 MB |
| PC, `pread` / SQLite / `mmap` / SQLite + `mmap_size` | 0.3-0.5 ms | (not cold: see 3.2) | 0.6 / 2.9 / 0.3 / 2.5 us | 1.3 / 4.0 / 13.3 / 15.7 MB |

The cold median is small because the kernel's read-ahead brings neighbouring tiles in; the p95 (about 2 ms) is a
real SD read. Every reader is far under the 2 ms warm threshold; SQLite costs 10x `pread` per lookup and 33 ms to
open a cold file, both negligible against a 1.4-6 ms decode. The deciding difference is RAM: **`mmap` makes VmRSS grow
with the part of the archive that was looked at** (file-backed, reclaimable, but counted by the guidelines' VmRSS
check), so a long session over a large archive would breach 25 MB. SQLite's anonymous 2 MB is its default page cache
(`cache_size` -2000); a small `PRAGMA cache_size` gives most of it back.

### 3.4 M3 RAM of a decoded-tile LRU

The probe decodes tiles through fixed slots (1,000 decodes on the boards, 4,000 on the PC). The deck and zero8 gave
identical numbers.

| LRU | Cache | VmRSS deck/zero8 (PC) | VmHWM deck/zero8 | With the app base of 13-15 MB (I) | vs 25 MB |
|---|---|---|---|---|---|
| 64 x 256 px RGB565 | 8.0 MB | **9.1 MB** (9.3) | 9.3 MB | 22.1-24.1 MB | PASS, tight |
| 64 x 256 px RGB888 | 12.0 MB | - (13.3) | - | 26.3-28.3 MB | FAIL |
| 64 x 256 px ARGB8888 | 16.0 MB | 17.5 MB (17.7) | 17.7 MB | 30.5-32.5 MB | FAIL |
| 128 x 256 px RGB565 | 16.0 MB | 17.1 MB (17.3) | 17.3 MB | 30.1-32.1 MB | FAIL |
| 128 x 256 px RGB888 / ARGB8888 | 24 / 32 MB | - (25.3 / 34.0) | - | 38-49 MB | FAIL |
| 12 x 512 px RGB565 (2x magnified) | 6.0 MB | - (7.5) | - | 20.5-22.5 MB | PASS |
| 16 x 512 px RGB565 (2x magnified) | 8.0 MB | **9.3 MB** (9.5) | 9.6 MB | 22.3-24.3 MB | PASS, tight |
| 32 x 512 px RGB565 (2x magnified) | 16.0 MB | - (17.6) | - | 30.6-32.6 MB | FAIL |
| 16 x 512 px RGB565 (`@2x` tiles decoded at 512) | 8.0 MB | - (9.4, peak 10.8) | - | 22.4-24.4 MB (peak 23.8-25.8) | tight |

The probe's own overhead over the cache is 1.1-1.5 MB on the boards (process, decode scratch freed after each tile).
The app base (13-15 MB for a kind A app, APP-GUIDELINES section 1, R) is added by arithmetic; an app's real VmRSS was
not measured (Mesh Hop was not running on the deck and I did not start it).

### 3.5 M4 the engine in a second app

Binary size, arm64 cross-build, stripped, `size` text+data (V):

| Stub | text+data | Added over the empty stub | vs Mesh Hop's binary (0.8.2 on the deck: 4,722,744 B file; 0.8.1 package: 4,624,338 B text+data) |
|---|---|---|---|
| empty C++ stub | 7,321 B | - | - |
| + stb_image + SQLite through `dlopen` | 59,001 B | **+51,680 B (50 KB)** | +1.1 % |
| + stb_image + SQLite static, trimmed (`-Os`, 14 `SQLITE_OMIT_*` and size options) | 756,125 B | **+748,804 B (0.71 MB)** | +16 % |
| + stb_image + SQLite static, default options | 1,099,662 B | **+1,092,341 B (1.04 MB)** | +23 % |

amd64 gives the same picture (+49 KB, +0.68 MB, +1.06 MB). stb_image alone is 48 KB of text+data on arm64 with its
26 global `stbi_*` functions; built with `STB_IMAGE_STATIC` it exports none and is 36.5 KB. Both boards have
`libsqlite3.so.0` (`libsqlite3.so.0.8.6`, 1.59 MB) and glibc 2.41 (the stubs need 2.38).

VmRSS added over the empty stub (2.8 MB on the boards, 2.0 MB on the PC); the stub opens the MBTiles file and fills a
cache of 128 KB RGB565 tiles from zoom 14:

| Stub | deck: no cache / 3 MB / 4 MB cache | zero8: no cache / 3 MB / 4 MB | PC: no cache / 4 MB / 8 MB |
|---|---|---|---|
| static default | +1.3 / - / **+5.6 MB** | +1.3 / - / +5.6 MB | +2.3 / +6.4 / +10.8 MB |
| static trimmed | +1.0 / - / **+5.2 MB** | +1.0 / - / +5.2 MB | +2.2 / +6.2 / +10.0 MB |
| `dlopen` | +1.75 / **+4.95** / **+5.95 MB** | +1.75 / +4.95 / +5.95 MB | +3.0 / +7.0 / +10.7 MB |

On the boards all three stay within 6 MB at a 4 MB cache (`dlopen` just: 5.95 MB), and `dlopen` with 3 MB adds 4.95
MB. The `dlopen` extra over the static builds is file-backed pages of the shared library (shared with any other
process that maps it). The PC figures (x86_64) are larger and were the only ones in the first run.

Not checked (in 054's M4 but not in this brief): whether apps that do not enable `CONFIG_CP0_LVGL_MAP` stay unchanged
in size (it depends on how the toolkit is linked into apps: an archive drops unused objects, a list of objects does
not).

### 3.6 M8 folder to MBTiles (the 2,107-file palette folder, 12.9 MB, page cache emptied first)

| Host, mode (256 tiles per transaction) | Time | Tiles/s | Written to disk | Output |
|---|---|---|---|---|
| deck, rollback journal (`DELETE`) | 2.7 s | **784** | 14.7 MB (1.04x) | 14.1 MB |
| deck, WAL, `synchronous=NORMAL` | 3.3 s | 643 | 28.7 MB (2.0x) | 14.1 MB |
| zero8, rollback journal | 3.8 s | 558 | 14.7 MB | 14.1 MB |
| zero8, WAL | 4.1 s | 511 | 28.7 MB | 14.1 MB |
| PC, rollback journal / WAL / no journal | 0.27-0.38 s | 5,570 / 5,990-7,890 / 6,450 | 14.7 / 28.7 / 14.3 MB | 14.1 MB |
| PC, WAL, one transaction per tile (300 tiles) vs 256 per transaction | - | 5,870 vs 7,480 | 6.6 vs 3.2 MB for 1.6 MB (4.1x vs 2.0x) | - |

A large folder (054 2.5: a city at zoom 0-17 is about 13,300 tiles) converts in about 17-20 s on the deck at this
rate (I, from 784 tiles/s; real tiles are larger). For a one-shot conversion into a new file a rollback journal halves
the SD writes against WAL and is faster (a failed import deletes the file and starts again); WAL stays right for the
live cache store (054 2.7).

## 4. Legibility harness (M5 prepared, M6)

A throwaway full-screen LVGL page on the boards was not practical: it would have to take the screen from the running
launcher, which the brief forbids stopping. So the harness writes PNGs at each panel's pixel size
(`C:\CLAUDE\zero7\maps-p0-work\out\legibility\`: `deck_*.png` 640x480, `zero8-480x320_*.png`, `zero8-320x480_*.png`, one
contact sheet per board) and computes the physical size of the labels. Panel densities, read on 2026-11-05 from each
board's `screen.state`: **the deck 11.3 px/mm** (`ppmm=1130`, `density=default`: the launcher's assumed value; the deck
has no `APPLAUNCH_PANEL_MM`; text tokens `32,28,24,18`, `target=79`); **zero8 6.3 px/mm** (`ppmm=630`,
`density=panel_mm`, board.conf `APPLAUNCH_PANEL_MM=79x49`; in narrow portrait text tokens `24,20,16,14`). `board.state`
carries no density (it has `ram_mb`: 414 on the deck, 415 on zero8).

Labels are DejaVu Sans 12 px on the 256 px tiles (cap height 9 px, x-height 7 px) and 24 px on the 512 px ones. The
reference is the smallest text the launcher draws on that canvas (`screen.state` `text=`: the deck 18 px, zero8 in
narrow portrait 14 px):

| Board | Option | Label font on the panel | Font size | Cap height | Smallest launcher text |
|---|---|---|---|---|---|
| deck 640x480, 11.3 px/mm | 1x | 12 px | 1.06 mm | 0.80 mm | 1.59 mm (18 px) |
| | 1.5x | 18 px | 1.59 mm | 1.19 mm | 1.59 mm |
| | **2x** | 24 px | **2.12 mm** | 1.59 mm | 1.59 mm |
| | `@2x` tile 1:1 | 24 px | 2.12 mm | 1.59 mm | 1.59 mm |
| zero8 480x320 and 320x480, 6.3 px/mm | 1x | 12 px | 1.90 mm | 1.43 mm | 2.22 mm (14 px, portrait) |
| | **1.5x** | 18 px | **2.86 mm** | 2.14 mm | 2.22 mm |
| | 2x | 24 px | 3.81 mm | 2.86 mm | 2.22 mm |
| | `@2x` tile at 0.75 | 18 px | 2.86 mm | 2.14 mm | 2.22 mm |

So on the generated tiles the labels reach the launcher's smallest text at **2x on the deck** (1.5x only equals it)
and at **1.5x on zero8** in both orientations (1x stays under it). That is what the automatic rule of D5 gives (ppmm /
3.8 to the nearest of 1x/1.5x/2x: deck 3.0, capped at 2x; zero8 1.66, so 1.5x). The landscape text tokens of zero8
were not read (it was in portrait; switching restarts the launcher).

**What remains for M5:** the owner reads real tiles (street names, place labels) at arm's length on the deck and on
zero8 at 480x320 and 320x480, at each factor and with `@2x` tiles where the source has them. That needs the real tile
set (054 section 13) and a way to show a full-screen picture on the panels: a throwaway `X-Fullscreen` viewer app
started from the launcher (so the launcher keeps the screen otherwise), or Deck Atlas 0.1 itself with the scale
override.

## 5. Go/no-go against 054 section 13

| Check | Threshold (go) | Result | Status |
|---|---|---|---|
| M1 decode, Zero 2 W | 256 px PNG median <= 15 ms; JPEG <= 10 ms; 512 px PNG <= 60 ms; cold 640x480 view at 2x <= 0.5 s with two workers | deck: RGB PNG 256 2.34 ms (palette 1.37), JPEG 4.47 ms, RGB PNG 512 9.34 ms (p95 24.0, town tiles 19.7), view 11.8 ms median, 19.6 ms p95; zero8 within 7 % (V) | **PASS** |
| M2 reads | one lookup <= 2 ms warm for the chosen reader | deck `pread` 5.6 us, SQLite 53 us (p95 111 us); zero8 the same (V) | **PASS** |
| M3 RAM | the deck probe with its 2x budget under 25 MB VmRSS | probe 9.3 MB at 16 x 512 KB on the deck (V) + base 13-15 MB = 22.3-24.3 MB (I) | **PASS** (tight at 8 MB, comfortable at 6 MB); 128 tiles or ARGB8888: FAIL |
| M4 second app | engine <= 1.5 MB binary and <= 6 MB VmRSS with a 3-4 MB cache | binary +0.05 / +0.71 / +1.04 MB (V); VmRSS on the deck +4.95 MB (`dlopen`, 3 MB), +5.2-5.95 MB (4 MB) (V) | **PASS** (`dlopen` at 4 MB within 0.05 MB of the limit) |
| M5 legibility | one factor readable on each of deck, zero8 480x320, zero8 320x480 with the real set | not run (no real set); generated tiles: deck 2x and zero8 1.5x reach the launcher's smallest text (I) | **open** |
| M6 density | per board from the launcher | deck 11.3 px/mm (assumed default), zero8 6.3 px/mm (panel size) (R) | done |
| M7 GPS | puck found in <= 10 s, a fix outdoors | not in this run | **open** |
| M8 folder import | >= 50 tiles/s on the deck | 784 tiles/s (rollback journal), 643 (WAL) (V) | **PASS** |

## 6. Recommendation

1. **Tile format for the archives we prepare: 8-bit palette PNG** (256 px and `@2x` 512 px). Half the size of RGB PNG
   (6.0 vs 12.1 KB) and the fastest decode on the Zero 2 W (1.37 ms median, 2.67 ms on town tiles, against 2.34/6.09
   for RGB PNG and 4.47/5.49 for JPEG; at 512 px 4.52 against 9.34 and 16.8). JPEG only for imagery. This confirms 054
   12.4; containers unchanged (MBTiles cache and import, PMTiles import).
2. **LRU: RGB565, 6-8 MB on the 512 MB boards** (48-64 tiles at 1x, 12-16 at 2x), as 054 3.2 proposed; 6 MB is the
   comfortable end (the 8 MB end is within 1 MB of the 25 MB rule with the guidelines' app base). Never ARGB8888 or 128
   tiles on these boards. Larger boards keep 054 3.2's classes.
3. **Decode path:** stb_image with RSS Reader's flags, plus `STB_IMAGE_STATIC` inside the engine (no exported
   symbols, 12 KB smaller); lodepng is not needed (1.6-2.7x slower on the boards, and LVGL's copy is tied to its draw
   buffers). Decode to RGB, convert to RGB565 (0.68 ms). `STBI_NEON` makes JPEG 1.4x faster on arm64; it only matters
   for JPEG sources, and turning it on means the PC tests no longer run the board's JPEG code: a choice for 0.1.
   **Magnify: 2x by nearest** (0.8 ms) or with `@2x` tiles; **1.5x by the fixed-phase path** (6.3 ms, against 10.1 ms
   for a generic bilinear; a full 480x320 view at 1.5x still comes up in about 20 ms).
4. **Read path: `pread` for PMTiles, plain SQLite for MBTiles, never `mmap`** (VmRSS grows with the archive pages
   touched: 13.4 MB after one pass over 12.9 MB). SQLite with a small `PRAGMA cache_size` (its default 2 MB page cache
   showed up as anonymous memory). The PMTiles directories inflate with stb_image's zlib: no zlib dependency.
5. **SQLite: `dlopen("libsqlite3.so.0")` as 054 7.4 proposed** (+50 KB per app, no build change; both boards have
   the library; +0.4-0.7 MB more VmRSS than a static build, mostly shared file pages). One correction to 054 7.4:
   linking SQLite statically would not need the sysroot's development files (the amalgamation is one source file);
   its cost is a 9 MB vendored source, build time and +0.71-1.04 MB per app binary.
6. **Folder import:** convert into a new MBTiles file with a rollback journal and about 256 tiles per transaction
   (784 tiles/s on the deck, 1.04x writes; WAL 643 tiles/s and 2x writes; one commit per tile 4x). The live cache
   store keeps WAL (054 2.7).
7. **Display scale per board:** deck 2x, zero8 1.5x in both orientations (the D5 automatic rule gives exactly that);
   the owner's real-tile read decides (M5).
8. **Mesh Hop (0.4): a 3 MB tile cache** (24 tiles at 1x) through `ram_cache_bytes`: +4.95 MB VmRSS on the deck with
   `dlopen`, a margin of 1 MB under the 6 MB of the threshold; the overlay-file fallback of 054 6.2 is not needed.
9. **Engine interface (054 3.7): no change.** Points 3, 4 and 6 are implementation rules inside the engine;
   `ram_cache_bytes` already carries point 8. **Cost: no change** to 0.1, 0.2, 0.4 or the totals.

## 7. What remains

- **Real-tile legibility (M5):** the owner's tile set (054 section 13) and a full-screen way to show it (section 4).
- **Live GPS (M7):** the u-blox puck on a named board, outdoors.
- Not in this brief, still in 054's P0 list: vector tile sizes (M9), the fake tile server with the three guards,
  whether apps without `CONFIG_CP0_LVGL_MAP` stay unchanged in size, a real app's VmRSS with the engine.
- zero8's reset of 2026-11-05 about 22:50: not reproduced; no cause found (no persistent journal on zero8).

## Appendix A. How to re-run

Paths are the PC's. The password only from the `PIPW` environment variable.

1. **Tiles** (Windows Python with Pillow; a venv with the BSD-3 `pmtiles` package):
   `python -m venv --system-site-packages C:\CLAUDE\zero7\maps-p0-work\venv`,
   `venv\Scripts\python -m pip install pmtiles`, then
   `venv\Scripts\python gen\gen_tiles.py tiles --jobs 8` in `maps-p0-work` (22 s; 6 folders, 6 MBTiles, 6 PMTiles).
2. **Third-party sources** into `maps-p0-work\third_party\`: `sqlite/sqlite-amalgamation-3500400/` (from
   `https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip`), `stb/stb_image.h` (the apps repo's
   `apps/rss-reader/third_party/stb/`), `lodepng/lodepng.cpp` and `lodepng.h` (upstream `lvandeve/lodepng`).
3. **Build** (check `wsl -e pgrep -a 'cmake|make|ninja'` first): `wsl -e sh /mnt/c/CLAUDE/zero7/maps-p0-work/probe/build.sh`.
   It copies the sources to `~/maps-p0/src` (LF), builds in fresh `~/maps-p0/build-amd64` and `build-arm64` under
   `/tmp/wsl-build.lock`, and stages each `bin/` (`p0probe`, `p0probe_neon`, the four stubs, `run_p0.sh`).
4. **PC run:** copy the tiles to `~/maps-p0/tiles` (WSL file system), then
   `sh ~/maps-p0/build-amd64/bin/run_p0.sh ~/maps-p0/build-amd64/bin ~/maps-p0/tiles ~/maps-p0/work info verify decode mag view read lru convert stub`.
5. **Board run** (one board at a time; check `uptime`, `free -m`, `vcgencmd get_throttled` and that nothing heavy runs;
   `/tmp` is a small tmpfs on these boards, so use the SD card). In Git Bash set `MSYS_NO_PATHCONV=1` (else `/home/...`
   arguments become Windows paths); use the IP when `.local` names do not resolve.
   - `python tools\brd.py run HOST "mkdir -p ~/p0-probe/bin ~/p0-probe/tiles ~/p0-probe/work"`
   - `python tools\brd.py putmany HOST /home/osrde/p0-probe/bin out\payload\one.sh out\payload\p0probe out\payload\p0probe_neon out\payload\stub_empty out\payload\stub_static out\payload\stub_static_trim out\payload\stub_dlopen`
   - `python tools\brd.py putmany HOST /home/osrde/p0-probe/tiles tiles\png256.pmtiles tiles\pal256.pmtiles tiles\jpg256.pmtiles tiles\png512.pmtiles tiles\pal512.pmtiles tiles\jpg512.pmtiles tiles\pal256.mbtiles out\payload\pal256.tar`
     (189 MB: about 70 s to zero8, 130 s to the deck), then untar `pal256.tar` in `~/p0-probe/tiles` and delete it.
   - `python tools\board_seq.py HOST 100 out\steps_deck.txt out\deck_run2.log --pause 25` (80 for zero8 with
     `out\steps_zero8.txt`): one probe per ssh session, stops by itself on bad health; 16-18 minutes per board.
     The step files list the 28-31 probe commands of this run (`sh $B/one.sh TIMEOUT $B/p0probe ...`).
   - `python tools\summarize.py out\deck_run2.log` prints the tables' numbers.
   - **Cleanup:** `python tools\brd.py run HOST "rm -rf ~/p0-probe; ls -d ~/p0-probe"` and check it is gone.
6. **Legibility:** `venv\Scripts\python gen\legibility.py tiles out\legibility`.

## Appendix B. Files in `055-assets/`

Copies of the throwaway sources from `C:\CLAUDE\zero7\maps-p0-work` (MIT, LF); the third-party sources, the generated
tiles, the venv, the logs and the build trees are not copied.

| File | What |
|---|---|
| `gen/gen_tiles.py` | the synthetic tile generator (folders, MBTiles, PMTiles) |
| `gen/legibility.py` | the legibility harness |
| `probe/p0probe.c`, `probe/pmt.c`, `probe/pmt.h`, `probe/stbimpl.c` | the probe binary and the PMTiles reader |
| `probe/stub.cpp` | the second-app stub (M4) |
| `probe/build.sh`, `probe/run_p0.sh` | WSL build and the run script of the PC and the first board run |
| `probe/one.sh`, `probe/board_seq.py`, `probe/summarize.py` | the second board run: one probe with health lines, the sequencer, the log summary |
| `probe/steps_deck.txt`, `probe/steps_zero8.txt` | the probe lists of the second board run |
| `probe/test/magtest.c` | dumps one tile through the fast 1.5x and the bilinear paths, to compare by eye |
| `probe/brd.py` | the ssh helper (paramiko; password from `PIPW` only); `tools/brd.py` in the work folder (the repo's `.gitignore` skips `tools/` folders) |
