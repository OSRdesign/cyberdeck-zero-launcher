# 013 Mesh Hop phase 1 - hand-off (apps-dev)

```
Task: 013 mesh-hop phase 1 (brief 010)   Agent: apps-dev   Result: done (built and checked on the PC; NOT yet run on the deck)
Changed: new package apps/mesh-hop in the apps repo (untracked, nothing committed); the launcher repo is untouched
         (only its local, untracked .git/info/exclude got two scratch-build lines, see "How it was built").
How it was checked: unit tests (core 260, simulator 30, UI logic 155 checks: all pass), x86 headless build run against
         the simulator with 19 state dumps and 23 screenshots, aarch64 build + .deb inspected. Not run: the aarch64 binary,
         the real framebuffer, the touch screen, the Bluetooth keyboard, a real board.
Not done / risks: see "Risks" below.
Needs from the Controller: nothing blocking; two questions at the end.
```

## What was built

`apps/mesh-hop` (id `mesh-hop`, title "Mesh Hop", 0.1.0, `"draft": true`, MIT). `apps/meshzero` was not modified.

| Path (all under `C:\CLAUDE\zero7\cyberdeck-zero-apps\apps\mesh-hop`) | What |
| --- | --- |
| `app.json`, `icon.png`, `README.md`, `root/` | metadata (depends `libfreetype6`), icon (64x64, new), `.desktop` with `X-Fullscreen=true`, `copyright`, the aarch64 binary `usr/share/APPLaunch/bin/M5CardputerZero-mesh-hop` (1.9 MB, stripped) |
| `src/main/core/` | copy of the MeshZero core (transport, protocol, client, model, store, clock policy, input_state, sha256, deck clock) with three edits only: the environment variables and the data folder are `MESHHOP_DATA`, `MESHHOP_PORT`, `~/.local/share/mesh-hop`; the one-time migration of the old `meshcore` folder was removed from `Store::default_dir()`. Namespace `meshzero` kept. |
| `src/main/ui/ui_logic.{hpp,cpp}` | UI-independent logic, unit tested: key translation (evdev to text, US and AZERTY), editor filters, formatting, chat rows, contact rows and sorting, settings edits, the Back rule (`back_action`), the left-edge swipe (`EdgeSwipe`), delivery-status texts, footer board line |
| `src/main/ui/platform.{hpp,cpp}` | LVGL display on `/dev/fb0` (XRGB8888, also RGB565), touch and keyboard read from evdev (same device discovery as the viz1090 bridge: touch found by capabilities, keyboard `/dev/input/bt-keyboard` with a scan fallback), headless mode (memory, scripted input, PNG screenshots) |
| `src/main/ui/app.{hpp,cpp}` | the screens |
| `src/main/ui/cp0_statusbar.[ch]` | the launcher's shared top bar, byte-identical copy (verified with diff, CRLF ignored) |
| `src/main/ui/png_writer.*` | tiny PNG encoder for the screenshots |
| `src/main/src/main.cpp` | start-up, SIGTERM/SIGINT handling, the script runner of the headless mode |
| `src/tests/` | `test_core.cpp`, `test_sim.cpp` (as in MeshZero), new `test_ui.cpp`, `run_tests.sh`, `ui_smoke.sh`, `smoke.script`, `offline.script` |
| `tools/meshcore_sim.py` | the simulator (copy); only change: the contacts now have different positions (Alice, Bob and the repeater around Paris, the room none) so that the distance column has data |
| `build/build.sh` | aarch64 build (`build.sh`) and x86 host build (`build.sh host`) |

Screens (640x480: top bar 56 px, page 400 px, footer 24 px; screenshots in `C:\CLAUDE\zero7\pkg-out\mesh-hop-screens\`):

* **Chats**: left pane 204 px: CHANNELS (`# Public`, `# test`, ...) with last message and unread badge, `+ Add channel`, DIRECT with unread; right pane: title, "n nodes heard (1 h)" (or "n left" while typing), messages with sender, time and delivery status (`sending / sent / delivered / no ack / failed: ...`, check marks when DejaVu is present), a 2-line entry box and a **Send** button; **Remove** button for a channel added by the app (asks twice).
* **Contacts**: toolbar (Advert, Zero-hop, Refresh, Sort), column titles (tap = sort), table name / type / heard / SNR / hops / distance (haversine, when both positions are known), detail panel for repeaters, rooms and sensors (Message button only for chat nodes). Enter on a chat node opens its conversation with the keyboard on the entry (the conversation is listed even before it has a message).
* **Settings**: board (connection, model and firmware, battery, clock source and time), node name, frequency, bandwidth, SF, CR, TX power (rows with `-` / `+` touch buttons, Enter types the value), Save to radio, Undo changes, Preset EU 868, Sync clock now, channel list (Remove for the app's own) and `+ Add hashtag channel`, `Add the Public channel` when slot 0 is empty. The clock prompt of the policy (deck offline, board without GPS) opens the typed-time editor.
* **Map** / **Terminal**: placeholders ("phase 3" / "phase 2").
* Footer: hints for the current screen or the last notice; right side: `board: <model>  OK` with a coloured dot (green connected, gold connecting, red denied / busy / wrong firmware, grey not found).
* Modal editor (node name, numbers, channel, clock): title, hint, entry, Cancel / OK; numbers and the clock replace the old value when you start typing. Without a keyboard it does not open and says "Keyboard needed: wake the Bluetooth keyboard".

Fixed rules: no on-screen keyboard; short Esc = `back_action()` (editor, then detail, then entry-to-list, else only the hint "Hold Esc 3 s to exit") and the repeat of a held Esc is ignored; the 3 s hold is the launcher's (it ends the app with SIGTERM; the app exits its loop cleanly on SIGTERM / SIGINT / SIGHUP). Touch: tap, drag-scroll (LVGL), swipe from the left edge (28 px, 56 px travel) = Back, buttons; every action also has a key. Rows are never rebuilt under a finger, and the click at the end of an edge swipe is ignored.

## How it was built

* Unit tests: `wsl -e sh /mnt/c/CLAUDE/zero7/cyberdeck-zero-apps/apps/mesh-hop/src/tests/run_tests.sh` -> `260 checks, 0 failed`, `sim: 30 checks, 0 failed`, `ui: 155 checks, 0 failed`.
* aarch64: `wsl -e sh .../apps/mesh-hop/build/build.sh` (copies `src/` into the scratch project `launcher/projects/MeshHop`, `flock /tmp/wsl-build.lock scons`, strips and installs the binary). Host: `build.sh host` (scratch project `projects/MeshHopHost`, new config `linux_x86_host_config_defaults.mk`, binary `/tmp/mesh-hop-host/mesh-hop`; WSL clears `/tmp` now and then, rebuild when it is gone). The script appends `projects/MeshHop/` and `projects/MeshHopHost/` to `launcher/.git/info/exclude` (local file, same as the MeshZero script); no git command was run from WSL.
* Build facts worth knowing: the app does **not** link the `cp0_lvgl` runtime (it owns display and input), so `libinput` / `libxkbcommon` are not needed (`-Wl,--as-needed`; NEEDED = libstdc++, libfreetype, libm, libc, libgcc_s); the SDK compiles **without any `-O` flag** by default (seen on the host compile line), so the app adds `-O2` for itself and for LVGL; build warnings: none.
* Package: `python tools/make_registry.py --only mesh-hop --out C:/CLAUDE/zero7/pkg-out --owner OSRdesign --repo cyberdeck-zero-apps` -> **`C:\CLAUDE\zero7\pkg-out\mesh-hop_0.1.0_arm64.deb`** (925 394 bytes, md5 `5123541ae75dce3bb7b7f090b638d665`, made from the last aarch64 build). `dpkg-deb -I/-c` checked: `Depends: libfreetype6`, files as listed above. `git status` of the apps repo: only `?? apps/mesh-hop/`; `registry.json` and `packages/` untouched.
* Memory: x86 headless run with the simulator (4 contacts, all tabs visited): VmRSS = VmHWM = **8.7 MB**. The deck number has to be measured (below).

## Decisions

1. **Own platform layer instead of the `cp0_lvgl` runner.** The runner builds the dpi-scaled two-display backend and starts the launcher services; in a full-screen child that would fight the launcher for the panel. Mesh Hop draws with LVGL straight into `/dev/fb0` and reads evdev like the viz1090 bridge does. No launcher capability was needed. The launcher's own Esc watcher (`cp0_external_app_runner`, `ExecBlocking` path of a full-screen launch) still ends the app after 3 s.
2. **Top bar**: the shared renderer draws into a 640x56 ARGB overlay (refreshed every 2 s) on top of the five tabs, which sit in the left 420 px of the bar (clock pill, Wi-Fi and Bluetooth are on the right as in the home grid).
3. **Fonts**: Montserrat 14/16/18 built into LVGL (same family as the launcher); message text, names and values in DejaVu Sans 16/14 through FreeType (accents, check marks) from `/usr/share/APPLaunch/share/font` (installed by the launcher), falling back to Montserrat. The fonts are not shipped in the package.
4. **Keyboard layout**: a small table (US, and AZERTY when `MESHHOP_KEYMAP=fr` or `/etc/default/keyboard` says `fr`) instead of xkbcommon; see question 2.
5. **Contacts** got hops and distance columns and a sort (tap a title or `S`, `R` reverses) beyond the brief's list.
6. The two-pane list shows the selected conversation at once (select = shown and marked read); Enter / typing moves the keyboard to the entry, Esc back to the list.
7. Hashtag channel add and remove, advert, clock policy and Settings behave as in MeshZero 0.1.0 (same core calls, same validation).

## What `deck-verifier` can test over ssh (no panel, no password needed from me)

The headless mode renders in memory, so it can run on the deck while the launcher owns the panel (it never opens `/dev/fb0` or the input devices).

1. Install the `.deb` (Settings > Apps flow or `deck.py install`); check `dpkg -s mesh-hop`, the tile appears with the icon, `readelf -d` shows only libstdc++, libfreetype, libm, libc, libgcc_s (they must all resolve: `ldd /usr/share/APPLaunch/bin/M5CardputerZero-mesh-hop`).
2. Copy `src/tests/ui_smoke.sh`, `smoke.script`, `offline.script` and `tools/meshcore_sim.py` into one folder on the deck and run:
   `MESHHOP_BIN=/usr/share/APPLaunch/bin/M5CardputerZero-mesh-hop SHOTS=/tmp/mh-shots sh ui_smoke.sh`
   Expected: exit code 0, 19 `state ...` lines that match the `say == ... (expect ...)` labels of `smoke.script` (they are the expected values; the PC run is in `C:\CLAUDE\zero7\pkg-out\mesh-hop-screens\smoke-dump.txt`), PNG screenshots in `SHOTS` equal in layout to the ones in `pkg-out\mesh-hop-screens\` (the clock digits differ). Offline case: `DATA=/tmp/mh-data` for a first run, then `NOSIM=1 DATA=/tmp/mh-data SCRIPT=offline.script sh ui_smoke.sh` (history and contacts come back, send is refused with "Not connected to a radio").
3. RSS and CPU of the headless run: start the script with a long `wait`, read `/proc/<pid>/status` (VmRSS target well under 60 MB; the PC value is 8.7 MB) and `top` (idle CPU should be a few percent; the loop wakes every 15 ms).
4. SIGTERM: `kill -TERM <pid>` ends the app within a fraction of a second, exit code 0 (the launcher's Esc hold relies on it).
5. The history folder: `~/.local/share/mesh-hop` (or `MESHHOP_DATA`) gets `messages.jsonl`, `contacts.jsonl`, `channels.jsonl`, `read.txt`; `~/.local/share/meshzero` is not touched.
6. Optional, with the real service stopped by the operator and only when allowed: starting the app for real from the tile and a framebuffer screenshot with `deck.py shot` (that is the first run of the real display path; see the user list below).

## What needs the user's hands (real deck, panel and board)

* The look on the real panel: colours, the top bar overlay (clock pill, Wi-Fi, Bluetooth glyph: the PC run has Bluetooth off so the glyph was not seen), text sizes, no residue after leaving the app (the launcher repaints on return), start and exit by tile and by Esc hold.
* **Touch**: tap the five tabs, conversation rows, Send, the entry box, the `-` / `+` buttons, contact rows, column titles; drag-scroll the lists and a long conversation; swipe from the left edge (Back); check that a tap never acts twice and that a drag never opens anything; touch axes (same defaults as the launcher: swap, invert Y).
* **Bluetooth keyboard (M4)**: typing a message with accents (layout!), Backspace, Left/Right, Enter to send, Tab and F1..F5, Up/Down in the lists, Esc short (hint only on a top-level tab, Back elsewhere), Esc hold 3 s (quit), wake / sleep of the keyboard while the app runs (the "Keyboard needed" notice and the entry hint).
* **Real board** (Seeed XIAO nRF52840): connect, contacts and history sync, send and receive in a channel and a direct chat, delivery status changes, unread counts, SNR / hops / positions of real contacts, the Settings edits and Save, Preset EU 868, Sync clock now, the clock prompt (deck offline), add and remove a hashtag channel (writes the board's channel table), unplug and replug.
* Memory and responsiveness on the Pi Zero 2W (draw speed with `-O2` at 640x480, scrolling smoothness).

## Launcher capabilities needed

None for phase 1. Observations for `launcher-dev` (no request):

* `docs/dev/deck/check_statusbar_drift.py` checks only `apps/viz1090/build`; add `apps/mesh-hop/src/main/ui` to its `COPIES`.
* The SDK builds without optimisation (no `-O` on the compile lines of the host build I ran; not checked for the launcher's own cross build): if the launcher binary is built the same way, `-O2` would speed up its drawing too.
* Phase 2 (emoji spike) will want the FreeType glyph cache size and colour-bitmap support checked on the deck; nothing to do now.

## Deviations from the brief

* Map and Terminal are placeholders, as allowed. Emoji, terminal, map, route view and polling are not started.
* Contacts has two extra columns (hops, distance) and the simulator was given contact positions (the brief puts that in phase 3; it was needed to test the distance column).
* Keyboard layout table instead of xkbcommon (see decision 4).
* "RSS" was measured on x86 in headless mode, not on the deck.
* The `core/` copy was edited in three places (names of the environment variables and the data folder, migration removed) rather than copied byte for byte; its behaviour and tests are otherwise identical.

## Risks

* The aarch64 binary has not been executed anywhere; the framebuffer, evdev touch, evdev keyboard and `/dev/input/bt-keyboard` paths were only compiled (they follow the viz1090 bridge closely). First run on the deck may show a black or wrongly oriented panel (`MESHHOP_FB`, `MESHHOP_TOUCH`, `APPLAUNCH_TOUCH_SWAP_XY/INVERT_X/INVERT_Y` override the discovery).
* The edge swipe starts within 28 px of the left edge, where list rows also start: a slow drag from the edge that stays under 56 px is a normal tap or drag.
* A very long conversation shows its last 60 messages only (history on disk is bounded by the core: 200 per conversation).
* The status bar is read from sysfs every 2 s; a missed second-boundary shows the minute late.

## Questions for the Controller

1. Should the docs-writer add `mesh-hop` to the apps README and the pending physical tests list now, or only after the verifier's PASS?
2. Keyboard layout: is the user's M4 US or AZERTY? If it is something else, the layout table needs a third entry (or xkbcommon through the launcher's configuration).
