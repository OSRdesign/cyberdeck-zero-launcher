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
| `/proc/cardputerzero_hw_id` | hardware id file of the stock device | read by apps and the launcher to tell the stock hardware apart |
| `/tmp/M5CardputerZero-APPLaunch_fcntl.lock` | single-instance lock file | shared with the stock apps |
| `M5CardputerZero-Calculator` | the stock Calculator app's binary | upstream app, unchanged |
| `M5IOE1`, `tca8418_keypad_m5stack_keymap` | I/O expander and keymap of the stock hardware | hardware and system file names |

Copyright notices (`LICENSE`, the `SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD` headers, `LICENSES/`)
stay exactly as they are: the MIT licence requires them.

## Checking

`tools/dev/check-branding.sh` (a step of `tools/dev/check-all.sh`) fails when a tracked file names M5Stack or
CardputerZero outside these technical names, the credits and the allow-listed paths
(`tools/dev/branding-allow.txt`). Run `tools/dev/check-branding.sh -h` for the rules.
