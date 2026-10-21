# 015 - Multitasking P-d: the switcher screen, the legacy app sheet, the grid's dots, the first-run hint

Status: built 2026-10-21 on branch `multitasking-pd`, on `main` 8c08509 (P-b #63 and P-c #64 merged); verified on the
PC (section 9); board tests pending. Test build 0.8.0-pd.2.
Study: [`reports/026-multitasking-study.md`](../reports/026-multitasking-study.md) (sections 5.2, 6.1-6.3; owner
decisions D6-D9, D12, D15). Builds on P-b ([`015-multitasking-pb.md`](015-multitasking-pb.md): lifecycle, hide /
resume / close, Alt+Tab) and P-c ([`015-multitasking-pc.md`](015-multitasking-pc.md): G1, G2, the `open_switcher`
seam). Design: the owner-approved mockup "Task switcher" (2026-10-20). Board protocol of this phase:
[`tests/multitasking-pd-protocol.md`](../tests/multitasking-pd-protocol.md). No protocol change for apps;
`docs/APP-CONTRACT.md` section 13 only describes what the switcher message and the gestures now do (and that an app
without `X-Background` is frozen behind the legacy sheet).

## 1. Scope

| In P-d | Not in P-d |
|---|---|
| the switcher screen "Open apps" (launcher-owned, built on open, deleted on close) | thumbnails or pictures of the apps, animations, swipes, flick to close |
| opening it: G1 / G2 in a lifecycle app (hidden first), on the launcher with an app resident, Alt+Tab | apps' own G2 (`switcher` from their strip: P-e) |
| a card tapped (resume), its X (close), Close all, Home, leaving without a change (Esc, header Esc, G1 / G2 again) | settings, tuning G1 / G2 |
| the legacy sheet "Close <app>?" over the frozen app's dimmed last frame: Keep, Close | the sheet for wrapped (compat) apps (section 7, deviation 2) |
| a dot on the home tile of a resident app | the optional `[n]` count in the status strip (D8: no) |
| the one-time hint (D15) | the switcher on canvases under 640x480 |

Everything stays behind multitasking being on (built behind `APPLAUNCH_MULTITASK=1`; since task 015 "default on",
2026-10-21, on by default on canvases of 640x480 and more and off below, `APPLAUNCH_MULTITASK=0` turns it off;
nothing of P-d exists or runs with it off), and needs a
canvas of at least 640x480 (the deck, the Pi 3A+ DSI at 800x480, the HackBerry at 720x720, 1024x600, 1280x720).
Smaller canvases (480x320, the 320x480 SPI panel turned) keep the P-b / P-c behaviour exactly: the Alt+Tab text on the
toast, G1 / G2 from an app = Home, on the launcher = nothing, no dot, no hint (`SupervisorConfig::switcher_ui = false`).
This was the simplest rule: one flag computed once at start (`multitask::switcher_fits`), the supervisor's P-c code kept
as it was behind it.

## 2. The switcher screen (`multitask/switcher_view.{hpp,cpp}`, `switcher_model.{hpp,cpp}`)

```
640x480 (deck, 11.3 px/mm)
+----------------------------------------------------------------+
| Open apps                          [ Esc ]  BT |||  [20:30]    |  the shared page header (cp0::ui::Page + strip)
|  +---------------------------+  +---------------------------+  |
|  | [icon] Serial       [ X ] |  | [icon] Mesh Hop     [ X ] |  |  2x2 cards, 302x148, 12 px apart
|  |        Console            |  |        * Running          |  |  a long name on two lines
|  |        * In front         |  |        * Running          |  |  X: a 7 mm target (79 px) in the corner
|  +===========================+  +---------------------------+  |  blue border = the selection
|  +---------------------------+  + - - - - - - - - - - - - - +  |
|  | [icon] RSS Rea...   [ X ] |  |     Room for one more     |  |  a free slot: dashed, not selectable
|  |        o Paused           |  |                           |  |
|  +---------------------------+  + - - - - - - - - - - - - - +  |
|  [          Home            ]  [         Close all          ]  |  TARGET high (7 mm), Close all in red
+----------------------------------------------------------------+
```

Sizes (the mockup's px at 640x480 turned into the layout service's terms, so every canvas gets them):

| Part | Value | Deck | 800x480 (9.2 px/mm) | 720x720 | 1024x600 (6.6) | 1280x720 (11.6) |
|---|---|---|---|---|---|---|
| header | the shared page header (title, 9 mm Esc, status strip) | 56 | 56 | 64 | 56 | 64 |
| margins, gaps | 1.1 mm | 12 | 10 | 11 | 7 | 13 |
| bottom bar | TARGET (7 mm) | 79 | 64 | 70 | 46 | 81 |
| card | the rest, 2x2 | 302x148 | 385x160 | 343x271 | 501x235 | 620x261 |
| X target / visible key | TARGET square / TARGET - 2 SPACE_S | 79 / 63 | 64 / 48 | 70 / 54 | 46 / 30 | 81 / 61 |
| icon | 5.7 mm | 64 | 52 | 57 | 38 | 66 |
| name / state text | BODY bold / CAPTION, dot 2/3 of CAPTION | 24 / 18 | 22 / 18 | 24 / 20 | 22 / 18 | 26 / 20 |

- The name takes up to two lines (`layout_text`): a line beside the X's target ends at the target's left edge, a line
  below it runs to the card's inset. A name too long for one line wraps (whole words; a first word longer than the
  line is cut between characters) and its second line is placed under the X when the card has the room, so it gets
  the card's width; only that second line ends with dots ("An App With A Thirty ..." on the deck). Real names fit:
  "Serial / Console", "System / Monitor", "Net / Toolbox", "RSS / Reader" on the deck, one line from 800x480 up. The
  state line follows, never cut ("Not responding").
- States (`card_label`): the app the switcher was opened from says **In front** (green) unless it stopped answering
  (**Not responding**, amber, wins); otherwise **Running** (green, `X-Background=run`), **Paused** (grey, frozen),
  **Not responding** (amber). Cards are in recently-used order (study 6.2): the app it was opened from first.
- Colours: the home tiles' (ground #1E1E1E, border #3A3A3A, pressed #33414D, the blue selection #2D9CDB), the toolbar
  keys' grey (#2A2A2A) for the bar, the mockup's X key (#2E2E32), dots (#35C25A, #9B9BA1, #E7A31A) and red (#FF8A8E).
- Touch: a card = pick (resume), its X (the TARGET square in the corner, the visible key inside it) = close, Home,
  Close all, the header's Esc = cancel. Keyboard (on the screen, `LV_EVENT_KEYBOARD`, as the home grid): arrows move in
  the grid (cards in two rows, the bar under them; a free slot is skipped), Tab / Shift+Tab in reading order (wrap),
  Enter (on its release, so no app gets it) activates the selection, Esc / Backspace cancel. The selection starts on
  the first card (Alt+Tab: P-b's rule, the previous app). While it is up the keypad has no input group (a page left
  under it, Settings, must not get the keys too); it comes back when the screen closes.
- Resources: a few dozen objects, built when it opens and deleted when it closes (the launcher screen that was active
  comes back). No timer of its own (the header's status strip keeps its clock and 2 s status poll, as on every
  launcher screen), no picture of the apps; the cards' icons are the tiles' icon files.

## 3. What opens and closes it (`Supervisor::open_switcher`, the P-c seam)

| Where | G1 / G2 / the app's `switcher` / FIFO `g1` `g2` | Alt+Tab |
|---|---|---|
| a lifecycle app in front (`X-Background=run\|freeze`) | it is hidden (P-b: hide, ack or 300 ms, freeze, snapshot), then the switcher shows, that app "In front" | the same (the P-b session), the selection on the previous app |
| a legacy fullscreen app in front | the legacy sheet (section 4) | nothing (P-b: "cannot stay in the background") |
| the launcher (grid or a page), an app resident | the switcher | the switcher (P-b: consumed only while an app is resident) |
| the launcher, nothing resident | nothing (log line) | the key goes to the page |
| a wrapped app in the compat window | nothing (log line), the fingers swallowed | the key goes to the app |
| the switcher is up | leave it (as Esc) | the session takes it over (cancel goes back to where the switcher was opened from) |
| the sheet is up | Keep | ignored |

- **Order on the panel.** The supervisor sends the switcher (or the sheet) to the launcher's UI thread **before** the
  `launcher_front()` of the same hand-over: the UI loads the screen while the display is still suspended, then takes
  the panel back and paints it, never the grid in between. An app that takes the panel (a card, cancel, Alt release on
  an app) closes it after the display is suspended (`app_next`): nothing is drawn in between either.
- **Pick** = `Model::resume` (P-b: the app in front is none, so restore, drain, `oom_score_adj` back, SIGCONT, `show`).
  **X** = `Model::close` (SIGCONT, `close`, SIGTERM 3 s later, SIGKILL 3 s after); the card goes with the next update and
  the switcher stays; with no app left it closes. **Close all** = `Model::close_all` (with the launcher in front every
  resident app is hidden: all close), the switcher closes at once. **Home** = the switcher closes (the apps stay
  hidden). **Cancel** (Esc, Backspace, the header's Esc, G1 / G2 again) = back to the app it was opened from (resumed;
  also while that app is still being hidden), or to the launcher.
- **Alt+Tab.** P-b's session stays as it was (Alt held / Tab / Shift+Tab / release commits / Esc or Backspace cancels,
  its key trace and `session ...` lines). What it shows changes: instead of the toast text, the switcher screen with
  the selection border on the session's selection (`session_item`: an app's card, or Home); touch is ignored while the
  session runs (as with P-b's text). Alt released on an app resumes it, on Home closes the switcher to the launcher.
- Super = Home (unchanged).

## 4. The legacy app sheet (study 6.2, D12)

```
+----------------------------------------------------------------+
|  (the app's last frame, dimmed to 38 %)                        |
|     +------------------------------------------------------+   |
|     | Close viz1090?                                        |   |  the toolkit's Dialog (cp0::ui::Dialog),
|     | This app can't stay in the background. Closing ends  |   |  centred on the panel
|     | it; keeping it returns you to it.                     |   |
|     | [        Keep        ]   [        Close        ]      |   |  TARGET high; Close in red (ButtonRole::Destructive)
|     +------------------------------------------------------+   |
+----------------------------------------------------------------+
```

- **What happens to the app: frozen** (the study's choice; decided so). G1 / G2 on a legacy fullscreen app (no
  `X-Background`: viz1090, System Monitor, Serial Console until P-e, the MT Legacy / MT NoEsc test apps) runs
  `Model::hold_front`: `SIGSTOP` on its process group (as the P-b hide), its last frame copied (snapshot), the new state
  **HELD**, the launcher in front. HELD counts against the cap but is not "hidden": the cap's LRU, the memory rule and
  Close all never pick it.
- **Its frame stays on the panel, dimmed.** The snapshot is written back to the launcher's page with every colour at 38 %
  (`cp0_display_fb_backdrop`, the mockup's 62 % black veil), and the launcher draws only the sheet's box over it: the
  display manager's **native clip** (`cp0_display_set_native_clip`) writes only that rectangle, and in it no pure black
  pixel (the screen's ground around the rounded corners). No picture is decoded or converted (any framebuffer format and
  rotation work: the dim is per pixel in the panel's own format). The lock screen stays off while it is up (only the box
  is drawn), the supervisor's notices wait.
- **Input while it is up.** The launcher grabs its touch and keyboard fds (P-b's grab, now also with an app HELD), so the
  frozen app's queues stay empty; the keypad has no group. The three fingers that made the gesture click nothing (P-c's
  hand-over block and swallow 2).
- **Keep** (also Esc, Backspace, G1 / G2 again) = `Model::resume` of a HELD app: the P-b resume path exactly (the grabs
  released, the original snapshot restored, the app's evdev queues drained, SIGCONT; no `show` message: no socket). The
  touch that pressed Keep ended before (a click is a release), so the app sees nothing of it.
- **Close** = `Model::close`: SIGCONT then SIGTERM at once (no socket), SIGKILL 3 s later: Esc held 3 s with the freeze
  undone first. The sheet goes at once, the launcher paints its grid; a legacy app continued may draw once before it
  dies: the launcher repaints after its exit (as for a not responding app, P-b).
- **viz1090 and readsb.** viz1090 runs readsb in its own process group (`run_viz1090.sh`), so the freeze stops readsb
  too, exactly as a P-b freeze would: while the sheet is up no ADS-B is decoded and the RTL-SDR's USB buffers may
  overflow (readsb reports and goes on after SIGCONT); P-a measured readsb's TCP link to viz1090 dropping after about
  15 s frozen (whether viz1090 reconnects after the thaw is not verified). Keep after a long sheet may therefore show a
  viz1090 without new aircraft until it reconnects. Not freezing would leave a legacy app drawing over the sheet (it
  does not know about hide), so freezing is the only safe choice; the risk is listed for the board run (protocol
  step 4 uses the MT test apps; viz1090 is optional).

## 5. The home grid's dots (`native_ui::set_resident_apps`)

The supervisor sends the resident apps (Exec lines) whenever they or their states change (`UiPort::apps`, from the
loop's existing turn: no new polling). The tile of each gets a green dot (#35C25A) at its top right, 1.4 mm (16 px on
the deck) inside a 3 px ring of the tile's colour, 1.1 mm (12 px) inside the tile's border; created the first time a
tile needs one, hidden when the app closes (the harness checks the grid is then pixel-identical to before). Tapping the
tile resumes the app (P-b, unchanged). Without the flag no dot object is ever created.

## 6. The first-run hint (D15)

The first time an app is left open (any app becomes hidden, by any path) the supervisor queues one notice on the
launcher's toast (P-b's NoticeQueue: shown when the launcher is in front and neither the switcher nor the sheet is up):
**"Hold three fingers to switch apps"** when the touch screen tracks 3 contacts (G1 works), else **"Alt+Tab switches
apps"** (no touch screen, or a single-touch panel: G1 does not exist there). It is remembered in
`~/.config/cardputerzero/multitask-hint-shown` (the launcher's config directory, next to `config.json`; created with
its parents); delete that file to see the hint again. No `HOME`: shown once per run.

## 7. Deviations from the mockup and the study

1. **The X's touch target is 7 mm (79 px on the deck)**, not the mockup's 56 px (5 mm on the deck's 2.8" panel): the
   brief asks for at least 7 mm. The visible key inside it is TARGET minus SPACE_S on each side (63 px on the deck);
   text beside it ends at the target's left edge (a long name wraps onto a second line under it).
2. **No sheet for wrapped (compat) apps** (the brief's example list names them). They run in the compat window with
   the launcher's toolbar, whose Esc held 3 s already ends them, so a touch-only user is not stuck; a sheet there needs
   the compat blit paused, the display mode switched, the external runner given a close request and the virtual
   keyboard's forwarding paused (several components outside P-d). G1 in a wrapped app is logged and swallowed; Alt+Tab
   passes to it. Follow-up if wanted.
3. **The sheet is the toolkit's Dialog**: its width (DIALOG_MAX_W, 560 px on the deck; mockup 72 %, 460 px) and body
   text size (BODY 24 px; mockup 19 px) are the shell's dialog tokens; the buttons are TARGET high (79, mockup 78).
   One additive role in cp0_lvgl: `ButtonRole::Destructive` (the red label). Keep is the Cancel role (Esc); Enter
   presses nothing (Keep and Close are touch first; the sheet appears only after a touch gesture).
4. **Cards are in recently-used order** (study 6.2), the app the switcher was opened from first and selected. The
   mockup's two screens differ in order (the 800x480 one has the app in front last); it does not fix one.
5. **"In front" yields to "Not responding"** (amber) for the app it was opened from (an app that did not answer `hide`
   within 300 ms).
6. **The X is not reachable by keyboard** (Tab and the arrows go over the cards and the bar): a keyboard user closes one
   app by opening it and holding Esc 3 s, or all with Close all. Open question for the owner (Delete on a card?).
7. **The switcher also opens on launcher pages** (Settings, the Calculator; G2 works on every page with the strip),
   not only on the grid; Home, Close all and cancel go back to that page (P-b already brought Alt+Tab back to it).
8. **The legacy sheet's frame is not a picture**: the frozen app's last frame stays in the framebuffer, dimmed by the
   supervisor, and the launcher draws only the sheet's box (a display-manager clip): no image decode, no extra buffer.
9. The toast of the hint is the launcher's toast (top of the screen), not the mockup's bottom pill.
10. **Not responding cards can be picked** (P-b made them resumable; study 4.2 said Close only).

## 8. Log lines (`[mt] ` prefix; the board protocol greps them)

| What | Line |
|---|---|
| start | `switcher screen (P-d): on (640x480); first-run hint "Hold three fingers to switch apps" (/home/<user>/.config/cardputerzero/multitask-hint-shown)` (under 640x480: `off (...: the P-b / P-c stand-ins)`) |
| G1 in a lifecycle app | `switcher (G1): hiding MT Freeze first`, the P-b hide lines, then `switcher: opened by G1 with 2 apps (from MT Freeze)` |
| on the launcher | `switcher: opened by G2 with 1 app (on the launcher)`; nothing resident: `switcher (G1): nothing open, nothing to do` |
| Alt+Tab | P-b's `session ...` lines, `switcher: opened by Alt+Tab with 2 apps (from MT Run)` |
| a card | `switcher: card tapped MT Run`, then P-b's resume lines |
| X / Close all / Home | `switcher: closed MT Run` / `switcher: close-all (2 apps)` / `switcher: home` |
| leaving | `switcher: cancelled (back to MT Freeze)`, `switcher: cancelled by G1 (back to MT Freeze)`, `... (back to the launcher)` |
| the sheet | `switcher: sheet shown for MT Legacy (G1): it cannot stay in the background`, `MT Legacy: SIGSTOP ...`, `MT Legacy: snapshot ...`, `MT Legacy: backdrop (its last frame dimmed under the sheet) backdrop_ms=...`; `switcher: sheet keep MT Legacy` / `switcher: sheet close MT Legacy`; G1 on it: `switcher (G1): the sheet is up: Keep` |
| the hint | `hint: "Hold three fingers to switch apps" (the first app left open; shown once, remembered in ...)` (no `notice:` line: it is not a model notice) |
| a wrapped app | `G1 fired (3 contacts, held 1003 ms) in a wrapped app: nothing` |

Status file: `switcher=open|off` and `sheet=<name>|-` (P-d only).

## 9. As built (2026-10-21)

### Files

| File | What changed |
|---|---|
| `projects/APPLaunch/main/ui/multitask/switcher_model.{hpp,cpp}` | new, pure: card states and tones, the keyboard selection, the session's selection, `switcher_fits`, the hint text |
| `.../multitask/switcher_view.{hpp,cpp}` | new, LVGL: `SwitcherScreen` (header, cards, free slots, bar, keys) and `LegacySheet` (the Dialog) |
| `.../multitask/multitask_model.{hpp,cpp}` | state `Held`, `hold_front()`, op `Backdrop`, `held()`; Held is resident and frozen, not hidden; the touch grab also with an app held; Keep = resume, Close = close; repaint after a held app closed |
| `.../multitask/supervisor.{hpp,cpp}` | `open_switcher` (the P-c seam) for P-d; `SwitcherCard`, `SwitcherInfo`; `UiPort::switcher / sheet / apps` and `FramebufferPort::backdrop` (defaults: nothing); `switcher_pick / close / close_all / home / cancel`, `sheet_answer`; the screens posted before the hand-over; Alt+Tab on the screen; the hint; status lines; `switcher_ui` (off: P-b / P-c exactly) |
| `.../multitask/multitask_ui.{hpp,cpp}` | the screens built and deleted on the supervisor's events, the keypad group detached meanwhile, touch swallowed during a session, notices waiting, the dots, `switcher_ui` from the canvas, the hint's text and file, the wrapped-app guard, the start log line; `screen_up()`, `sheet_up()`, `Hooks::show_home` |
| `projects/APPLaunch/main/ui/launch.cpp` | `mt_launcher_front` keeps the switcher / sheet screen (no grid over it) and the lock off under the sheet; the `show_home` hook |
| `projects/APPLaunch/main/ui/native_ui.{hpp,cpp}` | `set_resident_apps` (the dots), `app_icon`, `external_app_running` |
| `ext_components/cp0_lvgl/include/cp0_display.h`, `src/cp0/cp0_lvgl_dpi_scaled.c` | `cp0_display_fb_backdrop` (the dimmed frame), `cp0_display_set_native_clip` (only a rectangle written, no pure black in it); inactive by default |
| `ext_components/cp0_lvgl/include/cp0_ui_widgets.hpp`, `src/cp0_ui_dialog_widget.cpp` | `ButtonRole::Destructive` (red label; additive) |
| `projects/APPLaunch/tests/test_switcher_model.cpp`, `test_multitask_model.cpp`, `run_tests.sh` | the pure rules (64 checks); Held: hold, Keep, Close, killed while held, not picked by cap / memory / Close all (18 checks) |
| `projects/APPLaunch/tests/multitask/test_supervisor_host.cpp`, `run_supervisor_host.sh` | five P-d scenarios (96 checks): G1 from an app, a card, X, Home, G2 on the launcher, Close all; cancel by Esc and G1; the legacy sheet (frozen, dimmed, Keep without stale input, G1 = Keep, Close); Alt+Tab on the screen; the hint once and remembered |
| `projects/APPLaunch/tests/render/{Makefile, src/harness_main.cpp, src/harness_launch.cpp}` | the views compiled in; commands `switcher`, `sheet`, `dots`, `tapitem`, `expect_event`, `expect_targets`; tiles named `tile:NAME` (their Exec line) |
| `projects/APPLaunch/tests/render/scenes/multitask_{switcher,sheet,home}.scene`, `golden/640x480/` (13 new) | the three scenes (section 9.2) and their 640x480 goldens |
| `docs/dev/tests/multitasking-pd-protocol.md` | the board protocol |
| `docs/APP-CONTRACT.md` | section 13: the `switcher` message and the gestures as P-d does them (no protocol change) |
| `projects/APPLaunch/tests/render/README.md` | the new commands and scenes |
| `docs/dev/reports/026-multitasking-study.md`, `docs/dev/ROADMAP.md` | the P-d status |

### Tests (PC, WSL)

| Suite | Before (`main` 8c08509) | After |
|---|---|---|
| `cp0-unit` | PASS | PASS (unchanged tests) |
| `app-unit` | model 112, keys 130, protocol 32, notice queue 29, host process suite 192 checks, PASS | model **130**, keys 130, protocol 32, notice queue 29, **switcher model 64** (new), host process suite **288** (+96, P-d), PASS |
| render harness | 41 scenes, 64 goldens, 1469 screenshots; twice 1509 PNGs; header 288; contract 363 | 44 scenes (+3), **77 goldens identical: the 64 existing byte-identical + 13 new** (640x480 of the new scenes), 1540 screenshots; twice 1583 PNGs; header 293 (+5: the switcher's header against the grid's at 5 sizes); contract 363; PASS |
| `branding` | PASS | PASS |
| launcher build (`tools/dev/wsl-build.sh`, aarch64) | OK | OK, no new warning |

The three scenes, at 640x480, 800x480 (9.2 px/mm), 720x720, 1280x720 and 1024x600 (6.6 px/mm); 480x320 has no switcher:
- `multitask_switcher`: 3 apps and a free slot (Serial Console In front), the keyboard selection (Right, Down to Close
  all; Tab), a Not responding card, 4 apps, long names on two lines ("Serial Console", "System Monitor", "Net
  Toolbox", a 30-character name), 1 app with a very long name. Checked: every touch target at least 7 mm,
  cards and bar inside the panel and apart (`expect_targets`), the header pixel-identical to the grid's, taps (a card,
  an X at the far corner of its 7 mm target outside the visible key, Home, Close all) and keys (Esc, Backspace, Enter)
  reach the right action (`expect_event`).
- `multitask_sheet`: the sheet over the Calculator standing in for the app's frame, and with a long app name. Checked
  on every pixel: outside the box the frame dimmed to 38 %, inside the sheet; Keep and Close 7 mm; Keep / Close by
  touch, Esc and Backspace = Keep.
- `multitask_home`: the grid with two dots and the hint, a dot on the selected tile, the dots gone (pixel-identical to
  before), the keyboard-only hint.

Shots looked at (all five sizes): `switcher_three`, `switcher_keys`, `switcher_tab`, `switcher_not_responding`,
`switcher_four`, `switcher_names`, `switcher_one`, `sheet_viz1090`, `sheet_long_name`, `home_plain`, `home_dots_hint`,
`home_dots_selected`, `home_hint_keyboard`.

### Verified on the PC only (needs the boards)

- The whole launcher integration: the screens loaded while the display is suspended and painted at the hand-over, the
  keypad group detached and given back, the toast waiting, the lock screen off under the sheet, the dots on the real
  grid, the hint file in the real home directory.
- The native clip and the backdrop on the real framebuffers (the deck's XRGB8888, the Pi 3A+'s RGB565 with its
  layout; the harness checks XRGB8888 pixel by pixel).
- Real hands on the cards, the X targets and the sheet; the three fingers lifting after G1 clicking nothing on the
  switcher or the sheet (P-c's hand-over, unchanged).
- The legacy sheet with real legacy apps (viz1090 + readsb frozen, System Monitor, Serial Console).

### Decisions for the owner to confirm

1. The switcher only from 640x480 up; smaller canvases keep the P-b / P-c stand-ins.
2. The X's target 7 mm (visible key 63 px on the deck), text beside it cut at its edge.
3. Freezing the legacy app behind the sheet (with its helpers, viz1090's readsb), Close as Esc held 3 s.
4. No sheet in wrapped apps (their toolbar's Esc held 3 s ends them).
5. The sheet as the toolkit's Dialog (width and text size of the shell's dialogs).
6. Cards in recently-used order; In front first and selected.
7. The hint file `~/.config/cardputerzero/multitask-hint-shown`; the keyboard wording "Alt+Tab switches apps" also
   on single-touch panels.
8. No keyboard way to close one app from the switcher (open question: Delete on a card?).

### Risks for the board runs

- **The hand-over order.** The screens rely on the supervisor's events arriving in order on the UI thread (they do: one
  queue). If a board shows the grid flashing before the switcher, the log's order of `opened by` and `launcher in
  front` tells.
- **The keypad group.** Detached while the switcher or the sheet is up and given back on close; a page's keys that stop
  working after the switcher (Settings) would point there.
- **readsb frozen with viz1090** behind the sheet (section 4).
- **A legacy app continued for Close may draw once** over the grid before it dies; the launcher repaints after its exit.
- **The colour key of the clip**: a sheet pixel of pure black would not be written (none is drawn today).
- **Status strip timers** run on the switcher screen as on every launcher screen (clock, 2 s status poll).
