# Decisions

Short, dated, never re-argued without the user. Newest first.

- **2026-10-21: multitasking is on by default from 640x480, released as v0.8.0.** Owner decision (`reports/026-multitasking-study.md`, update of 2026-10-21): on logical canvases of 640x480 and more, off below (480x320, 320x170); `APPLAUNCH_MULTITASK=0` turns it off, `=1` forces it on. Known limits at the release: viz1090 can hang on three-finger touches (accepted by the owner; hold Esc 3 s closes it), the HackBerry CM5 uses Alt+Tab only (its touch reports 2 contacts), Net Toolbox stays legacy.

- **2026-10-18: the native page header's Esc stays 9 mm.** Owner decision, amending the 5/7 mm entry below: the header Esc (`cp0_ui_roller_page`) is excluded from the 7 mm rule and keeps its pre-change size (102 px wide on the deck at 11.3 px/mm), the same in the launcher and every app. It takes the stock-app toolbar keys' constant through `cp0_ui_toolbar_key_px` (`CP0_UI_TOOLBAR_KEY_MM_X10`, at least `CP0_UI_TOOLBAR_KEY_FLOOR_PX`), height still clamped inside the bar's margins; `CP0_TOK_TARGET` stays 7 mm for buttons, action bars and dialog buttons.

- **2026-10-18: the Calculator is exempt from the touch-target token and fills the screen.** Owner decision: the built-in Calculator's keypad is not sized from `CP0_TOK_TARGET` (no 1.3 x TARGET cap, no 75 % floor, no 640 px width cap, no 2.4:1 aspect cap). On every computed canvas (`page_app/calculator_layout.hpp`) the 6x4 keypad spans the full width between the `SPACE_M` margins and runs down to the bottom margin; the display above it takes at most 22 % of the room under the header, with the biggest result font up to 48 px that fits there (the source line 7/12 of it) and no more height than that font needs; the keys get all the rest. Keys may stretch wide; on a very tall screen a key stops at 1.5 x its width and the display takes the extra. Key labels come from the key (55 % of its height, at most 45 % of its width, snapped to the even builtin sizes). Gaps (`SPACE_S`), margins (`SPACE_M`) and radius (`RADIUS_M`) still come from the tokens. The pinned deck 640x480 and Pi 3A+ 480x320 presets already fill the screen and keep their pixels.

- **2026-10-18: two standard touch sizes, 5 mm and 7 mm; the 9 mm target is dropped.** Owner decision: lists already work at 5-7 mm on touch (CompactList 5 mm, rows 7 mm), so a separate 9 mm class for buttons was inconsistent. Standard now: 5 mm for rows where a tap only selects (`CP0_TOK_LIST_ROW_H`), 7 mm for everything that acts (buttons, action bars, dialog buttons, rows that act: `CP0_TOK_ROW_H`; the header Esc was in this list until the same day's amendment above keeps it 9 mm). `CP0_TOK_TARGET` stays as a name for compatibility and equals the row value (7 mm); its pixel floor goes from 48 to 44 px (the usual finger minimum; the floor applies below 6.3 px/mm, e.g. the Pi 3A+ at 6.1 px/mm gets 44 instead of 55). The stock-app toolbar (pinned 140 deck / 100 Pi 3A+, computed sizes 9 mm keys) and the on-screen keyboard (keys and 9 mm Esc/Tab/Ctrl) keep their sizes: they have their own constants (`CP0_UI_TOOLBAR_KEY_MM_X10`, the OSK's) and no longer derive from TARGET. App-facing: `screen.state` `target=` is now 7 mm (79 px deck); the apps repo's APP-GUIDELINES and layout checker change separately.

- **2026-10-15: Net Toolbox, scope and phases as in report 027** (`reports/027-net-toolbox.md`). One native app,
  package `net-toolbox`, name **Net Toolbox**, `X-MinSize=640x480`; it does not repeat LanScan or Wi-Fi Survey. Phases:
  0.1 Home, Interfaces, Diagnosis, Ping, DNS lookup, Port check; 0.2 Traceroute, HTTP check, Public IP, Whois; 0.3 speed
  test, Wake-on-LAN, BLE radar; 0.4 Wi-Fi walk test, DNS race, bandwidth meters. Report first, then tasks.
  - D1 accepted with the start of 0.3: if the BLE spike shows it is needed, the shared `_common/scons/main.SConstruct`
    takes an optional per-app library list so the app can link libsystemd; otherwise the BLE radar uses a tool that
    needs no change there.
  - D2 accepted: `bluez` stays an optional dependency; without it the BLE tool says Bluetooth is not available.
  - D3 accepted: the speed test and the captive-portal check use third-party endpoints (Cloudflare, a generate_204
    URL), both documented in the README and off until the user starts them.
  - No BLE scans on the deck: its Bluetooth keyboard is paired, and Wi-Fi and Bluetooth share one radio chip.

- **2026-10-14: multitasking decisions D1-D15, then parked** (`reports/026-multitasking-study.md`, section 8).
  Nothing is built; restart with the spike P-a (measure first). Taken as recommended except where marked changed.

  | D | Decision |
  | --- | --- |
  | D1 | Hybrid opt-in lifecycle (`X-Background=run\|freeze`), legacy apps unchanged |
  | D2 | **Changed:** touch gesture = 3-finger hold **1 s** (not 700 ms); still to confirm with P-a data |
  | D3 | Long-press on the status strip, 500 ms |
  | D4 | Alt+Tab (release Alt to switch) and Super = Home when present |
  | D5 | **Changed:** short Esc at an app's root keeps **quitting** (no minimise); hold Esc 3 s force-closes |
  | D6 | 4 resident apps, MemAvailable floors 64 / 32 MB, LRU auto-close with a toast |
  | D7 | 2x2 card grid (list at 480x320) |
  | D8 | **Changed:** a dot under the tile label only; no `[n]` in the status strip |
  | D9 | **Changed:** X on each card and a Close all button; no flick-up |
  | D10 | `SIGSTOP` on the process group |
  | D11 | Terminal sessions kept alive (P-f), after P-e |
  | D12 | Wrapped VFB apps: no multitasking; legacy Close/Keep sheet |
  | D13 | Additive extension of contract 1, no version bump |
  | D14 | **Changed:** bottom-edge swipe dropped |
  | D15 | First-run hint toast, shown once |

- **2026-10-14: apps get a second list, `cp0::ui::CompactList`; the Roller stays the Settings menu look.** Owner
  feedback on System Monitor (deck): the roller shows 2-3 big rows, centres the first item and its orange chevrons
  overlap content and read as page jumps. CompactList is top-aligned (scrolls only to keep the highlight and one row of
  context in view), has no chevrons, a thin auto-hiding scrollbar, two densities (Compact: 5.0 mm `LIST_ROW` token,
  about 7.5 rows on the deck; Comfortable: the roller's 7 mm row), sections, value tones, in-place live refresh, a
  read-only mode and an empty state. Roller for few big items (menus, Settings), CompactList for data and app lists
  (8+ rows). Compact rows are 5.0 mm, below D5's 7 mm list-row rule on purpose: they are for dense data lists, where a
  tap on an unselected row only moves the highlight, so a mis-tap never activates anything; anything whose rows
  trigger an action uses Comfortable (7 mm, roller-sized). No edge fade; Page Up / Down are LVGL focus keys and do not
  reach the list.

- **2026-10-13: the minimum resolution for new work is 640x480 (the deck); app guidelines come first.**
  - 480x320 (the Pi 3A+) is too small for the apps the project wants to build. New apps and new launcher screens
    are designed for 640x480 and up. The launcher keeps working at 480x320 as it does today (nothing is removed, no
    regression), but 480x320 is best effort: a new app declares `X-MinSize=640x480` by default (hidden on the
    Pi 3A+) unless it is cheap to support there (for example a text-mode app in the native terminal).
  - If making something work at 480x320 needs heavy effort or rework, the project acts on this decision and does
    not do it. The Compact class and its goldens stay as they are until they cost something.
  - Before the first new apps: an app guidelines document (`docs/APP-GUIDELINES.md`) that pins down the UI elements,
    colours, fonts, size requirements and rules every LVGL app follows so it runs on all boards (extracted from the
    launcher's tokens and widgets), plus a template app that follows it.
- **2026-10-05: Settings > Apps decisions (task 006).**
  - The home grid order is stored in the launcher's own config (`app_order` in `~/.config/cardputerzero/config.json`,
    FNV-1a keys of the `.desktop` names), because the Store backend (`projects/AppStore` submodule) cannot be changed.
  - Versions are compared the Debian way (`dpkg --compare-versions` semantics), never as text.
  - Sync failure reasons come from a curl probe of the source, because `--edit-registry` exits 0 for an unreachable
    source (it prints `REGISTRY UPDATED ... cached`). A source counts as synced only on a positive `ok`. The probe
    needed `execvp` in `run_program`: `execv` does not search PATH, so a bare `curl` never started.
  - After a failed install the backend's `pending-package.json` is parked aside
    (`pending-package.parked.<id>.<action>.json`) so other apps install, and it is restored before a retry of the
    same app, which then resumes. Deleting it would break that retry. After a wrong password or Esc the file may stay
    until the next operation; it blocks nothing.
  - Letter shortcuts compare the physical key code, not the ASCII value. W, E, R, T have evdev codes 17 to 20, equal
    to LVGL's up/down/right/left, so a real W/E/R/T press is recorded and the matching native key within 300 ms is
    ignored (same approach as Wi-Fi Survey). Real arrows and F/X/Z/C still work.

- **2026-10-04: a touch screen is found by its capabilities, never by a fixed `/dev/input/eventN`.** The numbers move
  (the RTL-SDR dongle's infrared node and the Bluetooth keyboard take event1 or event2). viz1090 0.1.1 scans
  `event*` for a device with multitouch X/Y and no keyboard keys, prefers a name with "goodix" or "touchscreen", and
  retries every second. `VIZ_TOUCH` stays as an override. Any new app that reads input does the same.
- **Fixed 2026-10-04: `deck.py sudo` exposed the sudo password.** It built `echo '<pw>' | sudo -S ...` as the
  remote command line, so `ps` on the deck, or a shell error that echoes the command, showed it (it happened twice in
  report 006; nothing was written to a file). Now the command is `sudo -S -p '' sh -c <shlex-quoted command>` and the
  password is written to the ssh channel's stdin, never in the command text (checked: `ps` on the deck shows no
  password). The password from those two runs should still be rotated if the tool logs of those runs are kept. Also: `deck.py put` with a Git Bash path
  such as `/tmp/x` is rewritten by MSYS, use `MSYS_NO_PATHCONV=1`.
- **2026-10-04: apps run in the launcher's 640x340 window**, under the shared top bar (320x170 drawing scaled 2x),
  not in X-Fullscreen. Decided by the user for Wi-Fi Survey. Full-screen stays an opt-in for apps like viz1090.
- **2026-10-04: a detail view follows the item's identity, not its list position.** Wi-Fi Survey tracks the access
  point by BSSID; if it vanishes the title shows "- gone" with the last values, and it recovers when it returns.
  R only rescans on every screen (held R does not switch screens).
- **2026-10-04: lean agent setup**: Controller plus `launcher-dev`, `apps-dev`, `deck-verifier`, `docs-writer`.
  Pilot task: Wi-Fi survey. One agent per app only when apps are built in parallel.
- **2026-10-04: apps live in the apps repo**, installed from Settings > Apps (user-added sources only). The launcher
  gets generic capabilities only (full-screen flag, shared top bar).
- **2026-10-04: normal boot lines stay visible.** No `quiet` / `fbcon=map` on the boot line. The launcher service
  waits for `systemctl is-system-running --wait` so nothing prints over the grid.
- **2026-10-16: list rows get an optional affordance (`RowAffordance`), amending the 2026-10-14 "no chevrons" line for tagged rows only.** Owner feedback on Serial Console (Pi 3A+, touch only): nothing told actionable rows from static information. Info = grey Muted value, no chevron; Open = chevron; Edit = value kept plus chevron; Action = a filled dot in the chevron's slot, white while idle (`Action`), the accent blue #5AAAFF while it runs (`ActionRunning`); the Action label is not coloured any more (owner, same day, after the first accent-label version: the dot keeps values in one column and shows the running state). Untagged rows render exactly as before; apps tag rows per PR.
- **2026-10-03: the built-in CardputerZero Hub stays in the Store**, not in Settings > Apps.
- **2026-10-03: no root-free package installs.** Installing from Settings > Apps asks for the sudo password.
- **Releases and pushes need the user's explicit go-ahead.**
- **ADS-B Radar (native LVGL) was dropped on 2026-10-05**; viz1090 is the ADS-B app. Source archived outside the repos.
