# Report 044: apps with the band (PDA S4), plan

Status: design 2026-10-28; owner decisions in section 7 (they override the sections above where they differ); phase 1
(launcher) committed on `pda-s4-plan`, not released. Launcher at `github/main` 27954f43 (v0.8.10), apps repo at
`origin/main` 31779ac (Serial Console 0.5.0, toolkit pin `apps/_common/LAUNCHER_REF` = 25927c32, v0.8.9). Sources:
reports 038 (section 3.1), 041, 042 (section 2, 13), 043, `docs/APP-CONTRACT.md` sections 3, 5, 10, 13, and the code
cited below. (S) = read in the source, (I) = inferred, not measured.

## 1. Summary

- **The band can already run in an app's own process.** Its whole drawing, routing and recognising side lives in
  `cp0_lvgl` (the display backend `cp0_lvgl_dpi_scaled.c`, `cp0_zone.c`, `cp0_zone_ui.cpp`, `strokes_*.c`), which every
  native app links at the toolkit pin; the backend builds it whenever `APPLAUNCH_PDA=1`. Since v0.8.9 the launcher
  passes `APPLAUNCH_PDA=0` to every app on purpose (S: `app_launch_env.cpp:22-24`), because an app has no glue to use it.
- **Recommendation: B, the band in the app's process.** The launcher passes `APPLAUNCH_PDA=1` to an app that opts in
  (`X-Handwriting=true`) and runs upright, and tells every app the user's choice (`hand=1` in `input.state`). The glue
  that decides when the band is the text input (today `launcher_zone.cpp`, about 60 lines) moves into `cp0_lvgl` so the
  launcher and the toolkit share it. Keys are injected in-process, exactly as in the launcher.
- **The app's canvas does not shrink.** As in the launcher since report 042, the band shows on demand (a text field
  wants input and the keyboard rule allows it) and the page leaves its rows free through the on-screen keyboard's
  inset. Serial Console already relayouts on that inset.
- **Cost:** launcher 0.75 session, toolkit 0.5, Serial Console 0.5, then a 25-minute owner test on the Pi 4 B
  (480x800) and the tablet (800x1280). Option A (the launcher draws the band over the app) is about 3 sessions and
  reworks multitasking; it is not recommended.

## 2. How the band works today (v0.8.10)

| Piece | Where (S) | Process |
| --- | --- | --- |
| Geometry, hit test, router (first contact owns the touch), strip modifiers, result to keys | `cp0_lvgl/src/cp0_zone.c` | whoever runs the backend with `APPLAUNCH_PDA=1` |
| Band widgets, ink, recogniser, timer, injection (`cp0_keyboard_inject_key()`), inset (`osk::set_reserved_inset`) | `cp0_lvgl/src/cp0_zone_ui.cpp` | same |
| Third LVGL display under the native one, flush clipped, touch frames routed, suspend / resume reset the band | `cp0_lvgl/src/cp0/cp0_lvgl_dpi_scaled.c:97-109, 240-251, 476-505, 1080-1085, 1289-1377` | same |
| Show the band where the keyboard would (`cp0::ui::osk::set_zone`), Keyb (standard keyboard instead), the cross | `projects/APPLaunch/main/ui/launcher_zone.cpp:23-90` | launcher only |
| Setting Auto / On / Handwriting + keyboard, `input.state vkb=` | `launcher_osk.cpp:49-67` | launcher only |
| Tables, user overlay | `/usr/share/APPLaunch/strokes`, `~/.config/applaunch/strokes-user.txt` | read at start by the process that builds the band |

- **Sizes** (report 042, 043): strip 51 px over the writing zone (a third of the height, capped so 400 px stay above):
  480x800 band 318 px at y 482; 480x640 band 240 at y 400; 800x1280 band 478 at y 802.
- **On demand:** shown while a text field edits, the keyboard rule allows it and the setting is Handwriting + keyboard;
  hidden with a physical keyboard. The pages get the whole panel and keep the band's rows free through the keyboard's
  inset. The soft-button row (Home, Menu, Apps, Keys) of report 041 was removed in pda.7 (report 042 section 2): Home
  and the switcher are the multitasking gestures; Menu is Ctrl armed on the strip plus a written o (Ctrl+O).
- **Fullscreen apps:** an app owns `/dev/fb0` while in front; the launcher's display is suspended and it only reads the
  touch screen passively for the gestures (APP-CONTRACT section 13). An app with `X-Portrait` that fits runs upright
  on the whole portrait canvas; others run turned (S: `desktop_entry.h:43-58`). Every app gets `APPLAUNCH_PDA=0`.
- **What an app knows:** `input.state vkb=` is the usual rule while an app is in front; Handwriting + keyboard maps to
  `auto` (S: `cp0_ui_osk.c:588-595`), so an app cannot tell that the user chose the band. The toolkit's
  `osk_policy` shows the standard keyboard (Esc, Tab, Ctrl, Alt, arrows row) when the rule allows.
- **Serial Console 0.5.0** (`X-Portrait=480x640`, `X-Background=run`): with a physical keyboard every key goes to the
  port (Raw mode); without one, a one-line `TextField` plus Send (Field mode), Enter sends the line, the keyboard's
  one-shot Ctrl + a letter goes to the port (S: `terminal_view.cpp:549-566`). Tab, Up and Down move focus in a one-line
  `TextField` (S: `cp0_ui_text_field.cpp:366-373`), so today they "send nothing" (its README).

## 3. Candidate architectures

### A. The launcher keeps drawing the band over the app

The app gets a canvas of 2/3 height (`APPLAUNCH_CANVAS=480x482`, `WINDOW=0,0,480,482` at 480x800); the launcher keeps a
display alive for the band rows while the app writes the rows above.

- **Canvas:** today a fullscreen backend owns the whole framebuffer. A windowed fullscreen mode (framebuffer offset,
  touch mapped and clipped to the window) is new backend code, so the app needs a re-pin anyway. The band would be
  fixed while the app is in front; on demand would need a run-time display resize and a relayout of every page (the
  reason report 041 D3 and 042 avoided it). Serial Console would lose 318 of 800 rows even with a keyboard (I).
- **Keys:** the launcher cannot inject into another process. Either a uinput device (`applaunch-*`, `BUS_VIRTUAL`,
  which apps skip by discovery; it would have to be named per app in `LV_LINUX_KEYBOARD_DEVICE` and kept out of the
  keyboard count, report 038 section 2.4) or a new `key <code> <mods> <utf8>` message on the lifecycle socket (only
  apps with `X-Background` under a multitasking launcher have one).
- **Text wanted:** the app must tell the launcher when a field edits (a new app-to-launcher message).
- **Multitasking:** the launcher must run LVGL and read the touch screen actively while an app is in front, against
  the "launcher quiet" hand-over (`mt_app_front()`, `display_handoff`); two writers of `/dev/fb0` through the rotated
  16 bpp output; hide, show, the switcher, the dimmed backdrop and the not-responding sheet all need a band case.
- **Pros:** one recogniser, one Keyb state, the user's templates always current, the app draws no band.
- **Effort / risk:** about 3 sessions plus a multitasking rework (report 038 section 3.1, B); tearing and CPU on the
  Pi 3A+ (I); a new IPC to keep stable.

### B. The band runs in the app's process (shared toolkit) - recommended

The app's own `cp0_lvgl` backend builds the band, exactly as the launcher's does; the launcher only says "you may"
(per launch) and "the user wants the band" (`input.state`).

- **Canvas:** the whole portrait canvas, unchanged. The band shows on demand and reserves its rows through the
  keyboard inset (`cp0::ui::osk::bottom_inset()`, inset listeners); a page that handles the standard keyboard handles
  the band. At 480x800 the band (318 px) is the keyboard's height (318 px, report 038 section 2.2), so Serial Console
  keeps its 19 rows (I); at 480x640 it gains rows (240 vs 285 px); on the tablet it loses some (478 px vs about a
  quarter of 1280).
- **Keys:** in-process `cp0_keyboard_inject_key()`, as in the launcher: the app sees `key_item`s exactly like a
  physical key's (`LV_EVENT_KEYBOARD` with `key_code`, `mods`, `utf8`, then `LV_EVENT_KEY`).
- **Multitasking:** already covered: the lifecycle helper's hide calls `cp0_display_suspend()`, show calls
  `cp0_display_resume()`, whose band block drops a stroke in progress and the strip's owed keys
  (S: `cp0_lvgl_dpi_scaled.c:1080-1085`); `cp0_display_touch_resync()` resets the router.
- **Pros:** no new IPC, no second framebuffer writer, no launcher work while the app runs; 90 % of the code exists and
  is tested (`test_zone.c`, 30+ goldens); fallback is automatic (section 4.4).
- **Cons:** each band app holds its own recogniser and band buffer (about 0.3 MB at 480x800 RGB565, up to 1.5 MB at
  800x1280 32 bpp, I); the band's code is pinned per app (a later strip change reaches an app at its next re-pin; the
  tables and the user's templates are read from disk, so recognition stays shared); the app reads the user's
  templates at start (section 4.5 adds a reload on show); the app's Keyb choice is its own.
- **Effort:** 1.75 sessions in all (section 4.5).

### C. No band in apps (today)

Apps keep the standard keyboard with its Esc / Tab / Ctrl / Alt / arrows row. Cost 0. It is the fallback of B, not
an answer to the owner's wish.

| | A. launcher draws | B. in the app (recommended) | C. today |
| --- | --- | --- | --- |
| App canvas | 2/3 height, fixed | whole panel, inset while shown | whole panel |
| Keys reach the app | uinput or socket message (new) | in-process injection (exists) | standard keyboard |
| Launcher while app in front | must draw and read touch | quiet, as today | quiet |
| App change | windowed backend, key messages | glue at the new pin, opt-in key | none |
| Effort | about 3 + multitasking rework | 1.75 | 0 |

## 4. Recommendation: B

### 4.1 Contract (contract 1, additive)

| Item | Proposal |
| --- | --- |
| `.desktop` | `X-Handwriting=true`: the app's layout is checked with the band; it may draw it. Absent: as today |
| Per launch | `APPLAUNCH_PDA=1` when the app declares `X-Handwriting=true`, runs upright (`DesktopFit::Native` on a portrait launcher) and the launcher's band exists (`launcher_zone::active()`); `0` in every other case (turned, not opted in, compat, terminal), as today |
| `input.state` | new `hand=1` while Settings > On-screen keyboard is Handwriting + keyboard and the band exists, else `hand=0`; written whoever is in front. `vkb=` unchanged (an older app reads `auto` and shows its keyboard). A file without `hand` means 0 |
| Reserved keys | `APPLAUNCH_ZONE`, `screen.state orient=` / `zone=` (report 038) are not needed (the backend computes the band from the panel with `cp0_zone_layout()`); remove the reservation from APP-CONTRACT section 3 |

### 4.2 What the app sees

| From the band | `key_item` in the app |
| --- | --- |
| A written letter, digit, punctuation | `KEY_*` + text (`utf8`); characters without a US key: code 0 + UTF-8 |
| Space, Backspace, Enter strokes | `KEY_SPACE`, `KEY_BACKSPACE` (`\x7f`), `KEY_ENTER` (`\r`) |
| Strip Tab, arrows (held: repeat), Enter | `KEY_TAB`, `KEY_LEFT/UP/DOWN/RIGHT` (`KBD_KEY_REPEATED`), `KEY_ENTER` |
| Strip Ctrl armed + a written letter | the letter's `KEY_*` with `KBD_MOD_CTRL`, no text (Ctrl+C) |
| Strip Alt armed + a key | `KEY_LEFTALT` press, the key, Alt release |
| Strip Keyb | the app's own standard keyboard with its cross, until the cross |

Known limit: Alt + Tab from the strip is injected in-process, so it never reaches the launcher (it reads evdev) and
the lifecycle filter drops Tab with Alt held: no switcher from the strip. Three fingers held, the status-strip
long-press and Super still open it.

### 4.3 Esc header, gestures, switching

- The app's header Esc (top) is unchanged; the band is at the bottom. Esc while a field edits ends the edit (the band
  hides), as with the keyboard. The strip has no Esc key (question 3).
- No soft buttons exist since pda.7; nothing to map. Three fingers in the writing zone write nothing (router) and the
  launcher's passive reader still sees them (G1). The status-strip long-press (G2) is the app's, as today.
- Hide (switcher, Super, Home): the backend suspends the band with the page; show repaints it and drops any stroke.
  The band's shown state follows the field, as in the launcher (report 042 section 9, item 5).

### 4.4 Fallback

| Case | Result |
| --- | --- |
| Launcher v0.8.9 / v0.8.10 (passes `APPLAUNCH_PDA=0`) | no band in the backend: standard keyboard |
| Launcher v0.8.8 (app inherits `APPLAUNCH_PDA=1`) | band built, but no `hand=` in `input.state`: the glue keeps the standard keyboard |
| Setting Auto or On (`hand=0`) | standard keyboard (On: even with a keyboard), as the launcher |
| A physical keyboard | neither (Serial Console: Raw mode) |
| App without `X-Handwriting`, or turned | as today |

### 4.5 Phases

**P1, launcher (0.75 session; branch `pda-s4-band` from main).**
1. `desktop_entry`: parse `X-Handwriting`; `launch_Exec` / `launch_fullscreen_supervised` pass a `band` flag;
   `app_launch_env::variables()` writes `APPLAUNCH_PDA=1` for it (S: `app_launch_env.cpp:24`).
2. `input_presence` / `launcher_osk::publish_vkb()`: `hand=`; `cp0_app_input_t.hand` and its parse in the copy-ready
   `cp0_app_contract.[ch]`.
3. New `cp0_lvgl` helper `cp0_zone_host.{h,cpp}`: `bind(std::function<bool()> band_wanted)`, the Keyb flag and the
   cross (the code of `launcher_zone.cpp:23-90`); `launcher_zone` calls it with its own `band_input()`; every zone
   golden must stay byte-identical. (Alternative: copy the 60 lines into the toolkit; cheaper now, two copies later.)
4. Resume: reload the user templates when the overlay file changed (`cp0_zone_ui_reload()` in the backend's resume
   block), so a glyph trained while the app was hidden counts at once.
5. `TextField`: an optional key hook (`TextFieldEvents::key`, asked first, returns true to take the key), so an owner
   can take Tab / Up / Down before `focus_step()`. Generic; no launcher page uses it.
6. Docs: APP-CONTRACT sections 3, 5, 6, 10; `docs/cp0_lvgl.en.md`; PDA-PROFILE "Limits".

**P2, toolkit (0.5 session; apps repo).** Move `LAUNCHER_REF` to the P1 merge; `osk_policy::install()` binds
`cp0_zone_host` with `cp0_display_zone_active() && hand && !keyb`; `Policy` exposes `hand()`; headless harness: a
`band` option (a second display with `cp0_zone_ui_create()`, `stroke`, `zonekey`, `expect_zone`, the band composed
into the PNG); `test_common` (`hand` parse, the fallback rows of 4.4); APP-GUIDELINES "Text input" rewritten.

**P3, Serial Console (0.5 session; 0.6.0).** `X-Handwriting=true` and the new pin. In Field mode, through the P1 key
hook: Tab, Up, Down go to the port (`translate_key()`, as Raw mode); Left / Right go to the port while the line is
empty, else move the caret; Backspace on an empty line goes to the port; Enter sends the line and the line ending (as
today); Ctrl + letter as today (question 2). Scenes, README, `tests/BOARD-TESTS.md`.

**P4, boards (owner, about 25 minutes).** Section 4.6.

Release: the launcher alone first (v0.8.11: nothing visible changes until an app opts in), then the pin and Serial
Console 0.6.0 (question 5).

### 4.6 Test plan

- **Launcher, PC:** `test_app_launch_env` (opted-in upright: `APPLAUNCH_PDA=1`; turned, not opted in, compat,
  launcher without the band: `0`); desktop-entry parse; `test_input_state` / `test_app_contract` (`hand` round trip,
  old file = 0); `test_zone` unchanged; the contract probe step prints `hand` and the per-launch value; `check-all.sh`
  with every golden byte-identical (the glue refactor must not move a pixel).
- **Apps, PC:** Serial Console scene `portrait-band` at 480x800, 480x640 and 800x1280 in the demonstration world: the
  line tapped (band shown, layout check: nothing under it); "ls" written into the field; strip Enter: the simulated
  port receives `ls` and the line ending; Ctrl + c: `\x03`; Tab: `\x09`; Up: `ESC [ A`; Left on an empty line: `ESC [ D`;
  Keyb: the keyboard with its cross, cross: the band; `keyboards=1`: Raw mode, no band; `hand=0`: standard keyboard;
  `lifecycle` hide / show with the band shown. Every landscape and existing portrait shot unchanged.
- **Pi 4 B** (`pi4b-dsi`, PDA mode On rotate left, 480x800, stylus or finger; a USB serial board with a shell or a
  MicroPython REPL, or a USB-serial adapter with TX tied to RX):
  1. Serial Console upright, open the port, tap the line: the band comes up, same look as the launcher's.
  2. Write `help`, strip Enter: the board answers. Ctrl then a written c: the board stops a command.
  3. Tab completes on the board's shell; Up recalls the last command; Left on an empty line moves the board's cursor.
  4. Keyb: the keyboard with its cross; type a word; the cross: the band again.
  5. Three fingers held on the writing zone: the switcher opens, nothing is written; back: the band, no stray letter.
  6. Connect the Bluetooth keyboard: the band goes, keys go straight to the port.
  7. With the app hidden: Settings > On-screen keyboard Auto; back in the app: the standard keyboard. Then
     Handwriting + keyboard again; train one glyph in Strokes training; back in the app: that glyph is recognised.
  8. `free -m` before and after opening the app (band memory).
- **Tablet** (800x1280 portrait, finger, amd64 package): steps 1, 2, 4 and 5.

## 5. Risks

1. **Code drift across pins:** a strip or recogniser-engine change in the launcher reaches each band app only at its
   next re-pin. The tables and templates are on disk, so the alphabet itself stays shared.
2. **Two recognisers at once** (launcher and a hidden or shown app): memory only; the recogniser is idle between
   strokes. The strokes log (`APPLAUNCH_STROKES_LOG`) may get lines from both processes (append only; acceptable).
3. **Serial Console's Field mode** grows key rules; a wrong rule could send arrows to the port while the user meant
   the caret. The empty-line rule keeps it predictable; question 2.
4. **The tablet** at 800x1280: the band takes more height than the keyboard; the terminal has fewer rows there (I).
5. **The pin move** to the P1 commit is a whole-toolkit move (every later app build takes it); the other apps are not
   rebuilt by this plan and keep their published binaries.

## 6. Open questions for the owner

1. **Opt-in:** a `.desktop` key per app (`X-Handwriting=true`, proposed), or every `X-Portrait` app built at the new
   pin gets the band without a key?
2. **Serial Console keys with the band:** Tab, Up and Down always to the port; Left, Right and Backspace to the port
   only while the line is empty (else they edit the line). And Tab with a half-written line: send the line's text then
   Tab (shell completion), or only Tab?
3. **Esc:** the strip has no Esc, and Serial Console's key bar (Send Esc, Ctrl-D, Reset board) gives its room while
   text is edited. Keep the key bar visible above the band (about 3 fewer text rows at 480x800), add Esc to the strip
   on every page (a launcher-wide change), or leave Esc to Keyb's keyboard?
4. **Templates trained while an app is open:** reload them when the app comes back to front (proposed, P1 step 4), or
   only at the app's next start?
5. **Release order:** ship the launcher part as v0.8.11 on its own (no visible change), then the pin and Serial
   Console 0.6.0; or hold the launcher release until Serial Console has passed the board test?

## 7. Owner decisions (2026-10-28)

1. **No opt-in key.** No `X-Handwriting`: every portrait-native app (`X-Portrait` that fits, run upright) gets
   `APPLAUNCH_PDA=1` from a launcher with the writing zone and follows the user's Settings (Auto / On / Handwriting +
   keyboard); the band shows only when a text field asks for input, exactly like the on-screen keyboard. Apps run
   turned keep `APPLAUNCH_PDA=0` and the standard keyboard rule.
2. **The band's keys are the on-screen keyboard's.** Nothing special for Serial Console: no `TextField` key hook, no
   Tab-sends-line rule (P1 step 5 and the P3 key rules are dropped; question 2 closed).
3. **Trained glyphs reach an app at its next start** (no reload on resume: P1 step 4 dropped).
4. **Release with the board test.** The launcher part is not released alone: it ships with the toolkit pin and the
   Serial Console board test (no tag, push or PR for phase 1 by itself).

Phase 1 as built (differences from 4.5): the shared rule is `include/cp0_zone_host.hpp` / `src/cp0_zone_host.cpp`
(`cp0::ui::zone_host::bind(Hooks{hand, gate, changed})`, `hand_changed()`, `gate_changed()`), its pure core
`cp0_zone_host_*` in `cp0_zone.[ch]`; `bind(band_wanted)` alone was not enough, Keyb and the cross need the user's choice
(`hand`) apart from the launcher's own condition (`gate`: no app in front). `hand=` is `cp0_ui_osk_effective_hand()`;
`APPLAUNCH_PDA` is `app_launch_env::app_band(mode, turned, launcher_zone::active())`, no `.desktop` change.
