# 038 - PDA profile: a portrait launcher with a "Strokes" writing zone

Status: study written 2026-10-24 from the owner's brief `launcher/docs/dev/tasks/032-pda-profile.md` (P1-P4, S1-S6),
with the owner instruction of the same day: base everything on **portrait 480x640 and 480x800** logical canvases;
reference board the **Pi 3A+ with the Waveshare 4.3" DSI** (profile `pi3a-wsdsi43`, 800x480 turned to 480x800); the
320x480 SPI resistive case is out of scope. Untracked in the launcher worktree `launcher-c1` (branch `c7-media-keys`,
HEAD = `github/main` = v0.8.6 aef009b; its uncommitted c-7 work was only read). Code citations are v0.8.6; for the
files c-7 modifies they are `git show github/main:<path>`. Apps repo read in the `apps-notes` worktree (1d04432, pin
v0.8.5, `markdown-notes` untracked there). No product code changed, nothing committed, no board touched.

Markers: **S** = our source (file:line); **M** = measured today on the PC (render harness of a v0.8.6 copy in WSL
`~/pda-study`, or a probe compiled against `cp0_ui_metrics.c` / `cp0_ui_osk.c`); **W** = web source (section 13);
**I** = inferred, to check on a board. Cost unit: one worker session.

## 1. Summary

- **Portrait works today in the display layer; it is a profile setting, not new rendering code.** `APPLAUNCH_ROTATE=90`
  turns any logical canvas into the framebuffer, at 16 and 32 bpp, with touch mapped back
  (S: `cp0_lvgl_dpi_scaled.c:12-28`, `cp0_fb_output.h:7-14`). On the DSI panel, `APPLAUNCH_ROTATE=90` +
  `APPLAUNCH_LOGICAL=480x800` gives a 480x800 launcher. The harness renders Home, Settings, the on-screen keyboard,
  lists, the switcher and the terminal correctly at 480x640 and 480x800 (M). What breaks is policy, not pixels: every
  fullscreen app is **hidden** (`X-MinSize=640x480`), multitasking is **off** (needs 640 wide), and page titles are
  cut by the 320 px status strip.
- **The writing zone should live in the display backend (`cp0_lvgl`), drawn by whichever process owns the panel**:
  the launcher on its own pages, over compat apps and in the terminal; a portrait-capable app in its own process,
  exactly as the status strip and the fallback keyboard work today. Recognised characters go in-process through
  `cp0_keyboard_inject_key()` (the OSK's path) or, for compat apps, through the existing `applaunch-vkbd` uinput hub.
  The brief's "launcher-owned zone + uinput, every app unchanged" does **not** work for fullscreen apps: the launcher
  stops drawing while they run, and their keyboard readers ignore virtual devices (section 2.4).
- **Landscape apps run turned, at no compositing cost.** A fullscreen app draws into `/dev/fb0` itself with the
  rotation it inherits from the board profile. Giving it the panel's native rotation (`APPLAUNCH_ROTATE=0`,
  `APPLAUNCH_LOGICAL=800x480`) in its launch environment makes it a plain 800x480 (or 640x480) landscape app; the user
  turns the device. About 0.5 session in the launcher; no app change. "Letterbox" is not viable for fullscreen apps
  and is dropped.
- **Native portrait is cheap for the list apps.** With the minimum lowered, System Monitor, RSS Reader, Net Toolbox
  and Mesh Hop pass every layout check of their own harness at 480x640 and 480x800 (M). Only text is cut, so they cost
  0.25-0.75 each. Serial Console (80 columns) and viz1090 (the map) stay turned.
- **Layout (proposal).** Zone = one writing row (letters 60 %, digits 40 %, 12-18 mm high) + one row of four 7 mm
  soft buttons, at most 33 % of the height. 480x800 at 8.54 px/mm: zone 240 px, app area **480x560**. A 640x480 panel
  of 3.5-4" turned (7.9-9 px/mm): zone about 206-211 px, app area **480x434**. The deck's own 2.8" panel turned
  (11.3 px/mm, 42 x 57 mm) is too small for a 7 mm-based PDA. The launcher at 480x560 and 480x434 renders correctly
  today (Square class, M).
- **Recogniser (S1, independent):** our own C99 library: resample to 32 points, aspect-preserving scaling, **no
  rotation normalisation**, ordered-point template distance plus a few hard gates (start and end region, line class,
  closedness), reject below a threshold. Data in a plain text glyph table plus a user overlay. All reference
  recognisers are permissive (BSD) or to be avoided (xstroke GPL-2.0, Tomoe LGPL, EdgeWrite patented); we copy none.
- **Contract 1, additive:** `X-Portrait=WxH` in `.desktop`; `orient=` and `zone=` in `screen.state`;
  `APPLAUNCH_ZONE` per launch; one new `vkb` value. No old app ever gets a portrait canvas, so nothing old breaks.
- **Cost:** launcher S2-S4 and S6 about 6.25 sessions, S1 2.0, apps (toolkit 0.5, Memo 0.5, To-Do 1.5, Address
  1.75, Date Book 3.0). A one-day proof of concept needs no code (section 9).

## 2. What the code does today

### 2.1 Rotation and the display layer

| Board (profile) | Framebuffer | `APPLAUNCH_ROTATE` | Logical canvas | Touch | Status |
| --- | --- | --- | --- | --- | --- |
| Deck (no profile) | 640x480 XRGB8888 | 0 | 640x480 | legacy axes: `SWAP_XY=1`, `INVERT_Y=1` | production |
| Pi 3A+ Luckfox 3.5" SPI (`pi3a-luckfox35`) | 320x480 RGB565 | **90** | 480x320 | `buffer` | production (S: `install.sh:493-511`) |
| Pi 3A+ Waveshare 4.3" DSI (`pi3a-wsdsi43`) | 800x480 RGB565 | 0 | 800x480 | `buffer`, ft5x06, 10 slots | production (S: `install.sh:515-536`) |
| Pi 5 HyperPixel 4 (`pi5-hyperpixel4`) | **480x800** XRGB8888 | **90** | 800x480 | `buffer` | test board (S: `install.sh:540-548`) |
| HackBerry CM5 | 720x720 | 0 | 720x720 | `buffer` | desktop board |

- **One output stage** writes every pixel (native display, scaled compat window, external VFB picture, overlay,
  fills) through a clockwise pre-rotation in 32x32 tiles (S: `cp0_fb_output.h:1-14`, `cp0_fb_output.c:152-232`). The
  logical size is always the buffer seen through the rotation; a mismatching `APPLAUNCH_LOGICAL` is logged and ignored
  (S: `cp0_fb_output.h:58-62`, `cp0_lvgl_dpi_scaled.c:1102-1133`).
- **Two touch mappings.** `buffer` mode normalises the raw axes and turns them back through the rotation
  (`cp0_fbo_touch_to_logical`, S: `cp0_lvgl_dpi_scaled.c:366-370`). The legacy mode (the deck) applies swap and invert,
  then `phys_to_land()`, which also handles 90/180/270 (S: `:120-129`, `:371-383`); untested with a rotation (I).
- **Density follows the canvas.** `APPLAUNCH_PANEL_MM` is written landscape and turned to the canvas's orientation
  (S: `cp0_ui_metrics.c:161-165`); the driver's mm are swapped for 90/270 (S: `:177-181`).
- **Gotcha (M).** A portrait buffer defaults to `APPLAUNCH_ROTATE=90` (S: `cp0_lvgl_dpi_scaled.c:1090-1091`). A natively
  portrait panel (the HyperPixel) must say `APPLAUNCH_ROTATE=0` to stay portrait; the harness rendered landscape until
  the scenes set it.
- **So portrait is reached two ways:** the DSI (landscape buffer) with `ROTATE=90` (or 270) and `LOGICAL=480x800`,
  which is the 16 bpp rot-90 path the SPI panel already uses; or the HyperPixel with `ROTATE=0`, with no rotation
  cost at all. The rotation runs on the CPU. Its cost on a full 480x800 RGB565 repaint on the Pi 3A+ is (I), to measure
  in the proof of concept.
- **The layout service already classifies portrait** (`a < 0.8`, D12 "not supported; computed so nothing breaks", S:
  `cp0_ui_metrics.h:55-61`, `cp0_ui_metrics.c:31-32`, `:72`). The responsive-shell study kept portrait "best effort
  only" (S: `reports/022-responsive-shell-study.md:212-214`, `:591`); the apps guidelines say "Landscape only: the
  launcher rotates portrait panels for you" (S: apps `docs/APP-GUIDELINES.md:28`).

### 2.2 The launcher on a portrait canvas today (M: render harness, v0.8.6 unchanged)

| Item | 480x640 at 11.3 px/mm | 480x800 at 8.54 px/mm (DSI) | Verdict |
| --- | --- | --- | --- |
| Class, tokens | portrait; row and target 79, header 56 | portrait; row and target 60, header 56 | as designed (D12) |
| Home grid | 3x4 tiles of 129x117 | 3x5 tiles of 137x128 | **works** |
| Compat window | 320x170 at 1x, toolbar 122 px | 320x170 at 1x, toolbar 97 px | works; small (37 x 20 mm on the DSI, I) |
| Native Settings, CompactList, dialogs | rows fit | rows fit | **works**; the roller leaves the top half empty |
| Page header | title cut: "Setti...", "List ...", "Ope..." | the same | **breaks**: the strip is 320 px wide at `status_pct` 100 |
| On-screen keyboard | 284 px, keys 39x49 | 318 px, keys 39x56 (4.6 x 6.6 mm) | works (cap 45 %, S: `cp0_ui_osk.c:57-60`) |
| Terminal with the keyboard | works | works | works |
| Switcher (rendered by hand) | cards fit, 7 mm targets pass; names break mid-word ("Ser ial Con...") | passes, names wrap at words | small fix; **off by default** (below) |
| Fullscreen apps | **hidden**: `X-MinSize=640x480` > 480 (S: `desktop_app_loader.cpp:201-208`) | hidden | **breaks** |
| Multitasking | **off**: `switcher_fits()` needs w >= 640 (S: `switcher_model.cpp:94-97`, `:115-120`) | off | **breaks** |

The same scenes at the proposed **app areas** 480x560 (8.54 px/mm) and 480x434 (7.9 px/mm) classify as **Square**
and render Home (3x3 and 3x2), Settings, the keyboard, lists, the switcher and the terminal correctly; only the header
title is still cut at 480 px (M). The launcher's own pages therefore need no redesign for the PDA: they need a narrower
status strip and the smaller canvas (section 4).

### 2.3 Apps and the contract on a portrait canvas

- **Every native app is fullscreen and refuses a portrait canvas twice.** Mesh Hop, RSS Reader, Net Toolbox, System
  Monitor, Serial Console and viz1090 all declare `X-Fullscreen=true` and `X-MinSize=640x480` (S: their `.desktop`
  files), so the launcher hides their tiles at 480 wide (S: `desktop_entry.cpp:85-89`). Started anyway, the toolkit
  shows the "needs 640x480" notice (S: apps `_common/app.hpp:31-32`, `app.cpp:89-105`). LAN Scan, MeshZero and Wi-Fi
  Survey are compat apps and run in the 1x window.
- **A fullscreen app owns `/dev/fb0` and inherits the board's geometry.** The launcher only prefixes the contract
  variables (S: `launch.cpp:224-226`, `:426`). The app's own `cp0_lvgl` backend reads `APPLAUNCH_ROTATE`,
  `APPLAUNCH_LOGICAL` and the touch settings from the inherited board environment (S: apps `_common/platform.hpp:6-12`).
  Compat apps, by contrast, get `APPLAUNCH_ROTATE=0` and the board geometry unset, because they draw into the 320x170
  VFB that the launcher scales (S: `native_ui.cpp:615-633`).
  - Consequence 1: on a portrait launcher an app would draw portrait by itself (option a below).
  - Consequence 2: overriding `APPLAUNCH_ROTATE` and `APPLAUNCH_LOGICAL` (plus the contract variables) per launch
    makes it a landscape app on the same panel (option b).
- **The contract says "landscape" in three places:** "All coordinates are logical: landscape" (S: `APP-CONTRACT.md:55`),
  `APPLAUNCH_SCREEN_W/H` "logical (landscape) panel size" (S: `:64`), and `desktop_entry.h:20`. The fullscreen canvas is
  `m->w x m->h` (S: `app_launch_env.cpp:56-61`).
- **The apps' layouts already follow the canvas.** Four apps run portrait in their own harness with no layout-check
  failure, once the minimum is lowered (M, section 3.2).

### 2.4 Input: touch frames, key injection, uinput, grabs

- **Raw touch with timestamps exists.** The display manager's touch reader hands every evdev frame (all slots, raw
  position, `CLOCK_MONOTONIC` event time) to a frame callback before LVGL polls the pointer every 33 ms (S:
  `cp0_touch_reader.h:25`, `:51`; `cp0_lvgl_dpi_scaled.c:399-424`; `cp0_display.h:141-142`). Strokes must take these
  frames, not the 30 Hz LVGL pointer. The hook is single-subscriber today (multitasking gestures); the zone needs a
  second consumer.
- **In-process injection exists.** `cp0_keyboard_inject_key(code, state, mods, utf8)` (S: `keyboard_input.h:68`) is
  what the on-screen keyboard uses; the key then arrives exactly like a physical key's on both delivery paths
  (S: `projects/APPLaunch/AGENTS.md`, "Keyboard input systems"). The OSK's tables already map characters to US `KEY_*`
  codes plus Shift (S: `cp0_ui_osk.c:180-199`).
- **uinput is available and already used.** `/dev/uinput` is `GROUP="input" MODE="0660"` and the user is in `input`
  (S: `91-applaunch-vkbd.rules:5-6`, `install.sh:1197-1221`). The launcher creates `applaunch-vkbd` (`BUS_VIRTUAL`,
  every key) and mirrors the physical keyboards plus the toolbar into it for compat apps only, passed by name in
  `LV_LINUX_KEYBOARD_DEVICE` (S: `native_vkbd.cpp:197-242`, `native_ui.cpp:623-632`).
- **But native apps never read a virtual keyboard by discovery.** The keyboard list excludes `BUS_VIRTUAL` devices and
  names starting with `applaunch-` (S: `cp0_keyboard_presence.c:154-158` at v0.8.6). An app reads that list plus the
  one explicit device. A launcher-made "Strokes" uinput keyboard would therefore reach a fullscreen app only if the
  launcher named it in that app's `LV_LINUX_KEYBOARD_DEVICE`. It would also have to stay virtual: counted as a keyboard,
  it would switch off the fallback keyboard rule (`keyboards=`).
- **Grabs.** With multitasking, the launcher grabs its keyboard fds only while it is in front with an app hidden (S:
  `cp0_lvgl_keyboard.c:240-246` at v0.8.6) and reads the touch screen passively while an app is in front (S:
  `APP-CONTRACT.md:476-480`). In-process injection never passes through evdev, so grabs do not affect it. c-7 adds
  media-key devices, also by capability and never grabbed; a new uinput device must stay `BUS_VIRTUAL` and named
  `applaunch-*` so that none of those rules counts it.

### 2.5 The brief checked against the code

| Brief says | Code says |
| --- | --- |
| "a virtual keyboard device (`LV_LINUX_KEYBOARD_DEVICE`)" lets every app get injected keys | `applaunch-vkbd` feeds compat apps only; native apps skip virtual devices (2.4) |
| the launcher owns the zone while any app runs | the launcher stops drawing while a fullscreen app runs, and the app owns `/dev/fb0` (S: `launch.cpp:208-247`, `APP-CONTRACT.md:242`) |
| fullscreen apps "rotated 90 degrees" need compositing | no compositing: per-launch environment only (2.3) |
| S2 = the layout service, S3 = the keyboard input | the main files are the display backend (`cp0_lvgl_dpi_scaled.c`) and the layout service (`cp0_ui_metrics.c`); the keyboard code is only called (`cp0_keyboard_inject_key`) |
| glyph table in JSON | no JSON parser with arrays in the C core: a plain text table (section 5) |
| keep multitasking off below 640x480 | the rule is "640 wide", which also switches it off on 480x800; proposed: the short side at least 480 (3.4) |
| the number 032 | taken (`reports/032-apps-status-strip.md`); this study is 038 |

## 3. Decisions proposed

### 3.1 Where the zone lives

| | **A. Zone in the display backend, drawn by the panel's owner (recommended)** | B. Launcher-owned zone over every app + uinput (the brief) | C. Zone as an app-level widget only |
| --- | --- | --- | --- |
| How | a third LVGL display (after native and compat) in `cp0_lvgl_dpi_scaled.c`: the native display shrinks to the app area, the zone display maps the bottom band; zone touches go to the recogniser; keys are injected in-process | the launcher keeps drawing and reading the zone while a fullscreen app draws its window above it | each app adds a `StrokeZone` widget to its pages |
| Launcher pages, terminal, compat apps | yes (compat: keys through `applaunch-vkbd`) | yes | launcher only |
| Portrait fullscreen apps | yes, in their own process (new pin); every page gets it with no app code: the canvas is just smaller | needs two processes writing one framebuffer, a touch split (the app must ignore zone touches), a non-suspended launcher, and the device named per app | each app's own work, every page |
| Landscape apps | no zone (turned; their own keyboard) | no zone either (turned) | no |
| Precedent | status strip and on-screen keyboard: shared code, each process draws its own (S: `APP-CONTRACT.md:293-300`, `:333-341`; report 020 option C) | none; contradicts "the launcher draws nothing over a fullscreen app" (S: `APP-CONTRACT.md:335`) | - |
| Drift | code pinned per app; glyph data shared at run time (`/usr/share/APPLaunch/strokes/`) | none | high |
| Cost | about 1.0 (display) + 1.5 (widget, injection) | about 3+ and a multitasking redesign | per app |

### 3.2 Existing landscape apps on a portrait PDA

| Option | What happens | Launcher cost | App cost |
| --- | --- | --- | --- |
| **(a) native portrait** | the app declares `X-Portrait` and lays out in the app area with the zone | in S2/S4 | 0.25-0.75 per app (table below) |
| **(b) turned** | launched with the panel's native landscape rotation (`APPLAUNCH_ROTATE=0`, `APPLAUNCH_LOGICAL=800x480`, the contract and `APPLAUNCH_SCREEN_*` variables of that canvas, no zone); the user turns the device. Touch follows (buffer mapping through the app's own rotation, S: `APP-CONTRACT.md:74-78`); snapshot, restore and the dimmed backdrop of multitasking copy raw buffer pages and do not care about rotation (S: `cp0_lvgl_dpi_scaled.c:952-1021`) | **0.5** (per-launch overrides in `app_launch_env`, `X-MinSize` checked against the landscape canvas, tests) | **none**: the app sees exactly a landscape board |
| (c) not supported | hidden (today) | 0 | 0 |
| ~~letterbox~~ | a fullscreen app would need a 480x288 canvas | - | dropped |

**Measured (M).** In a scratch copy, four apps had their minimum lowered to 480x640 and were run by their own
headless harness at 480x640 (11.3 px/mm) and 480x800 (8.54 px/mm).
- Every shot passed every layout check: nothing off screen, rows and targets at their sizes, nothing under the header
  or the keyboard.
- What breaks is text cut short: the header title (the strip starts at x 261, leaving about 140 px), and label/value
  rows at 480x640.
- The app areas 480x560 and 480x434 were not run; lists scroll there (I).

| App | Portrait today (M) | Proposal | Cost of (a) |
| --- | --- | --- | --- |
| System Monitor | fits; at 480x640 Processes drops the memory column on long names | (a) | 0.25 |
| RSS Reader | fits; the Add-feed placeholder is cut | (a) | 0.25 |
| Net Toolbox | 480x800 fine. At 480x640: "N..." for the Network row, SSIDs cut hard, the Ping target label becomes "...", the chart pushes the actions below the fold | (a), with a label/value width rule | 0.5 |
| Mesh Hop | offline pages fit (bars on one line); one Settings row is cut, with a stray dot. The chat view with messages was not shown (no board) | (b) now, (a) later: chat suits portrait | 0.5-0.75 (I) |
| Serial Console | not run; a terminal wants 80 columns, and at 480 px it gets about 40-48 (I) | **(b)** | 0 |
| viz1090 | an upstream SDL map with our framebuffer glue | **(b)** | 0 |
| LAN Scan, MeshZero, Wi-Fi Survey (compat) | 320x170 in the 1x window | as today | 0 |

Each (a) cost assumes the toolkit's narrow strip, `X-Portrait` and a re-pin (S4). Today the apps' harness ignores
`APPLAUNCH_PANEL_MM` and has no portrait densities; S4 adds them.

### 3.3 Contract changes: contract 1, additive

No app built before the change can be given a portrait canvas: the launcher gives one only to apps that declare
`X-Portrait`, and that key arrives with the new pin. All other apps get a landscape canvas, turned (b). Contract 1's rule
"keys and variables are only added" (S: `APP-CONTRACT.md:265-270`) therefore holds, and contract 2 is not needed.

| Item | Proposal |
| --- | --- |
| `.desktop` | `X-Portrait=WxH`: the smallest portrait canvas the app supports. Absent = landscape only (turned on a portrait launcher). `X-MinSize` keeps its meaning, the landscape minimum |
| per launch | portrait app: `APPLAUNCH_CANVAS`/`WINDOW` = the app area, `APPLAUNCH_ZONE=x,y,w,h` (the app's backend builds the zone there). Turned app: the landscape canvas, and `APPLAUNCH_ROTATE`, `APPLAUNCH_LOGICAL`, `APPLAUNCH_SCREEN_*` of that canvas |
| `screen.state` | `w,h,rot` stay the panel as the launcher shows it; new `orient=portrait\|landscape`, `zone=x,y,w,h` (absent = none); tokens computed for the app area |
| `input.state` | `vkb=zone`: the zone is the text input, the fallback keyboard does not pop up by itself (`cp0_app_osk_allowed()` returns 0) |
| Wording | "landscape" in `APP-CONTRACT.md` sections 3-4 becomes "as the launcher shows it"; D12 is superseded for the PDA profile |
| Kept out | the zone's geometry is not for apps to draw in; apps receive key events only (brief, section 6) |

### 3.4 Multitasking

The switcher is fine in portrait: 2x2 cards and 7 mm targets pass the harness's `expect_targets` at 480x640, 480x800,
480x560 and 480x434 (M). Card names break mid-word at 480x640 (11.3 px/mm); that needs a label wrap fix, about 0.1.

**Proposed rule:** multitasking is on when the panel's **short side is at least 480**. Today the rule is 640x480 in
landscape. The new rule keeps 480x320 and 320x170 off. Memory is the same as on the deck (512 MB). A turned app hidden
behind the portrait switcher shows its dimmed frame sideways (I, acceptable).

## 4. Layout proposal

Rule for the layout service. Inputs: the logical size, the density and `APPLAUNCH_PDA=1` in the board profile.

```
pad = SPACE_S (8)     btn = TARGET (7 mm)     write = clamp(18 mm, down to 12 mm, so that zone <= 33 % of h)
zone_h  = 1 (line) + pad + write + pad + btn + pad          (row variant)
app area = full width x (h - zone_h), at the top: status strip / page header, pages, compat window and toolbar
letters box = 60 % of (w - 3 pad), digits box = the rest; 4 soft buttons share the bottom row
write < 12 mm (dense small panels): column variant, two soft buttons at each end of the writing row (Palm III style)
```

| Panel turned to portrait | Density | Writing row | Buttons | Zone | App area (class) |
| --- | --- | --- | --- | --- | --- |
| **Pi 3A+ DSI 4.3", 480x800 (reference)** | 8.54 px/mm | 154 px (18 mm); letters 274, digits 182 px wide (32 + 21 mm) | 4 x 108x60 px | **240 px (28 mm, 30 %)** | **480x560** (Square) |
| 640x480 panel 4", 480x640 | 7.9 px/mm | 126 px (16 mm) | 55 px | 206 px (32 %) | 480x434 (Square) |
| 640x480 panel 3.5", 480x640 | 9.0 px/mm | 123 px (13.6 mm) | 63 px | 211 px (33 %) | 480x429 (Square) |
| Deck 2.8", 480x640 | 11.3 px/mm | < 12 mm in the row variant; column variant 166 px, boxes only 26 mm wide | 79 px | 183 px | 480x457; **not recommended** (42 x 57 mm panel) |

```
480x800 @ 8.54 px/mm (56 x 94 mm)                    480x640 @ 7.9 px/mm (61 x 81 mm)
+--------------------------------------+ 0          +--------------------------------------+ 0
| Title           Esc  (bt)(wifi)20:30 | 64         | Title           Esc  (bt)(wifi)20:30 | 64
|                                      |            |                                      |
|   app area 480x560: Home 3x3,        |            |   app area 480x434: Home 3x2,        |
|   Settings, apps, compat window 1x   |            |   Settings, apps, compat window 1x   |
|   + toolbar                          |            |                                      |
+--------------------------------------+ 560        +--------------------------------------+ 434
| letters 274 x 154      | digits 182  |            | letters 274 x 126      | digits 182  |
|                        |             |            |                        |             |
+--------------------------------------+ 731        +--------------------------------------+ 577
| Home | Menu | Apps | Kbd (4 x 60 px) | 800        | Home | Menu | Apps | Kbd (4 x 55 px) | 640
+--------------------------------------+            +--------------------------------------+
```

- **320x480 (out of scope; what would break, M: probe).** At 6.3 px/mm the zone would take about 40 % of the height,
  the keyboard's keys would be 23x34 px (3.7 mm wide), and the 320 px status strip would leave no room for a title. The
  resistive panel would also need a press debounce and its own calibration.
- **Status strip:** stays in the page header of the app area, the shared renderer. For widths under 600 px the strip
  should use the Compact scale (72 %, 230 px), so titles stop being cut. It is one rule in `cp0_ui_metrics.c` (the deck
  and the Pi 3A+ are pinned presets and stay pixel-identical); apps get it with their next pin.
- **Soft buttons** (default mapping, owner question 5): **Home** (launcher: Home; app: lifecycle `home`), **Menu**
  (Ctrl+O, the deck's options key, S: `APP-CONTRACT.md:345-351`), **Apps** (the switcher; lifecycle `switcher` from an
  app), **Kbd** (the on-screen keyboard on demand). In a compat app the row can carry the toolbar keys instead (Esc,
  arrows, Enter), which saves the 97 px toolbar.
- **Feedback:** ink trail in the boxes from the raw frames, the character committed on lift, a one-line echo of the
  last character and the mode (Shift, Caps, Punct, 123) above the boxes, the key click through the existing sound
  path.
- **Turned apps:** full panel landscape, no zone, no strip of ours (they draw their own).

## 5. The recogniser (S1)

**Algorithm.** Graffiti 1 shapes are single-stroke, orientation-specific and direction-specific (brief P3).
- **$P** ignores stroke direction and order (W), so it is out.
- **$1** normalises rotation and scales non-uniformly, which merges lines with other shapes (known limits of $1, W).
- So we keep $1's front end and change the rest:

1. **Input:** raw frames of contact 0 in logical px and µs. A stroke starts on a press inside a box and may leave the
   box.
2. **Clean-up:**
   - capacitive drop-outs: a lift followed by a press within 50 ms and 3 mm continues the same stroke (I, tune on the
     board);
   - a 3-point moving average, or a 1€ filter;
   - duplicates dropped.
3. **Tap or stroke:** a tap is under 1.5 mm of path and under 250 ms. It is the punctuation shift, or a dot.
4. **Resample** to N = 32 equidistant points.
5. **Normalise:** centroid to 0, **uniform** scale by the longer bbox side (aspect kept, so `-`, `/`, `l`, `I` differ),
   **no rotation**.
6. **Features (hard gates):**
   - start and end cell in a 3x3 grid of the bbox;
   - line class (straightness > 0.95, with its 8-way direction);
   - closedness;
   - turning sign;
   - an 8-direction chain code, kept for diagnostics and confusion pairs (U/V, D/P, 2/Z).
7. **Match:** the mean point-to-point distance to every template of the box's set, ordered points (so direction counts),
   k = 1 per glyph over its variants.
   - Reject when the best score is under the threshold, or when the two best glyphs are within a margin: no
     character, a short red flash of the ink.
8. **Modes** (a small state machine, pure C, tested):
   - one-shot Shift (up stroke), Caps lock (two up strokes), punctuation shift (tap);
   - the digits box selects the digit set;
   - output: `{KEY_* code, mods, UTF-8}` through the OSK's US table.

Protractor's closed-form rotation search is not needed: orientation-sensitive matching does not rotate. Its two Google
patents are listed as expired (fee related) anyway (W).

**Cost on the CPU:** 32 points x about 150 templates = 4,800 point distances per stroke. That is under 1 ms on a
Cortex-A53 at 1.0-1.4 GHz (I, measured in S1 on the PC with the scale factor of report 037). Recognition runs on the
lift; the ink is drawn as the frames arrive.

**Glyph table: a plain text file, not JSON.** The C core then needs no parser dependency. `cp0_config_json` keeps no
arrays (S: `cp0_config_json.h:9-16`), and nlohmann is C++. A JSON export can come later if a tool wants one.

```
# id     box      out          gates             template, x,y in 0..100, y down (several lines = variants)
a        letters  a            start=bl end=br   0,100 50,0 100,100
space    any      KEY_SPACE    line=right        0,50 100,50
bksp     any      KEY_BACKSPACE line=left        100,50 0,50
```

- The shipped table goes in `/usr/share/APPLaunch/strokes/glyphs.txt`.
- The user overlay goes in `~/.config/applaunch/strokes-user.txt`; the same format, appended: it adds variants, or
  replaces a glyph with `replace`.
- The glyph shapes are drawn by us from public descriptions; no Palm/ACCESS file is used.
- First set (S1):
  - a-z and 0-9;
  - space, backspace, return, Shift, Caps, and the period (tap tap);
  - 42 entries in all.
- Punctuation, accents and the "command" stroke (from the zone to the top) come in S6.

**Corpus without a board.** Line format `glyph<TAB>ppmm<TAB>t_ms,x,y ...`.
1. A **synthetic generator** from the table: per-axis scale 0.8-1.2, rotation ±12°, shear, non-uniform speed, Gaussian
   noise of 1 % of the size, landing and lift hooks, small truncations. It is the regression gate: at least 98 % top-1
   and at most 2 % rejects, deterministic seed.
2. A **capture tool** reading any evdev touch node: the FZ-M1 tablet, the Pi 3A+ over ssh with the launcher stopped, or
   `evtest` dumps through a converter.
3. Later, a dev switch in S3 that logs real zone strokes.

An eval command prints accuracy per glyph and the confusion matrix.

**References and licences** (read, never copied).

| Work | Licence / IP (W) | Use |
| --- | --- | --- |
| $1, $P (Wobbrock et al.; $N of the same family, I) | New BSD | ideas: resampling, scoring; $P unsuitable (no direction) |
| Protractor (Y. Li) | paper; Google US 8,341,558 and US 9,805,241 listed "expired - fee related" | idea only; we do no rotation search |
| Zinnia | BSD (2- or 3-clause by packager) | not needed (SVM, CJK) |
| Tomoe | LGPL-2+ | do not link or copy |
| xstroke (C. Worth) | GPL-2.0 | **do not read the code**; GPL never enters the MIT binary (the `tree.hh` precedent, PR #44) |
| EdgeWrite (Wobbrock, Myers) | CMU patent US 7,729,542, listed active | do not use the corner-sequence method |
| Unistrokes (Xerox US 5,596,656) | expired | - |
| "Graffiti" | trademark | never in code, files, UI or tests: add it to `tools/dev/check-branding.sh`'s word list (docs allow-listed) |

**Stylus.**
- A passive capacitive stylus has a 4-6 mm tip and can lose contact under light pressure (drop-outs; hence the merge
  rule above).
- Fine-tip passive styluses usually do not register on capacitive panels at all (I).
- Sample rate (60-100 Hz on ft5x06 and Goodix, I), jitter and drop-outs are measured in the proof of concept before S1's
  thresholds are frozen.
- The 3.5" resistive case would also need a press debounce and its own calibration; it is out of scope.

## 6. PIM apps (apps repo, S5)

- **Storage:** plain files in **vdir** folders, one entry per file, which is what vdirsyncer syncs to CalDAV and
  CardDAV (BSD-3, in Debian; I) and what Syncthing handles without merge conflicts. The apps never sync themselves.
- **Shared code:** Markdown Notes' atomic save and changed-on-disk check (S: report 037 section 2.4) are copied, or
  moved to `apps/_common/pim/` (0.2).

| App | Scope v1 | Cost |
| --- | --- | --- |
| **Memo = Markdown Notes, portrait** | no second app: the notes app gets `X-Portrait` and a layout pass at 480x560, 480x434 and 480x800 (editor bar, open page, preview). Its core is LVGL-free and its pages are token-based (S: apps `markdown-notes/src/core`, about 3,200 lines) | **0.5** after N1 merges |
| **To-Do** (`todo.txt`) | our own parser that keeps unknown tokens and the line order; `x` done, `(A)` priority, `+project`, `@context`, `due:`; list with priority tones, filters, add and edit, tick, archive to `done.txt` | **1.5** |
| **Address** (`.vcf`, one per contact in `~/Contacts`) | vCard 3.0 and 4.0: unfolding, escaping, FN, N, TEL, EMAIL, ADR, ORG, NOTE, BDAY, unknown lines kept; 2.1 import read-only; list with search, detail, edit | **1.75** |
| **Date Book** (`.ics`, one per event in `~/Calendar`) | VEVENT with DATE, UTC, floating and TZID times (tzdata), RRULE subset (DAILY, WEEKLY, MONTHLY, YEARLY, INTERVAL, COUNT, UNTIL, BYDAY, BYMONTHDAY), EXDATE; agenda, day and month views, event form with Rollers; alarms later (only a `run` app could ring) | **3.0** own parser; about 2.5 with libical (MPL-2.0 / LGPL-2.1, dynamic link, I) |

## 7. The 640x480 minimum: proposed wording of the exception

> **PDA exception (owner, 2026-10-xx).** `X-MinSize=640x480` stays mandatory for every new native app and keeps
> meaning the landscape minimum. An app may in addition declare `X-Portrait=480x420` (its smallest portrait canvas,
> never below 480x400) when it passes the headless layout checks at the PDA app areas 480x560 (8.5 px/mm) and 480x434
> (7.9 px/mm) and on the full portrait panels 480x800 and 480x640. On a portrait launcher, an app without `X-Portrait`
> runs turned to landscape, full screen, without the writing zone. 480x320 and 320x480 stay best effort. No existing app
> is ported unless the owner asks for it.

## 8. Phases (re-cut) and cost

| Phase | Content | Cost | Shared files |
| --- | --- | --- | --- |
| **P0** proof of concept | section 9 | owner 1 day; optional 0.25 throwaway | none |
| **S1** Strokes core | C99 library (capture clean-up, resample, gates, matcher, modes), text table, synthetic generator, capture tool, eval command, unit tests. New files only (`tools/strokes/` first; moved into `ext_components/cp0_lvgl` in S3, after 029 lands) | **2.0** | none |
| **S2** portrait launcher, no zone | a portrait variant of `pi3a-wsdsi43` in `install.sh`; narrow status strip; switcher rule and label wrap; landscape apps turned (b) with `X-MinSize` checked against the landscape canvas; `X-Portrait` parsed; harness scenes and goldens at 480x640 and 480x800. A usable milestone on its own (text by the fallback keyboard) | **1.25** | layout service, multitask model, `launch.cpp`, `app_launch_env`, `desktop_entry` |
| **S3** zone | third display in `cp0_lvgl_dpi_scaled.c` (app area plus band, touch routing, a second raw-frame consumer), metrics `zone`, zone widget (boxes, ink, mode line, soft buttons), in-process injection, `applaunch-vkbd` for compat apps, `vkb=zone`, Settings > Strokes on/off, harness support | **2.5** | display backend, `input_presence.cpp`, `launcher_osk` |
| **S4** contract and toolkit | `APPLAUNCH_ZONE`, `screen.state` `orient`/`zone`, `APP-CONTRACT.md`, `cp0_app_contract.[ch]`, probe; apps `_common` (portrait minimum in `AppSpec`, harness sizes with a zone), APP-GUIDELINES exception | **1.0** launcher + **0.5** apps | `APP-CONTRACT.md`, `cp0_app_contract.h` |
| **S5** apps | Memo 0.5, To-Do 1.5, Address 1.75, Date Book 3.0; existing apps (a) only on request (section 3.2) | **6.75** | apps repo only |
| **S6** training and tuning | training screen, user overlay, punctuation and extended set, thresholds from real strokes | **1.5** | small |

Launcher total: S2 to S4 plus S6 = 6.25 sessions, plus S1 = 8.25. Order: S1 can start now in parallel; S2 after c-7
merges; S3 after S1 and S2; S4 with S3; S5 after S4 (Memo after Markdown Notes N1).

## 9. One-day proof of concept (Pi 3A+ DSI)

1. **Portrait by configuration, no code (about 1 h).** In `/etc/applaunch/board.conf` set `APPLAUNCH_ROTATE=90` (or
   270, for the side the ribbon cable should be on) and `APPLAUNCH_LOGICAL=480x800`. Remove `APPLAUNCH_COMPAT_SCALE=2`
   (it would be lowered to 1 with a log line). Keep `APPLAUNCH_PANEL_MM=94x56`; it is turned automatically. Restart the
   service. A later `install.sh` keeps a hand-edited profile and replaces it only with `--force`, keeping
   `board.conf.bak` (S: `install.sh:26-27`, `:663-665`).
   - **Shows:** that the 16 bpp rot-90 path and the ft5x06 `buffer` touch are right on this panel, the repaint speed
     (scroll Settings, open the keyboard), Home 3x5, Settings, the keyboard and the terminal with a stylus, how readable
     the 1x compat window is, the cut titles, and that fullscreen apps vanish (expected).
2. **Stylus capture (about 1 h).** With the launcher stopped: `sudo apt install evtest`, then
   `evtest /dev/input/eventN > strokes.txt`. Write each of 10 letters 10 times with the owner's capacitive stylus.
   - **Shows:** the sample rate, the jitter, the drop-outs and how the stroke ends look. It is the first real corpus
     for S1.
3. **Optional, 0.25 session, a throwaway branch never merged:** `APPLAUNCH_ZONE_H=240` shrinks the native display and
   paints the band grey, with raw touch points echoed as dots. **Shows:** the app area at 480x560 and the feel and
   latency of ink in the band. Without it, the harness shots of 480x560 already show the app area (M).

## 10. Merge hot spots

| Area | Files | Other work | Advice |
| --- | --- | --- | --- |
| Contract text | `docs/APP-CONTRACT.md` | c-7 (uncommitted: sections 5, 13, 14; reserves `KEY_F14`, the FZ-M1's rotation-lock key, S: working copy `:551`) | S4 after c-7 merges. A later rotation toggle could use `KEY_F14` |
| Keyboard path | `keyboard_input.h`, `cp0_keyboard_presence.[ch]`, `cp0_lvgl_keyboard.c`, `cp0_keyboard_sources.h`, `cp0_external_app_runner.cpp`, `input_presence.cpp` | c-7 | S3 touches only `input_presence.cpp` (`vkb=zone`) and uses `cp0_keyboard_inject_key` (not touched by c-7): rebase after c-7 |
| Multitasking | `multitask_ui.cpp`, `supervisor.*`, `switch_keys.*` | c-7 | S2 changes `switcher_model.cpp` and `switcher_view.cpp` (not in c-7) and one log line in `multitask_ui.cpp` |
| Layout and display | `cp0_ui_metrics.[ch]`, `cp0_lvgl_dpi_scaled.c`, `cp0_fb_output.*` | no open branch | SDK files that apps copy; the goldens at 640x480 and 480x320 must stay identical (pinned presets) |
| Launch and entries | `launch.cpp`, `app_launch_env.*`, `desktop_entry.*`, `desktop_app_loader.cpp` | none | - |
| Build lists, components | `SConstruct`, `Kconfig`, removed components | 029 upstream decoupling (draft PR) | S1 stays in `tools/strokes/` until 029 merges |
| Apps repo | `_common` (AppSpec, harness), `markdown-notes` (untracked, N1) | pin PRs and the notes work | S4-apps after the pin moves; Memo after N1 |

## 11. Risks

- **Stylus quality** (capacitive drop-outs, a wide tip) caps the accuracy. It is measured in P0 before thresholds are
  frozen.
- **Rotation cost** of full repaints at 16 bpp on the Pi 3A+ (I). The HyperPixel needs no rotation.
- **Per-app pinned zone code** may behave differently between apps. Mitigated by the shared run-time table and a core
  version line in `--version`.
- **Turned apps** ask the user to turn the device; the switcher's backdrop is sideways meanwhile.
- **The fallback-keyboard policy** ("never summoned by hand", S: `APP-CONTRACT.md:340-341`) conflicts with a Kbd soft
  button (owner question 4).
- **Small compat text** at 1x on a portrait panel (37 x 20 mm window on the DSI; I).
- **Accuracy below Palm's** while the table is young. Rejecting instead of guessing keeps errors visible.
- **vCard and iCalendar edge cases** (Outlook TZIDs, vCard 2.1) are scoped as read-only.

## 12. Owner questions (default in bold)

1. **Panels:** **the 480x800 4.3" DSI is the reference; 480x640 only on 3.5-4" panels (7.9-9 px/mm); the deck's
   2.8" turned is not a PDA target.** Profile name **"PDA"**, input method **"Strokes"**?
2. **Zone architecture:** **in the display backend, drawn by the launcher or by a portrait-capable app in its own
   process (option A)**; no launcher-owned zone over foreign apps in v1?
3. **Landscape apps on the PDA:** **shown and run turned, full screen, no zone (b); "letterbox" dropped**; none ported
   to portrait unless you ask (candidates in section 3.2)?
4. **Fallback keyboard:** **while the zone is shown it is the text input (`vkb=zone`: no automatic keyboard), and the
   Kbd soft button shows the keyboard on demand** (an exception to "never summoned by hand"); or no Kbd button?
5. **Soft buttons:** **Home, Menu (Ctrl+O), Apps (switcher), Kbd**; Find dropped (there is no global search); in a
   compat app the row shows the toolbar keys?
6. **Declaration:** **`X-Portrait=WxH` plus the wording of section 7**; or `X-Orientation=portrait|landscape|any`?
7. **Multitasking:** **on when the short side is at least 480** (portrait 480x640 and 480x800 included)?
8. **PIM:** **Memo (= Markdown Notes portrait) and To-Do first, then Address, then Date Book; vdir folders, sync by
   Syncthing or vdirsyncer, documented only**?

## 13. Sources

- **S (launcher v0.8.6 aef009b):**
  - `ext_components/cp0_lvgl/src/cp0/{cp0_lvgl_dpi_scaled.c, cp0_fb_output.[ch], cp0_keyboard_presence.c,
    cp0_lvgl_keyboard.c}`;
  - `src/{cp0_ui_metrics.c, cp0_ui_osk.c, cp0_keyboard_inject_key.c}`;
  - `include/{cp0_ui_metrics.h, cp0_ui_osk.h, cp0_touch_reader.h, cp0_display.h, keyboard_input.h,
    cp0_config_json.h}`;
  - `projects/APPLaunch/main/ui/{launch.cpp, native_ui.cpp, native_vkbd.cpp, app_launch_env.*, desktop_entry.*,
    desktop_app_loader.cpp, multitask/switcher_model.*, model/screensaver_clock_layout.hpp}`;
  - `pizero2w/{install.sh, 91-applaunch-vkbd.rules}`, `tools/boards/boards.json`, `docs/APP-CONTRACT.md` (main and c-7
    working copy), `docs/dev/decisions.md:59-63`, reports 020, 022, 026, 037.
  - Apps repo 1d04432: `_common/{app.hpp, app.cpp, platform.hpp, headless/README.md}`, every `.desktop`,
    `docs/APP-GUIDELINES.md:20-28`, `markdown-notes/src`.
- **M (scratchpad of this session):**
  - render harness of a v0.8.6 copy (`~/pda-study/launcher`) with study scenes `pda_portrait`, `pda_settings`,
    `pda_lists` at 480x640 (11.3 px/mm) and 480x800 (`APPLAUNCH_PANEL_MM=94x56`), and `*_area` at 480x560 (8.54) and
    480x434 (7.89), all with `APPLAUNCH_ROTATE=0`;
  - `metrics_probe.c` linking `cp0_ui_metrics.c`, `cp0_ui_osk.c` and `cp0_app_contract.c` for 640x480, 800x480,
    480x800, 480x640 at three densities and 320x480;
  - the apps' headless harness on a scratch copy (`~/pda-study/apps`, built against the v0.8.6 copy), with System
    Monitor, RSS Reader, Net Toolbox and Mesh Hop at a 480x640 minimum and one portrait scene each.
- **W:**
  - $1 and $P pages, depts.washington.edu/acelab/proj/dollar (New BSD; $P ignores direction);
  - Google Patents US8341558B2 and US9805241B2 (Protractor, Google, "Expired - Fee Related"), US7729542B2 (EdgeWrite,
    CMU), US5596656A (Unistrokes);
  - xstroke (USENIX FREENIX 2003 paper; T2 package: GPL);
  - Zinnia and Tomoe distribution package listings (BSD; LGPLv2+).
  - Not legal advice.
