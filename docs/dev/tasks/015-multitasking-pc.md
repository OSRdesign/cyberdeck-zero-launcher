# 015 - Multitasking P-c: touch (slot tracker, three-finger hold G1, status strip long-press G2)

Status: built 2026-10-20 on branch `multitasking-pc`, on `main` 4086c1a (P-b squash-merged from PR #63, build
0.8.0-pb.4); verified on the PC (section 11); board tests pending. Test build 0.8.0-pc.1.
Study: [`reports/026-multitasking-study.md`](../reports/026-multitasking-study.md) (sections 1.3, 2.1, 5.1-5.3, 7.1,
7.3, 10; owner decisions D2, D3, D12, D13 of 2026-10-14). P-b, the launcher core it builds on:
[`015-multitasking-pb.md`](015-multitasking-pb.md). Board protocol of this phase:
[`tests/multitasking-pc-protocol.md`](../tests/multitasking-pc-protocol.md). App side: `docs/APP-CONTRACT.md`
sections 4 and 13.

## 1. Scope

Touch is the owner's primary way to switch apps; Alt+Tab (P-b) is the keyboard fallback.

| In P-c | Not in P-c |
|---|---|
| a slot-aware multi-touch tracker under the launcher's LVGL pointer (every slot, tracking ids, sizes) | the switcher screen, its cards, the legacy Close / Keep sheet (P-d) |
| G1, the three-finger hold, as pure C (SDK-copyable), table-tested and replayed over the P-a recordings | G2 inside apps (`StatusStrip` sending `switcher`, the shims: P-e) |
| G1 while the launcher is in front (its own touch reader) and while an app is in front (a passive reader) | apps ignoring 3+ contacts (viz1090's pinch with 3 fingers: P-e) |
| G2, the long-press on the launcher's status strip | |
| swallow after fire: the fingers of a gesture click nothing | |
| `touch_slots` in `screen.state` | |
| dev entry: FIFO `g1` / `g2`, status `evdev_touch`, host process tests with a FIFO touch screen, `mtplay` | |

**What a finger-only user gets in P-c.** There is no switcher to pick from yet, so the gestures run a stand-in at the
place P-d will draw the switcher (`Supervisor::open_switcher`):
- **G1 or G2 inside an app = Home** (as Super, P-b): a lifecycle app (`X-Background=run|freeze`) goes to the
  background and the home grid shows; tapping its tile resumes it. A legacy app (every real app until P-e) stays in
  front: the log says `cannot stay in the background`, as for Super.
- **G1 or G2 on the home grid (or any launcher page) = nothing** but a log line: there is nothing to choose from.
- G2 exists only on the launcher's own screens in P-c (only they show the strip as a launcher object), so in P-c it
  never does anything visible; it is there, tested and logged, for P-d.

## 2. The flag

The **slot tracker** is always on (it replaces the touch reading of the display manager for every protocol B panel,
flag or not) and is proven not to change single touch (section 3). **Everything else** (G1, G2, the passive reader,
the swallow, `touch_slots`, the FIFO commands) exists only with multitasking on (built behind `APPLAUNCH_MULTITASK=1`; since task 015 "default on",
2026-10-21, on by default on canvases of 640x480 and more, `APPLAUNCH_MULTITASK=0` turns it off): without it no
gesture is recognised, no frame callback is set, no clock is changed on the touch fd and `screen.state` is unchanged.

## 3. The slot tracker (`cp0_touch_slots.{h,c}`, `cp0_touch_reader.{h,c}`)

Study 1.3: the old reader flattened multi-touch (one position from whatever slot reported last, released by any
slot's lift). Now:
- **Protocol B** (`ABS_MT_SLOT` in the device's bits: the deck's Goodix, the Pi 3A+'s FT5x06, a uinput test panel):
  every slot is tracked, with its tracking id, raw position and `ABS_MT_TOUCH_MAJOR` (-1 when the panel has none).
  The table has 10 slots (the FT5x06 has 10, the Goodix 5); a contact in a higher slot is counted (`extra`).
- **The single LVGL pointer is the first contact of a touch**: pressed while that contact is down, at its position.
  Other contacts never press it: a second finger no longer makes it jump, lifting the second no longer releases the
  first. Once the first contact is up the pointer stays released until every finger is up (no press appears under a
  finger that was only resting). Several contacts landing in one frame: the lowest slot. **One finger: exactly as
  before.** The kernel's pointer emulation (`ABS_X/Y`, `BTN_TOUCH`) is ignored on protocol B panels.
- **Other panels** (single touch, protocol A) keep the old reading, event for event.
- `SYN_DROPPED` (the reader fell behind; the launcher's own fd overflows while an app is in front): events up to the
  next `SYN_REPORT` are dropped and the slots are read back with `EVIOCGMTSLOTS` (cleared if that fails, so nothing
  can stay pressed); the next frame says `resync` and the gestures restart.
- Touch swallow 2 (the touch that closed an app or woke the screen) now lasts until every finger is up.
- Nothing is read back when the launcher starts: a finger already down then does not press, as before.

Proof that single touch is unchanged: `tests/test_touch_slots.c` drives random one-finger Goodix-like streams (slot,
tracking id, position, size, the pointer emulation) and plain single-touch streams through the tracker and the old
reader (copied into the test) and compares the pointer after every packet; the flattened mode is compared with the
old reader event for event on random multi-finger streams. And the render harness's fake touch screen now says it is
a 5-slot protocol B panel (`EVIOCGBIT`, `EVIOCGABS(ABS_MT_SLOT)`; the touch reader's object is redirected to the fake
like the display manager's), so every touch scene (taps, drags, the on-screen keyboard) goes through the tracker:
**the 64 goldens stay byte-identical**.

## 4. G1, the three-finger hold (`cp0_gesture_g1.{h,c}`)

Pure C: no LVGL, no I/O, no clock, no globals; it takes frames (contacts in screen pixels, each frame stamped in
microseconds) and the time. Study 5.2 rules, exactly as `tools/multitasking-spike/mta_analyse.py` (the reference the
P-a numbers came from). A session starts when the first contact lands and ends when the last one lifts.

| Constant | Value | Environment override | Deck (11.30 px/mm) | Pi 3A+ DSI (8.54 px/mm) |
|---|---|---|---|---|
| `CP0_G1_CONTACTS` | 3 | - | | |
| `CP0_G1_LAND_MS` (third contact within, of the first) | 250 ms | `APPLAUNCH_MT_G1_LAND_MS` | | |
| `CP0_G1_HOLD_MS` (from the third contact; **D2: 1 s**) | 1000 ms | `APPLAUNCH_MT_G1_HOLD_MS` | | |
| `CP0_G1_STILL_MM_X10` (most a contact may move from where it landed) | 4.0 mm | `APPLAUNCH_MT_G1_STILL_MM` | 45 px | 34 px |
| `CP0_G1_SEP_MM_X10` (least distance between two contacts at t3) | 8.0 mm | `APPLAUNCH_MT_G1_SEP_MM` | 90 px | 68 px |
| `CP0_G1_EDGE_MM_X10` (no contact closer to an edge at t3) | 2.0 mm | `APPLAUNCH_MT_G1_EDGE_MM` | 23 px | 17 px |
| `CP0_G1_MAJOR_MAX` (largest contact size; 0 = off) | 0 (off) | `APPLAUNCH_MT_G1_MAJOR_MAX` | | n/a (no size) |

The environment values (read once at the launcher's start, `cp0_g1_params_apply_env`, malformed ones ignored, ranges
clamped) take mm as `4` or `4.5`. Distances are taken **after** the display manager's transform to screen pixels
(the deck's raw axes are not square: 14.9 raw units per mm across, 8.5 along, P-a) and turned into millimetres with
the screen density the launcher uses (`ppmm`, `screen.state`; `APPLAUNCH_PANEL_MM`).

- ARMED when the third contact lands within `LAND_MS` of the first with exactly three down, every two at least `SEP`
  apart, none within `EDGE` of an edge (and none above `MAJOR_MAX` when it is set).
- Cancelled by a fourth contact (at any point), a lift before the hold is complete, a contact moving more than
  `STILL` from where it landed, `LAND_MS` passing without a third contact, or lost events (`SYN_DROPPED`).
- FIRED at t3 + `HOLD_MS`, fingers still down: by the next frame or by a tick. The tick matters: in the P-a
  recordings the Goodix sent 1 report in a 3.5 s still hold (it reports changes only, about 58 a second while a
  finger moves; the FT5x06 about 15); the readers tick, the launcher at every touch read (each LVGL input period),
  the supervisor at the due time. Never again before every finger is up.
- Reported once per gesture (FIRED) and once per **attempt** that did not fire (a session that had three or more
  contacts down at some point), when its last finger lifts; one- and two-finger touches report nothing (no log
  noise from taps, drags, pinches or two-thumb typing).
- `cp0_g1_describe()` makes the log lines, the same from both readers:
  `G1 fired (3 contacts, held 1003 ms)`, `G1 cancelled: the third finger came 312 ms after the first (limit 250 ms),
  3 contacts`, `... no third finger within 250 ms of the first ...`, `... two fingers 6.1 mm apart (min 8.0 mm) ...`,
  `... a finger 0.9 mm from the edge (min 2.0 mm) ...`, `... a contact of size 210 (max 120: a palm?) ...`,
  `G1 cancelled: 4 contacts in one touch (a palm or a grip?)`, `... a finger lifted after 640 ms (hold 1000 ms) ...`,
  `... a finger lifted before the three were down ...`, `... a finger moved 5.3 mm (limit 4.0 mm) ...`,
  `... touch events were lost (the reader fell behind) ...`.
- `cp0_g1_block(now)`: the hand down when the screen changes hands is not a gesture (ignored until a frame with no
  contact at or after `now`, so older queued frames cannot end it); `cp0_g1_reset()` when nothing is down.

## 5. G2, the status strip long-press (`cp0_gesture_g2.{h,c}`)

Pure C like G1, fed with the same frames. One contact lands inside the strip's area, stays the only contact and
within `STILL`, and is still down `HOLD_MS` after it landed: FIRED (once per touch). A second contact, a lift or more
movement ends it silently. The caller says where the strip is when the finger lands: the launcher asks
`cp0::ui::StatusStrip::active_area()` (new, `cp0_ui_widgets.hpp`): the canvas of a strip on the native display's
active screen, from `status_strip_left()` to its right edge, header height (the deck: x 320..639 for the canvas,
y 0..55). **The strip object is not changed**: it stays a non-clickable canvas, so a tap on it does what it did (it
reaches what lies under it) and the header's layout is untouched. Compat pages (the native display shows the toolbar
there) and app screens have no launcher strip: no G2.

| Constant | Value | Environment | Deck | Pi 3A+ |
|---|---|---|---|---|
| `CP0_G2_HOLD_MS` | 500 ms (D3) | `APPLAUNCH_MT_G2_HOLD_MS` | | |
| `CP0_G2_STILL_MM_X10` | 3.0 mm | `APPLAUNCH_MT_G2_STILL_MM` | 34 px | 26 px |

Log line: `G2 fired (status strip held 503 ms)`.

## 6. Who recognises what, and when

```
launcher in front (home grid, pages; P-b may hold its touch grab with apps hidden: the launcher's own fd reads on)
  display manager touch_update (LVGL thread, each indev read)
    cp0_touch_reader on the launcher's fd -> tracker -> frames (screen px) -> frame callback (multitask_ui)
      G1 + G2 -> Supervisor::launcher_gesture(source, line, fired) -> log; fired: open_switcher()
      returns 1 when a gesture fired -> swallow (section 7)
    then a tick (frame NULL) so a still hold completes
app in front (the launcher's timers are paused; its own fd is not read)
  Supervisor: EvdevTouchPort = a second, passive fd on the same node (open on AppFront, close on LauncherFront;
  O_RDONLY | O_NONBLOCK, never EVIOCGRAB: the app reads the node and owns the touches), polled with the keyboards;
  frames mapped with cp0_display_touch_map (the pointer's own transform) -> G1 -> log; fired: open_switcher()
  the poll timeout includes G1's due time; status file: evdev_touch=1
```

- **Hand-over.** When an app comes to the front the passive reader opens and reads back the contacts down
  (`EVIOCGMTSLOTS`): if any (the tap that launched the app), G1 is blocked until they lift. When the launcher comes
  back, its fd's queue holds everything since (often overflowed: `SYN_DROPPED`, read back): `cp0_display_touch_sync()`
  reads it without frames; a hand still down (the one that just made a G1 in the app) blocks G1 and G2 until it
  lifts. So one hold never fires twice.
- **Grab consistency (P-b).** The launcher grabs only its own fd, only while it is in front with apps hidden; the
  passive reader exists only while an app is in front, when nothing is grabbed (P-b releases the grab before any app
  comes to the front). G1 on the grid works grabbed or not (it reads the grabbing fd).

## 7. Swallow after fire

- **Launcher in front.** When G1 or G2 fires, the frame callback returns 1: touch swallow 2 (the pointer reads
  released until every finger is up) and `lv_indev_wait_release()` on both pointer devices: the press in progress
  ends with `LV_EVENT_PRESS_LOST` instead of a click (a tile is not opened; the toolbar keys, the calculator's Esc and
  the on-screen keyboard send the key release they owe on `PRESS_LOST`), and the LIST / SWIPE key gestures of compat
  pages are dropped.
- **App in front.** The app gets `hide` while the fingers are still down (a lifecycle app cancels its press,
  APP-CONTRACT 13); when the launcher comes back, P-b's swallow 2 plus the hand-over block keep the lifting fingers
  from clicking the grid.

## 8. `touch_slots` in `screen.state`

Study 6.4 / D13: `touch_slots=N` (0 = no touch, 1 = single touch or no slots, N = protocol B slots), after `touch=`.
Under the flag only (section 2): `cp0_ui_state_set_touch_slots()` then a republish when multitasking starts
(`gen + 1`); without the flag the file is byte-for-byte as before. Not exported as an `APPLAUNCH_SCREEN_*` variable
(setenv after the threads started is not safe; the file is the contract).

## 9. Dev entry

- FIFO (`$XDG_RUNTIME_DIR/applaunch/multitask.ctl`): `g1`, `g2` = a recognised gesture without the fingers
  (`G1 fired (dev FIFO)`, then the stand-in).
- Status file: `evdev_touch=1` while the passive reader is open.
- Log at start: `touch gestures (P-c): /dev/input/event0, 5 slots; G1 three-finger hold 1000 ms (land 250 ms, still
  4.0 mm, sep 8.0 mm, edge 2.0 mm, size off; 45 / 90 / 23 px at 11.30 px/mm); G2 status strip 500 ms`.
- Log while running: `touch: reading the touch screen passively (no grab) for G1 while <app> is in front`,
  `touch: reader closed (the launcher is in front)`, the G1 / G2 lines of sections 4 and 5, then
  `switcher (G1): P-c stand-in = Home (as Super) for <app>` or
  `switcher (G1): the launcher is in front, nothing to do (P-c stand-in; P-d opens the switcher)`.
- `tools/multitasking-spike/mtplay`: a uinput protocol B panel with the real panel's ranges, scripted patterns in
  millimetres (`g1`, `g1short`, `g1close`, `g1slow`, `g1move`, `g1edge`, `four`, `pinch`, `strip`, `hold`, `tap`) for
  the board protocol's scripted part (`APPLAUNCH_TOUCH_DEV=<its node>`); `--dry-run` writes the patterns as an mtrec
  recording, which the spike's tests replay through G1.

## 10. The seam for P-d

`Supervisor::open_switcher(source)` is the one place every switcher request arrives: G1 (passive reader and
launcher), G2, the FIFO's `g1` / `g2` and the app's `switcher` message (P-b handled that one as `home`; it now comes
here, with the same effect). P-d replaces its body (draw the switcher, with the launcher in front too); the gestures,
the swallow and the hand-over stay as they are.

## 11. As built (2026-10-20)

### Files

| File | What changed |
|---|---|
| `ext_components/cp0_lvgl/include/cp0_touch_slots.h`, `src/cp0_touch_slots.c` | new, pure: the slot tracker, frames, the single pointer, `SYN_DROPPED` resync |
| `ext_components/cp0_lvgl/include/cp0_touch_reader.h`, `src/cp0/cp0_touch_reader.c` | new: the tracker on an evdev fd (capabilities, `EVIOCSCLOCKID`, `EVIOCGMTSLOTS`, batched reads, frame times) |
| `ext_components/cp0_lvgl/include/cp0_gesture_g1.h`, `src/cp0_gesture_g1.c` | new, pure: G1, its parameters and `APPLAUNCH_MT_G1_*`, the log text |
| `ext_components/cp0_lvgl/include/cp0_gesture_g2.h`, `src/cp0_gesture_g2.c` | new, pure: G2 and `APPLAUNCH_MT_G2_*` |
| `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_dpi_scaled.c`, `include/cp0_display.h` | the touch path on the tracker (single pointer = first contact); `cp0_display_set_touch_frame_cb`, `_touch_map`, `_touch_path`, `_touch_slots`, `_touch_sync`; swallow 2 until every finger is up; the gesture swallow (`lv_indev_wait_release`); `, N slots` in the profile line |
| `ext_components/cp0_lvgl/include/cp0_ui_metrics.h`, `src/cp0_ui_metrics.c` | `cp0_ui_state_set_touch_slots`, the optional `touch_slots=` line |
| `ext_components/cp0_lvgl/include/cp0_ui_widgets.hpp`, `src/cp0_ui_status_strip_widget.cpp` | `StatusStrip::active_area()` (a registry of the live strips); the strip itself unchanged |
| `ext_components/cp0_lvgl/tests/test_touch_slots.c`, `test_gesture_g1.c`, `test_g1_replay.c`, `fixtures/mta/*.csv.gz`, `test_ui_metrics.c`, `run_tests.sh` | the tracker (39 checks), G1 + G2 (72 checks: the study 5.3 table in two orientations), the replay of the four P-a recordings, `touch_slots=`; ASan + UBSan |
| `projects/APPLaunch/main/ui/multitask/supervisor.{hpp,cpp}` | `TouchPort`, `TouchConfig`, the passive reader and G1 in the loop, `open_switcher` (the seam), `launcher_gesture`, FIFO `g1` / `g2`, `evdev_touch=` in the status, the app's `switcher` through the seam |
| `projects/APPLaunch/main/ui/multitask/supervisor_linux.cpp` | `EvdevTouchPort` |
| `projects/APPLaunch/main/ui/multitask/multitask_ui.cpp` | the launcher's G1 + G2 (frame callback), the hand-over, the parameters from the environment and the metrics, `touch_slots`, the start log line |
| `projects/APPLaunch/tests/multitask/test_supervisor_host.cpp`, `run_supervisor_host.sh` | a FIFO touch screen; three P-c scenarios (36 checks) |
| `projects/APPLaunch/tests/render/src/harness_fb.c`, `Makefile` | the fake touch screen is a 5-slot protocol B panel; the touch reader's object redirected to it |
| `tools/multitasking-spike/src/mtplay.c`, `build.sh`, `run_tests.sh`, `README.md` | the uinput player, built for host and aarch64; its patterns replayed through G1 |
| `docs/APP-CONTRACT.md` | `touch_slots` (section 4), gestures (section 13) |
| `docs/dev/reports/026-multitasking-study.md` | the P-c row and status (sections 7.1, 12) |
| `docs/dev/tests/multitasking-pc-protocol.md` | the board protocol |

### Tests (PC, WSL)

| Suite | Before (`main` 4086c1a, P-b pb.4) | After |
|---|---|---|
| `cp0-unit` | 106 test programs (108 runs), PASS | 109 programs (114 runs: + `test_touch_slots` 39 checks, `test_gesture_g1` 72 checks, `test_g1_replay` on 4 recordings; `test_ui_metrics` + the `touch_slots` case), PASS |
| `app-unit` | 57 compiled tests (model 112, keys 130, protocol 32, notice queue 29) + 17 Python + host process suite 156 checks, PASS | the same + host process suite **192 checks** (+36: G1 from an app, false positives, legacy app, FIFO `g1` / `g2`, the launcher's gestures), PASS |
| render harness | 41 scenes, 64 goldens identical, 1469 screenshots; twice 1509 PNGs; header 288; contract 363 | **the same, 64 goldens byte-identical**, with the fake panel now a protocol B one (every touch scene through the tracker: the profile line says `5 slots`) |
| `branding` | PASS | PASS |
| launcher build (`tools/dev/wsl-build.sh`, aarch64) | OK | OK, no new warning |
| spike tests (`tools/multitasking-spike/run_tests.sh`, not in check-all) | 20 Python, host e2e | + `mtplay --dry-run` of every pattern through G1: deck and Pi 3A+ geometry, 2 of 2 holds fire, 0 of 9 other patterns |

**Replay of the P-a recordings** (`test_g1_replay`, hold 1000 ms, the defaults above, the recordings' axis swap, the
launcher's density):

| Recording | P-a (`mta_analyse.py`) | P-c G1 (C, screen pixels) | Holds missed, why (both) |
|---|---|---|---|
| deck `mtrec-zero7-20261020-000203` | 9 / 10, 0 / 23 false | 9 / 10, 0 / 23 | 1 hold3close: a finger 0.0 mm from the edge |
| deck `-000730` (corners only) | 0 / 0, 0 / 2 | 0 / 0, 0 / 2 | - |
| deck `-000753` | 8 / 10, 0 / 23 | 8 / 10, 0 / 23 | 2 hold3close: a finger lifted before the three were down; a 4th contact |
| Pi 3A+ `mtrec-PI3A-20261020-002544` | 9 / 10, 0 / 23 | 9 / 10, 0 / 23 | 1 hold3wide: a finger moved 4.3 mm |
| **total** | **deck 17 / 20, Pi 3A+ 9 / 10, no false positive** | **identical** | |

No difference. The C recogniser works in screen pixels after the launcher's transform (truncated to whole pixels)
where the analyser works in millimetres on the raw axes; near a threshold that could differ by a fraction of a
millimetre, and on these recordings it does not. The Pi 3A+ density is 8.54 px/mm (the launcher's, from
`APPLAUNCH_PANEL_MM=94x56`), not the study's 8.51. One fixture note: the analyser's corner check is inconclusive on
the first deck recording (`try --swap 0`: the corners were not touched in order); the numbers use swap 1 as P-a did
and as the deck's `board.conf` sets it.

### Verified on the PC only (needs the boards)

- Real evdev: `EVIOCGMTSLOTS`, `EVIOCSCLOCKID` (it also flushes the launcher's queue once, with a `SYN_DROPPED`,
  handled), a second reader on the Goodix and FT5x06 nodes while an app reads them, the FT5x06's 10 slots, contact
  sizes on the Goodix.
- The launcher integration: the frame callback in the LVGL indev read, `lv_indev_wait_release` on a real press (a
  tile, a toolbar key, the on-screen keyboard), the hand-over after a real app, `StatusStrip::active_area` on the
  device's screens, the start log line, `touch_slots` in the real `screen.state`.
- Real hands: G1 reliability on the 2.8" panel at 1 s with the launcher (P-a measured it without), false positives
  in real apps (viz1090's pinch, two-thumb typing in Serial Console, a palm, a grip).
- `mtplay` itself (WSL has no uinput): only its `--dry-run` output is tested.

### Deviations from the brief and the study

1. **Slots: a table of 10, not 5**: "every slot" of both panels (the FT5x06 has 10; its slot numbers are hardware
   ids). Contacts in higher slots are counted as extra contacts (G1 cancels on them).
2. **The single pointer** is "the first contact of a touch", not literally slot 0; after it lifts, nothing presses
   until every finger is up (a slot-0 rule would press under a resting finger that lands in slot 0 later).
3. **Touch swallow 2 waits for every finger** on multi-touch panels (it was "until the flattened pointer lifts", which
   with two fingers was the first lift). Single touch unchanged. Part of the tracker, so also without the flag.
4. **G1 / G2 from the launcher = nothing; from an app = Home** (the brief's choice for P-c; study: the switcher).
   A legacy app stays in front with a notice, as for Super; the study's Close / Keep sheet (D12) is P-d.
5. **The app's `switcher` message goes through the same seam** (`open_switcher`); same effect as in P-b (Home).
6. **G2 is recognised from the touch frames** against the strip's area (`StatusStrip::active_area`), not by making the
   strip clickable: the strip, its tap behaviour and the header stay untouched.
7. **The swallow uses `lv_indev_wait_release`** (the press ends with `PRESS_LOST`), not `lv_indev_reset`, so keys
   that owe a release (toolbar, on-screen keyboard, the calculator's Esc) get it.
8. **`MAJOR_MAX` is off by default** (P-a's numbers were made without it); settable.
9. **Cancel reports only for attempts with three or more contacts** (taps, drags, pinches, typing log nothing).
10. **`touch_slots` only with the flag** (the brief's flag rule), via a republish at start; not in the environment.
11. **The render harness's fake touch screen became a protocol B panel**, to prove the tracker on every touch scene.
12. **FIFO `g2`** besides `g1`; **`mtplay`** (new dev tool) for the board protocol's scripted part.

### Decisions for the owner to confirm

1. G1 and G2 inside an app = Home until the switcher exists; on the grid = nothing.
2. The single pointer rule (first contact; nothing presses after it lifts until every finger is up).
3. The thresholds: hold 1000 ms (D2), land 250 ms, still 4 mm, sep 8 mm, edge 2 mm, size off; G2 500 ms, 3 mm.
4. A cancelled three-finger attempt on the grid still clicks under the first finger when the hand lifts (as today;
   no swallow without a fire). Swallowing every 3+-finger touch on the launcher is possible; it is a UX choice.

### Risks for the board runs

- **Supervisor wake-ups while touching an app**: the passive reader wakes the supervisor for every touch report (the
  Goodix sends about 58 a second while a finger moves, P-a; nothing while it is still). Cheap per frame, nothing while
  nobody touches, but a viz1090 pan costs some launcher CPU (protocol part C measures it with `top`).
- **`EVIOCSCLOCKID`** on the launcher's touch fd at start flushes its queue once (a `SYN_DROPPED`, read back).
- **Real apps are legacy until P-e**: in viz1090, System Monitor and Serial Console G1 only logs (the app stays); the
  protocol uses those runs for the false-positive counts and the MT test apps for the hide.
- **A three-finger hold in an LVGL app** (old `cp0_lvgl` copy, flattened reader) also presses there: in a lifecycle
  app the `hide` cancels it; in a legacy app the fingers act as before P-c.
- **The FT5x06** reports no contact size and up to 10 slots; P-a showed palms there as 4-5 contacts (G1 cancels).
- **The deck's case bezel** near the edges (E_EDGE 2 mm): one P-a hold was cancelled at 0.0 mm from the edge.
- `mtplay` needs write access to `/dev/uinput` (`sudo`); the launcher must be started after it with
  `APPLAUNCH_TOUCH_DEV` set to its node, and then reads only that device (real touches do not reach the launcher
  during the scripted part).
