# 024 - Small native cyberdeck apps: platform, template, guidelines, plan

Status: read-only study written 2026-10-13, Controller decisions at the end. Nothing was built, committed or deployed for
it. Four read-only commands were run: `readelf`/`size` on the existing app binaries, `check_statusbar_drift.py`, and file
searches.

Markers: **V** = verified in code or by a command, **I** = inferred, needs a board. Code was read at launcher `main`
2b8cb96 (the working-tree files read are identical to main) and apps repo dca4516. The user's 2026-10-13 decision is
applied throughout: new apps need at least 640x480, declare `X-MinSize=640x480`, and text-mode apps may still run at
480x320.

## 0. Summary
- **Platform: (a) native fullscreen apps linking `cp0_lvgl`.** Such an app already gets the launcher's own display backend, tokens, widgets and the on-screen keyboard (OSK) inside its own process. Four small pieces of glue are missing, and nobody has run this path in a child process yet, so a spike comes first.
- **Compat mode is a dead end** for responsive apps: it always draws 320x170 and the launcher cannot show a keyboard over it.
- **Built-in launcher pages (c) are ruled out** by the role rule that apps live in the apps repo.
- **Terminal-wrapped apps (b)** are right only for the serial console. It also works on the Pi 3A+ at no extra cost.
- **Shared code:** one source folder `apps/_common` plus a draft app `apps/cyberdeck-app-template`. No copies of launcher widgets; LVGL apps link `cp0_lvgl` statically from a pinned launcher commit.
- **One small launcher task** moves the LVGL status strip and title into `cp0_lvgl`.
- **Status bar drift exists today:** `check_statusbar_drift.py` exits 1, and it does not check the Mesh Hop copy at all.

## 1. Platform question

**What an X-Fullscreen app linking `cp0_lvgl` gets today:**
- **Display backend (V).** A fullscreen child inherits the whole launcher environment, with only the contract variables added in front (`launch.cpp:203-206`, 023:12). That includes `APPLAUNCH_DISPLAY=dpi-scaled` (`APPLaunch.service:7`) and the board settings in `board.conf` (`EnvironmentFile`).
  - `cp0_lvgl_freambuffer.c:57-62` then picks the same backend as the launcher. It reads the framebuffer, rotation, logical size, panel mm and touch orientation (`cp0_lvgl_dpi_scaled.c:733,769,781,815,834`), draws through `cp0_fb_output` (16/32 bpp, rotation), and computes the same metrics (`:831`).
  - It also creates a native display at the panel's logical size (`:857`).
  - Its `screen.state` write is skipped because `APPLAUNCH_APP_MODE` is set (`cp0_ui_state.c:130`).
- **Widgets (V).** Page (header, title, status line, on-screen Esc), Roller (Nav, Choice, Toggle, Slider, Info, Network and Busy rows), InfoTable, TextBlock, TextField (including multiline), Dialog, OnScreenKeyboard and the `osk` service are all in `cp0_lvgl/src` (`cp0_ui_widgets.hpp:9-55`). Any app linking the component gets them; LanScan already links it (`lanscan/src/main/SConstruct:13`).
- **On-screen keyboard (V).** It is reusable in-process: `osk::set_policy(fn)` exists, and without a policy the keyboard never shows (`cp0_ui_widgets.hpp:471-473`). The app sets the policy to `cp0_app_osk_allowed()` reading `input.state`, the same rule the launcher uses (APP-CONTRACT:229-235).
  - Memory cost is under 0.5 MB (020:258).
  - So the answer to "is there a reusable app-side OSK?" is **yes**. It just is not listed in APP-CONTRACT §9, because that list only covers pure-C files.

**The four missing pieces of glue:**
1. **Switch to the native display.** The backend starts on its 320x170 compat display (`dpi_scaled.c:871`); the app must call `cp0_display_set_mode(CP0_DISPLAY_MODE_NATIVE)`.
2. **Read every keyboard.** The child inherits `LV_LINUX_KEYBOARD_DEVICE=/dev/input/bt-keyboard` (`APPLaunch.service:11`). It must call `cp0_keyboard_set_read_all_keyboards(1)` before init (`keyboard_input.h:92`; `cp0_lvgl_keyboard.c:67-69,729`), otherwise USB keyboards and the HackBerry's built-in one are not read.
3. **Status strip and title.** Their LVGL wrapper lives in the launcher (`native_ui.cpp:237-330,741-765` at main), not in `cp0_lvgl`. The renderer `cp0_statusbar.c` is in `cp0_lvgl` and is linked.
4. **Root exit.** Keys from the app's own OSK do not reach the launcher's hold-Esc watchdog, which reads the evdev nodes (`cp0_external_app_runner.cpp:65`, I). A touch-only user therefore needs the header Esc to quit at the app's root.

**Not verified until a board run:** whether the shared touch device behaves while the launcher's timers are off (`launch.cpp:202`), and whether `input.state` is rewritten while a fullscreen app runs (023:61, marked I there).

| | (a) native fullscreen with `cp0_lvgl` | compat (LanScan model) | (b) terminal app (`Terminal=true`) | (c) built-in launcher page |
|---|---|---|---|---|
| Responsive | yes, uses tokens and the real canvas | no, 320x170 fixed (APP-CONTRACT:239-244) | text grid follows the screen (`ui_app_st.cpp:93-109`) | yes |
| OSK | app-side, same widget and rule | none (F1 phase 2 not done, `kb_inset=0`) | free, from P4b (014:276) | free |
| Touch | full | toolbar and gestures | drag scrolls the scrollback only; no mouse modes (parser handles only `?1049`) | full |
| Look | launcher look | scaled-up stock look | fixed 20 px monospace, no status bar | launcher look |
| RAM | about 13-15 MB per app (Wi-Fi Survey, a cp0_lvgl app: 13 MB, report 003:39), on top of the launcher's ~20 MB that stays loaded; the deck has 414 MB (010:34) | same | about 1-2 MB for picocom (I) | grows the always-loaded launcher |
| Owner | apps repo | apps repo | apps repo | launcher repo: violates `launcher-dev.md:14` |

**Recommendation per app:**

| App | Model | Why |
|---|---|---|
| System monitor | (a) | Read-only roller and table pages plus small charts, no typing. The ideal first app to prove the template. |
| Serial console | (b) for v0.1 | picocom gives a full terminal, scrollback and the P4b OSK at no cost, and runs on the Pi 3A+. A native version needs the launcher's terminal view (`ui_app_st*`, about 2,000 lines in the launcher) moved into `cp0_lvgl`: defer to v0.2. |
| Network toolbox | (a) | Forms with a target field and the OSK, live results, touch lists. |
| Notes | (a) | Typing-heavy, so look and OSK matter. Needs a new multi-line editor widget. Using nano in the terminal is too cramped (014:278). |

With the 640x480 minimum, the native apps use `X-MinSize=640x480` and are hidden on the Pi 3A+. The 480x320 layout and the 480x320 OSK case (only 5 terminal rows) stop being constraints. What still has to work: 640x480 with the OSK on a touch-only deck, 800x480 (Pi 5 HyperPixel, rotated 90°), 720x720 square (HackBerry, which forces the OSK on: APP-CONTRACT:141) and 1280x720.

## 2. What exists vs what must be built

| Item | Exists (V) | To build | Size (lines) |
|---|---|---|---|
| Contract reader, metrics, fb output | `cp0_app_contract`, `cp0_ui_metrics`, `cp0_fb_output` (APP-CONTRACT:183-191) | nothing; the backend already uses them | 0 |
| Widgets and OSK | `cp0_ui_*` (about 5,200 lines; harness Makefile:56-90 lists the subset) | `_common/osk_policy`: re-read `input.state` when `gen` changes, at most once a second | 60 |
| Platform start-up | `cp0_lvgl_run` (runner) | `_common/platform`: read-all keyboards, native mode, root exit, "needs 640x480" notice for older launchers (APP-CONTRACT:152) | 300 |
| Status strip and title | renderer in `cp0_lvgl`; wrapper in the launcher | **L1** move into `cp0_lvgl` as `cp0::ui::StatusStrip` (launcher-dev); without L1, a copy in `_common` (drift) | about 300 moved |
| Background commands | — | `_common/async_run`: fork and exec with a timeout, line callback, back onto the LVGL thread with `lv_async_call`, kill the process group on exit | 250 |
| Multi-line editor | TextField multiline is a single row (`cp0_ui_text_field.cpp:346,367`) | `_common/text_editor` (later a `cp0_lvgl` candidate) | 500 |
| Host harness | launcher render harness (Makefile, `objcopy` fake framebuffer); Mesh Hop headless mode and scripts | `_common/headless`: plain LVGL display at WxH, `cp0_ui_metrics_compute`, scripted keys and taps, PNG output, layout checks; Makefile using the launcher's LVGL and the UI subset (needs the SDK downloaded: Makefile:24-26) | 700 |
| Build | copy-in plus `flock scons` (`lanscan/build/build.sh:17-34`), host mode (Mesh Hop) | `_common/build_app.sh` (cross and host, strip, launcher pin check, version stamp); per-app `build.sh` about 10 lines | 200 |
| Template app | — | `apps/cyberdeck-app-template` (`"draft": true`) using every widget | 300 |
| CI | none in the apps repo; launcher CI only runs on `master` and `ci/**` (`launcher-build.yml:5`) | optional: Action that pins the launcher, cross-builds, runs host tests and layouts, uploads contact sheets | 120 |

**Copy and drift policy:**
1. LVGL apps link `cp0_lvgl` and LVGL statically. Verified with `readelf`: the only shared libraries they need are libinput, xkbcommon, freetype and libstdc++. So there is no runtime coupling; the contract is the interface.
2. Pin the launcher commit in `apps/_common/LAUNCHER_REF`. `build_app.sh` warns or fails when the checkout differs, and stamps the commit into `--version` and the package's doc file. Rebuild all apps when the pin moves.
3. No widget or status bar copies in LVGL apps after L1. Fonts are read at run time from `/usr/share/APPLaunch/share/font` and never bundled.
4. Source copies stay only for the non-LVGL apps (viz1090 shim, Mesh Hop). Fix the drift today (**H1**): both copies lack `cp0_statusbar_render_scaled`, and the drift script's `COPIES` list at line 22 only covers viz1090.
5. Each app binary is about 2.5-4 MB (Mesh Hop stripped 2.5 MB; LanScan unstripped 4.2 MB). The common code is duplicated on disk; that is acceptable because there is no ABI promise for a shared library (APP-CONTRACT:17).

**Effort under the 640x480 minimum:** the template drops from about 2,000 to about 1,700 lines. That is 4 layout sizes instead of 5, no Compact class, and the OSK checks only at 640x480 and above.

## 3. Per app (v0.1)

**System monitor** — savvy-medium, about 1,100 lines with tests
- **Screens:** an Overview, plus detail pages for CPU and temperature history (sparkline, LVGL `lv_chart`), Disks, Network and Throttling.
  - Overview shows: CPU % and load, RAM and swap, temperature, throttling now and since boot, uptime, `/` usage, Wi-Fi (SSID, signal, IP), Bluetooth (on, connected).
  - Throttling page decodes the bits: under-voltage, frequency capped, throttled, soft temperature limit.
- **Data:** `/proc/stat`, `loadavg`, `meminfo`, `uptime`, `statvfs`, `thermal_zone*/temp`. Throttling from `vcgencmd get_throttled` (video group) or the firmware sysfs node (I, path per board; hide the row when neither exists). Wi-Fi and Bluetooth read the same way as `cp0_statusbar.c:90-125`; SSID and IP from `nmcli -t`.
- **Permissions:** no sudo. **Packages:** none required; vcgencmd optional, degrades if missing.
- **Risks:** different throttling source on the Pi 5 / CM5 (I); keep polling at 1 Hz with near-zero idle CPU.
- **Controller designs:** Overview as tiles or an InfoTable; chart look; warning and error thresholds; refresh rate.

**Serial console** — model (b), savvy-light, about 250 lines of shell plus tests
- `Terminal=true` with `Exec=` a POSIX sh menu, no `X-MinSize`, so it runs on all boards including the Pi 3A+.
- **Menu:** ports from `/dev/serial/by-id` mapped to `ttyUSB*`/`ttyACM*`, numbered so the OSK digits work; baud presets 9600 to 921600 (default 115200); 8N1/7E1; line-ending mapping; remembers the last choice in `~/.config`; then `exec picocom` (optionally `--logfile`).
- **Exit:** Ctrl-A then Ctrl-X, typable on the OSK because Ctrl is one-shot (014:276).
- **Permissions:** the user must be in the `dialout` group (as Mesh Hop requires); the menu says so if not. **Package:** `picocom` (I: in trixie).
- **Risks:** the OSK has no F-keys, Home/End or Page keys (014:278); no touch input in the terminal; with the OSK up the grid is 53x6 at 640x480 and 40x5 at 480x320.
- **Test:** a host run against a `socat` PTY pair.
- **Controller designs:** menu text and defaults.

**Network toolbox** — savvy-medium to heavy, about 1,800 lines
- **Screens:** a tool list Roller (Ping, Traceroute, DNS, Speed test, BLE scan). Each tool page has a target TextField, Run/Stop, a live log and a summary InfoTable, plus recent targets.
- **Data:**
  - `ping`: unprivileged via `ping_group_range` (I).
  - `traceroute -n -q1`: UDP, no root; `-I` would need root, so avoid it.
  - DNS: `getaddrinfo` for A/AAAA/PTR plus the resolver list.
  - Speed test: curl (already a Mesh Hop dependency) against a configurable endpoint.
  - BLE scan: parse `bluetoothctl --timeout N scan le`. Not through `cp0_bluez`, which registers a BlueZ agent at init (`cp0_bluez_dbus_client.cpp:982`) and would compete with the launcher's.
- **Packages:** `iputils-ping`, `traceroute`, `curl`, `bluez` (I: most already installed).
- **Risks:** a BLE scan may disturb the Bluetooth keyboard or Wi-Fi on the Zero 2 W's combined radio chip (I): short scans plus a warning. Parse with `LC_ALL=C`. Kill child processes on exit. Privacy of a third-party speed endpoint.
- **Controller designs:** tool list; result layout per tool; speed test experience; BLE list columns.

**Notes** — savvy-heavy for the editor widget, medium for the rest; about 1,500 lines
- **Screens:** a list (Nav rows sorted by modified date, a "+ New note" row), a full-page editor, and Dialogs for rename and delete (with confirmation).
- **Files:** `~/Notes/*.md` as UTF-8; save atomically (temporary file then rename); autosave after 2 s idle and on exit.
- **OSK room:** at 640x480 the OSK is 300 px high, leaving 180 px; minus the 56 px header that is about 4 text lines. On the 720x720 HackBerry the OSK is always shown and 360 px high.
- **Permissions:** none. **Packages:** none.
- **Risks:** `lv_textarea` speed on long text on the Zero 2 W (I) → cap file size; cursor handling in UTF-8.
- **Controller designs:** list and editor look (Montserrat or JetBrains Mono); whether the header hides while the OSK is up; Markdown preview (suggested for v0.2).

## 4. Task split (briefs in `docs/dev/tasks/015+`)

| # | Task | Agent / tier | Depends on | Owns | Definition of done |
|---|---|---|---|---|---|
| S0 | Spike: a minimal template running fullscreen on the deck and HackBerry (glue pieces 1-4) | apps-dev, savvy-heavy | — | `apps/_common`, template | physical tests T1-T8 pass |
| L1 | Move StatusStrip and title into `cp0_lvgl`; doc updates: packaging checklist item 3 (`X-MinSize=640x480` by default), apps README:61, apps-dev agent "same 640x480 look", HOSTING-APPS | launcher-dev, savvy-medium | — (runs in parallel with S0) | `cp0_lvgl`, `native_ui.cpp`, docs | harness goldens byte-identical; `check-all` green |
| T1 | Finish the template: async runner, headless harness, layout checks, `build_app.sh`, launcher pin | apps-dev, savvy-heavy | S0, L1 | `apps/_common`, template | host tests green; contact sheets at 4 sizes; aarch64 build |
| H1 | Resync the status bar copies; add Mesh Hop to the drift check; Mesh Hop `X-MinSize=640x480` (R0 becomes final, R4 dropped) | savvy-light | — | viz1090, Mesh Hop, drift script | drift check exits 0 |
| A2 | Serial console | apps-dev, savvy-light | — | `apps/serial-console` | socat test; Pi 3A+ and deck tests |
| A1 | System monitor | apps-dev, savvy-medium | T1 | `apps/sysmon` | fixture tests; layouts; deck test |
| G1 | APP-GUIDELINES.md (§5) | docs-writer | T1, A1 | `apps/docs/APP-GUIDELINES.md` | facts match the template's tests |
| A3 | Network toolbox | apps-dev, savvy-medium to heavy | T1 | `apps/nettools` | parser tests; layouts; deck test |
| A4 | Notes (editor in `_common`) | apps-dev, savvy-heavy | T1 | `apps/notes`, `_common/text_editor` | editor unit tests; OSK scenes |
| C1 | Apps CI (optional) | savvy-medium | T1 | `.github/workflows` | green run |

Order: S0, L1, H1 and A2 in parallel; then T1; then A1 and G1; then A3; then A4.

**Test approach:**
- Host unit tests for each app's pure logic in `core/` (fixture files from `/proc` and `/sys`, command output samples).
- Headless layout runs at 640x480, 800x480, 720x720 and 1280x720 (plus 1024x600), with the OSK off and forced. Automated checks: everything on screen; interactive elements at least ROW / TARGET high; nothing covered by the header or the OSK; the "needs 640x480" notice below that size.
- Contact sheets for the Controller's review.
- Board tests are run by the user from a protocol, per the controller-led testing practice.

**First physical tests (S0):**
- T1. Deck: the template opens fullscreen; the status strip is pixel-identical to the home grid's.
- T2. Deck: Esc behaves correctly from the header, the BT keyboard and a USB keyboard; a 3 s Esc hold exits; the home grid returns intact; `screen.state` still says `w=640` with a higher `gen`.
- T3. Deck with the BT keyboard off or gone: tapping a field shows the OSK; typing and Enter work; when a keyboard returns the OSK hides within about 1 s.
- T4. HackBerry 720x720 (lightdm stopped): square layout; built-in keyboard types; touch is accurate; OSK always shown.
- T5. Pi 5 HyperPixel at 800x480 rotated 90°, if available: picture upright, touch mapped.
- T6. Pi 3A+: the template's tile is hidden; the serial console is visible and works.
- T7. VmRSS of the app under 25 MB; `free -m` with launcher and app running.
- T8. A console started over ssh without the launcher's environment degrades cleanly (APP-CONTRACT §7).

## 5. APP-GUIDELINES.md

**Location:** apps repo `docs/APP-GUIDELINES.md`, linked from the launcher's APP-CONTRACT.md and HOSTING-APPS.md. The template app should embody every rule, and its harness asserts the numbers below, so a launcher token change fails a test and forces a doc update.

**Outline:**
1. Platform rule: X-Fullscreen native app with `cp0_lvgl`; `X-MinSize=640x480`; landscape only (D12); classes standard, wide, square, large.
2. Contract use: environment variables, the two state files, degrade rules; never hard-code 640x480 or `/dev/fb0`.
3. Tokens per class (read them, never copy them; values from the harness's `out/<WxH>/runtime/applaunch/screen.state`; densities are the harness's assumptions):

| Size / class | px/mm | space s/m/l | radius s/m/l | header | row | target | dialog max width | text title/heading/body/caption | OSK height cap (014:79) |
|---|---|---|---|---|---|---|---|---|---|
| 640x480 standard | 11.30 | 8/16/20 | 8/18/28 | 56 | 79 | 102 | 560 | 32/28/24/18 | 62.5% = 300 |
| 800x480 wide | 9.21 | 8/14/20 | 8/16/24 | 56 | 64 | 83 | 600 | 32/26/22/18 | 50% |
| 720x720 square | 10.00 | 8/16/24 | 8/18/28 | 64 | 70 | 90 | 600 | 32/28/24/20 | 50% (keys grow) |
| 1280x720 large | 11.62 | 10/16/24 | 10/20/28 | 64 | 81 | 105 | 720 | 36/30/26/20 | 45% |

   Roller (014:63): row / centre text / value = 79/34/32 at 640x480, 64/28/26 at 800x480, 70/30/28 at 720x720, 81/36/32 at 1280x720. Width capped at 560 px (640 on Large) and centred; neighbouring rows 8% smaller and 20% fainter per step.

4. Colours (from the widget sources): background #000000; text #FFFFFF; secondary text #E0E0E0; labels #9A9A9A; values #00CC66; selection and check #2D9CDB; title #F0B400; centre bar #2B2B2B; chevrons #FF6A3D; buttons #2A2A2A (pressed #2D9CDB); primary #1F5F8A (pressed #2A7AB0); status OK #66CC88, warning #F0B400, error #FF6666; fields #1C1C1C with border #3A3A3A, placeholder #666666; Wi-Fi bars #33CC33 / #4D4D4D; OSK #111111, #333333, #1C1C1C, #EEEEEE, #E8E8E8.
5. Fonts: Montserrat Medium and Bold through `cp0_fonts()`, sizes snapped by `cp0_ui_font_snap`; JetBrains Mono or DejaVu Sans Mono for logs; FontAwesome 5 only through the status strip. All from `/usr/share/APPLaunch/share/font`.
6. Widget catalogue with when to use each (list in §1), plus the `_common` additions (editor, async runner).
7. Header and status strip: always use the shared strip (L1); never draw your own clock or icons.
8. Input: keyboard first; every action reachable by keys and by touch; touch targets at least TARGET (9 mm), rows at least ROW (7 mm); tap and drag as in the Roller; no gesture-only actions; on-screen Esc in the header.
9. OSK: set the policy to `cp0_app_osk_allowed`; never offer a button to summon it; at 640x480 only 180 px is left above it, so a text page is header plus one field; dialogs follow the keyboard (`keep_above_keyboard`).
10. Performance and RAM: under 25-30 MB RSS; idle CPU a few percent; blocking work on worker threads; no Python for long-running parts.
11. Permissions: no root at run time; required groups (dialout, video, bluetooth); the async sudo prompt only when unavoidable.
12. Packaging: `app.json` fields, `.desktop` keys, icon, screenshots (the 320x170 size is stale), `draft`, the libc6 floor, licence notice.
13. Testing: unit tests, the 4 layout sizes, degrade cases, the board protocol.
14. Do and don't list.

## 6. Open questions for the user
1. **Serial console v0.1 as a text-mode app** (numbered menu plus picocom, also runs on the Pi 3A+), with a native port picker only later? *Recommendation: yes.*
2. **Speed test method:** curl against a configurable public endpoint (no new package; the provider sees your traffic), or `speedtest-cli` (Python, about 25 MB of RAM, I), or leave it out of v0.1? *Recommendation: curl with a configurable URL.*
3. **Notes v0.1 as a plain text editor**, with Markdown preview in v0.2? *Recommendation: yes.*
4. **Approve L1**, a small generic launcher change that moves the status strip into `cp0_lvgl`, so apps carry no copies? *Recommendation: yes.*
5. **Guidelines in the apps repo**, and apps CI added after the template? *Recommendation: yes to both.*

Also for the Controller to decide: does a short Esc at an app's root quit the app? I recommend yes, as the Settings page does at level 1, because touch-only users have no other way out. Mesh Hop keeps its current behaviour.

Files most relevant to this study:
- `launcher/docs/APP-CONTRACT.md`
- `launcher/ext_components/cp0_lvgl/include/cp0_ui_widgets.hpp`
- `launcher/ext_components/cp0_lvgl/src/cp0/cp0_lvgl_dpi_scaled.c`
- `launcher/ext_components/cp0_lvgl/src/cp0/cp0_lvgl_keyboard.c`
- `launcher/projects/APPLaunch/main/ui/launch.cpp`
- `launcher/projects/APPLaunch/main/ui/native_ui.cpp`
- `launcher/docs/dev/deck/check_statusbar_drift.py`
- `cyberdeck-zero-apps/apps/mesh-hop/build/build.sh`
- `cyberdeck-zero-apps/apps/lanscan/build/build.sh`

## Controller decisions (2026-10-13)

- **Q1** Serial console v0.1 = a text-mode menu plus picocom (`Terminal=true`, works on the Pi 3A+ too); a native port
  picker comes later.
- **Q2** Speed test = curl against a configurable URL.
- **Q3** Notes v0.1 = a plain text editor; Markdown preview in v0.2.
- **Q4** L1 approved: the status strip (`cp0::ui::StatusStrip`) and the title move into `cp0_lvgl`.
- **Q5** The guidelines live in the apps repo, `docs/APP-GUIDELINES.md`; apps CI is added after the template.
- **Root Esc:** a short Esc at an app's root QUITS the app (touch-only users need it); Mesh Hop keeps its current
  behaviour.

Order of work: S0, L1, H1 and A2 in parallel; then T1; then A1 and G1; then A3; then A4.
