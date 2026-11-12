# 049 - App classification for the main / small-screen fork

Read-only inventory, 2026-10-31. Rule applied (owner, decided): an app **we made** that meets the minimum screen
640x480 (portrait apps declare `X-MinSize=640x480` plus `X-Portrait`) **stays on main**; anything designed for a
smaller screen (320x170 class) **moves to `small-screen`** (removed from main).

Outcome (2026-11-12): applied in the v0.9.0 split (main drops the Store tile, Snake, Tank and IP Panel and keeps the apps that fit 640x480 and the portrait PDA; the `small-screen` branch keeps what was removed; the Store program stays in the bundle as the back end of Settings > Apps). The rule used here (640x480 as the minimum screen for apps that stay on main) was superseded on 2026-11-12: there is no global minimum screen any more, each app declares its own requirement (`docs/dev/decisions.md`). This report is kept as the record of the 2026-10-31 split.

Sources read:
- Launcher `github/main` = `github/small-screen` = `5ddd2e9f` (v0.8.13), via `git show github/main:<path>`. Note: the
  worktree `C:\CLAUDE\zero7\launcher-updates` is NOT at main; it is at `d0c14291` (updates-p2 merge), so it was not used.
- Store submodule `projects/AppStore` (local checkout, pinned `b02db122` per report 048).
- Apps repo `C:\CLAUDE\zero7\cyberdeck-zero-apps` `main` = `c2a73ef` (viz1090 0.3.2), clean.
- **One internet fetch**: the Hub registry `https://cardputerzero.github.io/generated/registry.json` (referenced at
  `projects/APPLaunch/pizero2w/default-sources.sh:49`), saved to the session scratchpad. 15 apps; it has **no size keys**
  (keys: assets, author, categories, depends, description, download, i18n, icon, license, locales, permissions,
  published_at, review, risk_flags, screenshots, share_code, source_repo, summary, title, updated_at, uuid, version);
  `device_targets = ["CardputerZero"]`.
- Memory notes in `C:\Users\osrde\.claude\projects\C--CLAUDE-zero7\memory\` for board installs.

Launcher behaviour that matters: a `.desktop` without `X-` keys is a fixed-pixel 320x170 compat app (2x on 640x480, 1x
on 480x320, hidden on portrait); an app whose `X-MinSize` exceeds the screen is hidden
(`projects/APPLaunch/main/ui/desktop_app_loader.cpp:202-221`). So every 640x480 app below is already invisible on a
480x320 board, and every compat app also "runs" on 640x480 only as a scaled 320x170 window.

Not apps (checked, excluded): `tools/dev/app-contract-probe/desktop/*.desktop` and
`tools/multitasking-spike/desktop/*.desktop` (dev tools, not copied by `pizero2w/build.sh`, whose only `.desktop` is
`cyberdeckzero.desktop` at :143, the launcher's own menu entry); `APPLaunch/preinstalled-desktop-apps.tsv` (header
only, no entries); the names in `app_display_order.cpp:17-41` (`zclaw`, `files`, `camera`, `gps`, `lora`, ...) are an
ordering list for stock M5 tiles that have no registration on main.

## 1. All apps

Abbreviations: L = launcher repo `5ddd2e9f`; A = apps repo `c2a73ef`; H = M5Stack Hub registry. "reg" =
`registry.json` (arm64), "reg64" = `registry-amd64.json`. "-" = key absent.

| # | Name (id) | Source | Published / built-in | X-MinSize | X-Portrait | Hard-coded resolution (file:line) | Verdict | Reason |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Settings (`app_Setting`) | L built-in | built-in | n/a | n/a | none (native tree page `UISettingTreePage`, `builtin_app_registry.cpp:74-75`) | STAYS ON MAIN | native responsive page |
| 2 | Store (`app_Store`, binary `M5CardputerZero-AppStore`) | L built-in tile + submodule `projects/AppStore` (M5Stack code) | built-in (bundled with `--with-store`, `pizero2w/build.sh:127`) | n/a | n/a | `projects/AppStore/main/ui/appstore_pages.cpp:139,173,579` (`320, 170`); `appstore_low_battery_overlay.cpp:44,51`; `appstore_image_renderer.cpp:118` | MOVES TO small-screen (UI) - **see ambiguous #A1** | fixed 320x170 keyboard UI, not ours; but its binary is the Settings > Apps backend |
| 3 | CLI (`app_CLI`) | L built-in | built-in | n/a | n/a | none (terminal `UISTPage`, layout from display) | STAYS ON MAIN | native terminal |
| 4 | Python (`app_Python`) | L built-in | built-in | n/a | n/a | none (runs `python3` in the terminal, `builtin_app_registry.cpp:82-83`) | STAYS ON MAIN | terminal app |
| 5 | SSH (`app_SSH`) | L built-in (Linux non-SDL only) | built-in | n/a | n/a | fallback form `ui_app_ssh_view.cpp:24,38,80,145` (320x150); native page on the dpi-scaled backend (`ui_app_ssh.hpp:28-39`, `ui_app_ssh_native.cpp`) | STAYS ON MAIN - see #A2 | native form on every Pi/PC profile |
| 6 | IP Panel (`app_IP_Panel`) | L built-in (Linux non-SDL only) | built-in | n/a | n/a | `ui_app_ip_panel.hpp:14,36-40`; `ui_app_ip_panel_view.cpp:16,27,59,80` (320 wide, 150/128 high) | MOVES TO small-screen | 320x170 page; owner named it |
| 7 | Calculator (`app_Math`) | L built-in | built-in | n/a | n/a | pinned presets for 640x480 and 480x320 (`calculator_layout.hpp:7-8,89-96`), computed layout otherwise (`ui_app_calculator.cpp:222-230`) | STAYS ON MAIN | responsive, 640x480 preset |
| 8 | Snake (`app_Game`) | L built-in | built-in | n/a | n/a | `ui_app_game.hpp:16,40-46` (`SCREEN_W 320`, `SCREEN_H 170`) | MOVES TO small-screen | 320x170 page; owner named it |
| 9 | Tank (`app_Tank`) | L built-in | built-in | n/a | n/a | `ui_app_tank_battle.hpp:38-39,47-48` (`320`, `170`, arena 304x124) | MOVES TO small-screen | 320x170 page; owner named it |
| 10 | Klondike Solitaire (`klondike-solitaire`) | A | 0.1.1 reg + reg64, Games | 640x480 | 480x640 | none found | STAYS ON MAIN | meets 640x480 + portrait |
| 11 | LAN Scan (`lanscan`) | A | 0.1.3 reg only (arm64), Network | - | - | `apps/lanscan/src/main/ui/lanscan.cpp:24` (`kWidth = 320`), `:47` (`kWidth, 150`), `:83` | MOVES TO small-screen | 320x170 compat app; replaced by Net Toolbox |
| 12 | Markdown Notes (`markdown-notes`, tile "Notes") | A | 0.2.1 reg + reg64, Productivity | 640x480 | 480x640 | none found | STAYS ON MAIN | meets 640x480 + portrait |
| 13 | Mesh Hop (`mesh-hop`) | A | 0.8.2 reg + reg64, Radio | 640x480 | - | none found | STAYS ON MAIN | meets 640x480 (no portrait layout; runs turned on portrait) |
| 14 | MeshZero (`meshzero`) | A | **draft** 0.1.0 (not in any registry), Radio | - | - | `apps/meshzero/src/main/ui/meshzero.cpp:41-42` (`kWidth = 320`, `kContentH = 150`) | MOVES TO small-screen | 320x170 compat app; owner named it |
| 15 | Mine Field (`mine-field`) | A | 0.1.1 reg + reg64, Games | 640x480 | 480x640 | none found | STAYS ON MAIN | meets 640x480 + portrait |
| 16 | Net Toolbox (`net-toolbox`) | A | 0.5.1 reg + reg64, Tools | 640x480 | 480x640 | none found | STAYS ON MAIN | meets 640x480 + portrait |
| 17 | RSS Reader (`rss-reader`) | A | 0.2.1 reg + reg64, Internet | 640x480 | 480x640 | none found | STAYS ON MAIN | meets 640x480 + portrait |
| 18 | Serial Console (`serial-console`) | A | 0.6.1 reg + reg64, Tools | 640x480 | 480x640 | none found | STAYS ON MAIN | meets 640x480 + portrait |
| 19 | System Monitor (`system-monitor`, tile "Monitor") | A | 0.3.1 reg + reg64, Tools | 640x480 | 480x640 | none found | STAYS ON MAIN | meets 640x480 + portrait |
| 20 | viz1090 (`viz1090`) | A | 0.3.2 reg + reg64, Radio | 640x480 | 480x640 | none fixed; screen from the app contract, fallback 640x480 (`apps/viz1090/build/README.md:13`) | STAYS ON MAIN | meets 640x480 + portrait |
| 21 | Wi-Fi Survey (`wifi-survey`) | A | 0.1.1 reg only (arm64), Network | - | - | `apps/wifi-survey/src/main/ui/wifisurvey.cpp:31-32` (`kWidth = 320`, `kContentH = 150`) | MOVES TO small-screen | 320x170 compat app; replaced by Net Toolbox |
| 22 | App Template (`cyberdeck-app-template`) | A | **draft** 0.1.1 (not in any registry), Tools | 640x480 | - | none found | STAYS ON MAIN | 640x480 starting point (not shipped) |
| 23 | 2048 (package `2048` 0.1.0) | H (installed on the deck, no longer listed by the Hub) | not ours | unknown (package not in any repo) | unknown | not readable | MOVES TO small-screen (with the Store) - see #A4 | Hub/CardputerZero app, reachable via Store only |
| 24-38 | Hub apps: AdvanceWebUI (AWUI, pkg `advancewebui`, 1.0.2), AstroBox (ASTR, `cardputerzero-astro-box`, 1.0.1), FontPreview (FONT, `fontpreview`, 0.4.6-1), Frame (FRME, `frame`, 0.2.1-1), FXCam (FXCM, `fxcam`, 1.0.1-m5stack1), MeshTerm (MTRM, `meshterm`, 0.11.5-1), Tools (TOOL, `pocket-zero-tools`, 0.2.1-1), RaspyJack (RJAK, `raspyjack`, 0.1.4-raspyjack1), Retro-Zero (RTZR, `retro-zero`, 0.1.0-2), RFID (RFID, `rfid`, 1.0.3), TalkToMe (TTME, `talktome`, 0.3.0), Telega (TLGA, `telega`, 0.2.0-1), TIC-80 (TC80, `tic80-c0`, 1.3.1-rev1), ZeroRadio (ZRAD, `zeroradio`, 1.2.0), ZeroTracker (ZTRK, `zerotracker`, 0.1.0-rev1) | H (fetched registry) | not ours; installs on our boards not recorded | not declared in the registry | not declared | not readable (debs not fetched); Frame's screenshots are named `store-1-320x170.png` | MOVES TO small-screen (with the Store) - see #A4 | `device_targets: CardputerZero`; reachable only through the Store UI |

Hard-coded-resolution check on the 640x480 apps: `git grep` for `= 320;`, `= 170;`, `kWidth = 3`, `SCREEN_W =` and
`320 ... 170` pairs over `apps/{klondike-solitaire,markdown-notes,mesh-hop,mine-field,net-toolbox,rss-reader,
serial-console,system-monitor,viz1090,cyberdeck-app-template,_common}` (tests, `.json`, `.md` excluded): **no hits**.

## 2. Needs the owner's call

| # | Item | Facts | Recommendation |
| --- | --- | --- | --- |
| A1 | **Store** | Its UI is fixed 320x170 (row 2), but the same executable is the backend of Settings > Apps (`settings/apps_backend.cpp:30`, `apps_backend.hpp:2-3`) and of `default-sources.sh:53`. Removing the binary breaks Settings > Apps (report 048 §1.4). | Remove the **tile and front end** from main (tile, Settings > Launcher toggle/credit, `store` display order, 15 Store images); **keep the binary** as a headless backend. The full UI stays on `small-screen`. |
| A2 | **SSH** | Native page on every dpi-scaled profile; the 320x150 form remains only for builds without that backend (stock/SDL). | STAYS ON MAIN. The 320x150 fallback code could go with the small-screen fork later; not a blocker. |
| A3 | **LAN Scan 0.1.3, Wi-Fi Survey 0.1.1** | Published (arm64 reg only), compat 320x170, replaced by Net Toolbox 0.4+ (`apps/net-toolbox/README.md:10-13`); memory: "stay in the store with a README note until the user says to remove them". | MOVE: unpublish from `registry.json` (no reg64 entry exists); keep the app dirs/debs for `small-screen` (or a separate small-screen registry). |
| A4 | **Hub apps + 2048** | Not ours, so the owner's rule ("apps we made") does not strictly cover them. On main they are reachable only through the Store UI; Settings > Apps filters the Hub out (`settings_apps_jobs.cpp:264-281`, per 048). No size is declared in the Hub registry. | Out of main together with the Store UI. Remove `2048` by hand on the deck (Debian `2048` 1.0.3 collision, `pkg-out-updates\p0\deck\09-apt-list.txt:3`). |
| A5 | **MeshZero** | Draft, never published; replaced by Mesh Hop. | MOVE (nothing to unpublish). Optionally delete the dir from apps `main` later. |
| A6 | **Mesh Hop** | `X-MinSize=640x480`, **no** `X-Portrait`: on a portrait PDA screen it runs turned (`desktop_app_loader.cpp:204`). | STAYS ON MAIN (meets the rule; portrait layout is a separate wish). |
| A7 | **Calculator** | Keeps a pinned 480x320 "compact" preset next to the 640x480 one. | STAYS ON MAIN (the compact branch is dead code on main, harmless). |
| A8 | **App Template** | Draft, 640x480, not shipped. | STAYS ON MAIN (apps repo). |
| A9 | Serial Console, viz1090, Klondike, Mine Field, Markdown Notes, RSS Reader, System Monitor, Net Toolbox | All declare `X-MinSize=640x480` + `X-Portrait=480x640`; no hard-coded small resolution found. | No ambiguity: STAY ON MAIN. |
| A10 | Report 048 line 160 | Says "All 11 published apps ... declare X-MinSize=640x480"; false for LAN Scan and Wi-Fi Survey (no `X-` keys, `apps/lanscan/root/.../lanscan.desktop`, `apps/wifi-survey/root/.../wifi-survey.desktop`). 048 row "LAN Scan, Wi-Fi Survey" (line 157) is correct. | Correct 048 if it is reused. |

## 3. What each "moves" item involves on main

| Item | Shipped from | Unpublish? | Boards with it installed (memory notes) |
| --- | --- | --- | --- |
| Store UI | bundle: `payload/bin/M5CardputerZero-AppStore` (`build.sh:127`, install `install.sh:1259-1261`), tile `builtin_app_registry.cpp:76-78`, images `build.sh:130-132`, toggle `settings_hw_profile.hpp:49`, `settings_launcher_model.hpp:39`, order `app_display_order.cpp:19` | no registry entry (built-in). Profile lines `install.sh:572,665` (`APPLAUNCH_HIDE_TILES=...app_Store`): leave the text (048: editing a profile body forces a by-hand update) | all six boards run v0.8.13 (bundle with the Store) |
| IP Panel | bundle (compiled into the launcher): `builtin_app_registry.cpp:86-87`, `page_app/ui_app_ip_panel*`, `ip_panel_100.png`, order `ippanel` | no (built-in); `install.sh:572` keeps the key (harmless) | all six boards (built-in) |
| Snake | bundle: `builtin_app_registry.cpp:90-91`, `ui_app_game*`, snake model/tests, `game_100.png`, order `snake` | no (built-in) | all six boards (built-in) |
| Tank | bundle: `builtin_app_registry.cpp:92-93`, `ui_app_tank_battle*`, tank model/tests, `tank_100.png`, order `tank` | no (built-in) | all six boards (built-in) |
| LAN Scan | apps repo `apps/lanscan`, deb `packages/lanscan_0.1.3_arm64.deb`, `registry.json` entry `1331569f-...` | **yes**: `registry.json` only | deck had it (0.1.2 shipped; 0.1.3 built, deployment "not yet" as of 2026-10-06, `project_zero7_pi_port.md:37,42`); current per-board state **not recorded** |
| Wi-Fi Survey | apps repo `apps/wifi-survey`, deb `packages/wifi-survey_*_arm64.deb`, `registry.json` entry `8ec73f7d-...` | **yes**: `registry.json` only | deck had 0.1.0 (same notes); current state **not recorded** |
| MeshZero | apps repo `apps/meshzero` (draft); docs mentions `NAMING.md:47`, `pizero2w/README.md:279`, `tests/test_desktop_entry.cpp:149` comment | no (never published) | deck had the older `meshcore` 0.1.3 and a local meshzero deb (`project_zero7_pi_port.md:42`); current state **unchecked** |
| 2048 | Hub (installed by the Store) | not ours | deck: `2048` 0.1.0 (`pkg-out-updates\p0\deck\09-apt-list.txt:3`) |
| 15 Hub apps | Hub registry via the Store UI | not ours | not recorded on any board |

The p0 board captures (`pkg-out-updates\p0\<board>\`) hold only upgradable packages, not a full installed list, so
they cannot confirm which boards still have LAN Scan / Wi-Fi Survey / MeshZero.

## 4. Counts

- STAYS ON MAIN: **15** = 5 built-ins (Settings, CLI, Python, SSH, Calculator) + 9 published apps (Klondike, Markdown Notes, Mesh Hop, Mine Field, Net Toolbox, RSS Reader, Serial Console, System Monitor, viz1090; all in both registries) + 1 draft (App Template).
- MOVES TO small-screen, ours: **7** (Store UI, IP Panel, Snake, Tank, LAN Scan, Wi-Fi Survey, MeshZero).
- MOVES with the Store, not ours: **16** (2048 + 15 Hub apps).
- Needs unpublishing from our registries: **2** (LAN Scan, Wi-Fi Survey; `registry.json` only, no amd64 entries).
- Owner's call items: **10** (A1-A10); only A1 (keep Store binary as backend) changes the plain rule's outcome.
