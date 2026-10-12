# 025: Rebranding to "CyberdeckZero Launcher" (study summary, decisions and plan)

Date: 2026-10-13. Written by the Controller from the read-only inventory study (launcher `main` 5b7a93f and
`desktop-session` 504886d, apps `main` dca4516 and the `app-serial-console` branch, Store submodule b02db12).
Counts below are from `git grep`; re-check before relying on a line number.

## 1. The decision (owner, 2026-10-13)

Separate our builds from the M5Stack line. The launcher is renamed **CyberdeckZero Launcher**. The names M5Stack and
CardputerZero (hardware by M5Stack) are limited to inspiration and credits. Nobody may think we are affiliated with
M5Stack or acting as M5Stack. The MIT notice of M5Stack stays (it is a licence obligation), and a plain disclaimer is
added.

Answers to the study's questions:

1. Scope: branding plus the light rename of the binary, units and menu entry (with compatibility aliases and an
   installer migration). **No path, environment-variable or config rename.** `APPLaunch` paths (`/usr/share/APPLaunch`),
   the `APPLAUNCH_*` variables (published contract, `docs/APP-CONTRACT.md`), `~/.config/cardputerzero`, the Store's
   `~/.local/share/cardputerzero-appstore`, the `cp0_` code prefix and `cp0_lvgl` stay. They are documented in
   `NAMING.md` as technical names inherited from upstream.
2. Spelling: **CyberdeckZero** (one word). Repository slugs stay (`cyberdeck-zero-launcher`, `cyberdeck-zero-apps`).
3. The home title word **"ZERO" is removed**: the home header shows no title word. The 11 render scenes that check
   `header home ZERO` are updated and the affected goldens are re-shot on purpose. Existing README screenshots are
   edited (the title painted over), not re-taken.
4. The stock Store tile keeps the label "Store"; a credit line "CardputerZero Store by M5Stack" is added in Settings
   (Launcher section) and in Licenses.
5. Git: rename the remote `origin` (CardputerZero/launcher) to `upstream` and disable pushing to it (local setting).
6. `LICENSE` stays exactly as it is. **Do not add any OSRdesign copyright line anywhere** (the owner does not own such a
   thing). Attribution text such as "Raspberry Pi port, responsive shell and native Settings by OSRdesign, written
   together with Claude" is not a copyright claim and may stay.
7. Apps drop the `M5CardputerZero-` prefix at their next version; the binary is named after the app id (for example
   `/usr/share/APPLaunch/bin/serial-console`), so process names no longer collide with the launcher's. The serial console
   does it before its first publication.
8. GitHub descriptions and release titles may be updated (the Controller does it at the end; not a worker task).

## 2. Names

| Item | New name |
|---|---|
| Product | CyberdeckZero Launcher |
| Short name | CyberdeckZero |
| Tagline | A touch launcher for Raspberry Pi cyberdecks. |
| Binary | `/usr/share/APPLaunch/bin/cyberdeckzero` (13 characters: `pkill -x cyberdeckzero` works); the old `M5CardputerZero-APPLaunch` stays as a symlink for one release |
| User service (appliance boards) | `cyberdeckzero.service` (replaces `APPLaunch.service`) |
| Console unit | `cyberdeckzero-console.service` (replaces `applaunch-console.service`) |
| Desktop unit | `cyberdeckzero-desktop.service` (replaces `applaunch-desktop.service`), polkit rule renamed to match |
| Menu entry (desktop boards) | `cyberdeckzero.desktop`, `Name=CyberdeckZero Launcher` |
| Release asset | `cyberdeckzero-launcher-<version>-arm64.tar.gz`; for the first release after the rename ALSO upload the same file as `pizero2w-bundle.tar.gz` (old quick-start name, no brand in it); drop that in the release after |
| Config, data, paths, env | unchanged (see the decision above) |

Disclaimer text (README, Settings > System > About, Licenses): "CyberdeckZero Launcher is an independent hobby project,
derived from the MIT-licensed CardputerZero launcher by M5Stack Technology CO LTD, and bundles M5Stack's CardputerZero
Store unchanged. It is not affiliated with, sponsored or endorsed by M5Stack." (shorten to fit the page where needed).

## 3. What must stay (legal and technical), with evidence

1. **MIT notices**: `LICENSE` (M5Stack's line), about 970 `SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD`
   headers, `LICENSES/`, the `LICENSE` files of cp0_lvgl, bluectl and nmtui, the Store's `LICENSE`. Removing them would
   breach MIT. `OPEN_SOURCE_COMPONENTS.md` keeps its entries.
2. **The release bundle must carry the licences** (today it carries none: `build.sh:60-75`, `install.sh:473-482`). Install
   them under `/usr/share/doc/cyberdeckzero-launcher/` (LICENSE, LICENSES/*, the font licences that exist). Fonts without a
   licence file in the repo (DejaVu, Liberation, Montserrat, Noto CJK, Alibaba PuHuiTi, Font Awesome; `svgfont.ttf` has no
   recorded source) must be listed in `OPEN_SOURCE_COMPONENTS.md` with their licence and source if known, flagged
   "to verify" if not.
3. **The Store is M5Stack's code and stays unchanged**: built-in "CardputerZero Hub" registry (it refuses to remove it), the app root
   `M5APPSTORE_APP_ROOT` default `/usr/share/APPLaunch`, `<root>/applications` for desktop entries, its own folders
   `~/.local/share/cardputerzero-appstore` and `~/.cache/cardputerzero-appstore`, registry keys `applaunch.exec` and
   `applaunch.desktop_entry`, its binary name `M5CardputerZero-AppStore` (the launcher maps it in
   `cp0_lvgl_filesystem.cpp:173` `appstore_exec`; `apps_backend.cpp:29` and `default-sources.sh:47` call it).
4. **Launcher duties towards the Store**: the `appstore_exec` path, the shared config keys `appstore.registry.*`
   (`shared_registry_store.cpp`), `/usr/share/APPLaunch`.
5. **Installed apps rely on** `/usr/share/APPLaunch/{bin,applications,share}`, `/usr/share/APPLaunch/share/oui.tsv`,
   `/usr/share/APPLaunch/share/font`, `APPLAUNCH_TOUCH_*`, `APPLAUNCH_STD_KEYBOARD`, `APPLAUNCH_FN_KEY`,
   `/proc/cardputerzero_hw_id`, and the lock file `/tmp/M5CardputerZero-APPLaunch_fcntl.lock` (technical, stays or is
   renamed with an alias; check). They do not use the launcher's binary or unit names. Home-grid order is keyed by the
   `.desktop` file name, so renaming an app's binary does not move its tile.
6. **Contract v1** names never change: all `APPLAUNCH_*`, `$XDG_RUNTIME_DIR/applaunch/`, `/tmp/applaunch-*.state`,
   `/usr/share/APPLaunch/applications/`, the SDK file names `cp0_app_contract.h` etc.

## 4. Inventory (launcher `main`, lines / files)

`M5Stack` 1122 / 1000 (970 are copyright headers); `M5CardputerZero` 377 / 87; `CardputerZero` 686 / 159;
`CardputerZero Hub` 16 / 9; `APPLaunch` 3548 / 307; `APPLAUNCH_` 1116 / 188; `cp0` 9954 / 657. Apps repo: `M5Stack` 40 / 37,
`M5CardputerZero` 34 / 23, `CardputerZero` 49 / 29. Brand mentions outside copyright headers: about 986 lines, mostly
upstream docs (597), build and CI (167), launcher source (81), upstream subprojects (54), dev reports (51), Pi port (44),
cp0_lvgl (34), our docs (28), tests and tools (26), licence files (13), `.gitmodules` (7).

Visible to the user and to change:

1. About page first line `"M5CardputerZero"` (`settings_about_help_model.cpp:47`), all three boards.
2. Licenses page: opens with our notice, then a section "Upstream CardputerZero" with the Kickstarter thanks
   (`settings_about_help_model.cpp:71-74`), then the sentence "CardputerZero is built with open-source software..." (:76) that
   presents our product as CardputerZero, then the upstream OS image's component list (Proxmark, FFmpeg, ZeroMQ...) that
   is not what our bundle contains. Rewrite: our notice and disclaimer, a "Derived from" credit, the real list from
   `OPEN_SOURCE_COMPONENTS.md`, the Store credit line.
3. Bluetooth name fallback `"CardputerZero"` (`settings_bluetooth_model.cpp:753`, `settings_bluetooth_page.cpp:1618`);
   other devices see it. New fallback: "CyberdeckZero". Tests: `test_settings_models.cpp:95-96`,
   `test_settings_bluetooth_model.cpp:459`.
4. Menu entry `applaunch.desktop:3` ("CardputerZero Launcher"), the desktop unit description
   (`applaunch-desktop.service:11`), `install.sh` messages (:392, :623), `APPLaunch.service` description
   ("APPLaunch (Pi Zero 2W / Waveshare 2.8 DPI)": wrong on other boards).
5. Process name `M5CardputerZero` in `ps` and `top` (the binary name cut to 15 characters); `board.py:45-46`.
6. Home title `"ZERO"` (`native_ui.cpp:364`; 11 render scenes check `header home ZERO`): REMOVED (decision 3).
7. M5's logo `logo_lcd.png` ("M5 STACK") is installed on every board (only the stock start-up path shows it,
   `ui_launch_page_startup.cpp:47`): drop it from the Pi bundle. `launcher_brand_logo.png` and
   `carousel_zero_wordmark.png` are unreferenced: drop. `lockscreen.png` and the 11 sound files have no recorded source:
   list as "source unknown, to verify".
8. Pages our boards cannot reach (stock profile only; leave unless trivial): "Device: M5CardputerZero"
   (`settings_system_page.cpp:1085`), ADB guide label (`settings_adb_guide_page.cpp:841`), SDL window title
   (`sdl_lvgl_display.c:48`).
9. Inside the Store (cannot change): "CardputerZero Hub", "Enter a code from CardputerZero Hub." Keep; credit line in
   Settings and Licenses explains it.

Binary, package, service and path names, config/data locations, environment variables and the contract: see sections 2
and 3. Build and CI: `launcher-build.yml` (runs on `master` only, which our GitHub does not have, so it never runs),
`emulator-build.yml`, `scripts/debian_packager.py:33,63-67` (Maintainer dianjixz@m5stack.com, Homepage m5stack.com: only
used by that workflow; replace with neutral values or delete the workflow with a note), `scripts/build_cardputerzero_release_local.sh`,
the `CardputerZero=y` flag, `*_cp0_config_defaults.mk` (technical, keep). URLs: Hub registry URLs (Store and
`default-sources.sh`), updater URL `github.com/CardputerZero/launcher/releases` (`cp0_lvgl_osinfo.cpp:139`, off on the
Pi builds), GitHub descriptions. The launcher repo is not a GitHub fork (`isFork:false`). Apps repo: `Exec=` lines use
`/usr/share/APPLaunch/bin/M5CardputerZero-<app>`; `registry.json` `"device_targets": ["CardputerZero"]`
(`make_registry.py:150`; read by neither the Store nor the launcher: rename to `"CyberdeckZero"`); README lines :3, :50, :106,
:116 recommend the `M5CardputerZero-` prefix.

## 5. Plan (staged; v0.7.0)

| # | Task | Tier | Scope | Done when |
|---|---|---|---|---|
| T0 | Branding check | light | `tools/dev/check-branding.sh` plus an allow-list file, added as a step of `check-all.sh` (and a copy for the apps repo) | greps the brand words, removes known technical tokens and copyright headers, allows only listed paths (licences, `docs/upstream`, upstream subprojects, stock-profile code, Store identifiers, credits text, this report and the decisions), fails on anything new |
| T1 | Docs | medium | README (root and `pizero2w/README.md`), new `NAMING.md`, the disclaimer, move `README_ZH.md`, `README_JA.md`, `docs/UPSTREAM_README.md`, the upstream launcher guides and packaging guides into `docs/upstream/` unedited with a banner (fix `projects/APPLaunch/AGENTS.md:6`), `HOSTING-APPS.md` and app docs wording, `OPEN_SOURCE_COMPONENTS.md` additions, home screenshot edited (title painted over) | branding check passes |
| T2 | Visible strings | medium | items 1-6 of section 4 (not 4/5 file names), the home title removal, tests, 11 scenes, goldens re-shot on purpose (contact sheet for review), Settings credit lines | `check-all.sh` all PASS except known failures; golden diffs are only the title area |
| T3 | Bundle and rename | heavy | drop the M5 logo and unused wordmarks and `adb/` from the Pi bundle; ship licence files; rename the binary, units, rule, menu entry (section 2) with the install.sh migration (disable and remove the old user unit incl. lingering case, remove retired files, keep the old binary name as a symlink), uninstall.sh knows both old and new names, `board.py`, `test-install-board.sh`, keyboard-tests protocol section O | install/uninstall tests PASS; dry run output reviewed |
| T4 | Apps repo | light | serial-console binary named by app id, README wording, template naming, `device_targets`, docs; each other app at its next version | registry rebuilt, branding check passes |
| T5 | Controller | | GitHub descriptions and release titles, git remote rename, v0.7.0 release | |

Order: T0, then T1, T2, T3, T4 in parallel by file ownership, then board tests (deck, Pi 3A+, HackBerry), then release.

Board checks after install over v0.6.0: the unit is `cyberdeckzero` and active; `pgrep -ax M5CardputerZero` returns nothing;
About and Licenses read correctly; the Bluetooth name is unchanged or "CyberdeckZero"; the Store syncs and installs and
removes LanScan; sources survive; LanScan, viz1090, Wi-Fi Survey, Mesh Hop and the probe start; after a reboot the launcher
comes up (HackBerry: the menu shows "CyberdeckZero Launcher", Quit to desktop works); the M5 logo is gone and the licence
files are in `/usr/share/doc/cyberdeckzero-launcher/`; `uninstall.sh --dry-run` lists old and new names.

Migration on the owner's boards: back up `/usr/share/APPLaunch/bin` and the user units, run `sudo ./install.sh` (it
migrates), reboot, run the checks. Rollback: `systemctl --user disable --now cyberdeckzero.service` before reinstalling v0.6.0.

## 6. Open risks

The name "CyberdeckZero" was not searched for collisions beyond one web search (no project found; a pending US trademark
application for "CYBERDECK" for games exists, different field; not legal advice). The sources of `lockscreen.png`, the
sounds and some fonts are unknown. "Zero" still echoes Raspberry Pi Zero and the Store's Hub screens still carry
CardputerZero names (credited, not ours).
