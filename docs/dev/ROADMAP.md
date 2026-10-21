# Roadmap

Owner of this file: the Controller. Status: `idea` > `brief` > `building` > `verifying` > `accepting` > `done`.

## Now

| # | Task | Owner | Status | Brief |
| --- | --- | --- | --- | --- |
| 001 | **Wi-Fi survey** app (pilot of the agent setup) | apps-dev | done | `tasks/001-wifi-survey.md` |
| 002 | **LanScan source move** to the apps repo (lanscan 0.1.1) | apps-dev | done | `tasks/002-lanscan-move.md` |
| 003 | **viz1090 0.1.1**: touch screen found by capabilities | apps-dev | done | `tasks/003-viz1090-touch.md` |
| 005 | **LanScan 0.1.2**: copyright file for the bundled IEEE OUI list | apps-dev | done | `tasks/005-lanscan-copyright.md` |
| 006 | **Settings > Apps polish** (tile order kept on upgrade, upgrade flow, sync progress, install feedback) | launcher-dev | done | `tasks/006-settings-apps-polish.md`, `reports/010.md` |
| 012/013 | **Launcher 0.5.0**: Pi 3A+ support, board profile, USB and Bluetooth keyboards | Controller + savvy workers | done (PR #23, release v0.5.0) | `tasks/012-launcher-0.5.0.md`, `tasks/013-multiboard-pi3a.md`, `reports/021` |
| 014 | **Responsive shell** (one code base for every SBC and capacitive screen): layout service, native Settings, on-screen keyboard, app contract | Controller + savvy workers | done (P1 to P5 and P4b, PRs #24 to #38, release v0.6.0) | `tasks/014-responsive-shell.md`, `reports/022` |
| 015 | **Multitasking** (keep apps open, switcher by three fingers, status strip long-press or Alt+Tab) | Controller + savvy workers | done (P-a to P-e, PRs #63 to #67, release v0.8.0) | `tasks/015-multitasking-pb.md`, `-pc.md`, `-pd.md`, `reports/026` |
| - | **Dev tooling**: `tools/dev` (WSL build, check-all) and `tools/boards` (deploy, test, screenshot on the boards) | Controller | done (PR #30) | `docs/dev/DEV-GUIDE.md`, `tools/dev/README.md`, `tools/boards/README.md` |
<<<<<<< HEAD
| 028 | **x86 (amd64) port** (decisions X1-X15 of 2026-10-21): x-a spike on the FZ-M1 done; **x-b launcher amd64** (native build `--arch amd64`, install.sh `pc` profile, `sysfs:` backlight also fixing the Pi 3A+ DSI, `APPLAUNCH_KBD_IGNORE`, terminal OSK Alt key, board `fzm1`, `tools/dev/x86-smoke.sh`); x-c apps amd64, x-d battery and release next | Controller + savvy workers | x-b verifying (draft PR) | `reports/028-x86-port.md` (PR #69) |
| - | **Settings > Date & Time > Time zone**: a two-level picker (region, then city; UTC direct), independent of Network Time, through timedated (sudo prompt when PolicyKit refuses); the clocks follow at once. Owner report 2026-10-21: the x86 tablet was stuck on Etc/UTC | Controller + savvy worker | verifying (branch `settings-timezone`, draft PR #71, test bundles 0.8.1-tz.2 amd64 and arm64) | scene `settings_native_timezone`, `settings_native_timezone_page.hpp` |
=======
| 028 | **x86 (amd64) port** (decisions X1-X15 of 2026-10-21): x-a spike on the FZ-M1 done; **x-b launcher amd64** (native build `--arch amd64`, install.sh `pc` profile, `sysfs:` backlight also fixing the Pi 3A+ DSI, `APPLAUNCH_KBD_IGNORE`, terminal OSK Alt key, board `fzm1`, `tools/dev/x86-smoke.sh`); **install.sh switches appliance <-> desktop variant** when a desktop is installed or removed later (the FZ-M1 with Armbian's XFCE: lightdm + Xorg, autologin; `board.py` desktop boards take a `dm`); x-c apps amd64, x-d battery and release next | Controller + savvy workers | x-b merged (#70); desktop switch verifying (draft PR, board test on the FZ-M1 pending) | `reports/028-x86-port.md` (PR #69), `projects/APPLaunch/pizero2w/README.md` "Switching between appliance and desktop" |
>>>>>>> ce04b57 (Installer: switch between appliance and desktop variant (pc profile with a desktop))

## Next (ideas, order not fixed)

| Task | Owner | Notes |
| --- | --- | --- |
| Replace `tree.hh` (GPL-3.0-only, in the launcher binary through the Settings tree) with our own small tree class so the binary is MIT again | launcher-dev | **done 2026-10-13** (PR #44, release v0.7.0): our own MIT `ext_components/cp0_lvgl/include/cp0_tree.hpp` (test `ext_components/cp0_lvgl/tests/test_cp0_tree.cpp`) replaces it; `tree.hh` and `LICENSES/tree.hh-GPL-3.0-only.txt` removed; README licence, Licenses page and component inventory updated | `docs/dev/reports/025-rebranding-plan.md`, `NAMING.md` |
| Follow-up of 002: remove `projects/LanScan` and its references (`.gitignore`, `docs/HOSTING-APPS.md`, `docs/dev/DEV-GUIDE.md`, `projects/APPLaunch/pizero2w/README.md`, `projects/APPLaunch/pizero2w/bundle/README.md`), then confirm the launcher still builds | launcher-dev | **done 2026-10-04** (task 004, `reports/004-handoff.md`): `projects/LanScan` removed, references cleaned, launcher builds; merged in pull request #4 |
| LoRa messaging (Meshtastic / MeshCore over a USB board) | apps-dev | MeshCore chosen: **Mesh Hop** (apps repo, `mesh-hop` 0.2.2 published; **0.3.0 published** (apps PRs #8, #9): LOG_DATA parser, "heard back by N repeaters", D8 packet log, Position box; deck test `tests/mesh-hop-0.3.0-deck-tests.md`, report `reports/019-mesh-hop-0.3.0.md`), see "Done (recent)" and `tasks/010`, `tasks/011`; rest of phase 3 (C5, C6/C7, R1, P1/P2, M2, BLE spike) and phase 4 not started |
| Packet capture viewer | apps-dev | PacketScope from the Store covers part of it |
| **Net Toolbox** 0.3 and 0.4 (apps repo, `net-toolbox`) | apps-dev | 0.1.0, 0.2.0 and 0.3.0 published (apps PRs #25 to #28, #32 to #34): 0.3 adds the speed test (bufferbloat grade), Wake-on-LAN, the Bluetooth radar and the interface chooser; the speed test and the radar ran on the deck and the Pi 3A+, Wake-on-LAN and a second adapter are still untried on a board. 0.4 planned: Wi-Fi walk test, DNS race, bandwidth meters. Plan and decisions: `reports/027-net-toolbox.md`, `decisions.md` 2026-10-15 |
| **Multitasking** (keep apps alive, switcher by gesture or key) | Controller | decisions D1-D15 of 2026-10-14 (`reports/026-multitasking-study.md`, `decisions.md`). **done P-a to P-e, release v0.8.0** (2026-10-21): P-a measured 2026-10-19/20 (report 026 section 10); **P-b launcher core** (PR #63), **P-c touch** (G1, G2; #64), **P-d UI** (the switcher screen, the legacy sheet, the grid's dots, the first-run hint; #65), **P-e** app side (`cp0_app_lifecycle` in `cp0_lvgl`, APP-CONTRACT section 13 final; #67; apps PR #74: System Monitor, RSS Reader freeze, Serial Console, Mesh Hop run) and **on by default** (#66) on canvases of 640x480 and more, off below (480x320, 320x170); `APPLAUNCH_MULTITASK=0` turns it off (service: `Environment=APPLAUNCH_MULTITASK=0` in a user drop-in), `=1` forces it on. Known limits: viz1090 can hang on three-finger touches (accepted; hold Esc 3 s closes it); HackBerry CM5 touch has 2 contacts (Alt+Tab only, its first-run hint still says three fingers); Net Toolbox stays legacy. Open: P-f (terminal sessions kept alive, D11, optional) |
| Battery: PiSugar S Plus | Controller | no usable data link to the Pi. Observed by the owner on 2026-10-14 on the Pi 3A+: touch stopped with the PiSugar fitted and worked again after removing it |

## Done (recent)

2026-10-21: **v0.8.0** (release PR, from main 8695c58, PRs #63 to #67): **multitasking** (task 015, report 026), a touch-first app switcher for low-power boards. Three fingers held 1 s anywhere, a 0.5 s long-press on the status strip, or Alt+Tab (Super = home) open "Open apps" (cards, Home, Close all); apps declare `X-Background=run|freeze` (APP-CONTRACT section 13 final, helper `cp0_app_lifecycle` in `cp0_lvgl`); apps that declare nothing get the Close/Keep sheet; a green dot on the home tile of an app in the background; cap 4 resident apps, MemAvailable floors 64 / 32 MB, `oom_score_adj` while hidden, the app's last frame saved and put back without a flash. On by default on canvases of 640x480 and more, off below; `APPLAUNCH_MULTITASK=0` turns it off, `=1` forces it on. Apps repo (apps PR #74, published separately): System Monitor 0.2.4 and RSS Reader 0.1.1 (freeze), Serial Console 0.4.3 and Mesh Hop 0.5.1 (run). Known limits: HackBerry CM5 touch reports at most 2 contacts (Alt+Tab only; its first-run hint says "Hold three fingers"); viz1090 is not multitasking-capable and can hang on three-finger touches (hold Esc 3 s closes it); Net Toolbox stays legacy. Release notes: `docs/dev/release-notes-0.8.0.md`.

2026-10-15: **Net Toolbox** (apps repo, `net-toolbox`): report 027 (PR #49) sets the name, the tools and the phases 0.1 to 0.4. 0.1.0 (Home, Interfaces, Diagnosis ladder, Ping with packet strip, DNS lookup, Port check) and 0.2.0 (Traceroute chain, HTTP check with timing waterfall and certificate badge, Public IP, Whois; depends `curl`, `ca-certificates`) built and published (apps PRs #25 to #28), the tested packages. **Serial Console 0.4.0** published (apps PRs #23, #24): its lists move to `CompactList`. Open: Net Toolbox 0.4.

2026-10-14: **CompactList and fixes** (launcher PRs #45 to #48, on `main`, not released). `cp0::ui::CompactList` (#45), a top-aligned list for apps next to the Roller (decision 2026-10-14). Home tiles reload when an icon file changes (#46: a 3 s poll of the icons' mtime and size). Stale tests fixed and the Bluetooth caption render flake removed (#47: labels with dots are no longer scrollable; `check-all.sh` passes all 8 steps). Report 026, multitasking study, with the owner's decisions D1-D15 (#48): **parked**, restart at the spike P-a. Apps repo: **System Monitor** 0.1.0 (draft, apps PR #19), then 0.2.0 on `CompactList` with full-width detail pages, published (apps PRs #20 to #22); `apps/_common/LAUNCHER_REF` moved to `main` after #46/#47.

2026-10-13: **v0.7.0 released** (main d4f648d, PRs #40 to #44): renamed CyberdeckZero Launcher (#43, disclaimer, `cyberdeckzero` binary and units, migration in `install.sh`, branding check), the binary MIT again (`tree.hh` replaced by our `cp0::Tree`, #44), Pi 3A+ with the Waveshare 4.3" DSI panel (board `pi3a-wsdsi43`, 800x480), desktop session for the HackBerry CM5 and the Pi 5 and the console unbound from the panel on appliance boards (#42), status strip and title in `cp0_lvgl` for apps (#41), minimum 640x480 for new work (#40). Tested by the owner on the deck, the Pi 3A+ (DSI) and the HackBerry CM5. Apps repo the same day (apps PRs #11 to #18): shared app skeleton `apps/_common` and `cyberdeck-app-template`, `docs/APP-GUIDELINES.md`, Serial Console 0.3.0 and 0.3.1 (drafts), Mesh Hop `X-MinSize=640x480` in the source, the icon system (`docs/ICON-PALETTE.md`: 100x100 square transparent PNG, one hue and silhouette per app, generator `tools/icons/make_icons.py`) with new Serial Console and template icons. Known limit: the Pi 3A+ DSI touch can stay dead after a soft reboot (a power cycle fixes it).

2026-10-12: **v0.6.0 released** (main f1e1b34): the responsive shell (task 014, phases P1 to P5 and P4b: PRs #24 to #38), native Settings by default, the fallback on-screen keyboard (forms and terminals), overlays, Calculator, clock and SSH form on layout tokens, app contract v1 (`docs/APP-CONTRACT.md`, `X-MinSize`, VFB2), our apps source provisioned by default, Settings > Apps sources kept across Store starts, Store toggle in Settings > Launcher, new README screenshots. Tested by the owner on the deck, the Pi 3A+ and (test mode) the HackBerry CM5. Open: rebuild LanScan, Wi-Fi Survey and viz1090 in the apps repo against this release; Mesh Hop responsive (report 023 R0-R4); `X-Canvas=fill` and a launcher-drawn keyboard over compat apps (future); the old Settings renderer is kept for stock and SDL builds.

2026-10-10 to 2026-10-11: **launcher 0.5.0 released** (PR #23: Pi 3A+ support, board profile in `/etc/applaunch/board.conf`, USB and Bluetooth keyboards; all 18 Pi 3A+ checks passed, the deck runs it). Then the **responsive shell** (task 014), all on `main` and not released: render harness (P1a, PR #24), layout service and tokens (P1b/P1c, #25, #26), native Settings host and widgets (P2a, #27), fallback on-screen keyboard (P2b, #28), native info pages and Wi-Fi (P3a/P3b, #29), dev tooling (#30), native Bluetooth with pairing dialogs and the rfkill udev rule (P3c, #31), native Apps page and responsive sudo prompt (P3d, #32). P3e flips the default: the native Settings runs on every Pi board (no switch, no compat path; legacy Apps page and compat host deleted, branch `p3e-native-default`, deck test pending); physical tests so far on the Pi 3A+ (sections E to H of `tests/launcher-0.5.0-keyboard-tests.md`). Not yet done: the SSH form and other built-in app pages (P4/P5), Pi 5, HackBerry, Pi 4 and Walnut screen tests.

v0.1.0 first port; v0.2.0 Settings > Apps + LAN Scan; v0.3.0 full-screen apps, shared top bar, clean boot; viz1090
as an installable app (apps repo).

2026-10-10: **v0.4.0 released** (commit 71cb402): `uninstall.sh` in the bundle (owner-tested dry run and removal), Settings > Apps
polish and D removes an app. The new launcher binary was not reinstalled on a deck by deck-verifier for this release.
Mesh Hop 0.3.0 is published in the apps registry (apps PRs #8 and #9).

2026-10-04: Wi-Fi Survey 0.1.0 passed deck verification (report 005), documented in the apps README, and accepted by
the user on the deck.

2026-10-04: LanScan source moved into the apps repo (`apps/lanscan/src`, lanscan 0.1.1) and viz1090 0.1.1 (touch
screen found by capabilities and name, with retry, instead of a fixed `/dev/input/event1`) passed deck verification
(report 006) and are documented in the apps README. Both are installed on the deck and accepted by the user
(real touch works on both). `projects/LanScan` was removed from the launcher repo.

2026-10-05: LanScan 0.1.2 adds a Debian copyright file (MIT, cp0_lvgl, IEEE OUI data source and shortening noted) to the
package; verified on the deck (report 007). The apps README now has one child page per app, with screenshots for each.

2026-10-05: `docs/dev/deck/check_statusbar_drift.py` checks the apps' copies of `cp0_statusbar.[ch]` against the launcher (exit 0 =
identical); it is part of the verification and release checklists. No drift found today.

2026-10-05: ADS-B Radar (native LVGL app, never published) dropped by the user; viz1090 is the ADS-B app. The source was
archived outside the repos (`C:\CLAUDE\zero7rchive\AdsbRadar-source.zip`, build output left out) and the untracked
`projects/AdsbRadar` folder deleted.

2026-10-05: Settings > Apps polish (task 006) passed deck verification (report 010) and was accepted by the user: tile
order kept on upgrade, installed vs available version with Update / Update all, per-source sync progress with
failure reasons, install/remove feedback, letter shortcuts U/S/A/D. This changes the launcher binary;
the Store backend is unchanged. Documented in `docs/HOSTING-APPS.md`.

2026-10-07 to 2026-10-09: **Mesh Hop** (`mesh-hop`, apps repo, tasks 010 and 011, reports 013 to 018): a full-screen 640x480 MeshCore client for a companion radio board on USB. Phase 1 (0.1.x: chats, contacts, settings, radio presets), phase 1b (conversation options, history deletion, preset list refresh) and phase 2 (0.2.x: message search, contacts select and groups, Nearby, statistics, path hash size, repeat, scheduled advert, per-board data, factory reset) are built and tested against the simulator and, for the earlier versions, on the deck by the user. Still `"draft": true`: not in `registry.json`, installed as a local `.deb`. Map and Terminal are placeholders and there is no BLE link yet. Documented on `apps/mesh-hop/README.md` in the apps repo (status `accepting`).

2026-10-10: **Mesh Hop 0.3.0 (in test)**, phase 3 part 1 (report 019): LOG_DATA parser in `core/`, "heard back by N repeaters" on own channel messages, D8 packet log (Settings, in memory, 500 packets), Position box (manual lat/lon, Clear, board GPS switch when the firmware lists `gps`). Built and tested on the PC (unit, simulator, UI logic); local `.deb` only, not deployed, not published. Deck protocol: `tests/mesh-hop-0.3.0-deck-tests.md`.
