# 055 - Deck Atlas P0 spike: results (generated tiles)

Date 2026-11-05. The P0 spike of report 054 (section 13), run with **generated tiles only** (no real tile set yet), as
the owner approved. Throwaway probes, no product code, nothing installed on any board. Boards named for P0: the deck
(`zero7.local`) and zero8 (`zero8.local`, 192.168.68.184). The live GPS test (M7) and the real-tile legibility read (M5)
were out of scope for this run.

**Markers.** **V** = measured in this spike; **R** = read on a board or in our docs; **I** = estimated (from the
measured board/PC ratio or from arithmetic), to confirm on a board.

## 0. Summary

**What ran.** The PC (WSL2 on an Intel i5-12450H, native x86_64 build): everything. **zero8** (Pi Zero 2 W, Debian
13, launcher 0.9.4, portrait 320x480 at the time): decode timing for the three 256 px formats, then **the board reset
by itself** during the next probe (512 px RGB PNG decode, a single-threaded `nice` process, about 3 minutes into
the run, memory and `throttled` fine at the last check); I stopped the board work there and removed everything I had
copied. **The deck** was not reachable (`zero7.local` did not resolve at three attempts, 21:37, 21:38 and 22:43):
nothing ran there. So the Zero 2 W numbers below are zero8's, for decode only; the rest are PC numbers, with board
figures estimated from the measured board/PC ratio (10-16x) where useful.

**Headline numbers.**

| Item | Result |
|---|---|
| Decode on a Zero 2 W (zero8, stb_image built like RSS Reader), median / dense-tile median / p95 | **palette PNG 256 px 1.28 / 2.54 / 3.05 ms**, RGB PNG 256 px 2.20 / 5.89 / 7.44 ms, JPEG q75 256 px 4.35 / 5.41 / 5.69 ms (V) |
| RGB888 to RGB565, 256 px tile, Zero 2 W | 0.67 ms (V) |
| Magnify a 256 px tile, Zero 2 W | 2x nearest **0.75 ms**; generic bilinear **10.0 ms at 1.5x, 17.7 ms at 2x** (V); a fixed-phase 1.5x path is 2.2x faster than bilinear on the PC, about 4.5 ms on the board (I) |
| 512 px decode (@2x tiles), PC; Zero 2 W estimate | PC median: palette 0.21, RGB PNG 0.54, JPEG 1.04 ms (V); board about 2.5-3.3, 6.5-8.7, 12.5-16.7 ms (I) |
| Read one tile, warm (PC) | PMTiles `pread` 0.6 us, `mmap` 0.3 us, MBTiles SQLite 2.9 us (V); board well under 0.1 ms (I) |
| Read path winner | **`pread` for PMTiles, SQLite without `mmap` for MBTiles.** `mmap` puts every touched archive page into VmRSS (13.3 MB after one pass over a 12.9 MB file), which the 25 MB rule counts (V) |
| Tile LRU, VmRSS of the probe (PC) | RGB565: 64 tiles 9.3 MB, 128 tiles 17.3 MB; ARGB8888: 64 tiles 17.7 MB, 128 tiles 34.0 MB; 2x budget 16 x 512 KB 9.5 MB (V) |
| Engine weight in a second app, arm64 binary (text+data, vs an empty stub) | SQLite `dlopen` + stb_image **+50 KB**; static trimmed SQLite **+0.71 MB**; static default SQLite **+1.04 MB**; Mesh Hop's binary is 4.66 MB (the local 0.8.1 arm64 package; 0.8.2 is the published one) (V) |
| Engine VmRSS in a second app (PC) | without tile cache +2.2 (static trimmed) to +3.0 MB (`dlopen`); with a 4 MB cache +6.2 to +7.0 MB (V) |
| Folder to MBTiles (PC) | 5,570-7,890 tiles/s; SD writes: WAL 2.0x the output size, rollback journal 1.04x, one commit per tile 4.1x (V) |
| Panel density | zero8 6.3 px/mm (`screen.state` `ppmm=630`, `density=panel_mm`, R); the deck 11.3 px/mm (the launcher's assumed default, R) |
| Label size, 12 px labels of a 256 px tile | deck 2x: font 2.12 mm (the launcher's smallest text there is 1.59 mm); zero8 1.5x: 2.86 mm (smallest launcher text 2.22 mm); zero8 1x: 1.90 mm, below it (I, arithmetic) |

**Go/no-go (thresholds of 054 section 13), as far as measured.** M1 decode **PASS** (measured on zero8 for 256 px;
512 px and the full cold view PASS by estimate); M2 reads **PASS** on the PC, board not measured; M3 RAM **PASS** for
an RGB565 LRU of 6-8 MB (64 tiles at 1x, 12-16 at 2x), **FAIL** for 128 tiles or ARGB8888 (estimate with the app base
of the guidelines); M4 binary **PASS** (all three link modes), VmRSS **PASS at a 3 MB cache, marginal FAIL at 4 MB**
(PC); M5 not run (real tiles needed; harness ready); M6 done for zero8, the deck's value is the launcher's default;
M7 not run; M8 **PASS** by estimate (PC 5,600+ tiles/s, board not measured). Details in section 5.

**Recommendation (details in section 6).** No change to the plan, the phases or the costs of 0.1-0.4. For 0.1:
8-bit palette PNG for the archives we prepare (smallest and fastest on the Zero 2 W); an RGB565 LRU of 6-8 MB on the
512 MB boards; stb_image with RSS Reader's flags plus `STB_IMAGE_STATIC`; 2x by nearest (or `@2x` tiles), 1.5x by a
fixed-phase path, never a generic per-pixel bilinear on the board; `pread` for PMTiles and plain SQLite for MBTiles,
never `mmap`; SQLite through `dlopen` (as 054 7.4 proposed); a small SQLite page cache; the folder import into a new
file with a rollback journal and 256-tile transactions; Mesh Hop's tile cache 3 MB. Display scale: deck 2x, zero8
1.5x in both orientations, which is what the automatic rule of D5 gives. **P0 is not finished on the boards:** a
follow-up of about 0.25 ws runs the same scripts on the deck and zero8 once the owner has looked at zero8's reset
(power supply, or a reboot by hand).

## 1. What was built

All probe code is our own, throwaway, MIT; it lives in `C:\CLAUDE\zero7\maps-p0-work` (outside the repos) and, since it
is small (79 KB), as plain files in `055-assets/` next to this report (Appendix B).

| Piece | What it does |
|---|---|
| `gen/gen_tiles.py` | Draws an imaginary area in world coordinates (3 towns, 9 villages, street grids, main roads, 2 motorways, a river, lakes, woods, about 48,000 buildings, street-name labels at 12 px on the 256 px scale, place names, each tile's z/x/y in a corner), so tiles join up. Zoom 0-14 over a 39 x 39 tile square at zoom 14 (about 64 x 64 km at 48.6 N): **2,107 tiles per set**. 256 px and 512 px (`@2x`: the same picture at twice the resolution, labels 24 px). Formats: RGB PNG (zlib level 6), 8-bit palette PNG (median cut, no dither), JPEG q75. Each set as a `z/x/y` folder, an MBTiles 1.3 file (flat `tiles` table, TMS rows) and a PMTiles v3 file (the official BSD-3 `pmtiles` 3.8.1 Python writer: gzip directories, tiles stored as they are). DejaVu Sans from the launcher repo draws the text. 22 s on the PC with 8 processes. |
| `probe/pmt.c` | A minimal PMTiles v3 reader written from the spec (CC0): header, varint directories, leaf directories with a small cache, Hilbert tile ids; gzip directories inflated with stb_image's own zlib (`stbi_zlib_decode_noheader_buffer`), so PMTiles needs no zlib. Reads by `pread` or `mmap`. |
| `probe/p0probe.c` | One static binary with the subcommands `decode` (M1), `view` (a full screen of tiles with N workers: read + decode + RGB565 + magnify), `read` (M2), `lru` (M3), `convert` (M8), `verify` (PMTiles and MBTiles hold the same bytes). SQLite 3.50.4 amalgamation compiled in. |
| `probe/stub.cpp` | M4: a C++ stub linked like the apps (`-O2`, dynamic glibc/libstdc++, `-Wl,--as-needed`), built empty, with SQLite linked statically (default and trimmed options) and with SQLite through `dlopen("libsqlite3.so.0")`; it opens an MBTiles file and fills a tile cache of 4 or 8 MB. |
| `probe/build.sh`, `probe/run_p0.sh` | WSL build (fresh dirs in `~/maps-p0/build-*`, under `/tmp/wsl-build.lock`, 3 jobs; x86_64 native and arm64 with `aarch64-linux-gnu-gcc`); the run script with a guard that stops when MemAvailable < 60 MB or `throttled` is non-zero, `nice -n 10`, one probe at a time. |
| `gen/legibility.py` | Renders what each panel shows of a town centre at 1x, 1.5x, 2x, `@2x` and `@2x` scaled to 1.5x, and prints the physical label sizes from the panel density. |

Third-party code compiled into the probes (none of it copied into the repos): stb_image 2.30 (public domain / MIT,
the copy of RSS Reader), SQLite 3.50.4 amalgamation (public domain, sqlite.org), lodepng 20261102 (zlib licence,
upstream GitHub; LVGL's own copy decodes into LVGL draw buffers and needs LVGL, so it was not used).

**The generated set.** Mean tile sizes: RGB PNG 12.1 KB (256 px) / 26.5 KB (512 px), palette PNG 6.0 / 12.4 KB, JPEG
4.6 / 12.6 KB; town-centre tiles 38-46 KB (RGB PNG 256 px), rural ones 1-2 KB. The 256 px RGB PNG mean is close to the
OSMF CDN average of 12.6 KB quoted in 054 2.5 (a coincidence of the drawing, but a fair load). Container overhead:
MBTiles +5 % (27.7 MB for 26.2 MB of tiles), PMTiles +0.02 % (a 4.4 KB root directory, no leaf directories at this
size). `verify`: all 2,107 tiles identical in both containers, for all six sets (PC; zero8 for the two sets that had
an MBTiles copy).

## 2. Hosts and board health

| Host | Hardware, OS | Ran | Health |
|---|---|---|---|
| PC | i5-12450H, WSL2 Ubuntu 24.04 (glibc 2.39), ext4 on a virtual disk | all probes | - |
| deck `zero7.local` | Pi Zero 2 W | **nothing**: the name did not resolve at 21:37, 21:38 and 22:43 (two retries, the brief's limit) | not read |
| zero8 192.168.68.184 | Pi Zero 2 W Rev 1.0, Debian 13.7, kernel 6.18.50+rpt-rpi-v8, glibc 2.41, launcher 0.9.4, PDA mode on (320x480), ext4 on the SD card | info, verify, decode of png256, pal256, jpg256 (stb); the board reset during png512 | see below |

zero8, step by step (R):

| Time | Load | MemAvailable | `throttled` | Note |
|---|---|---|---|---|
| 22:44 (first contact) | 0.01 | 261 MB | 0x0 | up 17 min, 41.9 C; `cyberdeckzero` 21.8 MB RSS; 11 GB free on `/`; `/tmp` a 208 MB tmpfs, so the scratch dir went to `~/p0-probe` on the SD card |
| 22:47:07 (run start) | 0.81 | 248 MB | 0x0 | after the 244 MB upload |
| 22:47:12 (decode start) | 0.82 | 267 MB | 0x0 | last health line before the reset |
| about 22:50 | - | - | - | **reset** while decoding the 512 px RGB PNG set (one `nice` process); no persistent journal on zero8, so the cause cannot be read; `throttled` is cleared by a reboot |
| 22:52 | 0.46 | 260 MB | 0x0 | up 1-2 min, 45.6 C, core 1.256 V, the launcher service active again (restarted by the reboot, not by me) |

**Cleanup.** zero8: `~/p0-probe` (binaries, six PMTiles, two MBTiles, two tile folders: 246 MB) deleted and checked
absent; `df` back to 2.6 GB used; no probe process left. Nothing was installed, no service, launcher, app or `/etc` file
touched. The deck: nothing copied. On the PC the WSL tree `~/maps-p0` (about 0.4 GB) stays for the follow-up.

## 3. Results per host and format

### 3.1 M1 decode (stb_image 2.30, PNG + JPEG only, no SIMD: RSS Reader's flags)

Per tile, ms. "Dense" = the quarter of the tiles with the largest files (town tiles). Three passes; the first pass in a
fresh process was within 5 % of the later ones, so "cold" and "warm" decode are the same here.

| Set | Mean file | PC median | PC p95 | PC dense median | zero8 median | zero8 p95 | zero8 dense median | zero8 dense p95 | zero8 max |
|---|---|---|---|---|---|---|---|---|---|
| RGB PNG 256 | 12.1 KB | 0.140 | 0.763 | 0.579 | **2.20** | 7.44 | **5.89** | 9.73 | 16.6 |
| palette PNG 256 | 6.0 KB | 0.081 | 0.304 | 0.241 | **1.28** | 3.05 | **2.54** | 3.77 | 6.7 |
| JPEG q75 256 | 4.6 KB | 0.291 | 0.491 | 0.441 | **4.35** | 5.69 | **5.41** | 6.02 | 8.5 |
| RGB PNG 512 | 26.5 KB | 0.542 | 2.153 | 1.706 | not run (reset) | | est. 17-27 (I) | | |
| palette PNG 512 | 12.4 KB | 0.208 | 0.705 | 0.525 | not run | | est. 5.5-8.4 (I) | | |
| JPEG q75 512 | 12.6 KB | 1.044 | 1.663 | 1.466 | not run | | est. 18-23 (I) | | |

zero8 / PC: 15-16x on the median, 10-12x on dense tiles; the estimates use that range. Other paths:

| Step | PC | zero8 |
|---|---|---|
| RGB888 -> RGB565, 256 px (512 px) | 0.039 ms (0.158) | 0.67 ms (V); 512 px est. 2.7 ms |
| 2x nearest on RGB565, 256 -> 512 | 0.030 ms | **0.75 ms** |
| 1.5x generic bilinear (fixed point, scalar), RGB888 -> RGB565 | 0.65-0.70 ms | **10.0 ms** |
| 2x generic bilinear | 1.15-1.25 ms | **17.7 ms** |
| 1.5x fixed-phase (every 2 px give 3, weights 1/3-2/3) | 0.32 ms | est. 4.5 ms (I) |
| lodepng instead of stb (PC): RGB PNG 256 / palette 256 / RGB 512 / palette 512, median | 0.250 / 0.194 / 0.912 / 0.606 ms (1.8x, 2.4x, 1.7x, 2.9x stb's) | not run |
| stb with `STBI_NEON` (JPEG IDCT and colour conversion) | - | not run (it was the next step on the board) |

The fixed-phase 1.5x output was compared with the bilinear one by eye on a town tile: no visible difference.

### 3.2 A full view (PC; read + decode + RGB565 + magnify, cold page cache, 12 random views per row)

| Screen, scale, tiles | Workers | RGB PNG | palette PNG | JPEG | tiles per view |
|---|---|---|---|---|---|
| 640x480, 1x, 256 px | 2 | 1.8 ms | 1.7 ms | 3.0 ms | 9.7 |
| 640x480, 2x (nearest), 256 px | 2 | 1.3 ms | 1.1 ms (1 worker 1.2-1.5, 4 workers 0.7-0.8) | 1.5 ms | 3.9 |
| 480x320, 1.5x (fixed phase), 256 px | 2 | 1.7 ms | 1.4 ms (generic bilinear 2.3-2.9) | 2.0 ms | 3.6 |
| 320x480, 1.5x (fixed phase), 256 px | 2 | 2.1 ms | 1.8 ms | 2.5 ms | 4.3 |
| 640x480, 1x, 512 px (`@2x` tiles) | 2 | 2.3 ms | 1.5 ms | 3.5 ms | 3.9 |
| 480x320, 1x, 512 px | 2 | 1.5 ms | 1.2 ms | 1.8 ms | 2.6 |

The PC's "cold" is not cold: `posix_fadvise(DONTNEED)` empties the WSL guest's cache, but Windows still caches the
virtual disk. **Zero 2 W estimate (I):** a cold 640x480 view at 2x is about 4 tiles (6 at worst); per tile 2.5-5.9 ms
decode + 0.7 ms RGB565 + 0.75 ms magnify, plus an SD read of a few KB (about 1-2 ms, I) and the PMTiles header and
root directory once; with two workers **about 15-35 ms**, at 1x (12 tiles at worst) about 50 ms; the PC times x 16
give the same order (18 ms). The threshold is 500 ms.

### 3.3 M2 read paths (PC, 2,107 tiles in random order, two passes)

| Method, file | Open | Cold median / p95 / mean | Warm median / p95 | VmRSS after one pass (anon + file) |
|---|---|---|---|---|
| PMTiles `pread`, palette 256 (12.9 MB) | 0.31 ms | 1 / 167 / 38 us | **0.6 / 3.4 us** | **1.3 MB** (0.2 + 1.1) |
| PMTiles `mmap`, palette 256 | 0.39 ms | 0 / 118 / 11 us | 0.3 / 1.5 us | **13.3 MB** (0.2 + 13.1) |
| MBTiles SQLite, palette 256 (14.1 MB) | 0.43 ms | 4 / 299 / 51 us | 2.9 / 6.5 us | 4.0 MB (2.1 + 1.9) |
| MBTiles SQLite + `PRAGMA mmap_size`, palette 256 | 0.51 ms | 3 / 151 / 15 us | 2.5 / 4.8 us | 15.7 MB (0.3 + 15.3) |
| PMTiles `pread`, RGB PNG 256 (26.2 MB) | 0.31 ms | 2 / 188 / 54 us | 0.7 / 6.5 us | 1.3 MB |
| PMTiles `mmap`, RGB PNG 256 | 0.39 ms | 1 / 211 / 21 us | 0.4 / 3.5 us | **26.0 MB** |
| MBTiles SQLite, RGB PNG 256 (27.7 MB) | 0.47 ms | 7 / 499 / 88 us | 3.3 / 10.9 us | 4.3 MB |

Every method is three orders of magnitude under the 2 ms threshold on the PC; on a Zero 2 W the warm lookups stay
far under it (I) and the cold ones are dominated by the SD card (not measured). The deciding difference is RAM:
**`mmap` makes VmRSS grow with the part of the archive that was looked at** (file-backed, reclaimable, but counted by
the guidelines' VmRSS check), so a long session over a large archive would breach 25 MB. SQLite's anonymous 2 MB is
its default page cache (`cache_size` -2000); a small `PRAGMA cache_size` gives most of it back.

### 3.4 M3 RAM of a decoded-tile LRU (PC; the probe decodes 4,000 tiles through the slots)

| LRU | Cache | VmRSS | VmHWM | With the app base of 13-15 MB (I) | vs 25 MB |
|---|---|---|---|---|---|
| 64 x 256 px RGB565 | 8.0 MB | 9.3 MB | 9.6 MB | 22.3-24.3 MB | PASS, tight |
| 64 x 256 px RGB888 | 12.0 MB | 13.3 MB | 13.5 MB | 26.3-28.3 MB | FAIL |
| 64 x 256 px ARGB8888 | 16.0 MB | 17.7 MB | 17.8 MB | 30.7-32.7 MB | FAIL |
| 128 x 256 px RGB565 | 16.0 MB | 17.3 MB | 17.6 MB | 30.3-32.3 MB | FAIL |
| 128 x 256 px RGB888 | 24.0 MB | 25.3 MB | 25.5 MB | 38-40 MB | FAIL |
| 128 x 256 px ARGB8888 | 32.0 MB | 34.0 MB | 34.1 MB | 47-49 MB | FAIL |
| 12 x 512 px RGB565 (2x magnified) | 6.0 MB | 7.5 MB | 7.6 MB | 20.5-22.5 MB | PASS |
| 16 x 512 px RGB565 (2x magnified) | 8.0 MB | 9.5 MB | 9.6 MB | 22.5-24.5 MB | PASS, tight |
| 32 x 512 px RGB565 (2x magnified) | 16.0 MB | 17.6 MB | 17.7 MB | 30.6-32.6 MB | FAIL |
| 16 x 512 px RGB565 (`@2x` tiles, decoded at 512) | 8.0 MB | 9.4 MB | 10.8 MB | 22.4-24.4 MB (peak 23.8-25.8) | tight |

The probe's own overhead over the cache is about 1.3-2.0 MB (process, decode scratch freed after each tile); VmHWM
shows the scratch of a 512 px decode (about 1.4 MB). The app base (13-15 MB for a kind A app, APP-GUIDELINES section
1, R) is added by arithmetic: the deck and zero8 were not measured.

### 3.5 M4 the engine in a second app

Binary size, arm64 cross-build, stripped, `size` text+data (V):

| Stub | text+data | Added over the empty stub | vs Mesh Hop 0.8.1 (4,624,338 B text+data; file 4,657,208 B) |
|---|---|---|---|
| empty C++ stub | 7,321 B | - | - |
| + stb_image + SQLite through `dlopen` | 59,001 B | **+51,680 B (50 KB)** | +1.1 % |
| + stb_image + SQLite static, trimmed (`-Os`, 14 `SQLITE_OMIT_*` and size options) | 756,125 B | **+748,804 B (0.71 MB)** | +16.2 % |
| + stb_image + SQLite static, default options | 1,099,662 B | **+1,092,341 B (1.04 MB)** | +23.6 % |

amd64 gives the same picture (+49 KB, +0.68 MB, +1.06 MB). stb_image alone is 48 KB of text+data on arm64 with its
26 global `stbi_*` functions; built with `STB_IMAGE_STATIC` it exports none and is 36.5 KB (the unused ones vanish).
Mesh Hop's binary needs `libstdc++`, `libinput`, `libxkbcommon`, `libfreetype`, `libm`, `libc`, `libgcc_s`; the stubs
need glibc 2.38 or later (zero8 has 2.41). zero8 has `libsqlite3.so.0` (`libsqlite3.so.0.8.6`, 1.59 MB), so `dlopen`
works there (R; the deck not checked).

VmRSS (PC; the stub opens the MBTiles file and fills a cache of 128 KB RGB565 tiles from zoom 14):

| Stub | Engine, no cache | + 4 MB cache | + 8 MB cache |
|---|---|---|---|
| static default | +2.3 MB | +6.4 MB | +10.8 MB |
| static trimmed | +2.2 MB | +6.2 MB | +10.0 MB |
| `dlopen` | +3.0 MB | +7.0 MB | +10.7 MB |

The `dlopen` overhead is mostly file-backed pages of the shared library (shared with any other process that maps
it). With a 3 MB cache the engine adds 5.2-6.0 MB (I, arithmetic from the measured overhead). Mesh Hop's own VmRSS
was not measured (the deck was not reachable); with the guidelines' 13-15 MB base plus 6-7 MB it stays under 25 MB.

Not checked (in 054's M4 but not in this brief): whether apps that do not enable `CONFIG_CP0_LVGL_MAP` stay unchanged
in size (it depends on how the toolkit is linked into apps: an archive drops unused objects, a list of objects does
not).

### 3.6 M8 folder to MBTiles (PC; the 2,107-file palette folder, page cache emptied first)

| Mode | Tiles/s | Written to disk | Output |
|---|---|---|---|
| WAL, `synchronous=NORMAL`, 256 tiles per transaction | 5,990-7,890 | 28.7 MB (2.0x) | 14.1 MB |
| rollback journal (`DELETE`), 256 per transaction | 5,570 | **14.7 MB (1.04x)** | 14.1 MB |
| no journal, 256 per transaction | 6,450 | 14.3 MB | 14.1 MB |
| WAL, one transaction per tile (300 tiles) | 5,870 | 6.6 MB for 1.6 MB (4.1x) | - |
| WAL, 256 per transaction (the same 300 tiles) | 7,480 | 3.2 MB (2.0x) | - |
| RGB PNG folder (26.2 MB), WAL, 256 | 6,800 | 55.9 MB (2.0x) | 27.7 MB |

Zero 2 W estimate (I): 2,107 small random reads from the SD card (about 1 ms each) plus 15-29 MB of writes at 10-20
MB/s: about 3-6 s, so **several hundred tiles per second**, far over the 50 tiles/s of the threshold; the SD card, not
the CPU, sets the rate. For a one-shot conversion into a new file a rollback journal halves the SD writes against
WAL (a failed import deletes the file and starts again); WAL stays right for the live cache store (054 2.7).

## 4. Legibility harness (M5 prepared, M6)

A throwaway full-screen LVGL page on the boards was not practical: it would have to take the screen from the running
launcher, which the brief forbids stopping. So the harness writes PNGs at each panel's pixel size
(`C:\CLAUDE\zero7\maps-p0-work\out\legibility\`: `deck_*.png` 640x480, `zero8-480x320_*.png`, `zero8-320x480_*.png`, one
contact sheet per board) and computes the physical size of the labels. Panel densities: **zero8 6.3 px/mm**, read from
its `screen.state` (`ppmm=630`, `density=panel_mm`, board.conf `APPLAUNCH_PANEL_MM=79x49`, 2026-11-05, R); **the
deck 11.3 px/mm**, the launcher's default (`density=default`, the APP-CONTRACT example of the deck, R; not read today).
`board.state` carries no density (it has `ram_mb=415` on zero8); `ppmm` is in `screen.state`.

Labels are DejaVu Sans 12 px on the 256 px tiles (cap height 9 px, x-height 7 px) and 24 px on the 512 px ones. The
reference is the smallest text the launcher draws on that canvas (`screen.state` `text=` tokens: the deck 18 px,
zero8 in narrow portrait 14 px):

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
| M1 decode, Zero 2 W | 256 px PNG median <= 15 ms; JPEG <= 10 ms; 512 px PNG <= 60 ms; cold 640x480 view at 2x <= 0.5 s with two workers | zero8: RGB PNG 2.20 ms (dense 5.89), palette PNG 1.28 ms (dense 2.54), JPEG 4.35 ms (dense 5.41) (V); 512 px PNG est. 6.5-8.7 ms median, 17-27 ms dense (I); cold view est. 15-35 ms (I) | **PASS** (256 px measured; 512 px and the view by estimate) |
| M2 reads | one lookup <= 2 ms warm for the chosen reader | PC warm 0.3-3.3 us for all readers (V); board est. under 0.1 ms (I) | **PASS** on the PC; board to confirm |
| M3 RAM | the deck probe with its 2x budget under 25 MB VmRSS | probe 9.5 MB at 16 x 512 KB (V, PC) + base 13-15 MB = 22.5-24.5 MB (I); 12 slots 20.5-22.5 MB | **PASS** (estimate; tight at 8 MB, comfortable at 6 MB); 128 tiles or ARGB8888: FAIL |
| M4 second app | engine <= 1.5 MB binary and <= 6 MB VmRSS with a 3-4 MB cache | binary +0.05 / +0.71 / +1.04 MB (V, arm64); VmRSS +5.2-6.0 MB at 3 MB (I), +6.2-7.0 MB at 4 MB (V, PC) | binary **PASS**; VmRSS **PASS at 3 MB, marginal FAIL at 4 MB** (PC); 0.4 keeps the engine path with a 3 MB cache |
| M5 legibility | one factor readable on each of deck, zero8 480x320, zero8 320x480 with the real set | not run (no real set); generated tiles: deck 2x and zero8 1.5x reach the launcher's smallest text (I) | **open** |
| M6 density | per board from the launcher | zero8 6.3 px/mm (R); deck 11.3 px/mm default (R) | done (deck value assumed by the launcher) |
| M7 GPS | puck found in <= 10 s, a fix outdoors | not in this run | **open** |
| M8 folder import | >= 50 tiles/s on the deck | PC 5,570-7,890 tiles/s (V); deck est. several hundred (I) | **PASS** by estimate; board to confirm |

## 6. Recommendation

1. **Tile format for the archives we prepare: 8-bit palette PNG** (256 px and `@2x` 512 px). Half the size of RGB PNG
   (6.0 vs 12.1 KB) and the fastest decode on the Zero 2 W (1.28 ms median, 2.54 ms on town tiles, against 2.20/5.89
   for RGB PNG and 4.35/5.41 for JPEG). JPEG only for imagery. This confirms 054 12.4; containers unchanged (MBTiles
   cache and import, PMTiles import).
2. **LRU: RGB565, 6-8 MB on the 512 MB boards** (48-64 tiles at 1x, 12-16 at 2x), as 054 3.2 proposed; 6 MB is the
   comfortable end (the 8 MB end is within 1 MB of the 25 MB rule with the guidelines' app base). Never ARGB8888 or 128
   tiles on these boards. Larger boards keep 054 3.2's classes.
3. **Decode path:** stb_image with RSS Reader's flags, plus `STB_IMAGE_STATIC` inside the engine (no exported
   symbols, 12 KB smaller); lodepng is not needed (1.7-2.9x slower on the PC, and LVGL's copy is tied to its draw
   buffers). Decode to RGB, convert to RGB565 (0.67 ms). **Magnify: 2x by nearest** (0.75 ms on the board) or with
   `@2x` tiles; **1.5x by a fixed-phase path** (2 source px give 3), not by a generic per-pixel bilinear (10 ms at 1.5x,
   17.7 ms at 2x on the board: more than the decode itself). Whether stb's NEON path helps JPEG on the board is open.
4. **Read path: `pread` for PMTiles, plain SQLite for MBTiles, never `mmap`** (VmRSS grows with the archive pages
   touched). SQLite with a small `PRAGMA cache_size` (its default 2 MB page cache showed up as anonymous memory).
   The PMTiles directories inflate with stb_image's zlib: no zlib dependency.
5. **SQLite: `dlopen("libsqlite3.so.0")` as 054 7.4 proposed** (+50 KB per app, no build change; zero8 has the library;
   +3 MB VmRSS, mostly shared file pages). One correction to 054 7.4: linking SQLite statically would not need the
   sysroot's development files (the amalgamation is one source file); its cost is a 9 MB vendored source, build time
   and +0.71-1.04 MB per app binary. Either fits the threshold; `dlopen` keeps the most margin.
6. **Folder import:** convert into a new MBTiles file with a rollback journal and about 256 tiles per transaction
   (1.04x writes; WAL writes 2x; one commit per tile 4x). The live cache store keeps WAL (054 2.7).
7. **Display scale per board:** deck 2x, zero8 1.5x in both orientations (the D5 automatic rule gives exactly that);
   the owner's real-tile read decides (M5).
8. **Mesh Hop (0.4): a 3 MB tile cache** (24 tiles at 1x) through `ram_cache_bytes` keeps the engine within 6 MB of
   added VmRSS; the overlay-file fallback of 054 6.2 is not needed on these numbers.
9. **Engine interface (054 3.7): no change.** Points 3, 4 and 6 are implementation rules inside the engine;
   `ram_cache_bytes` already carries point 8. **Cost: no change** to 0.1, 0.2, 0.4 or the totals; P0 needs a follow-up
   of about 0.25 ws for the board runs (section 7).

## 7. What remains

- **The board runs:** everything on the deck; on zero8 the 512 px decode, stb NEON, lodepng, full views at each scale
  with 1 and 2 workers, read paths (cold on the SD card), LRU, folder conversion on the SD card, the stubs. Same
  binaries and script (appendix). Before that: **look at zero8's reset** (its power supply and cable, or whether it was
  rebooted by hand at about 22:50), and get the deck online. About 0.25 ws.
- **Real-tile legibility (M5):** the owner's tile set (054 section 13) and a full-screen way to show it (section 4).
- **Live GPS (M7):** the u-blox puck on a named board, outdoors.
- Not in this brief, still in 054's P0 list: vector tile sizes (M9), the fake tile server with the three guards,
  whether apps without `CONFIG_CP0_LVGL_MAP` stay unchanged in size.

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
5. **Board run** (one board at a time; check `uptime`, `free -m`, `vcgencmd get_throttled` first; `/tmp` is a small
   tmpfs on these boards, so use the SD card): `python tools\brd.py run HOST "mkdir -p ~/p0-probe/bin ~/p0-probe/tiles ~/p0-probe/work"`;
   `python tools\brd.py putmany HOST /home/osrde/p0-probe/bin out\payload\*` (the arm64 `bin/`);
   `python tools\brd.py putmany HOST /home/osrde/p0-probe/tiles tiles\*.pmtiles tiles\pal256.mbtiles tiles\png256.mbtiles out\payload\pal256.tar out\payload\png256.tar`
   (about 2 minutes for 244 MB), untar the two folders there, then start each group detached so no ssh session stays
   open: `setsid sh -c 'sh ~/p0-probe/bin/run_p0.sh ~/p0-probe/bin ~/p0-probe/tiles ~/p0-probe/work info verify decode decode_neon mag > ~/p0-probe/work/log1.txt 2>&1; touch ~/p0-probe/work/done1' > /dev/null 2>&1 < /dev/null &`.
   Later groups, one at a time: `view read`, then `lru` alone, then `convert`, then `stub` alone. Poll `done*` with
   short sessions; the script stops itself when MemAvailable < 60 MB or `throttled` is non-zero. In Git Bash set
   `MSYS_NO_PATHCONV=1` (else `/home/...` arguments become Windows paths); use the IP when `.local` names do not
   resolve. **Cleanup:** `rm -rf ~/p0-probe` and check it is gone.
6. **Legibility:** `venv\Scripts\python gen\legibility.py tiles out\legibility`.

## Appendix B. Files in `055-assets/`

Copies of the throwaway sources from `C:\CLAUDE\zero7\maps-p0-work` (79 KB, MIT, LF); the third-party sources, the
generated tiles, the venv and the build trees are not copied.

| File | What |
|---|---|
| `gen/gen_tiles.py` | the synthetic tile generator (folders, MBTiles, PMTiles) |
| `gen/legibility.py` | the legibility harness |
| `probe/p0probe.c`, `probe/pmt.c`, `probe/pmt.h`, `probe/stbimpl.c` | the probe binary and the PMTiles reader |
| `probe/stub.cpp` | the second-app stub (M4) |
| `probe/build.sh`, `probe/run_p0.sh` | WSL build and the per-host run script |
| `probe/test/magtest.c` | dumps one tile through the fast 1.5x and the bilinear paths, to compare by eye |
| `probe/brd.py` | the ssh helper (paramiko; password from `PIPW` only); `tools/brd.py` in the work folder (the repo's `.gitignore` skips `tools/` folders) |
