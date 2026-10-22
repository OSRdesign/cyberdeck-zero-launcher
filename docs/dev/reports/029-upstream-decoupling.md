# 029 - Decoupling from the upstream CardputerZero launcher: remove the code we do not use

Status: done 2026-10-22 on branch `decouple-upstream` (from launcher main 2caea24, v0.8.0 + report 028 and x-b),
draft PR. Apps repo read at origin/main 3dde2c4; the bundled Store is the `projects/AppStore` submodule at b02db12.
No board touched, nothing merged, git history unchanged. Builds and PC tests in section 3; nothing was tried on a
board (the removed code was compiled out or unreachable on every board, so a short smoke test is enough).

## 1. Decision and scope

Owner, 2026-10-22: "we should be completely unrelated now to the CardputerZero from M5Stack repo". Chosen scope:
**remove the upstream code we do not use and the CardputerZero-only hardware and stock-app code**, and keep:

- every MIT copyright and SPDX notice, `LICENSE`, `LICENSES/`, the derivation statement in `NAMING.md` and the
  README credits (the MIT licence requires the upstream copyright notice to stay with the code we keep);
- the technical names installed apps, the published app contract and the bundled Store depend on (section 5): not
  renamed here;
- the bundled prebuilt Store, unchanged;
- everything our boards use: the native UI, the dpi-scaled display, the compat/VFB layer for wrapped legacy apps,
  Settings, multitasking, the app contract, the x86 (amd64) build and the board tooling.

Rule for each item: removed only when the evidence shows that no build we ship (arm64 and amd64 bundles, the Store
built with `--with-store`), no check of `tools/dev/check-all.sh`, no install test and no app of the apps repo uses
it. When in doubt it stays and is listed under "needs owner decision" (section 4).

How "used" was checked: `git grep` for includes, build targets and file names; the SCons component switches of the
launcher configs (`*_config_defaults.mk`) and of the Store's config; the source lists of the render harness, of the
unit-test runners and of the apps repo's `apps/_common/headless/Makefile`; the `cp0_*` symbols the apps repo and the
Store's sources call (grep of `apps/` at 3dde2c4 and of `projects/AppStore/main`).

## 2. Inventory

"Ours?" = used by a build, test or tool of CyberdeckZero today.

### 2.1 Projects, submodules, CI, scripts and documents

| Item | Ours? | Evidence | Action |
| --- | --- | --- | --- |
| `projects/ZClaw` (250 files) | no | built only by `scripts/build_cardputerzero_release_local.sh` and `launcher-build.yml`; its one trace in our code is a line of `APPLaunch/preinstalled-desktop-apps.tsv` (no binary installed) | removed, manifest line removed |
| `projects/LaunchWizard` (first-boot wizard) | no | built only by the stock release script and workflow; the launcher's "Run Setup Again" action (`RearmOobe`, `oobe_marker`) is in no Settings tree | removed, with the dead `RearmOobe` action and the `oobe_marker` path |
| `projects/UserDemo`, `projects/HelloWorld` | no | upstream demos, referenced by nothing we build | removed |
| `projects/CardputerZero-Emulator` (+ submodule `lib/lvgl`) | no | its own CMake build, only `emulator-build.yml` builds it; it is the only user of `cp0_lvgl/src/web` and `src/win32` | removed, with `src/web` and `src/win32` |
| `projects/CameraApp` (+ submodule `main_CameraApp`) | no | stock camera app; not in our bundles | removed |
| submodules `projects/Calculator`, `projects/Keyboard-Guide` | no | stock apps; our builds use the native Calculator (`APPLAUNCH_APPS_NATIVE_CALCULATOR`) | removed (with the stock external Calculator path, 2.2) |
| submodule `projects/AppStore` | **yes** | `build.sh --with-store`; Settings > Apps uses its backend | kept, unchanged |
| submodule `SDK` (M5Stack_Linux_Libs) | **yes** | the SCons/Kconfig build framework of every build | kept |
| `.github/workflows/launcher-build.yml`, `emulator-build.yml`, `.github/scripts/` | no | run only on a `master` branch this repository does not have; decision X15 "no CI for now" | removed |
| `scripts/build_cardputerzero_release_local.sh`, `scripts/debian_packager.py`, `projects/APPLaunch/build_support/verify_preinstalled_desktop.py` | no | the stock `.deb` release; our releases are `pizero2w/build.sh` tarballs | removed, with their tests `test_updater_packaging.py`, `test_appstore_packaging.py`, `test_preinstalled_app_packaging.py` |
| `scripts/cardputer_adb_hotplug.c` | no | stock USB ADB gadget | removed |
| `scripts/create_settings_ui_worker.sh` | no | copies `projects/ui_test`, which does not exist | removed |
| `scripts/cp0_rpc.py`, `scripts/zmq_log_sub.py`, `docs/automation-rpc.md` | no (off) | clients of the optional ZeroMQ RPC/log of `cp0_lvgl` (`CONFIG_CP0_LVGL_USE_ZMQ_*`, off in our configs); generic toolkit feature, not device code | kept, needs owner decision |
| `projects/APPLaunch/setup.ini` | no | stock deploy helper (fixed IP, `pi`/`pi` password, stock binary name) | removed |
| `darwin_`, `mac_cross_cp0_`, `win_x86_cross_`, `win_x86_sdl2_config_defaults.mk` and their Kconfig choices and SConstruct branches | no | macOS and Windows hosts; our build box is WSL/Linux (`tools/dev/wsl-build.sh`) | removed |
| `CardputerZero=y` switch in `projects/APPLaunch/SConstruct` | no | used by the stock workflow and the macOS guide only | removed |
| `linux_x86_sdl2_config_defaults.mk` (SDL simulator build) | indirectly | the unit tests compile the SDL backend sources (`cp0_lvgl/tests`, `tests/low_battery_ui`, `tests/bluetooth_ui`) | kept (the SDL backend too) |
| `linux_x86_cross_cp0_`, `linux_x86_64_native_cp0_`, `config_defaults.mk` | **yes** | arm64 bundle, amd64 bundle, on-board native build | kept |
| `docs/upstream/` (48 files, the upstream README and guides) | no | kept "for reference" since the rebrand; linked only from our README and `AGENTS.md` | removed (credits stay in README, NAMING.md, Licenses page) |
| `docs/adb-*.md` (3), `docs/file-transfer-performance.md`, `docs/macos-docker-build*.md` (2), `docs/settings_ui_integration_task_assignment.md`, `docs/current-implementation-and-components.md` | no | stock-device features and upstream internal documents; nothing links to them except the branding allow-list | removed |
| `docs/cp0_lvgl.md` (Chinese), `docs/cp0_lvgl.ja.md` | no | translations of `docs/cp0_lvgl.en.md`, which our docs link to; they would describe removed APIs | removed; `docs/cp0_lvgl.en.md` kept and updated |
| `ext_components/nmtui`, `ext_components/bluectl` | no | M5Stack components no launcher, Store or app source includes; no config enables them | removed |
| `ext_components/Spdlog`, `ext_components/SmoothUI` | no | third-party wrappers no config enables (`CONFIG_SPDLOG_*` unset, `CONFIG_SMOOTH_UI_*` commented out) and no source includes | removed |
| `ext_components/Sigslot` | **yes (apps)** | LAN Scan builds against the launcher tree with `REQUIREMENTS ... 'Sigslot'` | kept |
| `ext_components/Miniaudio` | **yes** | `CONFIG_CP0_LVGL_INIT_AUDIO` (system sounds) | kept |
| `ext_components/RadioLib` | no | needed only by the LoRa service (2.2) | removed with it |
| `ext_components/cp0_lvgl` | **yes** | the toolkit of the launcher, the Store and every app | kept; device-only parts removed (2.2) |
| `projects/APPLaunch/APPLaunch/adb/` (stock ADB gadget scripts) | no | `build.sh` already leaves them out of the bundle; `install.sh` retires them | removed |
| `projects/APPLaunch/APPLaunch/lib/nihao.so` (empty file), `applications/vim.desktop.temple` | no | an empty placeholder and an unused template, both installed by every bundle | removed; `install.sh` retires them on upgrade |
| images `claw_*`, `zclaw_*` | no | icons of the removed ZClaw app | removed; `install.sh` retires them |
| images `lora_*`, `camera_*`, `music_*`, `rec_*`, `file_*` and other upstream app icons no code names | maybe | an app installed by the Store from its built-in hub may point its `Icon=` at them | kept, needs owner decision |
| image `logo_lcd.png` (the M5Stack logo) | no | never in our bundles (`build.sh` drops it); shown only by the legacy start-up path of builds without the dpi-scaled backend | removed from the repo (that path then shows a black screen for its 1 s) |
| other upstream images and sounds, the fonts | yes | app icons, carousel parts and sounds used by the launcher; fonts used by the font service | kept |
| boot-time `unzip /var/template.zip -d ~/Downloads` (`main.cpp`) | no | unpacks a file of the upstream OS image; our boards have none (the command fails at every start) | removed |
| `projects/APPLaunch/AGENTS.md`, `ext_components/cp0_lvgl/AGENTS.md` (agent guidance) | yes | still accurate for the code; they linked to the removed documents | kept, links fixed |

### 2.2 CardputerZero-only hardware and stock-app code

| Item | Ours? | Evidence | Action |
| --- | --- | --- | --- |
| Settings > **ExtPort** (Ext 5V, Grove 5V; M5IOE1 expander) | no | compiled out on every board of ours (`APPLAUNCH_SETTINGS_EXTPORT` 0); no board has the expander | row, `settings_extport.*`, the `GpioSet`/`GpioGet` settings commands, the saved `extport_*` restore in `commount.cpp` removed |
| `kHwIdPath` `/proc/cardputerzero_hw_id`, `kCardputerZeroGpioMap`, `kFallbackGpioMap`, the M5IOE1 backlight line (`cp0_lvgl_settings.cpp`) | no | used only by the ExtPort GPIO commands; no app reads the file (apps repo grep) | removed |
| Settings > **Developer** (ADB gadget) | no | compiled out on every board of ours; the helper scripts are not in our bundles | row, `settings_adb_*`, `AdbStatus` (process), `AdbSet`/`AdbAuthorize`/`AdbRevoke`/`AdbClearAuthorizations` (admin), `adb_helper` path removed |
| Settings > System > **Software** (update) | no | compiled out on every board of ours. It ran `apt update` and installed the launcher `.deb` from `https://github.com/CardputerZero/launcher/releases/download/launcher-latest`; our releases are tarballs installed by `install.sh`, so it cannot work for us | row, update page (`LvSettingUpdatePage3`), update model, `cp0_update_job.hpp`, `cp0_launcher_updater.hpp`, the osinfo update commands, `cp0_system_apt_update_background`, `cp0_system_update_launcher_background` and their tests removed |
| LoRa page and mesh page (`ui_app_lora*`, `ui_app_mesh*`, their models) | no | compiled, but registered as no home tile and no Settings entry: unreachable | removed |
| LoRa service in `cp0_lvgl` (`cp0_lvgl_lara.cpp`, `cp0_lora_*`, `sdl_lvgl_lara.cpp`, `cp0_signal_lora_api`, `cp0_lora_info_t`, `cp0_lora_request_stop/clear_stop`) | no | drives the CardputerZero LoRa HAT (PI4IO expander, SPI SX126x); no board of ours has it; no app or Store source calls it (Mesh Hop talks to a MeshCore node over USB, BLE or IP) | removed, with `CONFIG_CP0_LVGL_INIT_LORA` and RadioLib |
| Stock external Calculator (`@calculator_exec`, `M5CardputerZero-Calculator`) | no | every board of ours uses the native Calculator page; the submodule is removed | the external branch and the `calculator_exec` path removed; the native Calculator is the only one |
| Settings > **Battery**, bq27220 gauge driver, battery service | no today, **planned** | decision X12 (2026-10-21) adds a battery from `power_supply` in phase x-d and reuses the battery service, page and the status strip; the Store calls `cp0_battery_read` | kept, needs owner decision after x-d |
| Settings > **Speaker** (ALSA volume) | no today | compiled out on our boards, but generic ALSA code (HackBerry CM5 and x86 PCs have audio) | kept, needs owner decision |
| Settings > **Ethernet** (NetworkManager) | no today | compiled out on our boards, but generic NetworkManager code (Pi 4/5 and PCs have Ethernet) | kept, needs owner decision |
| The default hardware profile (`#else` branch of `settings_hw_profile.hpp`) | indirectly | builds without `APPLAUNCH_HW=pizero2w` (the SDL simulator) use it | kept, comment reworded ("default profile"); needs owner decision |
| Legacy (320x170) Settings renderer, legacy home page and start-up path | indirectly | used when the dpi-scaled backend is absent (SDL simulator); the native Settings shares the menu tree with it | kept, needs owner decision |
| Camera service in `cp0_lvgl` (`CONFIG_CP0_LVGL_INIT_CAMERA`) | no (off) | generic libcamera service, not enabled in any of our configs; not device-specific | kept, needs owner decision |
| SDL simulator strings: window title `M5CardputerZero`, Bluetooth alias `CardputerZero` | test-only | the SDL backend stays (unit tests compile it) | reworded to `CyberdeckZero` |
| Display-order list of stock app names (`app_display_order.cpp`: camera, rec, music, lora, ...) | maybe | orders apps the bundled Store could still install from its built-in hub | kept, needs owner decision |
| `tca8418` keymap, Cardputer key aliases (Fn, F/X/Z/C as arrows) | **yes** | the keyboard code and the legacy apps rely on them | kept |
| arm64 sysroot download `https://github.com/CardputerZero/M5CardputerZero-UserDemo/releases/download/<v>/sdk_bsp.tar.gz` (`projects/APPLaunch/SConstruct`, also the apps' SConstructs) | **yes** | the arm64 cross build needs it | kept; needs owner decision (mirror the archive in our own releases) |

## 3. Results

### 3.1 Commits

| Commit | Content | Files | Lines |
| --- | --- | --- | --- |
| 8fc6103 | this report, inventory | 1 | +102 |
| a8f2dce | upstream projects, submodules, CI, scripts, documents, unused components, macOS/Windows configs (2.1) | 514 | +30 / -95 136 |
| b6e4db7 | the storage build-contract test follows the Kconfig of a8f2dce | 1 | +1 / -6 |
| 3a9ba70 | CardputerZero-only hardware and stock-app code (2.2) | 113 | +95 / -10 154 |
| 3840217 | rewording ("stock CardputerZero" to "legacy app" / "default profile"), branding check tightened | 29 | +65 / -66 |
| (last) | docs: NAMING.md, README, OPEN_SOURCE_COMPONENTS, LICENSES note, decisions, roadmap, this section | | |

### 3.2 Size (tracked tree, `git ls-tree`, before = 2caea24, after = 3840217; the docs commit adds a few KB)

| | Before | After | Change |
| --- | --- | --- | --- |
| Tracked entries | 2 719 (2 713 files, 6 submodules) | 2 169 (2 167 files, 2 submodules: SDK, AppStore) | -550 |
| Tracked bytes | 111.7 MB | 107.1 MB | -4.6 MB (the 66 MB of fonts dominate both) |
| Working tree without `.git` | 114 MB | 108 MB | -6 MB |
| Lines | | | -105 360 / +291 (code, build and tests -85 576; documents -19 784) |

The `.git` folder does not shrink: the history is kept on purpose (no rewrite).

### 3.3 Checks

| Check | Result |
| --- | --- |
| `tools/dev/check-all.sh` (8 steps: cp0-unit, app-unit, render, compare, twice, header, contract, branding) | before: 8/8 PASS; after: 8/8 PASS |
| Render goldens (640x480, 480x320) | unchanged: no removed row was in our boards' Settings tree (they were compiled out); `compare` byte-identical |
| Install tests `projects/APPLaunch/pizero2w/tests/test-install-board.sh` (on an LF export, as on Linux) | before: 260 passed, 0 failed; after: 260 passed, 0 failed |
| arm64 bundle with the Store (`tools/dev/wsl-build.sh --with-store`, 0.8.1-cut.1, development) | BUILD OK; `cyberdeckzero-launcher-0.8.1-cut.1-arm64.tar.gz`, 56 907 709 bytes, sha256 `e4e62aa20793627828e0f6df497c07e889eac27a35047b1099c267ccd6dea7bd`; the bundle holds no removed file (no `nihao.so`, desktop template, ZClaw icon, logo or `adb/`); the one compiler warning is the existing one in `settings_rtc_page.cpp` |
| amd64 bundle with the Store (`--arch amd64`) | BUILD OK (a full build of a fresh tree, with the private `-dev` root of x-b standing in for the build box's `apt install`); `cyberdeckzero-launcher-0.8.1-cut.1-amd64.tar.gz`, 56 878 572 bytes, sha256 `516fa2370dd7f9d4b12c1cc989b38fdb6b62d343f2ad0878c225f92b3fb81dc6`; warnings: the same existing ones (`settings_rtc_page.cpp`, `lv_inv_area` in `cp0_app_lifecycle_lvgl.c`, the Store config) |
| Branding check | passes with the tightened token list and the smaller allow-list |

Unit tests that went with their code: the LoRa tests (4 in cp0_lvgl, 2 in APPLaunch), the updater tests
(`test_update_job`, `test_launcher_updater`, the stock packaging tests); `test_settings_system_update_model.cpp`
became `test_settings_system_model.cpp` (what the model keeps); `test_settings_policy`, `test_init_plan`,
`test_runner_service_shutdown`, `test_osinfo_contract`, `test_settings_menu_order`, `test_settings_native_coverage`
and `test_settings_models` follow the removals.

### 3.4 What the apps repository and the Store see

- **Store** (`projects/AppStore`, built with `--with-store`): it calls `cp0_battery_read`, `cp0_wifi_*`, `cp0_sudo_*`,
  `cp0_system_shutdown`, `cp0_file_path_c` and the config signal; none of them changed. It builds unchanged (3.3).
- **Apps**: no app source calls a removed symbol (`cp0_lora_*`, `cp0_signal_lora_api`, the update functions, the
  `GpioSet`/`AdbStatus` commands, `calculator_exec`). The public headers `cp0_lvgl_app.h` and
  `signal_register_plan.h` lost only those declarations. The headless harness of `apps/_common` compiles a fixed
  list of cp0_lvgl files; none of them was removed. LAN Scan still finds Sigslot, Miniaudio and the bq27220 service.
- **The pin moves**: `ext_components/cp0_lvgl` and `projects/APPLaunch/tests/render/config` (two `#define`s) changed,
  so `apps/_common/LAUNCHER_REF` reports "differs" for this branch until the next pin move; apps built at the current
  pin are unaffected, and a rebuild at the new pin needs no source change.

### 3.5 Merging main

This branch starts at 2caea24. Since then `main` gained #71 (time zone) and #73 (pointer pad). A trial merge
(`git merge-tree`) conflicts in five documentation files only: `docs/cp0_lvgl.en.md`, the two removed translations
of it and `docs/current-implementation-and-components.md` (changed there, removed here: keep them removed) and the Pi
port README; the code merges cleanly. Re-run check-all after the merge.

## 4. Needs owner decision

1. **Battery** page, the bq27220 driver and the battery service (compiled out on our boards): X12 (phase x-d) plans
   a `power_supply` reader that reuses the service, the page and the status strip. After x-d, the bq27220 driver can
   go.
2. **Speaker** (ALSA volume) and **Ethernet** (NetworkManager) pages: generic code, compiled out. Enable them on
   boards that have the hardware (HackBerry CM5 audio, Pi 4/5 and PC Ethernet) or remove them.
3. **Camera service** of cp0_lvgl (libcamera, off in every config of ours): keep for a future camera app or remove.
4. **Default hardware profile and legacy 320x170 renderer** (legacy Settings, legacy home page and start-up path,
   with the upstream icons it shows): only the SDL simulator uses them now. Option: make our profile the only one and
   delete the legacy renderer (large; the unit tests of the SDL backend stay).
5. **ZeroMQ automation** (`CONFIG_CP0_LVGL_USE_ZMQ_*`, off): `scripts/cp0_rpc.py`, `scripts/zmq_log_sub.py`,
   `docs/automation-rpc.md` (Chinese). Keep as a dev tool or remove.
6. **arm64 sysroot archive**: the cross build downloads `sdk_bsp.tar.gz` from the upstream UserDemo release
   (`projects/APPLaunch/SConstruct`, also the apps' SConstructs). Mirror it in our releases so the build no longer
   depends on the upstream repository.
7. **Stock app names and icons**: the display-order and "managed" lists in `app_display_order.cpp` and the upstream
   app icons in `share/images/` serve only apps the Store could install from its built-in hub (which has listed no
   app since 2026-10-12).
8. **Old binary link** `M5CardputerZero-APPLaunch`: `install.sh` keeps it "for one release" since v0.7.0; it could go.
9. Small leftovers: the empty `projects/APPLaunch/main/lvgl/lv_sdl_keyboard.c` stub (in the apps' pinned path),
   `share/font/svgfont.ttf` (no code names it, source unknown), the upstream Kickstarter and community thanks on the
   Licenses page (kept as a credit).

## 5. Kept names: future rename with migration (options 2/3, not chosen)

None of these was renamed. Each rename needs a migration; this is what depends on each name and what the rename
would take.

| Name | Depends on it | A rename would need |
| --- | --- | --- |
| `/usr/share/APPLaunch/` (install root: `bin/`, `applications/`, `share/`, `lib/`) | every installed app's files and `.desktop` `Exec=`/`Icon=`; the Store (`M5APPSTORE_APP_ROOT` default); `install.sh`/`uninstall.sh` manifests | a new root with the old one as a link, apps repackaged, the Store started with the new root, an app contract v2 note |
| `APPLAUNCH_*` environment variables and build switches | the published app contract v1 (`docs/APP-CONTRACT.md`), apps, board profiles (`board.conf`), service files | contract v2 exporting both names for a period, apps rebuilt |
| `applaunch` paths (`$XDG_RUNTIME_DIR/applaunch/`, `/tmp/applaunch-*.state`, `/etc/applaunch/board.conf`) | the contract (state files), board profiles | the same contract v2, `install.sh` moving `board.conf` |
| `cp0_` prefix, `ext_components/cp0_lvgl` | every app build (headers, `LAUNCHER_REF` pin, `apps/_common`), the Store's build | a renamed component with compatibility headers, every app rebuilt at a new pin |
| `~/.config/cardputerzero/` | the launcher's settings and tile order, apps' settings (Serial Console, viz1090), the Store's shared `appstore.registry.*` keys | move with a link left behind (the unchanged Store keeps reading the old path) |
| `~/.local/share/cardputerzero-appstore`, `~/.cache/cardputerzero-appstore`, `M5APPSTORE_*` | the Store (upstream code, bundled unchanged) | only with a replaced Store |
| `M5CardputerZero-AppStore` (Store binary) | `cp0_file_path("appstore_exec")`, Settings > Apps backend, `default-sources.sh`, `install.sh`, `uninstall.sh` | a renamed binary with a link; tied to the Store decision |
| `CardputerZero Hub` and its registry URLs | built into the Store; `default-sources.sh` recognises it | only with a replaced Store |
| `/com/cardputerzero/applaunch/agent` (BlueZ agent path) | Mesh Hop's BLE notes | a code change and a doc update, no data migration |
| `/tmp/M5CardputerZero-APPLaunch_fcntl.lock` | single-instance lock shared with legacy apps | both sides renamed together, or kept |
| `M5CardputerZero-APPLaunch` (old launcher binary, a link) | boards upgraded from v0.6.0 | dropping the link (section 4 item 8) |
| `M5CardputerZero-<app>` app binaries | `.desktop` `Exec=` of LAN Scan, Wi-Fi Survey, the MeshZero draft | the apps' next versions (report 025 decision 7) |
| `tca8418_keypad_m5stack_keymap` | the inherited matrix keyboard path (`keyboard_map`) | goes with that keyboard support, if ever |
| `SPDX-FileCopyrightText: ... M5Stack Technology CO LTD`, `LICENSE`, `LICENSES/` | the MIT licence | never: they stay |
