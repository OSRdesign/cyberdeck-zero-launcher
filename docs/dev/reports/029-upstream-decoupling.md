# 029 - Decoupling from the upstream CardputerZero launcher: remove the code we do not use

Status: inventory written 2026-10-22 on branch `decouple-upstream` (from launcher main 2caea24, v0.8.0 + report 028
and x-b). Apps repo read at origin/main 3dde2c4; the bundled Store is the `projects/AppStore` submodule at b02db12.
No board touched, nothing merged, git history unchanged.

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
| images `claw_*`, `zclaw_*`, `lora_*` | no | icons of ZClaw and of the LoRa page | removed; `install.sh` retires them |
| image `logo_lcd.png` (the M5Stack logo) | no | never in our bundles (`build.sh` drops it); shown only by the legacy start-up path of builds without the dpi-scaled backend | removed from the repo (that path then shows a black screen for its 1 s) |
| other upstream images and sounds, the fonts | yes / unknown | app icons, carousel parts and sounds used by the launcher; fonts used by the font service | kept |

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

(filled in when the work is done)

## 4. Needs owner decision

(filled in when the work is done)

## 5. Kept names: future rename with migration (options 2/3, not chosen)

(filled in when the work is done)
