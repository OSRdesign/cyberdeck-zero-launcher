# Multitasking P-c - owner protocol (touch: three-finger hold, status strip long-press)

Phase P-c of the multitasking study ([`reports/026-multitasking-study.md`](../reports/026-multitasking-study.md);
design as built: [`tasks/015-multitasking-pc.md`](../tasks/015-multitasking-pc.md)) adds the touch way to switch:
**G1, three fingers held still for 1 s anywhere**, and **G2, a long-press (0.5 s) on the status strip** (the clock,
Wi-Fi and Bluetooth at the top right). It builds on P-b (Alt+Tab, hide and resume:
[`multitasking-pb-protocol.md`](multitasking-pb-protocol.md)).

**There is no switcher screen yet (P-d).** For now:
- **G1 inside an app = Home**: an app that can stay in the background (the MT test apps) is hidden and the home grid
  shows; tap its tile to resume it. Today's real apps (viz1090, System Monitor, Serial Console) cannot stay in the
  background yet (P-e): they **stay in front**, and the launcher only logs the gesture. That is expected.
- **G1 or G2 on the home grid = nothing visible**: only a log line. The fingers must not open or move anything.
- What we measure: **does G1 fire when you mean it** (3 fingers), **never when you do not** (2-finger pinch, typing
  with two thumbs, a palm, a grip), and **do the fingers click nothing**.

**Nothing changes unless the launcher runs with `APPLAUNCH_MULTITASK=1`** (the builds this protocol was written for),
except one thing that is always on: the launcher now follows **only the first finger** of a touch (part A checks
that one finger behaves exactly as before). **Since task 015 "default on" (2026-10-21)** a launcher of that change or later has multitasking **on by default**
on canvases of 640x480 and more (the deck): "flag off" below means `--env APPLAUNCH_MULTITASK=0`, and
`--env APPLAUNCH_MULTITASK=1` is then the same as no `--env` (it still forces it on, also under 640x480).

No keyboard is needed for this protocol (the deck's Bluetooth keyboard may stay asleep): everything is touch, and
the log is read in an ssh terminal on the PC.

| Part | Board | Time |
|---|---|---|
| 0. Setup | deck | 5 min (Controller) |
| A. Flag off: touch as before | deck | 5 min |
| B. G1 on the home grid | deck | 10 min |
| C. G1 in apps (MT test apps, viz1090, System Monitor, Serial Console) | deck | 30 min |
| D. G2 on the status strip | deck | 5 min |
| E. Scripted gestures (uinput, optional) | deck | 10 min (Controller) |
| F. Pi 3A+ DSI (800x480, FT5x06) | pi3a-dsi | 25 min |
| G. Answer sheet | - | 5 min |

Rules:
- Work in an ssh terminal on the PC next to the board (`ssh osrde@zero7.local`); a second one helps for the logs.
- **Counting with the log.** Define these once in the ssh terminal (they only read the log):

      L=~/applaunch-test/run.log
      c() { grep -c "\[mt\] $1" "$L"; }      # c "G1 fired"  -> how many so far
      t() { grep -E '\[mt\] (G1|G2|switcher|touch:|notice:)' "$L" | tail -n "${1:-6}"; }   # the last lines

  Before each block of attempts write down `c "G1 fired"` and `c "G1 cancelled"`; after it, the new values: the
  differences are the block's results. `t 20` shows the reasons.
- **One attempt** = put the fingers down, hold (or not) as the step says, lift **all** fingers, wait about 1 s.
- If something is stuck: `echo close-all > /run/user/$(id -u)/applaunch/multitask.ctl`, then
  `python tools/boards/board.py test-stop deck` (the installed launcher starts again); last resort `sudo reboot`.

The lines the launcher writes (prefix `[mt] `):

| What | Line |
|---|---|
| G1 recognised | `G1 fired (3 contacts, held 1003 ms)` (the number is 1000 or a little more) |
| ... then, in an MT test app | `switcher (G1): P-c stand-in = Home (as Super) for MT Freeze`, then P-b's `MT Freeze: sent "hide 1"`, `ack`, ... |
| ... then, in a real app (legacy) | `switcher (G1): P-c stand-in = Home (as Super) for viz1090`, then `notice: viz1090 cannot stay in the background` |
| ... then, on the grid | `switcher (G1): the launcher is in front, nothing to do (P-c stand-in; P-d opens the switcher)` |
| a 3-finger attempt that did not fire | `G1 cancelled: <reason>, 3 contacts`; the reasons: `the third finger came 312 ms after the first (limit 250 ms)`, `no third finger within 250 ms of the first`, `two fingers 6.1 mm apart (min 8.0 mm)`, `a finger 0.9 mm from the edge (min 2.0 mm)`, `a finger lifted after 640 ms (hold 1000 ms)`, `a finger lifted before the three were down`, `a finger moved 5.3 mm (limit 4.0 mm)`, `touch events were lost (the reader fell behind)` |
| 4 or more contacts | `G1 cancelled: 4 contacts in one touch (a palm or a grip?)` |
| 1 or 2 fingers (taps, drags, pinches, typing) | **nothing** |
| G2 | `G2 fired (status strip held 503 ms)`, then `switcher (G2): the launcher is in front, nothing to do ...` |
| an app comes to the front / the launcher comes back | `touch: reading the touch screen passively (no grab) for G1 while MT Freeze is in front` / `touch: reader closed (the launcher is in front)` |

## 0. Setup (Controller, once per board)

Files (scratchpad `pkg-out\launcher-mtc\`, see its `INSTALL.txt`): the bundle
`cyberdeckzero-launcher-0.8.0-pc.1-arm64.tar.gz`, `mt-fbhapp` and the five `mt-fbhapp-*.desktop` (the P-b test apps,
unchanged), `mtplay` (part E). Nothing is installed over the released launcher: the bundle runs staged.

1. Upload and stage: `python tools/boards/board.py upload deck <path>/cyberdeckzero-launcher-0.8.0-pc.1-arm64.tar.gz`.
2. The MT test apps, if not installed for P-b (on the board, after copying the files to `~`):

       sudo install -m 755 ~/mt-fbhapp /usr/share/APPLaunch/bin/mt-fbhapp
       sudo install -m 644 ~/mt-fbhapp-*.desktop /usr/share/APPLaunch/applications/

3. `python tools/boards/board.py test-start deck --env APPLAUNCH_MULTITASK=1`, then check:

       grep '\[mt\] touch gestures' ~/applaunch-test/run.log
       grep touch /run/user/$(id -u)/applaunch/screen.state

   Expected: `[mt] touch gestures (P-c): /dev/input/event0, 5 slots; G1 three-finger hold 1000 ms (land 250 ms,
   still 4.0 mm, sep 8.0 mm, edge 2.0 mm, size off; 45 / 90 / 23 px at 11.30 px/mm); G2 status strip 500 ms`, and
   `touch=/dev/input/event0` then `touch_slots=5`. Also `grep display-profile ~/applaunch-test/run.log` ends its
   `touch=` part with `, 5 slots`.
4. At the very end: `python tools/boards/board.py test-stop deck` (and remove the test apps as in P-b part 0.4 if
   they are not needed any more).

## A. Flag off: touch as before (deck, 5 minutes)

`python tools/boards/board.py test-start deck` (no `--env`; since "default on": `--env APPLAUNCH_MULTITASK=0`). `grep -c '\[mt\]' ~/applaunch-test/run.log` must print
`0`, and `screen.state` must have **no** `touch_slots` line.
1. One finger: tap tiles on the grid (each opens; Esc / hold Esc back), scroll a Settings list by dragging, type a few
   letters on the on-screen keyboard of a native page (Settings > Wi-Fi password, or the SSH page), long-press a
   toolbar key in a compat page (LAN Scan). Everything as with the released launcher.
2. **Two fingers on the grid** (one on a tile, then a second elsewhere, lift the second, then the first): the tile
   under the **first** finger is the one that opens (before P-c the highlight jumped to the second finger and lifting
   the second "released" the first: whichever tile was under the last position opened). Note what happens.
3. Close an app with Esc while two fingers rest on the screen, lift them one by one: nothing on the grid is clicked.

## B. G1 on the home grid (deck, 10 minutes)

`test-start deck --env APPLAUNCH_MULTITASK=1`. Stay on the home grid; start each attempt with the fingers on tiles
(a tile under each finger is the hard case: nothing may open).
1. **3 fingers, normal, 20 attempts**: index, middle and ring fingertips down together, about 1.5 cm apart, held still
   for about 1.5 s, lifted. Expected per attempt: `G1 fired (3 contacts, held 10xx ms)` and `switcher (G1): the
   launcher is in front, nothing to do`; **no tile opens, the grid does not scroll or change its highlight**. Count:
   `c "G1 fired"` grows by the number of good attempts.
2. **3 fingers, deliberately sloppy, 20 attempts**, a mix: lift after half a second; put the third finger down late;
   two fingers touching each other; slide a finger during the hold; one finger on the very edge of the glass.
   Expected: no fire; `G1 cancelled: ...` with the matching reason, one line per attempt. A sloppy attempt may still
   click the tile under the first finger when you lift (only a fired G1 is swallowed): note how often.
3. **Palm and grip, 20 attempts**: the flat palm on the screen; holding the deck as when carrying it, fingers over
   the glass. Expected: never `G1 fired`; `G1 cancelled: 4 contacts in one touch` for most palms, nothing for a
   two-finger grip.

## C. G1 in apps (deck, 30 minutes)

For each app below: open it from its tile, wait until it is drawn, then do the attempts. Before each block note
`c "G1 fired"` and `c "G1 cancelled"`.

1. **MT Freeze**, **MT Run**, **MT NoEsc** (the P-b test apps), **3 fingers normal, 20 attempts each**. Expected per
   attempt: `touch: reading the touch screen passively (no grab) for G1 while MT Freeze is in front` (when it opened
   or was resumed), `G1 fired (...)`, `switcher (G1): P-c stand-in = Home (as Super) for MT Freeze`, the P-b hide
   lines, `touch: reader closed`; the **home grid** shows and **nothing on it is clicked by the lifting fingers**.
   Tap the app's tile to resume it before the next attempt (its picture comes back at once, as in P-b part B). MT
   Freeze's counters: `touch` grew by the touches of the gesture (the app saw them before it was hidden), `stale` 0.
   MT NoEsc (no `X-Background`): it **stays in front**, `notice: MT NoEsc cannot stay in the background`.
2. **3 fingers deliberately sloppy, 20 attempts** in MT Freeze (as B2). Expected: `G1 cancelled: ...`, the app stays
   in front.
3. **viz1090** (tap Start on its intro screen). **3 fingers normal, 20 attempts**: expected `G1 fired`, then
   `notice: viz1090 cannot stay in the background`; viz1090 stays (it is not a lifecycle app until P-e). viz1090 may
   zoom a little (it sees two of the three fingers as a pinch: fixed in P-e). **2-finger pinch, 20 attempts** (zoom in
   and out normally): expected **no G1 line at all**. While pinching, in the second terminal:
   `top -b -d 2 -n 2 | grep cyberdeckzero` (second sample): the launcher's CPU while a finger moves (it reads the
   touch screen too now).
4. **System Monitor**: **3 fingers normal, 20 attempts**: `G1 fired`, `notice: System Monitor cannot stay in the
   background`, the app keeps running.
5. **Serial Console** with its on-screen keyboard (open a port or the page that shows it): **two-thumb typing, 20
   bursts** of quick typing with both thumbs (a few words each). Expected: **no G1 line** (`c "G1 fired"` and
   `c "G1 cancelled"` unchanged). Then **3 fingers normal, 20 attempts**: `G1 fired`, `notice: Serial Console cannot
   stay in the background`.
6. **Palm and grip in an app** (MT Run), 20 attempts: never `G1 fired`.

## D. G2: long-press on the status strip (deck, 5 minutes)

On the home grid:
1. **Hold one finger on the clock / Wi-Fi / Bluetooth area for about a second, 10 times.** Expected: `G2 fired
   (status strip held 5xx ms)` and `switcher (G2): the launcher is in front, nothing to do`; nothing else happens
   (nothing visible: there is no switcher yet).
2. **Short taps on the strip, 10 times**: no `G2` line, and the taps do what they did before (nothing).
3. **Hold the finger on the strip and slide it away** (more than 3 mm) before 0.5 s: no `G2`.
4. Open Settings (a native page with the strip in its header): hold the strip: `G2 fired` as on the grid.

## E. Scripted gestures with a uinput panel (deck, optional, Controller)

`mtplay` creates a virtual touch screen with the Goodix's ranges and plays exact patterns, so each rule is tested
with known geometry. While it runs, the launcher reads only that panel (the real touches do not reach the launcher).
1. Terminal 1 (keep it open; it owns the virtual panel):

       sudo ~/mtplay --like /dev/input/event0

   It prints `mtplay: /dev/input/eventN "mtplay touch panel" raw x 0..639 y 0..479; panel 56.6 x 42.5 mm, swap 1, ...`
   (the geometry from `/etc/applaunch/board.conf`).
2. Terminal 2: `python tools/boards/board.py test-start deck --env APPLAUNCH_MULTITASK=1 --env
   APPLAUNCH_TOUCH_DEV=/dev/input/eventN`. Check: `grep '\[mt\] touch gestures' ~/applaunch-test/run.log` names
   `/dev/input/eventN, 5 slots`.
3. In terminal 1 type, one per line, and compare with terminal 2 (`t 4` after each). Positions are millimetres from
   the top-left corner of the screen. On the grid:

   | Command | Expected line(s) |
   |---|---|
   | `g1` | `G1 fired (3 contacts, held 10xx ms)`, `switcher (G1): the launcher is in front, nothing to do` |
   | `g1short` | `G1 cancelled: a finger lifted after 6xx ms (hold 1000 ms), 3 contacts` |
   | `g1close` | `G1 cancelled: two fingers 5.8 mm apart (min 8.0 mm), 3 contacts` |
   | `g1slow` | `G1 cancelled: the third finger came 400 ms after the first (limit 250 ms), 3 contacts` |
   | `g1move` | `G1 cancelled: a finger moved 6.2 mm (limit 4.0 mm), 3 contacts` |
   | `g1edge` | `G1 cancelled: a finger 0.9 mm from the edge (min 2.0 mm), 3 contacts` |
   | `four` | `G1 cancelled: 4 contacts in one touch (a palm or a grip?)` |
   | `pinch` | nothing |
   | `strip` | `G2 fired (status strip held 8xx ms)` (held 800 ms), `switcher (G2): ... nothing to do` |
   | `strip 300` | nothing (too short) |

   The millimetre values may differ by a few tenths on the board (rounding to pixels).
4. With an app in front (only if an MT test app's tile is on the first screen of the grid): `tap X Y` on its tile
   opens it. Tile centres, row / column: deck `10 15`, `28 15`, `47 15` (first row), `10 33`, `28 33`, `47 33`
   (second); Pi 3A+ `12 19`, `35 19`, `58 19`, `81 19`, `12 43`, `35 43`, `58 43`, `81 43`. Then `g1`: expected
   `touch: reading the touch screen passively (no grab) for G1 while MT Run is in front` (when it opened),
   `G1 fired (...)`, `switcher (G1): P-c stand-in = Home (as Super) for MT Run`, MT Run hidden; and the patterns of
   step 3 give the same cancel lines while it is in front.
5. `quit` in terminal 1 (the virtual panel goes away), then
   `python tools/boards/board.py test-start deck --env APPLAUNCH_MULTITASK=1` again (the real touch screen).

## F. Pi 3A+ with the DSI panel (800x480, FT5x06, 10 slots)

Setup as in 0 with `pi3a-dsi` (clean its `/tmp` first, see `tools/boards/README.md`). Expected at start:
`touch gestures (P-c): /dev/input/event0, 10 slots; G1 ... (34 / 68 / 17 px at 8.54 px/mm)`, `touch_slots=10`. Then:
B1 and B3 (10 attempts each), C1 with MT Freeze only (20), C3 (viz1090, pinch 20 and 3 fingers 20), D1-D2, and
optionally E with `sudo ~/mtplay --like /dev/input/event0` (the board.conf of the 3A+ gives 94 x 56 mm, swap 0). The
FT5x06 reports no contact size: palms are told apart by their number of contacts only.

## G. Answer sheet

Deck and Pi 3A+: one row per block; "fired / cancelled" from the `c` differences.

| # | Where | Pattern | Attempts | G1 fired | G1 cancelled (main reasons) | Anything clicked or opened? Notes |
|---|---|---|---|---|---|---|
| A2 | grid, flag off | 2 fingers | 5 | - | - | which tile opened? |
| B1 | grid | 3 fingers normal | 20 | | | |
| B2 | grid | 3 fingers sloppy | 20 | (expected 0) | | clicks under the first finger? |
| B3 | grid | palm / grip | 20 | (expected 0) | | |
| C1 | MT Freeze | 3 fingers normal | 20 | | | grid clicked after the hide? |
| C1 | MT Run | 3 fingers normal | 20 | | | |
| C1 | MT NoEsc | 3 fingers normal | 20 | | | stays in front? |
| C2 | MT Freeze | 3 fingers sloppy | 20 | (expected 0) | | |
| C3 | viz1090 | 3 fingers normal | 20 | | | zoom jump? |
| C3 | viz1090 | 2-finger pinch | 20 | (expected 0) | (expected 0) | launcher CPU while pinching |
| C4 | System Monitor | 3 fingers normal | 20 | | | |
| C5 | Serial Console | two-thumb OSK typing | 20 bursts | (expected 0) | (expected 0) | |
| C5 | Serial Console | 3 fingers normal | 20 | | | |
| C6 | MT Run | palm / grip | 20 | (expected 0) | | |
| D1 | grid / Settings | strip hold 1 s | 10 + 1 | G2: | | |
| D2-3 | grid | strip taps / slide away | 10 + 3 | G2: (expected 0) | | |
| E3 | mtplay | the table of E3 | 10 | | | any line different? |
| E4 | mtplay | `g1` with MT Run in front | 1-3 | | | |
| F | Pi 3A+ | B1 / B3 / C1 / C3 / D | | | | |
| - | | Did 3 fingers for 1 s feel natural? Too long, too short? | | | | |
