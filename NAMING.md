# Naming

The product is **CyberdeckZero Launcher** (short name **CyberdeckZero**), "A touch launcher for Raspberry Pi
cyberdecks." It is an independent hobby project; see the disclaimer in the [README](README.md).

<!-- branding-allow-begin: derivation statement -->
The code is derived from the MIT-licensed CardputerZero launcher by M5Stack Technology CO LTD. Some technical names
come from that upstream project and stay as they are, because installed apps, the bundled Store and the published app
contract depend on them. They are names in code and on disk, not a brand.
<!-- branding-allow-end -->

## Product names

| Item | Name |
|---|---|
| Product | CyberdeckZero Launcher |
| Short name | CyberdeckZero |
| Tagline | A touch launcher for Raspberry Pi cyberdecks. |
| Binary | `/usr/share/APPLaunch/bin/cyberdeckzero` (13 characters, so `pkill -x cyberdeckzero` works); the old binary name stays as a symlink for one release |
| User service (appliance boards) | `cyberdeckzero.service` (replaces `APPLaunch.service`) |
| Console unit | `cyberdeckzero-console.service` (replaces `applaunch-console.service`) |
| Desktop unit | `cyberdeckzero-desktop.service` (replaces `applaunch-desktop.service`); the polkit rule is renamed to match |
| Menu entry (desktop boards) | `cyberdeckzero.desktop`, `Name=CyberdeckZero Launcher` |
| Release asset | `cyberdeckzero-launcher-<version>-arm64.tar.gz`; the first release after the rename also carries the same file as `pizero2w-bundle.tar.gz` (the older name), dropped in the release after |
| Repositories | `cyberdeck-zero-launcher`, `cyberdeck-zero-apps` (slugs unchanged) |
| Bluetooth name fallback | `CyberdeckZero` |

`install.sh` migrates a board from the old unit, binary and menu entry names to the new ones; `uninstall.sh` knows
both. See the [Pi port README](projects/APPLaunch/pizero2w/README.md).

## Technical names inherited from upstream (they stay)

| Name | Where | Why it stays |
|---|---|---|
| `APPLaunch` | project folder `projects/APPLaunch`, install root `/usr/share/APPLaunch/` (`bin/`, `applications/`, `share/`, `share/font`, `share/oui.tsv`) | installed apps put their files there and read shared assets from there; the Store installs into it (`M5APPSTORE_APP_ROOT` defaults to it) |
| `APPLAUNCH_*` | environment variables (`APPLAUNCH_TOUCH_*`, `APPLAUNCH_STD_KEYBOARD`, `APPLAUNCH_FN_KEY`, ...) and build switches | the published [app contract](docs/APP-CONTRACT.md) v1: its names never change |
| `applaunch` | `$XDG_RUNTIME_DIR/applaunch/`, `/tmp/applaunch-*.state`, `/etc/applaunch/board.conf` | app contract and board profile paths |
| `cp0_`, `CP0_`, `cp0_lvgl` | code prefix and the shared UI/HAL component `ext_components/cp0_lvgl`, SDK header names such as `cp0_app_contract.h` | used across the code and by apps built against the SDK; renaming would break every app build |
| `~/.config/cardputerzero/` | the launcher's config (`config.json`, home-grid `app_order`, shared `appstore.registry.*` keys) | existing installs keep their settings and tile order; the Store reads the shared keys |
| `~/.local/share/cardputerzero-appstore`, `~/.cache/cardputerzero-appstore` | the Store's own data and cache | the Store is M5Stack's code and is bundled unchanged |
| `M5CardputerZero-AppStore` | the Store's binary in `/usr/share/APPLaunch/bin/` | the Store's name, unchanged; the launcher starts it by that name |
| `M5APPSTORE_*` | the Store's environment variables | the Store's code, unchanged |
| `CardputerZero Hub` | the Store's built-in catalogue and its registry URLs | built into the Store, which refuses to remove it; credited in Settings and Licenses |
| `/com/cardputerzero/applaunch/agent` | the launcher's BlueZ pairing agent (D-Bus object path) | apps document it (Mesh Hop's BLE notes); changing it needs nothing on disk, but is a visible interface |
| `/tmp/M5CardputerZero-APPLaunch_fcntl.lock` | single-instance lock file | shared with legacy apps |
| `M5CardputerZero-APPLaunch` | the launcher binary's name of v0.6.0 and before | kept by `install.sh` as a link to `cyberdeckzero` |
| `M5CardputerZero-<app>` | binary names of some apps (`/usr/share/APPLaunch/bin/`: LAN Scan, Wi-Fi Survey, the MeshZero draft) | named in their `.desktop` `Exec=` lines; the apps drop the prefix at their next version (report 025 decision 7) |
| `tca8418_keypad_m5stack_keymap` | keymap file of the inherited keyboard path (`cp0_file_path("keyboard_map")`) | a system file name |

Copyright notices (`LICENSE`, the `SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD` headers, `LICENSES/`)
stay exactly as they are: the MIT licence requires them.

Renaming any of these is possible later, but only with a migration (install.sh moving folders and config, an
alias period for the binary and paths, a new app contract version, the apps rebuilt); report 029 section 5 lists
them with what each rename would need.

## Decoupling from upstream (2026-10-22, report 029)

On the owner's decision of 2026-10-22 the repository no longer carries upstream code that CyberdeckZero does not
use. Removed: the upstream app and demo projects (ZClaw, LaunchWizard, UserDemo, HelloWorld, the emulator,
CameraApp) and the Calculator and Keyboard-Guide submodules; the inherited CI workflows and `.deb` release scripts;
the upstream documentation (`docs/upstream/`, ADB, macOS build guides) and the Chinese and Japanese copies of the
cp0_lvgl reference; the unused nmtui, bluectl, Spdlog, SmoothUI and RadioLib components; the macOS and Windows build
configurations; and the code for hardware our boards do not have: Settings > ExtPort (M5IOE1 expander), Developer
(USB ADB), System > Software (the upstream `.deb` updater), the LoRa service and pages, the external stock
Calculator, the first-boot wizard hooks. Git history keeps all of it.

Kept: every copyright notice and licence text; the names above; the bundled Store (built from the `projects/AppStore`
submodule, unchanged) and the SDK submodule (the build framework); the native UI, the dpi-scaled display, the
compat/VFB layer for legacy fullscreen apps, Settings, multitasking, the app contract, the amd64 build and the board
tooling; and, until the owner decides (report 029 section 4), the Battery, Speaker and Ethernet pages (compiled
out on our boards), the camera service, the default hardware profile and the legacy renderer used by the SDL
simulator.

## Checking

`tools/dev/check-branding.sh` (a step of `tools/dev/check-all.sh`) fails when a tracked file names M5Stack or
CardputerZero outside these technical names, the credits and the allow-listed paths
(`tools/dev/branding-allow.txt`). Run `tools/dev/check-branding.sh -h` for the rules.
