# 010 - MeshZero full-screen UI (design brief)

Status: decisions taken 2026-10-06 (see "Decisions"), nothing built yet. Owner (when approved): apps-dev. Verifier: deck-verifier.
Docs: docs-writer. Acceptance: the user, on the deck with the board.

## Why
The first MeshZero (task 007, v0.1.0 draft) draws on the 320x170 compat canvas scaled 2x. The user tried it on 2026-10-06
and said the UI does not suit messaging and that MeshCore's usual features are missing and cannot fit that canvas: a
**map view**, a **terminal**, **emojis in conversations**. Decision: replace the UI, keep everything below it.

## What stays and what is new
- **Stays (apps/meshzero/src/main/core, pure C++ with unit tests):** serial transport and reconnect, frame parser,
  protocol, client, model, history store, clock policy, SHA-256 for hashtag channels, the simulator
  `tools/meshcore_sim.py`. Nothing in `core/` knows about LVGL.
- **New:** a full-screen UI (`X-Fullscreen=true`, the app owns all 640x480, see docs/HOSTING-APPS.md) written against
  `core/`. New package id **`mesh-hop`** (title "Mesh Hop"), a draft until the user accepts it. `apps/meshzero` stays as it is until then.
- **Not a web app, not a port of a GUI:** meshcore-gui (Python + NiceGUI + browser) and app.meshcore.nz need a browser and
  far more RAM than the deck has (414 MB); Meshy needs GTK. They are feature references only (see `ref/README.md`).
  Never copy GPL code (wadamesh, Meshy): UX ideas only.

## Decisions (user, 2026-10-06)
1. Map: **tile-less plot first** (design A below); real offline tiles are not planned.
2. Emoji: use an **existing open-licence emoji font** (not a hand-made set), see "Emoji".
3. Terminal: a meshcore-cli style command line plus the remote CLI of a repeater, as described below.
4. Layout: **two panes** as drawn.
5. Phase 4 content: **route view** and **repeater polling** (search, channel QR and auto-reply are not wanted for now).
6. Package id: **`mesh-hop`**, a new package next to `meshzero` while the old UI exists.

## Fixed rules (from the user, do not reopen)
- Text is typed on the **Bluetooth keyboard only**. No on-screen keyboard (see memory "no virtual keyboard"). Touch =
  taps, swipes and buttons. When no keyboard is awake an editor says so (existing behaviour).
- A short Esc is always Back and never quits; hold Esc 3 s quits (launcher). Every action reachable by keys and by touch.
- The shared top bar (clock, Wi-Fi, Bluetooth) is drawn by `cp0_statusbar` and takes the top 56 px at 640 wide.
- Memory budget: the whole app well under ~60 MB RSS (launcher is ~20 MB, deck has 414 MB).
- Clock policy of task 007 stays (deck NTP vs board GPS).

## Screen layout (640x480, about 424 px under the 56 px bar and a 24 px footer)

```
+--------------------------------------------------------------------------+
| MeshZero   [Chats] [Map] [Contacts] [Terminal] [Settings]   (top bar)    |
+---------------------+----------------------------------------------------+
| CHANNELS            |  #public                              3 nodes heard |
|  # public        2  |----------------------------------------------------|
|  # hamradio         |  Ana  14:02  Anyone on 869.525?                    |
|  # test             |  You  14:03  yes, loud and clear  :)  ✓✓           |
| DIRECT              |  Bob  14:05  coffee? ☕                             |
|  Ana             1  |                                                    |
|  Repeater-North     |----------------------------------------------------|
|  Bob                |  [ Message ... (keyboard) ]            [Send]      |
+---------------------+----------------------------------------------------+
| Enter: write   Tab: next tab   Esc: back     board: XIAO nRF52840  ● OK   |
+--------------------------------------------------------------------------+
```

- Two panes: list (about 200 px) and conversation. On a narrow action (Esc / swipe from the left edge) the list gets the
  focus; touch: tap a row, swipe the conversation to scroll.
- **Map tab:** contacts with a position as markers on a plot, own position if known, names, last heard; tap a marker =
  detail panel (same as Contacts detail) with Message / Path / Ping. Pinch is not available (no multitouch assumed):
  zoom with + / - buttons and keys, pan by dragging.
- **Contacts tab:** sortable table (name, type, last heard, SNR, hops, distance if both positions are known); Enter
  opens the chat or the repeater/sensor detail.
- **Terminal tab:** a monospace console, see "Terminal" below.
- **Settings tab:** radio parameters, node name, clock, channel manager (add hashtag channel, remove, backup/restore of
  the channel table), display options (emoji on/off, font size).

## Features, by phase (ranked by what a MeshCore user expects)
Sources: meshcore-gui (MIT), MeshCore Open (MIT), the official apps, wadamesh (ideas only).

| Phase | Content | Risk |
| --- | --- | --- |
| 1 | Two-pane Chats (channels + direct) on 640x480, unread, delivery status, history, Contacts table, Status in the footer/Settings, Settings parity with 0.1.0, key + touch navigation. All existing `core/` features. | low |
| 2 | **Terminal** and **emoji** (below). | medium |
| 3 | **Map** (below). | the heaviest; see open question 1 |
| 4 | **Route view** (the path a message took, hop by hop, with signal where known; on the map when positions exist) and **repeater polling** (login to chosen repeaters, poll battery/uptime on an interval, keep a log). Later candidates, not wanted now: search, channel QR, auto-reply, export. | each small |

### Terminal
The companion radio firmware has no shell of its own: the console users know is (a) the **meshcore-cli style command
line** (local commands: contacts, send, set radio..., advert, clock) executed by the app against `core/`, and (b) the
**remote CLI of a repeater/room server** after a login, sent as text to that node. Plan: one console, history, Tab
completion of command and contact names, scrollback by swipe, command output in a monospace font; the `ref/meshcore_cli.py`
command set is the model. To check in the references before building: exactly which commands the companion protocol
exposes and how remote CLI replies are returned.

### Emoji in conversations
Use an existing open-licence font, no custom set. First step is a **spike** (before phase 2) to pick the route:
- **Noto Color Emoji** (OFL, colour bitmaps, about 10 MB, Debian package `fonts-noto-color-emoji`): full set and the
  real look, if LVGL's FreeType layer can draw colour bitmap glyphs (to test; if not, decode the glyphs ourselves).
- **Noto Emoji** (OFL, monochrome outline, small): always renders through FreeType, tinted like text; the fallback.
Either way load glyphs on demand through a bounded glyph cache (RAM check in the spike), keep the font file in the
package or as an apt dependency (licence in `copyright`). Entering emoji on the keyboard: `:shortcode:` converts as
you type, and a tap-to-pick strip of recent emoji (touch buttons, no keyboard). Sequences (skin tones, flags, ZWJ)
are the part to look at in the spike; unsupported ones show a placeholder box.

### Map
The deck is often offline and has little RAM, so there are two designs:
- **A. Plot without tiles (cheap):** dark background, coordinate grid and scale, optional coastline/country outline
  (the viz1090 `world-map.bin` already exists in the apps repo: reuse its data format and licence check), markers and
  links to the repeaters heard. Works fully offline, tiny.
- **B. Offline raster tiles (the wadamesh experience):** the user downloads an area from a PC or from the deck when
  online, tiles are stored under `~/.local/share/meshzero/tiles/{z}/{x}/{y}.png` and drawn by the app. Needs a tile
  downloader respecting the tile server's usage policy (OpenStreetMap's policy forbids bulk downloading from its own
  servers: choose a source that allows it), a size cap, PNG decode memory care, and attribution on screen.
Recommendation: build A in phase 3, keep the drawing layer so B can be added.

## Technical plan (for apps-dev, after approval)
- UI layer as a new `ui/` set of files over `core/`, LVGL on a 640x480 framebuffer, full-screen app the same way as
  viz1090 (`X-Fullscreen=true`, the app owns input and the panel; draw the shared top bar with `cp0_statusbar`; read how
  viz1090's bridge handles touch, keyboard and the Esc hold). Ask the controller for a launcher capability instead of
  touching launcher source.
- Touch gestures inside the app: tap, drag-scroll, swipe from the left edge = Back. No hidden gesture is the only way to
  do something.
- Keep `core/` unit tests green and add tests for new logic (terminal command parser, emoji shortcode conversion, map
  projection and clustering). Run against the simulator first; extend the simulator with positions for contacts so the
  map is testable.
- Fonts and map data are separate files in the package; list licences in `copyright`.

## Definition of done (per phase)
`checklists/app-packaging.md`; unit tests pass; simulator run on the deck by the verifier (PASS report), screenshots of
each screen; the user tests with the real board using a protocol written by the controller (see
`tests/pending-physical-tests.md`); docs-writer updates the app README.

## Open points (not blocking phase 1)
- Emoji spike result (colour or monochrome) and the glyph cache size.
- Which terminal commands the companion protocol exposes, and how remote repeater CLI replies come back.
- Package metadata for `mesh-hop`: icon, description, copyright (MeshCore MIT, meshcore_py MIT as reference).
