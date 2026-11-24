# CyberdeckZero Launcher on Raspberry Pi boards

CyberdeckZero Launcher on Raspberry Pi boards with a touch panel and a keyboard. Supported boards: see [Boards](#boards)
(the **Pi Zero 2 W deck** with the Waveshare 2.8" DPI touch LCD, 480x640 portrait used as 640x480 landscape, and a
Bluetooth keyboard; the **Pi 3A+** with a 3.5" SPI or a 4.3" DSI panel; the **Pi 4 B** with a 5" DSI panel; the **Pi 5**
with the HyperPixel 4" or the 7" DSI panel; the **HackBerry CM5** (720x720); a **Pi Zero 2 W** with the Waveshare 3.5"
SPI panel; any Raspberry Pi with the Luckfox or Waveshare 3.5" SPI panel, set up by `--spi-panel luckfox35`; an
**Orange Pi Zero 2W** with Armbian and the Waveshare 3.5" (F), set up by `--spi-panel waveshare35f`) and PCs. Since phase x-b of the x86 port (report 028)
the same launcher also installs on **PCs** (x86_64, the amd64 bundle; the Panasonic FZ-M1 tablet first): see
[Install on a PC](#install-on-a-pc-amd64). The port is selected at run time with
`APPLAUNCH_DISPLAY=dpi-scaled` and at build time with `APPLAUNCH_HW=pizero2w` (every bundle sets both); without them
the code keeps the behaviour it was inherited with (the SDL simulator uses that). See [`NAMING.md`](../../../NAMING.md) for the product and technical names.

## Names and migration (v0.7.0)

The launcher's own names changed with the rename to CyberdeckZero Launcher. Paths, environment variables and config
locations did not (`/usr/share/APPLaunch`, `APPLAUNCH_*`, `~/.config/cardputerzero`, the Store's folders).

| Item | New name | Replaces |
| --- | --- | --- |
| Binary | `/usr/share/APPLaunch/bin/cyberdeckzero` (`pkill -x cyberdeckzero` works) | the old binary name, kept as a symlink for one release |
| User service (appliance boards) | `cyberdeckzero.service` | `APPLaunch.service` |
| Console unit | `cyberdeckzero-console.service` | `applaunch-console.service` |
| Desktop unit (desktop boards) | `cyberdeckzero-desktop.service` | `applaunch-desktop.service` |
| PolicyKit rule (desktop boards) | renamed to match the desktop unit | `52-applaunch-desktop.rules` |
| Menu entry (desktop boards) | `cyberdeckzero.desktop`, "CyberdeckZero Launcher" | `applaunch.desktop` |
| Release asset | `cyberdeckzero-launcher-<version>-arm64.tar.gz` (the first release after the rename also carries it as `pizero2w-bundle.tar.gz`); PCs: `cyberdeckzero-launcher-<version>-amd64.tar.gz` | `pizero2w-bundle.tar.gz` |

`install.sh` migrates a board installed with the old names: it disables and removes the old user unit (also when
lingering keeps it running), removes the retired unit, rule and menu files, installs the new ones and keeps the old
binary name as a symlink. `uninstall.sh` knows both the old and the new names. Rolling back to v0.6.0:
`systemctl --user disable --now cyberdeckzero.service` before installing v0.6.0.

## What the port adds

| Area | What it does |
| --- | --- |
| Display | Two LVGL displays share `/dev/fb0`: a **native 640x480** one (home grid, terminal, Calculator) and the legacy **320x170** one drawn 2x into a window at the top (`cp0_lvgl_dpi_scaled.c`, `cp0_display.h`). |
| Home | Touch-friendly app grid with clock, Wi-Fi bars and Bluetooth state (`native_ui.cpp`). A tile's name is one line (v0.9.3): a long name gets a smaller font, down to 80 % of the normal size, then ends in dots; it never runs over the icon. **Move mode** (v0.8.12): hold a tile 3 s (a bar shows the hold) or press Ctrl+O on the selected tile, then the arrows move it, a tap on another place puts it there, Enter drops it and Esc cancels; the order is saved (`home_order` in `config.json`) and **Settings > Launcher > Reset app order** puts the default back. |
| Terminal | CLI, Python and SSH use a full-screen terminal with touch scrolling (`ui_app_st*.cpp`). The grid fills the screen (v0.10.6: columns = width / text cell, rows = height / text cell, 20 x 8 to 255 x 255; covered by unit tests and renders, not yet tried on a board); F6 switches to an 80 x 24 view. The text size is **Settings > Screen > Terminal text size** (v0.10.5): Auto (`APPLAUNCH_TERM_FONT` of `board.conf`, else 20 px, 14 px on a portrait screen under 480 px wide), 14, 17, 20 or 24 px, each with its grid as a grey note; it applies the next time the terminal opens. |
| Calculator | Native touch + keyboard calculator (the only one: the external 320x170 calculator program of the inherited code is gone). |
| Quick controls | A built-in tile (`app_QuickControls`, hide it in Settings > Launcher or with `APPLAUNCH_HIDE_TILES`) opening a page of four big buttons: Shutdown, Reboot, Wi-Fi, Bluetooth. No confirmation: a tap or Enter acts at once. Wi-Fi and Bluetooth switch the radio and show On / Off (dot and word); a board without that adapter shows Not available (adapters found in sysfs, no process started). Shutdown and Reboot are the same `systemctl poweroff -i` / `reboot -i` as Settings > System, run as the launcher's user (logind, no password). Page: `ui_app_quickctl.cpp`, logic `model/quick_controls_model.*`, services `quick_controls_backend.cpp`; board tests: `docs/dev/tests/quick-controls-board-tests.md`. |
| Legacy apps | Fullscreen 320x170 framebuffer apps (the older apps, installed from a source in Settings > Apps) run unmodified, scaled 2x: an `LD_PRELOAD` shim (`vfb/`) gives them a virtual 320x170 framebuffer and a virtual keyboard (`native_vkbd.cpp`, uinput) that carries both the physical keys and the on-screen toolbar. |
| Touch | Toolbar (Esc, arrows, Enter) under the scaled window; Settings (native, full screen) takes taps and drags directly, and games are driven by swipe/tap gestures; **Settings > Touch** chooses the behaviour per app. |
| Screensaver | After the DarkTime timeout the backlight dims to the idle dim level (**Settings > Screen > Idle dim level**, v0.10.5: Default, the board profile's `APPLAUNCH_DIM_PERCENT`, else 10 %; or 5, 10, 20, 30, 50 %; an on/off backlight stays on and has no such row) and a big HH:MM clock with the date is shown; any touch or key wakes it (`native_screensaver.cpp`). |
| SSH indicator | While someone is connected to the board over SSH (an incoming connection to its sshd; `ssh localhost` counts), the status strip shows a terminal glyph (`>_`) in its leftmost place: blue, alternating blue and white once a second while data flows (typing, output, a copy). Nothing without a session; the place stays reserved, so the page headers do not move when one starts. On a board with a battery its percentage hides while the glyph shows. **Settings > System > SSH indicator** Off stops it (On by default); the clock screensaver covers it. Apps show it once rebuilt with a toolkit of v0.8.12 or later (`status.state` `ssh=`, `docs/APP-CONTRACT.md` section 5b). The launcher reads `/proc/net/tcp*` and `/etc/ssh/sshd_config` (ports; `APPLAUNCH_SSH_PORTS=22,2222` in `board.conf` overrides them) as the launcher user: no root, no new permission (`cp0_ssh_watch.h`). |
| Settings | **Speaker, Battery and Screen > Brightness are detected at run time** each time Settings opens (`cp0_capabilities`, report 030): a row shows only when the board has that hardware, see [Sound, battery and brightness per board](#sound-battery-and-brightness-per-board). The hardware profile (`settings_hw_profile.hpp`) still hides Ethernet and the upstream bq27220 battery pages and adds Shutdown (the upstream device's ExtPort, ADB and software-update pages were removed, report 029); Wi-Fi, Bluetooth, Date & Time work without sudo prompts. **Date & Time > Time zone** picks the zone (region, then city; independent of Network Time) through timedated: the clock follows at once, no restart (when PolicyKit refuses, the sudo password prompt asks first). **Battery** (after Bluetooth) shows only on a board with a Linux `power_supply` battery (a PC's ACPI `BAT0`/`BAT1`, a gauge driver's; read when Settings opens): charge, status, time left or to full, power, voltage, health, device, refreshed every 2 s; the status strip then shows the battery's level and percentage left of the Bluetooth icon (green while charging). The Pi boards have none: no row, nothing in the strip. **Settings > Screen** also has **PDA mode** (v0.8.9, where portrait fits; `docs/PDA-PROFILE.md`), **Idle dim level** (v0.10.5, where the backlight has levels; see Screensaver), **Terminal text size** (v0.10.5; see Terminal) and **Screen size** (v0.10.5, on a board with a profile; [Screen size](#screen-size)). **Settings > Updates** updates the launcher and the apt packages ([Settings > Updates](#settings--updates)). |
| Responsive shell | One code base for several screens: `cp0_ui_metrics` computes the layout (screen class, density, tokens) from the logical size and the panel size in mm (`APPLAUNCH_PANEL_MM=WxH`, written by `install.sh` for known panels); the home grid, status bar, toolbar and the native Settings (the default on the Pi boards since task 014 P3e; no switch) use it. A fallback on-screen keyboard (`APPLAUNCH_OSK=force|auto|off`) shows only when no physical keyboard is present (a keyboard listed in `APPLAUNCH_KBD_IGNORE` does not count); in a terminal it has an **Alt** key next to Ctrl (one-shot, double tap locks: Alt then Tab is Alt+Tab). Developer notes: `docs/dev/tasks/014-responsive-shell.md`. |
| Apps | **Settings > Apps** installs/removes apps from GitHub sources (`settings_native_apps_page.cpp`). `install.sh` adds the project's apps repo (`OSRdesign/cyberdeck-zero-apps`: Mesh Hop, Net Toolbox, viz1090, Home Assistant, ...) once for a user who has no source yet, so **Settings > Apps > Available** lists its apps out of the box; add your own under Sources. Available lists only the apps whose registry `min_size` fits the screen (v0.9.2; a portrait screen is judged turned, installed apps always stay listed), and an app's page shows Screen and Minimum size. Opening the page checks the sources for app updates in the background (v0.10.0; **Auto-check updates** at the end of Sources, On by default). Libraries an app needs that are missing are installed without apt's recommended packages (v0.10.2, the bundled Store fork's one change). See `docs/HOSTING-APPS.md`. |
| App contract | Every app the launcher starts learns its mode (`compat` / `fullscreen`), window, canvas and scale, the panel's size, rotation, density, framebuffer and touch device, the design tokens and the on-screen keyboard rule (environment, `$XDG_RUNTIME_DIR/applaunch/screen.state` and `input.state`); `X-MinSize=WxH` in a `.desktop` hides an app on a smaller screen. See `docs/APP-CONTRACT.md`; test app `tools/dev/app-contract-probe`. |
| Launcher | **Settings > Launcher** shows or hides the built-in tiles, and **Reset app order** (v0.8.12) puts back the default order of the home grid after tiles were moved. The Store has no tile any more (its program still ships, as the back end of Settings > Apps). |
| Keyboard | A standard keyboard is supported (`APPLAUNCH_STD_KEYBOARD=1` disables the inherited matrix keymap); the Bluetooth keyboard re-attaches after sleep. Volume, mute and brightness keys work from any device that has them, a keyboard or not (a tablet's buttons, the ACPI video bus, a headset's buttons: media-key devices, `docs/APP-CONTRACT.md` section 14), on the launcher's pages and inside apps; such a device never counts as a keyboard. Pointer navigation (Settings > System, off by default) turns a mouse or trackpoint into arrows, Enter and the options key; on the HackBerry a quick tap on the pad does not select, press the pad's button (`APPLAUNCH_POINTER_MIN_CLICK_MS`, see `docs/APP-CONTRACT.md` section 10b). |
| Multitasking | Since v0.8.0: up to 4 apps stay open; three fingers held 1 s, a 0.5 s long-press on the status strip, Alt+Tab, Super tapped twice (within 0.45 s) or held 0.6 s open "Open apps", Super tapped once goes home; apps opt in with `X-Background=run\|freeze` (`docs/APP-CONTRACT.md` section 13). On by default on canvases whose short side is 480 or more; see [Multitasking](#multitasking). Code: `main/ui/multitask/`. |

## Install on a Pi

1. **Prepare the Pi**: Raspberry Pi OS Lite 64-bit (Debian 13), Wi-Fi configured, SSH enabled.
   Add the panel configuration from `config.txt.snippet` to `/boot/firmware/config.txt` and reboot
   (`/dev/fb0` must be 640x480 at 32 bpp). Pair the Bluetooth keyboard with `bluetoothctl`.
2. **Get the bundle**: download `cyberdeckzero-launcher-<version>-arm64.tar.gz` from the releases, or build it on a
   Linux x86_64 host or WSL: `projects/APPLaunch/pizero2w/build.sh [--with-store]`
   (needs `gcc-aarch64-linux-gnu g++-aarch64-linux-gnu pkg-config libffi-dev libfreetype6-dev python3-venv`).
3. **Copy and install**: copy the bundle tarball to the Pi, unpack it, then
   `cd bundle && sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"`.
   Reboot if the script says so (PWM backlight, console cursor).

`install.sh` is idempotent. It installs the launcher and assets under `/usr/share/APPLaunch`, the udev
and PolicyKit rules, the user service (with lingering, so it starts at boot), the boot-time Network Time
service, masks the text console on tty1, frees the panel from the kernel text console once the boot is done
(`cyberdeckzero-console.service`, see [Text console](#text-console)) and adds the PWM backlight overlay (original files are
kept as `*.bak-applaunch`). It also installs the anchor package `cyberdeckzero-runtime` ([Settings > Updates](#settings--updates)) and the
network share helper with `cifs-utils` ([Network shares helper](#network-shares-helper)). On a board that boots to a desktop it installs the desktop variant instead (see
[Desktop boards](#desktop-boards)). The end of the install names the session and why (`session: appliance (...)` or
`session: desktop (...)`). **If you install or remove a desktop later, run `install.sh` again**: it switches between
the two variants ([Switching between appliance and desktop](#switching-between-appliance-and-desktop)). To take it all
off again, see [Uninstall](#uninstall).

**Removable drives.** Every variant (appliance, desktop, PC) also writes the PolicyKit rule
`/etc/polkit-1/rules.d/53-cyberdeckzero-udisks.rules` for the install user (`--user`) and only that user: it lets them
mount removable media through udisks (`udisksctl mount -b /dev/sda1`: the actions `org.freedesktop.udisks2.filesystem-mount`
and `-mount-other-seat`) without a password. The launcher runs as a user service in a background login session, and
PolicyKit's "active session" rule does not cover that, so without the file udisks answers "Not authorized". A user can
always unmount what they mounted. Fixed disks (`filesystem-mount-system`), encryption, formatting and the rest are not
touched. The file is rewritten at each run and removed by `uninstall.sh`; `install.sh --dry-run` lists it and `--plan`
prints `udisks_rule=`. It does not install `udisks2` itself (on a Lite image: `sudo apt install udisks2`).

It refuses to run (and changes nothing) on a system with glibc older than 2.38: the launcher and the apps are built
for Debian 13 trixie. It also refuses a bundle of the other architecture (it reads the launcher's ELF header: arm64 on
a Pi, amd64 on a PC). `sudo ./install.sh --dry-run` prints the detected board and what would be done.

### Install options

`sudo ./install.sh [options]`; `-h` prints the option list. Without options `install.sh` detects the board and
chooses the session itself.

| Option | Effect | Explained in |
| --- | --- | --- |
| `--keyboard-name NAME` | The Bluetooth keyboard's name as `bluetoothctl devices` shows it (default `M4 Keyboard`). | [Install on a Pi](#install-on-a-pi) |
| `--user NAME` | The user that runs the launcher (default: the user who ran `sudo`). | [Install on a Pi](#install-on-a-pi) |
| `--no-boot-config` | Do not touch `config.txt` and `cmdline.txt` (no PWM backlight). Also wins over `--pwm-backlight`. | [Reverting the PWM backlight](#reverting-the-pwm-backlight) |
| `--pwm-backlight` | Pi 5 + HyperPixel 4 only: turn its on/off backlight into a PWM one with levels (a `config.txt` line, a reboot). Never done without this option. | [Boards](#boards), "Pi 5 + HyperPixel 4: brightness levels" |
| `--board NAME` | Use this profile instead of detecting the board: `zero2w`, `pi3a-luckfox35`, `pi3a-wsdsi43`, `pi3a-wsdsi43-portrait` (never detected), `pi4b-dsi`, `pi5-hyperpixel4`, `pi5-dsi7`, `hackberry-cm5`, `generic`; `--board zero2w` writes no profile. `--board generic` is for a screen that has no profile (untested hardware): the profile is written from what is detected. | [My screen is not listed](#my-screen-is-not-listed) |
| `--screen-size INCHES` | The screen's **diagonal in inches** (1.5 to 15, a decimal such as `2.8`, `3.5`, `4.3`, `5`, `7`). Sets `APPLAUNCH_PANEL_MM` in the profile (the layout's density) from the diagonal and the profile's resolution, and nothing else (board, framebuffer, rotation, touch stay the profile's); for 5 on the DSI panels' backlight regulator also the idle dim level 20 that `pi4b-dsi` has. Use it when the profile's size is wrong (the DSI panels look the same to software) or with `--board generic`. Accepted on every profile that has a screen; the deck (`zero2w`, no profile) refuses it. | [Screen size](#screen-size) |
| `--panel-mm WxH` | The panel's visible area in mm, exactly (20 to 500 each); wins over `--screen-size`. | [Screen size](#screen-size) |
| `--board-only` | Only detect the board and write or update `/etc/applaunch/board.conf` (and the backlight rule), then stop. | [Boards](#boards) |
| `--dry-run` | Print the detected board, the session and what would be done; change nothing. | [Install on a Pi](#install-on-a-pi) |
| `--plan` | The dry run as `key=value` lines for a program (Settings > Updates' helper). | [Settings > Updates](#settings--updates) |
| `--force` | Continue on an unknown board (no profile is written); replace a hand-edited `board.conf` (the old one is kept as `board.conf.bak`). | [Boards](#boards) |
| `--no-default-source` | Do not add the apps repo to Settings > Apps for a user who has no source yet. | "Default app source", below |
| `--spi-panel luckfox35` | A **Raspberry Pi** (a Pi 5, a Zero 2 W, a Pi 3A+, ...; `waveshare35f` is the same panel family and also accepted there): set up the Luckfox 3.5" RPi LCD (CTP) / Waveshare 3.5" RPi LCD (F) (ST7796S + GT911) in `/boot/firmware/config.txt` (a block between marker lines in `[all]`, the init file in `/lib/firmware`; PWM backlight on a Zero 2 W and a Pi 5, channel 2 there), write the `pi3a-luckfox35` profile, ask for a reboot. **Edits `config.txt` also on a desktop board** (the option is explicit); `uninstall.sh` undoes it. Refused without `/boot/firmware/config.txt`, with `--board-only`, `--no-boot-config` or `--soft-pwm-backlight`. | [Raspberry Pi: the 3.5" SPI panel](#raspberry-pi-the-35-spi-panel) |
| `--spi-panel waveshare35f` | Armbian on an Allwinner H616 / H618 board (Orange Pi Zero 2W): set up the Waveshare 3.5" RPi LCD (F) on the 40-pin header (its init file, the driver module, a DKMS build of `drm_mipi_dbi` for the SoC's DMA limit, the overlay in `/boot/armbianEnv.txt`) and write the `pi3a-luckfox35` profile; then reboot. Refused elsewhere; not with `--board-only` or `--no-boot-config`. | [Orange Pi Zero 2W: the 3.5" SPI panel](#orange-pi-zero-2w-the-35-spi-panel) |
| `--soft-pwm-backlight` | With `--spi-panel waveshare35f` (Armbian; refused on a Raspberry Pi): 10 brightness levels by software PWM on header pin 12 (DKMS module `pwm-gpio`) instead of the on/off backlight. | [Orange Pi Zero 2W: the 3.5" SPI panel](#orange-pi-zero-2w-the-35-spi-panel) |
| `--network-manager` | Debian / Ubuntu with netplan and no NetworkManager renderer yet (Armbian, Ubuntu server): install `network-manager` and `polkitd`, hand the network to NetworkManager at the next boot (Settings > Wi-Fi); then reboot. | [NetworkManager by install.sh](#networkmanager-by-installsh---network-manager) |
| `--yes` | With `--network-manager`: go ahead when the default route is over Wi-Fi (an ssh session over it drops at the reboot). Without it a terminal is asked, a run without one is refused. | [NetworkManager by install.sh](#networkmanager-by-installsh---network-manager) |
| `-h`, `--help` | Print the option list. | |

Each install records its keyboard name, `--board`, `--screen-size`, `--panel-mm`, `--no-boot-config`, `--pwm-backlight` and `--no-default-source` in
`/etc/applaunch/install.conf` (`screen_size=` and `panel_mm=` are empty when none was given); Settings > Updates installs a newer release by running `install.sh` again with them.
The other options (`--user`, `--board-only`, `--dry-run`, `--plan`, `--force`, `--network-manager`, `--yes`) are not recorded.
`--spi-panel` records `board=pi3a-luckfox35` (so updates keep the profile) and, for information only, `spi_panel=` and
`soft_pwm_backlight=`, which later runs keep (on a Raspberry Pi also `spi_fw_added=1`, which tells `uninstall.sh` that `install.sh`
put `/lib/firmware/st7796s.bin` there; on Armbian `spi_armbian_files=`, `spi_armbian_dkms=` and `spi_armbian_env=`, what `install.sh` made, for the same purpose, see [Orange Pi Zero 2W: the 3.5" SPI panel](#orange-pi-zero-2w-the-35-spi-panel)); the panel's set-up is done once and is never run again by Settings > Updates.

**Default app source.** `install.sh` runs `default-sources.sh` as the launcher user (the user's own file, no root, no
password). When that user has no app source yet (no `~/.local/share/cardputerzero-appstore/registries.json`, or one
with only the Store's built-in CardputerZero Hub), it adds `OSRdesign/cyberdeck-zero-apps` in the Store's own format
and syncs it once, so Settings > Apps > Available lists its apps after the install. A list with sources of the user's
own is never changed. The decision is taken once per user (marker file `applaunch-default-source` in that folder):
a source you remove or switch off in Settings > Apps > Sources stays removed after the next `install.sh`. Offline
during the install, the source is added but not synced: the next opening of Settings > Apps with a network fetches it
(its check for updates, `docs/HOSTING-APPS.md`), or Settings > Apps > Sources > Sync my sources (key `S`) at once. `--no-default-source` skips all of this. By hand, as the user (never with sudo):
`sh default-sources.sh --dry-run` shows what it would do, `sh default-sources.sh` does it.

## Install on a PC (amd64)

Phase x-b of the x86 port (report 028, decisions X1-X15 of 2026-10-21): the same launcher, fbdev and LVGL with no
display server, on an x86_64 PC with a built-in touch panel. Reference machine: the Panasonic FZ-M1 mk2 tablet
(`pc-fzm1-2`, Armbian on Ubuntu 26.04). On amd64, Settings > Apps reads `registry-amd64.json` of the apps repo (the
arm64 boards keep `registry.json`), and the two CardputerZero hubs are not added to the source list (their packages
are arm64 only). A source given as a full registry address is used as typed.

**Requirements.**
- glibc 2.38 or newer: Debian 13, Ubuntu 24.04 or 26.04, Armbian on them (not Debian 12 or Ubuntu 22.04). Any x86-64
  CPU (no AVX needed). systemd, logind, polkit, udev.
- The DRM driver's framebuffer `/dev/fb0` (`CONFIG_DRM_FBDEV_EMULATION`, `CONFIG_FB_DEVICE`; i915 has it), a touch
  panel that reports as a direct touch device, a keyboard (USB or Bluetooth; the on-screen keyboard is the fallback).
- **polkit and BlueZ**: install.sh installs `polkitd` (logind and timedated ask polkit before Settings > Reboot, Shut
  down or a clock change from the launcher's user service: without it they refuse, and the rules cannot apply) and
  `bluez` (Settings > Bluetooth), starts `bluetooth.service`, adds the user to the `bluetooth` group, and checks that
  polkit answers (a WARNING names what will not work otherwise). The power and clock rule allows the `sudo` group.
- **Power key (Panasonic FZ-M1 only: DMI `FZM1-2*` / `FZ-M1*`; no other PC, no Pi).** Armbian's logind default powers
  the tablet off on a short press of the power key. install.sh writes
  `/etc/systemd/logind.conf.d/50-cyberdeckzero-powerkey.conf` (`[Login]`, `HandlePowerKey=suspend`,
  `HandlePowerKeyLongPress=poweroff`: a short press is standby, a long press powers off) and tells logind to re-read its
  settings (`systemctl kill -s HUP systemd-logind`; logind is never restarted, so an ssh or desktop session survives).
  A file of that name that already has these two settings (a hand-made one, whatever its comments) is left as it is
  ("already there"); one with other settings is kept and said so; `--force` replaces it and keeps the old one as
  `50-cyberdeckzero-powerkey.conf.bak`. `install.conf` records `powerkey_dropin=created` when install.sh wrote the file,
  and `uninstall.sh` removes it only then, and only while it still has the settings install.sh wrote; a hand-made or
  edited file stays. `--dry-run` says which of these will happen; `--board-only` writes nothing.
- **NetworkManager** for Settings > Wi-Fi: install it yourself (see below), or let `install.sh --network-manager` do it
  ([NetworkManager by install.sh](#networkmanager-by-installsh---network-manager)). Without that option install.sh does
  not: it takes the network over from netplan / systemd-networkd. The end of the install prints the recipe when `nmcli` is missing.

**NetworkManager on a netplan system** (the FZ-M1's Armbian: systemd-networkd and a netplan Wi-Fi entry). On the
console, not from an ssh session over Wi-Fi (the link drops while it switches, and at the reboot):

```
sudo apt install network-manager
sudo cp -a /etc/netplan /etc/netplan.bak-applaunch
echo 'network: {version: 2, renderer: NetworkManager}' | sudo tee /etc/netplan/90-applaunch-networkmanager.yaml
sudo chmod 600 /etc/netplan/90-applaunch-networkmanager.yaml
sudo netplan apply
sudo reboot
nmcli -t device status          # after the reboot: the Wi-Fi line ends in ":connected", not ":unavailable"
```

**Reboot after `netplan apply`** (measured on the FZ-M1): netplan's own Wi-Fi supplicant,
`netplan-wpa-<interface>.service` (a `wpa_supplicant -c /run/netplan/wpa-<interface>.conf`), keeps running after
the apply although its unit file is gone, and holds the interface. NetworkManager then logs "wpa_supplicant couldn't
grab this interface", the device stays `unavailable` and Settings > Wi-Fi says "Wi-Fi service unavailable". The reboot
ends it; without one, `sudo systemctl stop netplan-wpa-<interface>.service` on the console does too.

The netplan Wi-Fi entries then come up under NetworkManager (`netplan-<interface>-<SSID>`; the last file's `renderer`
wins). A Wi-Fi entry with its own `renderer: networkd` line keeps networkd: change that line too (the backup is in
`/etc/netplan.bak-applaunch`). If the boot then waits about 2 minutes for the network (`systemd-networkd-wait-online`,
nothing left for networkd to bring up): `sudo systemctl disable systemd-networkd-wait-online.service`. Roll back, on
the console, with a reboot as well:
`sudo rm /etc/netplan/90-applaunch-networkmanager.yaml && sudo apt remove network-manager && sudo netplan apply && sudo reboot`
(and `sudo systemctl enable systemd-networkd-wait-online.service` if you disabled it).

**Get the bundle**: `cyberdeckzero-launcher-<version>-amd64.tar.gz` from the releases (from v0.8.1), or build it natively on an x86_64 Linux or WSL box:
`projects/APPLaunch/pizero2w/build.sh --arch amd64 [--with-store]` (or `tools/dev/wsl-build.sh --arch amd64
--with-store`). It needs `gcc g++ pkg-config libinput-dev libxkbcommon-dev libudev-dev libdbus-1-dev libglib2.0-dev
libfreetype-dev`, and `libssl-dev` for the Store (the Settings > Apps engine). The layout is the arm64 bundle's.

**Install**: unpack it on the PC and run `cd bundle && sudo ./install.sh`, as on a Pi. On a PC (x86_64, no device
tree) it writes the `pc` profile (see [Boards](#boards)), installs `libinput10 libxkbcommon0 libfreetype6 curl polkitd
bluez alsa-utils` when missing and names any other library the launcher lacks, creates the `netdev` group if the
distribution has none, adds the user to the `audio` group when the PC has a sound card of its own (not HDMI; any board
with one gets it, see [Sound, battery and brightness per board](#sound-battery-and-brightness-per-board)), and
installs the user service with lingering, the console unit (it unbinds the kernel's framebuffer console, `vtcon1` on
the FZ-M1, as on the Pi boards) and the udev and PolicyKit rules. **No boot configuration is changed**: GRUB and the
kernel command line stay as they are (the boot lines show, then the launcher). A PC that boots to a desktop (a display
manager) gets the desktop variant, as a Pi does, with the same `pc` profile; a desktop installed later on a PC installed
as an appliance: run `install.sh` again (the FZ-M1 with Armbian's XFCE:
[Desktop on the FZ-M1 tablet](#desktop-on-the-fz-m1-tablet-armbian-xfce)).

**Known limits.** The FZ-M1's touch reports at most two contacts: the three-finger switcher gesture cannot fire there;
use the clock long-press, the Windows button pressed twice (its button cannot be held: it reports press and release at once), or Alt+Tab (the terminal's on-screen keyboard has Alt). Its
volume and brightness buttons are not on a keyboard device; the launcher reads them anyway (media-key devices,
`docs/APP-CONTRACT.md` section 14). The battery (`BAT1`) shows in the
status strip and in Settings > Battery. Settings > Apps reads `registry-amd64.json` of the apps repository (since v0.8.2; every published app has an amd64 package today). The profile still writes `APPLAUNCH_HIDE_TILES=app_Store` (from when the Store tile existed; the Store's hub has arm64
packages only); since v0.9.0 there is no Store tile, so the key names no tile and is ignored.

### NetworkManager by install.sh (`--network-manager`)

On a Debian or Ubuntu system with netplan and no NetworkManager renderer yet (Armbian on an Orange Pi or the FZ-M1, an
Ubuntu server image), `sudo ./install.sh --network-manager` does the netplan steps above itself, at the **end** of the
install (the launcher is in place first):

1. `apt-get install --no-install-recommends network-manager polkitd` (polkitd with the launcher's packages already);
2. `/etc/netplan` copied to `/etc/netplan.bak-applaunch` (once: a later run keeps the first copy);
3. `/etc/netplan/90-applaunch-networkmanager.yaml` = `network: {version: 2, renderer: NetworkManager}`, mode 600;
4. `netplan generate`, **not** `netplan apply`: the network switches at the next boot, so the session that runs the
   install keeps its link until then.

Then it says **reboot needed**. After the reboot the netplan Wi-Fi entries come up under NetworkManager by themselves
(tested on the Orange Pi Zero 2W, 2026-11-21: the Wi-Fi reconnected and Settings > Wi-Fi worked). **An ssh session over
Wi-Fi drops at that reboot**; when the default route goes over a Wi-Fi interface, install.sh asks first on a terminal
(`Go on? [y/N]`, before anything is changed) and refuses a run without a terminal unless `--yes` is given. Over Ethernet
or on the console it does not ask. Refused (nothing changed) without `/etc/debian_version` or without `/etc/netplan` (a
Debian with ifupdown: install it by hand, above); nothing to do when NetworkManager is installed and a netplan file
already names it as renderer. If apt cannot install the packages, the launcher stays installed, netplan is left as it
was and the end of the install prints the console recipe. `--dry-run` lists the steps and the default route's interface.
Not recorded in `install.conf` (Settings > Updates never does it). Check after the reboot: `nmcli -t device status` (the
Wi-Fi line `:connected`). Roll back, on the console: `sudo rm /etc/netplan/90-applaunch-networkmanager.yaml && sudo apt
remove network-manager && sudo netplan apply && sudo reboot` (or put `/etc/netplan.bak-applaunch` back).

## Settings > Updates

Since v0.8.13 (report 047; what the user sees: [README, Updating the launcher](../../../README.md#updating-the-launcher)).
The launcher checks GitHub's release list as the user, downloads the bundle of its architecture
(`cyberdeckzero-launcher-<version>-<arch>.tar.gz`) to `~/.cache/cyberdeckzero/update` and checks its sha256 against the
release's `.sha256` asset and GitHub's digest. Install is offered only on a `release`-channel build with a plain X.Y.Z
version; it needs 300 MB free for the download and, on a board with a battery, 50 % or the charger. After the
password, the shared sudo prompt runs one command:

```
systemd-run --unit=cyberdeckzero-update --collect --quiet /usr/share/APPLaunch/bin/cyberdeckzero-update apply ...
```

so the root part runs in a transient system unit `cyberdeckzero-update` of its own, outside the launcher (it survives
the launcher's restart, a crash or the hold-Esc watchdog).

**The root helper** `/usr/share/APPLaunch/bin/cyberdeckzero-update` (installed by `install.sh`; it runs from a copy in
`/run/cyberdeckzero-update`, since `install.sh` replaces it):

| Command | What it does |
| --- | --- |
| `apply --version X.Y.Z --from-version X.Y.Z --sha256 HEX --file PATH --user NAME` | the update: check (copy to the work folder, sha256 again, unpack, architecture), plan (`install.sh --plan` of the new bundle), backup, install (`install.sh` with the recorded options, never `--force`, under `systemd-inhibit`), restart, health (the new launcher must publish its version with a live pid in `board.state` within 90 s and keep that pid 20 s). A failure from the install on: the backup is put back and the old launcher restarted (`rolled-back`); before it nothing has changed (`refused`) |
| `resume --user NAME` | after an interruption (the status says `running` and no helper runs): a new launcher that is healthy now finishes the update, anything else from the install step on is rolled back |
| `plan --bundle DIR --user NAME` | the options it would replay and the plan check; changes nothing |
| `options --user NAME` | the install options it would replay |
| `status` | the status file |
| `apt-refresh --user NAME` | Packages > Refresh lists (v0.9.2): `apt-get update` and nothing else |
| `apt-upgrade --user NAME [--libraries] PACKAGE...` | Packages > Upgrade now (v0.9.2): exactly the listed installed packages (`apt-get install --only-upgrade --no-remove --no-install-recommends`): check and download (cancellable), install (not cancellable; local configuration files kept, never a question), `apt-get clean`. Never a removal, a new package, the boot chain or a held package; the packages the anchor `cyberdeckzero-runtime` depends on only with `--libraries` |
| `apt-repair --user NAME` | after an interrupted upgrade (or whenever `dpkg --audit` is not empty): `dpkg --configure -a`, then apt must plan nothing more; a broken dependency is left to `sudo apt-get -f install` in a terminal |

The apt commands run the same way, in a transient unit of their own: `systemd-run --unit=cyberdeckzero-apt --collect
--quiet .../cyberdeckzero-update apt-upgrade ...` (one unit name for every apt job, so two cannot run at once).

Exit status: 0 done (or nothing to resume), 1 rolled back or failed, 2 refused or cancelled (nothing changed) or a usage error.

- **Options.** Every `install.sh` run (not `--dry-run` or `--board-only`) records its options in
  `/etc/applaunch/install.conf`: `keyboard_name`, `board` (empty when the board was detected), `screen_size`,
  `panel_mm` (v0.10.4), `boot_config`, `pwm_backlight`, `default_source`, `user`. The helper replays them as
  `--keyboard-name`, `--board`, `--screen-size`, `--panel-mm`, `--no-boot-config`, `--pwm-backlight`, `--no-default-source`. Without the file (an install before 0.8.13) it infers
  them from the keyboard udev rule, `board.conf`, `cmdline.txt`, `config.txt` and the default-source marker.
- **The plan.** `install.sh --plan` is the dry run as `key=value` lines (`plan`, `board`, `session`,
  `desktop_refused`, `switch`, `migration`, `profile_action`, ...). The helper refuses, and asks for `install.sh` by
  hand, on an unknown board, a desktop on the deck, a switch between the appliance and the desktop variant, units of an
  install before 0.7.0, or a `profile_action` of `replace` or `remove` (it would change `board.conf`).
- **Restart.** Appliance boards: `install.sh` restarts the user service. Desktop boards: the helper asks the running
  launcher to exit with status 76 (`restart=launcher` in the status file), so `cyberdeckzero-desktop.service` starts it
  again without giving the desktop back (`RestartForceExitStatus=76`, [above](#desktop-boards)); if it is still there
  30 s later the unit is stopped and started again.
- **Status file:** `/var/lib/cyberdeckzero-update/status` (on disk, world-readable, `key=value`: `state` running, done,
  refused, rolled-back or failed; `step`, `from`, `to`, `variant`, `user`, `pid`, `boot`, `backup`, `log`, `reason`,
  ...). Settings > Updates reads it every 2 s.
- **Logs:** `/var/log/cyberdeckzero/update-<date>.log` (the last 5 kept; Settings > Updates > Show log), and
  `journalctl -u cyberdeckzero-update`.
- **Backup:** `/var/lib/cyberdeckzero-update/backup`, kept until the next update: `/usr/share/APPLaunch/bin`, `lib`,
  `share`, `strokes`, `adb`, `/usr/share/doc/cyberdeckzero-launcher`, the units, udev and PolicyKit rules,
  `board.conf`, `install.conf`, the menu entry, `cyberdeckzero-ctl`, and the user's unit and WirePlumber drop-in. Not
  the installed apps (`/usr/share/APPLaunch/applications`).
- **Work folder:** `/var/cache/cyberdeckzero-update` (the root copy of the bundle and its unpacked tree; removed at the
  end).
- **Free space:** the helper refuses unless the work folder and the backup fit: about 3x the bundle + 64 MB, plus the
  size of the backup (the previous backup is deleted first, so its space counts as free).
- **Test hook:** `APPLAUNCH_UPDATE_URL` in the launcher's environment replaces GitHub's release list with another
  address, for example a local fake server (`projects/APPLaunch/tests/fixtures/update/fake_release_server.py`). Leave it
  unset on a board.
- **Repair after a power cut** during the install: Settings > Updates offers "Repair the update"; by hand,
  `sudo /usr/share/APPLaunch/bin/cyberdeckzero-update resume --user <user>`.
- The first install of 0.8.13 on a board with 0.8.12 or older is by hand (`sudo ./install.sh` from the bundle): the
  older launchers have no Settings > Updates and no helper. `uninstall.sh` removes the helper, `install.conf`, the status folder with the
  backup, the work folder and the update logs, and purges the anchor package `cyberdeckzero-runtime`.

**Packages (apt, v0.9.2, report 047 P3 and P4).** Settings > Updates > Packages:

- **The list** comes from `apt-get -s upgrade` (as the user, no lock), in groups: **Safe to upgrade**; **Launcher
  libraries** (what the launcher's binaries load, curl, ca-certificates and the session services); **Kernel, firmware
  and boot** (kernel, firmware, bootloader, EEPROM, initramfs: terminal only); **Needs the terminal** (kept back, held,
  phased, new or removed packages; the Terminal page gives the `sudo apt-get upgrade` / `dist-upgrade` command). A
  package the Store installed, or one that owns files under `/usr/share/APPLaunch`, is left out of the list (Debian's
  own `2048` is not an update of the Store's). unattended-upgrades, PackageKit, snapd and holds are shown, never changed.
- **Upgrade N packages** offers the safe set, closed the way apt will really run it (a package that pulls in a launcher
  library moves to Launcher libraries, one that pulls in anything else to Needs the terminal). The confirmation page
  has the Launcher libraries switch (off by default, a warning before use); then the progress page (Cancel during the
  download only) and a result page.
- **Guards** (before the password, and again in the helper): another package job running, an interrupted run to
  repair first, a battery under 50 % and not charging, under-voltage now (the firmware's throttled bits), too little
  disk (the download, plus what the packages grow by, plus 64 MB or the largest package), no network, `dpkg --audit` not
  empty, a held or boot-chain package, and a second simulation that differs from the list ("refresh the lists and try
  again").
- **Never a reboot**: the result page advises one (`/run/reboot-required`, or libc6, systemd, udev or dbus upgraded).
  An apt upgrade has no rollback; an interrupted one (a power cut) is refused until **Repair** (`apt-repair`) has run.
- **The anchor package** `cyberdeckzero-runtime` (no files, Architecture all), built and installed by `install.sh`,
  depends on what the launcher needs on this board (the packages `install.sh` installs, ca-certificates, the owners of
  the libraries and programs it uses), with no version, so security updates keep coming; apt never autoremoves them,
  and a plan that would remove one removes the anchor too, which the helper refuses.
- **Waits**: every long wait (a scan, the plan, the helper's check, a refresh) shows a box with the step in words, a
  bar and an elapsed mm:ss timer, so a slow board never looks frozen.
- **Files**: status `/var/lib/cyberdeckzero-update/apt-status` (refresh) and `apt-upgrade-status` (upgrade, repair);
  logs `/var/log/cyberdeckzero/apt-refresh.log` and `apt-upgrade-<date>.log` (the last 5); `journalctl -u
  cyberdeckzero-apt`. Cancel file `/run/user/<uid>/applaunch/apt-cancel` (read during the check and the download).

### Upgrading from v0.8.13 by hand

`sudo ./install.sh` from the new bundle, as for any upgrade (Settings > Updates does the same). What changes on the
board after v0.8.13:

- `install.sh` removes the 24 retired images: the Store's tile and screen images and the Snake, Tank and IP Panel
  icons. The Store program stays (`M5CardputerZero-AppStore`, the back end of Settings > Apps); the tiles are gone
  with the new launcher binary. The config keys of the old tiles stay in `config.json`, unused.
- **`2048`** (an old app from the Store's built-in hub, the dpkg package `2048`) can no longer be removed from the launcher's UI unless
  Settings > Apps > Installed lists it. Check with `dpkg -s 2048`, then `sudo apt remove 2048`. Remove it: Debian has its
  own `2048` package, which would otherwise replace it on an apt upgrade.
- The same for the old packages `meshzero` or `meshcore`, `lanscan` and `wifi-survey` if `dpkg -l` shows them (320x170
  apps that now belong to the `small-screen` branch): Settings > Apps > Installed if listed, else
  `sudo apt remove <package>`.

## Multitasking

New in v0.8.0 (task 015, `docs/dev/reports/026-multitasking-study.md`). Nothing to install or configure: it comes
with the bundle and is **on by default** when the short side of the launcher's logical canvas is 480 or more (the
deck, the Pi 3A+ DSI at 800x480 or turned to 480x800 by the PDA profile, the Pi 4 B DSI, the Pi 5 with the HyperPixel
or the 7" DSI panel at 800x480, the HackBerry CM5 at 720x720, the FZ-M1 tablet). On smaller canvases (the Pi 3A+ SPI panel at 480x320, 320x170) it is off and the launcher
behaves as before.

- **Open apps:** three fingers held still for 1 s anywhere, a 0.5 s long-press on the clock / status strip, Alt+Tab
  on a keyboard (release Alt to switch), or Super (a tablet's Windows button) tapped twice within 0.45 s or held 0.6 s
  alone; Super tapped once goes to the home grid at once (the second tap then opens the switcher with that app first).
  Super with another key (Super+L, the FZ-M1's rotation lock Super+F14) does none of these; F14 is reserved for a later
  feature. The switcher has a card per open app, Home and Close all; a green dot on a home tile marks an app
  open in the background.
- **Apps** declare `X-Background=run` or `=freeze` in their `.desktop` file (`docs/APP-CONTRACT.md` section 13; the app
  side is `cp0_app_lifecycle` in `ext_components/cp0_lvgl`). An app that declares nothing is paused behind a
  "Close <app>?" sheet (Close / Keep) instead of being kept.
- **Limits it enforces:** 4 resident apps (a 5th launch closes the least recently used frozen one); while apps are
  hidden, MemAvailable under 64 MB closes the least recently used frozen app and under 32 MB a running one too; hidden
  apps get a higher `oom_score_adj` (700 running, 900 frozen). The app's last frame is saved and put back on resume.
- **Turn it off** (or force it on with `=1`, also on a smaller canvas): `APPLAUNCH_MULTITASK=0` (or `false`, `off`,
  `no`) in the launcher's environment. For the user service, a drop-in:

  ```
  mkdir -p ~/.config/systemd/user/cyberdeckzero.service.d
  printf '[Service]\nEnvironment=APPLAUNCH_MULTITASK=0\n' > ~/.config/systemd/user/cyberdeckzero.service.d/multitask.conf
  systemctl --user daemon-reload && systemctl --user restart cyberdeckzero.service
  ```

  On a desktop board, `sudo systemctl edit cyberdeckzero-desktop.service` with the same `[Service]` line. Delete the
  drop-in (and reload, restart) to go back to the default.
- **If an app is stuck:** hold Esc 3 s to close the app in front, or from ssh
  `echo close-all > /run/user/$(id -u)/applaunch/multitask.ctl` closes every app the launcher keeps.
- **Known limits:** the HackBerry CM5's touch reports at most two contacts, so there it is Alt+Tab only (which does
  nothing while an app without `X-Background` is in front), and its first-run hint still says "Hold three fingers".
  The FZ-M1 tablet has the same two-contact limit (clock long-press, the Windows button pressed twice, or Alt+Tab).
  viz1090 does not support multitasking and can hang on three-finger touches (hold Esc 3 s closes it). Net Toolbox
  stays a legacy app.

## Boards

`install.sh` detects the board from `/proc/device-tree/model`, the connected DRM connectors (`/sys/class/drm`) and
the framebuffers (`/sys/class/graphics`); a PC from `uname -m` (x86_64, no device tree) and DMI (`/sys/class/dmi/id`):

| Board | Detected by | Profile |
| --- | --- | --- |
| `zero2w`: Pi Zero 2 W deck, Waveshare 2.8" DPI | model contains "Zero 2", and no SPI panel (an SPI panel makes it `pi3a-luckfox35`, below) | none: `cyberdeckzero.service` alone, exactly as before (a stale generated profile is moved to `board.conf.bak`) |
| `pi3a-luckfox35`: Pi 3 Model A+ with the Luckfox 3.5" ST7796S SPI panel; also a Pi Zero 2 W with the Waveshare 3.5" RPi LCD (F) and a Pi 5 (or any other Raspberry Pi) with either panel (the same `mipi-dbi-spi` overlay; [Raspberry Pi: the 3.5" SPI panel](#raspberry-pi-the-35-spi-panel)) | a Raspberry Pi (a Pi 3A+, a Pi 5, a Pi 4B without its DSI panel, ...): connector `SPI-*` connected with mode 320x480, a 320x480 framebuffer named `panel-mipi-dbi*`, and no `DSI-*` / `DPI-*` connector connected as well (a Zero 2 W as well: the deck itself has no SPI panel, its 2.8" panel is a DPI one, so a Zero 2 W with none stays `zero2w`) | `/etc/applaunch/board.conf` (below); the deck's PWM backlight overlay is not added; a udev rule (`92-applaunch-backlight-gpio.rules`) lets the video group switch `backlight_gpio`. On a Zero 2 W the panel's own PWM backlight instead (brightness levels and `APPLAUNCH_DIM_PERCENT=10`, below). On a board that is not a Raspberry Pi (`--board pi3a-luckfox35`, or `--spi-panel waveshare35f` on an Orange Pi Zero 2W, [below](#orange-pi-zero-2w-the-35-spi-panel)) the backlight is what `/sys/class/backlight` has: one with levels (`max_brightness` above 1, `backlight_pwm` first) gives `APPLAUNCH_BACKLIGHT=sysfs:...`, `APPLAUNCH_DIM_PERCENT=10` and `92-applaunch-backlight-sysfs.rules`; the on/off `backlight_gpio` the `gpio:` line and its rule; none, no line (no brightness control) |
| `pi3a-wsdsi43`: Pi 3 Model A+ with an 800x480 DSI panel, the Waveshare 4.3" one (overlay `vc4-kms-dsi-waveshare-800x480`, see below); the 4.3", 5" and 7" panels look the same to software: for another size use `--screen-size` ([Screen size](#screen-size)) | model "Raspberry Pi 3 Model A Plus", connector `DSI-*` connected with mode 800x480, an 800x480 framebuffer named `vc4drmfb` (config.txt and the overlay's name are not looked at) | `board.conf`: `/dev/fb0`, `APPLAUNCH_LOGICAL=800x480`, `APPLAUNCH_ROTATE=0`, `APPLAUNCH_COMPAT_SCALE=2`, `APPLAUNCH_PANEL_MM=94x56` (to be confirmed with a ruler), touch `auto` / `buffer`, `APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045` (the overlay's backlight regulator, brightness levels; the only backlight device if that one is absent, none if there is none) with `APPLAUNCH_DIM_PERCENT=10` after it (no dim line without a backlight line) and a udev rule (`92-applaunch-backlight-sysfs.rules`) that lets the video group set it; config.txt is not edited (no PWM overlay), cmdline.txt gets `vt.global_cursor_default=0 consoleblank=0` only (no `fbcon=` argument) |
| `pi3a-wsdsi43-portrait`: the same board and panel turned to portrait, the PDA profile ([below](#pda-portrait-on-the-43-dsi-panel)) | never detected: only `--board pi3a-wsdsi43-portrait` (the panel must be there, as for `pi3a-wsdsi43`) | `board.conf`: as `pi3a-wsdsi43` but `APPLAUNCH_LOGICAL=480x800`, `APPLAUNCH_ROTATE=90`, `APPLAUNCH_PDA=1` (the writing zone) and no `APPLAUNCH_COMPAT_SCALE`; the same backlight line and rule, config.txt and cmdline.txt as `pi3a-wsdsi43` |
| `pi4b-dsi`: Pi 4 Model B with the Freenove 5" DSI panel (800x480, same family as the 4.3" one; for another size `--screen-size`) | model "Raspberry Pi 4 Model B", connector `DSI-*` connected with mode 800x480, an 800x480 framebuffer named `vc4drmfb` and the panel's backlight regulator `/sys/class/backlight/10-0045` (`rpi_panel_attiny_regulator`) | `board.conf`: as `pi3a-wsdsi43` (`/dev/fb0`, 800x480, rotate 0, compat scale 2, touch `auto` / `buffer`, `APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045` and its udev rule), with `APPLAUNCH_PANEL_MM=108x65` (a guess, to be checked with a ruler) and `APPLAUNCH_DIM_PERCENT=20` (10 % is black on this panel); config.txt is not edited (the tested board finds the panel with `display_auto_detect=1`), cmdline.txt gets the two console arguments only. Tip: this board logs undervoltage warnings with a weak supply; use the official 5.1 V 3 A one |
| `pi5-hyperpixel4`: Pi 5 with the Pimoroni HyperPixel 4" DPI panel | model "Raspberry Pi 5", connector `DPI-*` connected with mode 480x800, a 480x800 framebuffer named `drm-rp1-dpi*` | `board.conf`: that framebuffer, `APPLAUNCH_LOGICAL=800x480`, `APPLAUNCH_ROTATE=90`, touch `auto` / `buffer`; config.txt only with `--pwm-backlight` (below) |
| `pi5-dsi7`: Pi 5 with the official Raspberry Pi 7" Touch Display on the RP1 DSI port (800x480, same family as the Pi 4 B's panel; a desktop board when lightdm runs; for another size `--screen-size`) | model "Raspberry Pi 5", connector `DSI-*` connected with mode 800x480, an 800x480 framebuffer named `drm-rp1-dsi*` (`drm-rp1-dsidrmf`, 32 bpp; the HyperPixel's is `drm-rp1-dpi*`, so the two never overlap) and the panel's backlight regulator `/sys/class/backlight/10-0045` (`rpi_panel_attiny_regulator`) | `board.conf`: as `pi4b-dsi` (that framebuffer, 800x480, rotate 0, compat scale 2, touch `auto` / `buffer`: the panel's ft5x06 at i2c 10-0038, `APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045` and its udev rule) with `APPLAUNCH_PANEL_MM=154x86` (the display's active area) and `APPLAUNCH_DIM_PERCENT=20`; touch and brightness checked on the owner's board (2026-11-11). config.txt is not edited (the panel is found by `display_auto_detect=1`; no boot configuration lines on a desktop board, cmdline arguments only on an appliance one); `--pwm-backlight` is ignored (Pi 5 + HyperPixel only) |
| `hackberry-cm5`: HackBerry CM5 (720x720 DPI panel) | model "Compute Module 5", connector `DPI-*` connected with mode 720x720, a 720x720 framebuffer named `drm-rp1-dpi*` | `board.conf`: that framebuffer, `APPLAUNCH_LOGICAL=720x720`, `APPLAUNCH_ROTATE=0`, touch `auto` / `buffer` |
| `pc-<model>`: a PC (amd64 bundle), e.g. `pc-fzm1-2` (Panasonic FZ-M1 mk2) | x86_64 with no device tree, a framebuffer (the DRM driver's, `*drmfb`, first); `<model>` is the DMI product name in lower case | `board.conf`: `APPLAUNCH_DISPLAY=dpi-scaled`, that framebuffer, `APPLAUNCH_LOGICAL` = its size, `APPLAUNCH_ROTATE=0`, `APPLAUNCH_PANEL_MM` from the connected connector's EDID (eDP first; the detailed timing's mm, else the cm of the basic block), else a table of known models (FZM1-2: 150x93), else none (default density, large controls); touch `auto` / `buffer`; `APPLAUNCH_BACKLIGHT=sysfs:` the first backlight (`intel_backlight` first), `APPLAUNCH_DIM_PERCENT=10`, and its udev rule (no backlight: neither line); on a tablet (SMBIOS chassis 30 or 32, or a known tablet) `APPLAUNCH_KBD_IGNORE=bus:0011`; `APPLAUNCH_HIDE_TILES=app_Store`. No boot configuration (GRUB, kernel command line), no dtc |
| `generic-<W>x<H>`: a screen that has no profile (untested hardware) | only `--board generic`, or `--force` on an unknown board when exactly one panel is found (a connected DSI, DPI, SPI, eDP or LVDS connector whose mode is the size of a framebuffer); never on a PC | `board.conf` written from what was detected, see [My screen is not listed](#my-screen-is-not-listed) |
| anything else | - | none: the detected facts are printed and the install stops (`--force` installs a generic profile when one panel is found, else without a profile; the facts say when `--board generic` would find one). Run `tools/board-probe/probe-board.sh` on the board and send the report. A Pi 3A+ with both its panels connected is not guessed: choose with `--board`. With `--force`, the deck's PWM backlight overlay is added only on a model that is not one of the boards above (a Pi 3A+, Pi 4 B, Pi 5 or CM5 whose panel was not recognised keeps its config.txt) |

**Waveshare 4.3" DSI panel (`pi3a-wsdsi43`):** set up `/boot/firmware/config.txt` yourself before the install;
install.sh does not edit config.txt on this board. The panel needs the KMS driver and its own overlay:

```
dtoverlay=vc4-kms-v3d
dtoverlay=vc4-kms-dsi-waveshare-800x480
```

Do not use the generic `dtoverlay=vc4-kms-dsi-7inch`: it also lights this 4.3" panel, but the picture flickers and
shakes (from the boot on; a known kernel/firmware issue with this panel). Remove leftover lines of an earlier panel as
well, such as the SPI panel's overlay or an `fbcon=map:...,rotate:...` argument in cmdline.txt (install.sh never adds
an `fbcon=` argument). The detection does not depend on the overlay: it reads the DSI connector and the framebuffer.

### Screen size

The Waveshare 4.3", the Freenove 5" and the official 7" DSI panels give the software the same facts (the same ft5x06 touch,
the same `10-0045` backlight regulator, the same 800x480 framebuffer), so the installer cannot tell them apart: a Pi 3A+
with the Freenove 5" panel is detected as `pi3a-wsdsi43`, the 4.3" profile. The profile's size only sets the layout's
density (`APPLAUNCH_PANEL_MM`). The dry run and the install say so on every run that was given no size: *screen size
4.3"/5"/7" cannot be detected: assuming 4.3" (94x56 mm); for another size run again with --screen-size INCHES* (the
profile's own size is named: 4.3" for `pi3a-wsdsi43`, 5" for `pi4b-dsi`, 7" for `pi5-dsi7`; not in `--plan`, whose
lines are for Settings > Updates). When a hand-edited `/etc/applaunch/board.conf` is kept (no `--force`) the note says that this file decides the size
(its `APPLAUNCH_PANEL_MM`) and that `--screen-size` only applies when the profile is replaced with `--force`. Say which screen it is:

```
sudo ./install.sh --screen-size 5            # the Pi 3A+ with the Freenove 5" panel (the board is detected as before)
sudo ./install.sh --panel-mm 108x65          # or the visible area in mm, exactly (wins over --screen-size)
```

- **Inches are the diagonal**, 1.5 to 15, any decimal. `--screen-size` changes only the size line of `board.conf`
  (`APPLAUNCH_PANEL_MM`; a profile that has none, such as the HyperPixel's, gets one) and, for 5 on the DSI regulator's
  backlight, `APPLAUNCH_DIM_PERCENT=20` (10 % is black on the 5" panel). The board name, framebuffer, rotation, touch and
  the rest stay the profile's.
- **A re-install or update keeps your size.** A size given before (`screen_size=` / `panel_mm=` in `install.conf`) is
  applied again by a run without options. When `install.conf` has neither (it was deleted, restored without them, or
  rewritten by an older `install.sh`) and `board.conf` is the generated, unedited profile with a size other than the
  board's default, that `APPLAUNCH_PANEL_MM` is kept and recorded as `panel_mm=`. A hand-edited `board.conf` is always
  kept whole. A PC takes its size from the screen's EDID on every run. The profile still gets new lines of a
  later release; the previous file is kept as `board.conf.bak` when it is rewritten. Run the installer of the release you
  are updating to: a branch built before `--screen-size` existed rewrites `install.conf` without the size.
- **The formula**: width = diagonal x 25.4 x w / sqrt(w^2 + h^2) and height likewise, w and h the profile's landscape
  canvas, each rounded to the nearest mm (4 inch at 640x480: 81x61; 2.8 inch at 640x480: 57x43). The sizes the profiles
  already know keep their values, which differ from the formula by a millimetre or two: 4.3, 5 and 7 inch at 800x480
  (94x56, 108x65, 154x86; the 7" official panel's pixels are not square) and 3.5 inch at 480x320 (79x49). When the result
  is the profile's own size the profile is not touched.
- **The question**: run from a terminal, on a first install (no `screen_size=` in `/etc/applaunch/install.conf`), with a
  profile and without `--screen-size` or `--panel-mm`, `install.sh` asks once: "What is the screen's diagonal in inches?
  (for example 2.8, 3.5, 4.3, 5, 7; Enter = the profile's size, or unknown)". Enter keeps the profile's size; an unusable
  answer asks again (three tries, then the profile's size stays). It is never asked from a script, from Settings > Updates,
  with `--dry-run`, `--plan` or `--board-only`, on a PC (its size comes from the screen's EDID) or on the deck.
- **Kept**: the choice is recorded in `install.conf` (`screen_size=`, `panel_mm=`) and Settings > Updates runs `install.sh`
  again with it; a manual re-run without the options keeps it too. A hand-edited `board.conf` is still kept (`--force`
  replaces it, the old one stays as `board.conf.bak`): edit `APPLAUNCH_PANEL_MM` by hand there. `--dry-run` shows the size
  in use (`screen: panel size ... mm`); `--plan` prints `screen_size=`, `panel_mm=` and `panel_in_use=`.
- **The deck** (`zero2w`) has no profile, its 2.8" panel size is built in: the options are refused there.
- **In Settings** (since v0.10.5): Settings > Screen > Screen size picks the diagonal from a list (2.8, 3.5, 4.3, 5,
  5.5, 7, 8, 10.1 inches; "Auto (board profile)" clears the choice) on any board that has a profile, with the same formula
  and the same exceptions as above, and applies it at the next launcher start (the grey hint says so and shows the size in
  use and its source: installer, setting, board profile). It is stored in the launcher's user settings
  (`~/.config/cardputerzero/config.json`: `screen_diag`, `screen_mm`), not in `board.conf` or `install.conf`, so an update
  or a re-run of `install.sh` keeps it, and it wins over the size the installer wrote (`install.sh` writes the same
  `APPLAUNCH_PANEL_MM`). "Auto" clears only the setting: an installer-given size stays. No custom diagonal in Settings: use
  `--screen-size`.

### My screen is not listed

A screen that no profile describes works only as far as the hardware allows, and it is **untested hardware: at your own
risk**. In order:

1. **Light the panel at the system level.** Its overlay or driver in `/boot/firmware/config.txt` (and a reboot) so that a
   framebuffer exists and the console shows on it (`/dev/fb0`, `ls /sys/class/graphics`). The installer does not do this on
   an unknown board; see the panel's own documentation (the kernel's `vc4-kms-dsi-*`, `mipi-dbi-spi` and `vc4-kms-dpi-*`
   overlays are the usual ones). `sudo ./install.sh --dry-run` prints what it detects (connectors, framebuffers, touch,
   backlight).
2. **Install with the generic profile**: `sudo ./install.sh --board generic --screen-size <inches>` (the diagonal of the
   visible area). `--board generic` writes `/etc/applaunch/board.conf` from the detected facts: `APPLAUNCH_FB` the panel's
   framebuffer (the one connected DSI, DPI, SPI, eDP or LVDS connector's, else the only framebuffer there is);
   `APPLAUNCH_LOGICAL` the framebuffer's landscape size (a portrait buffer such as 480x800 gives 800x480 with
   `APPLAUNCH_ROTATE=90`, as the HyperPixel 4 profile does); `APPLAUNCH_COMPAT_SCALE` 1 below 640 px wide, else 2; touch
   `auto` / `buffer`; the backlight as the DSI profiles find it (the `10-0045` regulator or the only backlight device; an
   on/off `backlight_gpio` as on/off); `APPLAUNCH_BOARD=generic-<W>x<H>`; `APPLAUNCH_PANEL_MM` from `--screen-size` (without
   it no size line: the launcher's default density, large controls). It prints what it assumed and asks you to send the
   probe report. A board where the facts are not clear (two panels, none) is refused with the facts; a PC has its own
   profile. Or use the profile of the nearest known board with `--board NAME --screen-size <inches>` (for a panel of the
   same family).
3. **Check it.** The home grid, the touch (does a tap land where you tap?) and the rotation. Edit
   `/etc/applaunch/board.conf` by hand (the keys are listed at the top of `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_dpi_scaled.c`:
   rotation `APPLAUNCH_ROTATE`, canvas `APPLAUNCH_LOGICAL`, touch `APPLAUNCH_TOUCH_*`, backlight `APPLAUNCH_BACKLIGHT`), then
   restart the launcher (`systemctl --user restart cyberdeckzero.service`, or reboot). A hand-edited `board.conf` is kept by
   later installs and updates.
4. **Send the probe report**: run `tools/board-probe/probe-board.sh` on the board and send the output, so that a profile
   (and the detection) can be added for it.

### Orange Pi Zero 2W: the 3.5" SPI panel

The Waveshare 3.5" RPi LCD (F) (ST7796S 320x480 on SPI, Goodix GT911 touch on I2C, the same panel as zero8's) plugged
straight on the 40-pin header of an **Allwinner H616 / H618 board with Armbian** (tested: Orange Pi Zero 2W, Armbian
26.11, kernel 6.18.64-current-sunxi64; the board has no `config.txt`, its overlays load from `/boot/armbianEnv.txt`).
Opt-in, never detected:

```
sudo ./install.sh --spi-panel waveshare35f                          # on/off backlight
sudo ./install.sh --spi-panel waveshare35f --soft-pwm-backlight     # 10 brightness levels (software PWM)
sudo reboot
```

What it does (the board's hand-made, tested set-up of 2026-11-21, made repeatable; the files are in the bundle,
`payload/panels/waveshare35f/`, with a README of their origin and licences):

- `/lib/firmware/st7796s.bin`: the panel's init sequence (Waveshare's file; install.sh checks its sha256 `17204e39...`
  and refuses a bundle with another one);
- `/etc/modules-load.d/spi35.conf`: `panel-mipi-dbi` (with `--soft-pwm-backlight` also `pwm-gpio.conf`: `pwm-gpio`);
- `/boot/overlay-user/spi35-st7796s.dtbo`, compiled with `dtc -@` from the bundle's overlay: the panel on SPI1 (48 MHz,
  about 46 MHz real), the touch on I2C1, the backlight on header pin 12. Pin 12 (PI1) has no hardware PWM on the H618,
  so by default it is an on/off `gpio-backlight` (`backlight_gpio`); `--soft-pwm-backlight` makes it a `pwm-backlight`
  (`backlight_pwm`, 10 levels at 2 kHz) on the software PWM driver `pwm-gpio`, built with DKMS from the bundle's copy of
  Linux 6.18 `drivers/pwm/pwm-gpio.c` (Armbian's kernel has `CONFIG_PWM_GPIO` off);
- always the **DKMS module `drm-mipi-dbi-chunk`**: the kernel's own `drm_mipi_dbi.c` with each SPI transfer capped at
  64 KiB (`chunk.patch`, one line). The H618's SPI DMA cannot reach RAM above 4 GB, so on a 4 GB board the frame goes
  through swiotlb, whose segments hold 256 KiB at most: the 307200-byte frame failed ("swiotlb buffer is full", "Failed
  to update display -5"). The earlier workaround, `mem=3072M` on the kernel command line, cost 1 GB of RAM: install.sh
  **never** sets `mem=` (it only notes one it finds in `extraargs`, and leaves it). The source must match the running
  kernel, so install.sh fetches `drivers/gpu/drm/drm_mipi_dbi.c` of the kernel's upstream tag (`v6.18.64` for
  `6.18.64-current-sunxi64`, `v6.18` for 6.18.0) as a plain file from git.kernel.org (https), checks it has the line the
  patch changes, applies the patch and builds it; only when the module is not installed for the running kernel yet, so
  a re-run needs no network. `dkms.conf` gets `BUILD_EXCLUSIVE_KERNEL` for that kernel series: DKMS rebuilds the module
  for kernel updates of the same series (6.18.x) and skips another one, where the kernel's own module loads again (the
  panel then fails as before): run `install.sh --spi-panel waveshare35f` again after such an update, it fetches the new
  source;
- the packages DKMS needs, when missing: `dkms gcc make libc6-dev patch` and the headers of the running kernel
  (`linux-headers-<flavour>`, `linux-headers-current-sunxi64` here); `/lib/modules/<release>/build` must then exist (the
  archive's headers can be newer than the running kernel: update the kernel, reboot, run again);
- `user_overlays=spi35-st7796s` in `/boot/armbianEnv.txt` (a copy `armbianEnv.txt.bak-<date>` first; other user overlays
  stay on their line; nothing when it is there already);
- the `pi3a-luckfox35` profile ([Boards](#boards)) with the backlight that overlay brings (on/off: the `gpio:` line;
  `--soft-pwm-backlight`: `sysfs:/sys/class/backlight/backlight_pwm` and `APPLAUNCH_DIM_PERCENT=10`) and its udev rule.
  Before the reboot the panel's framebuffer does not exist yet: without an HDMI monitor at boot sun4i-drm makes no
  framebuffer, so the panel will be the only one, `/dev/fb0`; when a framebuffer is there now (a monitor was connected at
  boot) the panel comes after it (`/dev/fb1`), and booting later without the monitor moves it to `/dev/fb0`: run
  `install.sh` again then (Settings > Updates does too, with `--board pi3a-luckfox35` from `install.conf`): it finds
  the panel and updates the profile;
- **reboot needed**: the launcher is enabled but not started before it.

Every step that can fail (the packages, the headers, the download, the patch, `dtc`, a DKMS build) runs before any file
of the panel is put in place, and before the launcher's files: a failure takes off the DKMS modules this run added and
stops with a clear message, nothing of the panel changed (packages apt installed stay). A second run changes nothing
and asks for no reboot. `--dry-run` prints the whole plan (the URL, the `armbianEnv.txt` line, the foreseen
framebuffer). Refused, with nothing changed, on `luckfox35` (a Raspberry Pi's name: there the same panel is set up in
`config.txt`, [below](#raspberry-pi-the-35-spi-panel)), on another SoC, without `/boot/armbianEnv.txt`, or with a kernel release that is not `<version>-<flavour>`. Undo by hand:
take `spi35-st7796s` out of `user_overlays=` (or put the `.bak-<date>` file back), `sudo dkms remove
drm-mipi-dbi-chunk/1.0 --all` (and `pwm-gpio/1.0`), remove the two files in `/etc/modules-load.d`, reboot;
on a board installed before v0.10.9 (no record in `install.conf`) `uninstall.sh` does not touch them; from v0.10.9 on it
undoes what the record names (below).

**Undo.** `./uninstall.sh` removes only what `install.conf` records as made by `install.sh --spi-panel waveshare35f`
(`spi_armbian_files=`, `spi_armbian_dkms=`, `spi_armbian_env=`; each name is checked against a fixed list): `"spi35-st7796s"` taken off the
last `user_overlays=` line of `/boot/armbianEnv.txt` (the line goes too when `install.sh` wrote it; a timestamped
`armbianEnv.txt.bak-uninstall-<time>` first), `/lib/firmware/st7796s.bin`, `/etc/modules-load.d/spi35.conf` (and `pwm-gpio.conf`),
`/boot/overlay-user/spi35-st7796s.dtbo` (each only when `install.sh` created it and, for the first three, it is still unchanged), and
the DKMS modules `drm-mipi-dbi-chunk` and `pwm-gpio` it built (`dkms remove --all`, their `/usr/src` folders), then a reboot is needed.
A file that was there before `install.sh`, a panel set up by hand (no `install.conf`) and the packages (`dkms`, the kernel headers,
...) stay. `--no-boot-config` leaves all of it; `--dry-run` prints it. The `armbianEnv.txt.bak-<date>` of `install.sh` stays.

### Raspberry Pi: the 3.5" SPI panel

The same panel family as the Orange Pi's section above, on a **Raspberry Pi**: the Luckfox 3.5" RPi LCD (CTP) and the
Waveshare 3.5" RPi LCD (F) (ST7796S 320x480 on SPI0 CE0, Goodix GT911 touch on I2C 0x5d; DC GPIO22, reset GPIO27,
backlight GPIO18, touch interrupt GPIO4, touch reset GPIO17). Tested by hand on a **Pi 5** (Raspberry Pi OS trixie,
kernel 6.18, labwc + lightdm desktop): screen, touch, launcher and 15-step brightness. Since v0.10.7 the set-up is
`install.sh --spi-panel luckfox35` (`waveshare35f` is accepted as a name too: the same panel family):

```
sudo ./install.sh --spi-panel luckfox35 --dry-run    # the plan: the init file, the exact config.txt lines
sudo ./install.sh --spi-panel luckfox35              # then: sudo reboot
sudo ./install.sh                                    # after the reboot, once: the profile finds the panel's framebuffer
```

What it does (a whole install; refused with a clear message, nothing changed, with `--board-only` or `--no-boot-config`,
and on anything that is not a Raspberry Pi with `/boot/firmware/config.txt`; `--soft-pwm-backlight` is the Orange Pi's):

- puts the panel's init sequence in `/lib/firmware/st7796s.bin` (Waveshare's file, checked by its sha256
  `17204e39cce35fba857ad2dff14243e1d3a958c4dac00283f8df9b7ad5147cc7`; Luckfox's is byte-identical; it ships in the
  bundle's `panels/waveshare35f/`);
- **edits `/boot/firmware/config.txt`**, also on a desktop board: the option is explicit, so unlike the automatic
  backlight change below it is made there too. A copy goes to `config.txt.bak-applaunch-spipanel` (once, before the first
  change) and this block is appended to the `[all]` section (an `[all]` line goes in front of it when the file's last
  section is another one). The marker lines are what `uninstall.sh` looks for:

  ```
  # >>> cyberdeckzero spi panel
  dtparam=spi=on
  dtparam=i2c_arm=on
  dtoverlay=mipi-dbi-spi,spi0-0,speed=48000000
  dtparam=compatible=st7796s\0panel-mipi-dbi-spi
  dtparam=width=320,height=480,width-mm=49,height-mm=79
  dtparam=reset-gpio=27,dc-gpio=22
  dtparam=backlight-pwm
  dtparam=backlight-pwm-chan=2
  dtoverlay=goodix,addr=0x5d
  # <<< cyberdeckzero spi panel
  ```

  (`\0` is a literal backslash and zero in the file.) `dtparam=spi=on`, `dtparam=i2c_arm=on` and the goodix overlay are
  left out when `config.txt` has such a line already. A `config.txt` that already has a `dtoverlay=mipi-dbi-spi` line
  that install.sh did not write (a hand-made set-up) is left as it is: nothing is added, the profile is still written.
  A second run adds nothing;
- **the backlight by model**: a Zero 2 W and a Pi 5 get the PWM backlight (`dtparam=backlight-pwm`, 16 levels at
  `/sys/class/backlight/backlight_pwm`); every other model the on/off one (`backlight-gpio=18` on the `reset-gpio` line,
  `backlight_gpio`), because the Pi 3A+'s PWM0 is the headphone jack's;
- **a Pi 5 needs `dtparam=backlight-pwm-chan=2`.** GPIO18 is RP1 PWM0 channel 2 there; the overlay's default channel 0
  binds the wrong one and brightness does nothing. A Zero 2 W's default channel 0 is right (no channel line). The
  installer writes the channel line on a Pi 5 and warns when a `config.txt` has `backlight-pwm` without it;
- writes the `pi3a-luckfox35` profile (`APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/backlight_pwm` and the dim level
  10 for the PWM backlight) and its udev rule, records `spi_panel=` and `spi_fw_added=1` (the init file was put there by
  this install) in `/etc/applaunch/install.conf`, asks for a **reboot** (the launcher is not started before it) and keeps
  the profile for Settings > Updates through `board=pi3a-luckfox35`. The panel's framebuffer does not exist before the
  reboot: the profile names the one foreseen (`/dev/fb0` when there is none now), and `sudo ./install.sh` after the
  reboot finds the panel and corrects it when the foreseen number was wrong (a desktop with an HDMI framebuffer).

**Detection without `--board`.** A Raspberry Pi of any model (a Pi 3A+ as before; a Zero 2 W too: without an SPI panel it is the deck) with a connected `SPI-*` connector at 320x480 and a 320x480
framebuffer named `panel-mipi-dbi*` is `pi3a-luckfox35`. A Pi 5 with HDMI connected is fine; another panel (a connected
`DSI-*` or `DPI-*` connector) makes the detection refuse to guess: choose with `--board`.

**The backlight of an existing config.txt** (set up by hand, as on the owner's Pi 5): `backlight-pwm` with or without the
`-chan=` line is read as PWM (the profile names `backlight_pwm`); `backlight-gpio=18` on a whole install on an
appliance board is converted to PWM as before (on a Pi 5 with the channel line too), on the same conditions as the Zero 2
W. On a **desktop board** a whole install leaves `config.txt` alone (the dry run says so and names this option): to get
the PWM backlight there, run `--spi-panel luckfox35`, which edits `config.txt` because you asked.

**Undo.** `./uninstall.sh` removes the block between the two marker lines (a timestamped `config.txt.bak-uninstall-<time>`
first, then a reboot) and `/lib/firmware/st7796s.bin` when `install.conf` says install.sh put it there and the file is
still Waveshare's, unless `config.txt` still has a `dtoverlay=mipi-dbi-spi` line of its own. A hand-made block (no
markers) is never touched, nor a block with only one marker line. `--no-boot-config` leaves both. `--dry-run` prints it.
`config.txt.bak-applaunch-spipanel` stays. By hand: delete the lines between the markers and reboot.

### PDA: portrait on the 4.3" DSI panel

The PDA profile (report 038, phases S2 and S3; `docs/dev/reports/040-pda-s2.md`, `041-pda-s3.md`) shows the launcher in portrait on the Pi 3A+
with the Waveshare 4.3" DSI panel: 480x800 at 8.5 px/mm, held upright like a PDA. It is never detected (the same
panel is `pi3a-wsdsi43`, landscape, when install.sh detects it): choose it with

```
sudo ./install.sh --board pi3a-wsdsi43-portrait        # the whole install, or
sudo ./install.sh --board pi3a-wsdsi43-portrait --board-only   # only board.conf (and the backlight rule)
systemctl --user restart cyberdeckzero.service
```

- **Which side up:** `APPLAUNCH_ROTATE=90` puts the panel's right edge (as it sits in landscape) at the top: turn the
  device a quarter turn counter-clockwise from landscape. For the other side up (the cable on the other side), edit
  `/etc/applaunch/board.conf` to `APPLAUNCH_ROTATE=270` and restart the launcher; touch follows either way (`buffer`).
  A later `install.sh --board pi3a-wsdsi43-portrait` keeps the edited file (`--force` puts 90 back, the old file kept as
  `board.conf.bak`).
- **Pass `--board pi3a-wsdsi43-portrait` every time you run install.sh** (an update from a new bundle too): without it
  install.sh detects `pi3a-wsdsi43` and replaces a generated portrait profile with the landscape one (the portrait one
  is kept as `board.conf.bak`). A hand-edited `board.conf` is kept either way.
- **Back to landscape:** `sudo ./install.sh --board-only` (detects `pi3a-wsdsi43`), then restart the launcher.

What works in this phase:

- the home grid (3 columns), Settings, the dialogs, lists, the switcher and the terminal on the 480x800 canvas; the
  status strip is drawn smaller (the Compact scale, 230 px) so the page titles have room;
- multitasking: on, as on the deck (the rule is now "the short side is at least 480");
- terminal apps (`Terminal=true`); stock (compat) apps that are not terminals are hidden on a portrait screen (below);
- fullscreen apps (Mesh Hop, RSS Reader, Net Toolbox, System Monitor, Serial Console, viz1090 ...) are shown and run
  **turned**: full screen in landscape 800x480 (the panel's own orientation, rotate 0; rotate 180 with
  `APPLAUNCH_ROTATE=270`), and you turn the device while they run. They need no change: the launcher gives each one the
  landscape canvas on its command line (docs/APP-CONTRACT.md, section 3, "Portrait launcher"). An app that declares
  `X-Portrait` (none does yet) gets the portrait canvas instead;
- text: the **writing zone** at the bottom of the screen (report 041, 042; `APPLAUNCH_PDA=1` in the profile), with
  Settings > System > On-screen keyboard on **Handwriting + keyboard** (this profile's default, offered here only). It
  comes up where the fallback on-screen keyboard would (a text field or a terminal page wants input, no physical
  keyboard) and goes away after: a strip with Keyb, Tab, Ctrl, Alt, the arrows and Enter (Ctrl and Alt act on the next
  key or character, a double tap locks them) on top of the letters box (left) and the digits box (right), where you
  write with a pen or a finger, one stroke per character (the Strokes alphabet, `tools/strokes/README.md`). **Keyb**
  puts the standard on-screen keyboard in the band's place (exact typing: a sudo password in the terminal), for the
  following fields too, until the **cross** at the right of its top row brings the band back. Home and the switcher are
  the multitasking gestures (three fingers, a hold on the clock). A Bluetooth or USB keyboard works as everywhere (band
  and keyboard hidden). Auto: the standard on-screen keyboard only when no physical keyboard is present, no band; On:
  the standard keyboard for every text field, a physical keyboard or not, no band.
  A landscape app run turned has no zone: its own rule applies (a keyboard, else its own on-screen keyboard).
  `APPLAUNCH_PDA=0` in `/etc/applaunch/board.conf` shows the portrait launcher without the zone (the fallback keyboard
  then). Settings > System > Strokes training (report 042) learns the owner's own strokes;
  `APPLAUNCH_STROKES_LOG=/home/<user>/strokes.txt` added to `/etc/applaunch/board.conf` records every stroke written
  (for tuning the recogniser, report 041).
- the portrait profile still lists the keys of the old fixed-pixel tiles (`APPLAUNCH_HIDE_TILES=app_IP_Panel,app_Game,app_Tank,app_Store`):
  IP Panel, Snake, Tank and the Store tile are gone from this branch (they live on `small-screen`), so the keys name no
  tile and are ignored (the profile text stays as it is: it is checksummed, see install.sh). Desktop apps that are neither fullscreen
  nor a terminal (fixed-pixel 320x170 apps, as LAN Scan and Wi-Fi Survey were) are not listed on a portrait launcher at all
  (docs/APP-CONTRACT.md, section 3); responsive fullscreen apps and the terminal apps are;
- the number pad of the on-screen keyboard is the full width of the key area on a portrait canvas (keys about 148 px,
  digits at 26 px).

Not yet:

- a turned app hidden behind the switcher shows its dimmed frame sideways;
- long page titles are still cut with dots beside the narrower strip (for example "Keyboard test", "Network Time").

The Pi 5 + HyperPixel 4 and HackBerry values are the ones `tools/boards/board.py test-start` used on those boards.
Their panels' size in mm is not measured yet (task 014), so their profiles have no `APPLAUNCH_PANEL_MM` and the layout
uses its default density; `pi5-dsi7` writes `APPLAUNCH_PANEL_MM=154x86` (the display's active area). These boards
(the Pi 5 with either panel, and the HackBerry) normally run the Raspberry Pi OS desktop: they then get the desktop variant
([Desktop boards](#desktop-boards)); on a Lite image they are installed like the deck.

**Pi 5 + HyperPixel 4: brightness levels (`--pwm-backlight`, optional; report 030, c-4, not yet tested on the
board).** The HyperPixel overlay drives the backlight (GPIO19) with `gpio-backlight`: on or off only, so Settings
offers On / Off. `sudo ./install.sh --pwm-backlight` turns it into a PWM backlight with 10 levels; a plain install never
does (and `--no-boot-config` wins over it). On this board only, on the appliance and the desktop variant alike, it:

- builds `/boot/firmware/overlays/hyperpixel4-pwm-backlight.dtbo` from `hyperpixel4-pwm-backlight.dts` with `dtc`;
- copies `config.txt` to `config.txt.bak-applaunch` (once) and adds `dtoverlay=hyperpixel4-pwm-backlight` right after
  the `dtoverlay=vc4-kms-dpi-hyperpixel4...` line (it changes the backlight that overlay adds, so it must come after
  it; without that line nothing is done);
- asks for a reboot.

The overlay keeps the HyperPixel overlay's `backlight` node (the panel driver waits for that node's backlight) and makes
it a `pwm-backlight` on the RP1 hardware PWM0, channel 3 (GPIO19's alternate function), 10 kHz, levels 0..10
(`/sys/class/backlight/backlight`, `max_brightness` 10). Measured on the panel: 1 % duty is black, 2 % is dim and readable, and
the backlight is near full from about 15 %, so levels 1 to 9 run from 2 % to 14 % and level 10 is 100 % (it looks like level 9) (the table is in the `.dts`). A coil whine may be heard at
low levels. The profile needs no line: the launcher finds the levels by itself (as on the deck), and Raspberry Pi OS's
`60-backlight.rules` already lets the video group set the brightness. `uninstall.sh` removes the line and the overlay.

Revert by hand: delete the line `dtoverlay=hyperpixel4-pwm-backlight` from `/boot/firmware/config.txt` (or copy
`config.txt.bak-applaunch` back if nothing else changed since), `sudo rm /boot/firmware/overlays/hyperpixel4-pwm-backlight.dtbo`,
`sudo reboot`: the on/off `gpio-backlight` is back. If the panel stays dark after the reboot, take the SD card to another
computer and remove the line from `config.txt` on its boot partition.

**3.5" SPI panel on a Zero 2 W: brightness levels (`pi3a-luckfox35`, by default; tested by hand on zero8 with the
Waveshare 3.5" RPi LCD (F)).** The panel's overlay drives its backlight pin GPIO18 on or off
(`dtparam=...,backlight-gpio=18`: the `backlight_gpio` device), so Settings offered On / Off 10 s. The same stock
`mipi-dbi-spi` overlay has a `backlight-pwm` parameter: a kernel `pwm-backlight` on the hardware PWM0 of GPIO18 (5 kHz,
16 levels: `/sys/class/backlight/backlight_pwm`, `max_brightness` 15). On a Zero 2 W, whose PWM0 has no other user, a
whole install sets it up without any option:

- copies `config.txt` to `config.txt.bak-applaunch` (once), takes `backlight-gpio=18` off its line and adds
  `dtparam=backlight-pwm` on the next line (a `dtparam` applies to the last overlay loaded: it stays before the next
  `dtoverlay=` line, the touch panel's);
- writes `APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/backlight_pwm` and `APPLAUNCH_DIM_PERCENT=10` in `board.conf`
  (the only lines that differ from the on/off profile) and the udev rule `92-applaunch-backlight-sysfs.rules` for it;
- asks for a reboot: until then the on/off device is still the one there, and Settings > Screen > Brightness cannot set
  the panel.

After the reboot Brightness has the 10 steps, the idle clock dims (`APPLAUNCH_DIM_PERCENT`, default 10 %: level 1 of 15)
and the level comes back at start-up. A second run changes nothing. The on/off backlight stays (the output says why)
with `--no-boot-config`, with `--board-only` (a whole install does it), on a desktop board, when `config.txt` has no
`dtoverlay=mipi-dbi-spi` with `backlight-gpio=18` (another pin, two such overlays, both parameters), and when
`board.conf` was edited by hand: the kept file names its own backlight, so `config.txt` is kept as well (`--force` writes
both, the old `board.conf` kept as `board.conf.bak`). A `config.txt` that already has `dtparam=backlight-pwm` (an
earlier run, a hand edit) is left as it is and gets the PWM line in the profile, on any model. `--pwm-backlight` is not
needed (it is ignored here).

- **Pi 3A+:** keeps the on/off backlight. Its PWM0 is the 3.5 mm jack's, which `backlight-pwm` turns off
  (`snd_bcm2835.enable_headphones=0`), and its panel's backlight wiring is not tested.
- **Updates:** a board with the on/off profile an earlier release generated gets the new profile from
  `sudo ./install.sh --board pi3a-luckfox35` run by hand (the old one kept as `board.conf.bak`; it asks for the reboot).
  Settings > Updates does not do it: it stops there ("it would change /etc/applaunch/board.conf"). A board changed by
  hand already (both files, as zero8 was) updates as before, with nothing changed in either file.
- **Revert by hand:** in `/boot/firmware/config.txt` replace the line `dtparam=backlight-pwm` with
  `dtparam=backlight-gpio=18` (or copy `config.txt.bak-applaunch` back if nothing else changed since), run
  `sudo ./install.sh --board pi3a-luckfox35 --no-boot-config` (the on/off profile; the option is recorded, so updates keep
  it), `sudo reboot`. `uninstall.sh` leaves `dtparam=backlight-pwm` in `config.txt`: it is the stock overlay's own
  parameter, and without the launcher the panel stays lit (the kernel's default level 15, full).

The profile is a systemd `EnvironmentFile` (`EnvironmentFile=-/etc/applaunch/board.conf` in `cyberdeckzero.service`,
`-` = optional). systemd lets its values override the service's `Environment=` lines, so the deck's values
(`/dev/fb0`, rotation 0, touch swap/invert) stay in the service and a profile replaces the ones it names:

```
APPLAUNCH_BOARD=pi3a-luckfox35       # label, logged at start, shown in Settings > About
APPLAUNCH_FB=/dev/fb1                # framebuffer device (default /dev/fb0); install.sh writes the detected one
APPLAUNCH_LOGICAL=480x320            # landscape canvas the UI draws on (default 640x480)
APPLAUNCH_ROTATE=90                  # 0|90|180|270 clockwise pre-rotation into the buffer (default 0)
APPLAUNCH_COMPAT_SCALE=1             # 1 or 2: scale of the 320x170 legacy-app window (default 2)
APPLAUNCH_TOUCH_DEV=auto             # auto = pick by capability, or /dev/input/eventN
APPLAUNCH_TOUCH_ORIENT=buffer        # buffer = device axes follow the framebuffer; legacy = SWAP_XY/INVERT_* env (default legacy)
APPLAUNCH_BACKLIGHT=gpio:/sys/class/backlight/backlight_gpio   # gpio:<dir> on/off | sysfs:<dir> levels | none; unset: detected
APPLAUNCH_GAUGE=max17048:/dev/i2c-15:0x36   # battery fuel gauge read over i2c-dev when the kernel has no battery; auto = the bus of the touch controller
APPLAUNCH_KBD_IGNORE=bus:0011        # keyboards that do not count as connected (names, bus:XXXX; drop:... = not read either)
APPLAUNCH_HIDE_TILES=app_Python      # home tiles hidden by default (Settings > Launcher can show them)
APPLAUNCH_DIM_PERCENT=20             # idle dim level (screensaver, idle dim), percent of the backlight: 1..100 (default 10); Settings > Screen > Idle dim level overrides it
```

`APPLAUNCH_BACKLIGHT=sysfs:<dir>` (since x-b; before, it fell back to `/sys/class/backlight/backlight`) writes
`<dir>/brightness` in levels from 0 to `<dir>/max_brightness`: the Settings slider (10 % steps, never under 10 %),
the hotkeys, the screensaver's dim and the sleep / wake all use it. The dim goes to `APPLAUNCH_DIM_PERCENT` of
the maximum (an integer 1..100; unset or anything else: 10 %). Every profile with an `APPLAUNCH_BACKLIGHT=sysfs:` line
writes it out: 10 (`pi3a-wsdsi43`, `pi3a-wsdsi43-portrait`, `pi3a-luckfox35` with the PWM backlight, `pc-*`), except
`pi4b-dsi` and `pi5-dsi7`, which write 20 (the Freenove 5" panel is black at 10 %; the 7" panel is the same family);
they write it even without their backlight line. The on/off and no-backlight profiles (`pi3a-luckfox35` on/off, `pi5-hyperpixel4`, `hackberry-cm5`; the deck has no
profile) have no dim line: there is no level to dim to (on the deck and a Pi 5 with `--pwm-backlight` the launcher
detects the levels and uses the default 10). A board whose profile an earlier release generated without the line gets
it from `install.sh` run by hand (the old file kept as `board.conf.bak`); Settings > Updates stops there ("it would
change /etc/applaunch/board.conf"), and a hand-edited profile is kept. Without the line (the deck, the Pi 5 + HyperPixel 4, the
HackBerry) the launcher detects the backlight (report 030): the kernel's preferred device of `/sys/class/backlight`
(firmware, then platform, then raw); `max_brightness` 1 is on / off only (the Pi 5 HyperPixel's `gpio-backlight`:
Brightness offers On / Off 10 s), more is levels (the deck's PWM `backlight`, exactly as before), no device is no
backlight (no Brightness row). `APPLAUNCH_BACKLIGHT=none` says the board has no backlight to drive although a device
exists. `APPLAUNCH_HINT_BATTERY`, `APPLAUNCH_HINT_BACKLIGHT` (`button`) and `APPLAUNCH_HINT_SOUND`
(`bluetooth:<speaker>`) name hardware a board has but that is not set up: Settings then shows a read-only row with the
reason instead of nothing (the HackBerry profile writes `button` and `bluetooth:XWF-M18-M28-M38`).
`APPLAUNCH_GAUGE=max17048:<i2c-dev path or auto>:<address>` (report 030, C5) reads a MAX17048 fuel gauge over i2c-dev, only
when `/sys/class/power_supply` has no battery: percent, voltage and the charging direction (the HackBerry profile writes
`max17048:auto:0x36`; the user needs the `i2c` group, which install.sh adds on that board). `APPLAUNCH_KBD_IGNORE` is a comma list of device
names exactly as `/proc/bus/input/devices` prints them and `bus:XXXX` entries (hex: `bus:0011` = the i8042
keyboard of a laptop or tablet). A listed keyboard is still read (its keys work: a tablet's buttons, a keyboard
dock), but it does not count as "a keyboard is connected", so the on-screen keyboard shows when no other keyboard is
there; an entry prefixed `drop:` (`drop:<name>`, `drop:bus:XXXX`) is not read at all (a barcode scanner that must
not type into the launcher).

The `pi3a-luckfox35`, `pi3a-wsdsi43`, `pi4b-dsi` and `pi5-dsi7` profiles also set `LV_LINUX_FBDEV_DEVICE` to the same framebuffer and
`APPLAUNCH_TOUCH_SWAP_XY/INVERT_X/INVERT_Y=0`, so the deck's touch axes from the service do not apply.

- **Force a profile:** `sudo ./install.sh --board pi3a-luckfox35` (or `pi3a-wsdsi43`, `pi3a-wsdsi43-portrait`,
  `pi4b-dsi`, `pi5-hyperpixel4`, `pi5-dsi7`, `hackberry-cm5`; the panel's framebuffer must be there), or `--board zero2w` for no profile.
  `sudo ./install.sh --board-only` only (re)writes the profile and the backlight rule.
- **Hand edits** are kept: a re-run leaves an edited `board.conf` alone; `--force` replaces it and keeps the old one
  as `board.conf.bak`. Restart the launcher after an edit: `systemctl --user restart cyberdeckzero.service` (desktop
  boards: quit it and open it again from the menu).
- **Add a board:** probe it with `tools/board-probe/probe-board.sh`, then in `install.sh` add its detection to the
  `case "$MODEL"` block, its panel to `panel_spec()`, its values to `profile_body()` and its name to `--board`; add a
  fixture under `tests/fixtures/` (fake `proc/` and `sys/` trees) and cases to `tests/test-install-board.sh` (run it
  with `sh`, no Pi needed; on Windows, run it in WSL on a copy whose fixtures have LF line endings, as git stores them).

## Text console

The kernel's framebuffer console (fbcon) is bound to the panel's framebuffer at boot, so the boot lines show there. The
launcher draws on the same framebuffer and only redraws what changes: any console repaint afterwards (a VT switch,
kernel messages, a long `apt install`) would draw the old boot lines over it. `cyberdeckzero-console.service` (appliance
boards) runs `/usr/share/APPLaunch/bin/cyberdeckzero-console unbind` once `multi-user.target` is reached: it finds the
vtconsole named "frame buffer device" and sets its `bind` to 0 (nothing to do without one). The boot lines still show
during the boot; the unit is part of the boot transaction and the launcher's user service starts only once systemd
reports the system running, so after it. At shutdown the unit binds the console again (the shutdown lines show as
before). `cyberdeckzero-console status` prints the state; `sudo /usr/share/APPLaunch/bin/cyberdeckzero-console bind` gives the
panel back to the console by hand.

## Desktop boards

A board that boots to the Raspberry Pi OS desktop (the HackBerry CM5, the Pi 5: lightdm with labwc), or a PC with a
desktop (the FZ-M1 tablet with XFCE: lightdm with Xorg), keeps its desktop.
`install.sh` sees it (a display manager enabled for boot: `/etc/systemd/system/display-manager.service` naming an
installed unit, with the system booting to `graphical.target`; or one of the desktop processes
`tools/board-probe/probe-board.sh` looks for running now) and installs the desktop variant
(`--dry-run` prints `session: desktop (...)` and the unit it would write):

- no user service, no lingering, `getty@tty1` left as it is, no change to `config.txt` or `cmdline.txt`; the board
  profile, udev and PolicyKit rules, Network Time service and default app source as on the other boards;
- `/etc/systemd/system/cyberdeckzero-desktop.service`, written for the launcher's user and the display manager found
  (`lightdm.service`). It has no `[Install]` section: it cannot be enabled, the launcher never starts at boot;
- `/usr/share/applications/cyberdeckzero.desktop`: "CyberdeckZero Launcher" in the applications menu (Accessories), which
  runs `systemctl --no-block start cyberdeckzero-desktop.service`;
- `/etc/polkit-1/rules.d/52-cyberdeckzero-desktop.rules`: that user, and only that user, may start, stop or restart that
  one unit without a password. Nothing is opened up for lightdm: the unit stops and starts it as root;
- with WirePlumber installed, `~/.config/wireplumber/wireplumber.conf.d/50-cyberdeckzero-bluez-no-seat.conf` for that
  user (`monitor.bluez.seat-monitoring = disabled`): once the launcher has stopped the display manager, WirePlumber 0.5
  would otherwise drop its Bluetooth monitor and a Bluetooth speaker fails to connect ("a2dp-sink profile connect failed:
  Protocol not available"). It applies after the next login or `systemctl --user restart wireplumber`; uninstall.sh
  removes it.

**Start:** applications menu > CyberdeckZero Launcher (or `systemctl start cyberdeckzero-desktop.service` from a shell,
no sudo). The unit has `Conflicts=lightdm.service` and `After=lightdm.service`: systemd stops the desktop first, then
runs, as root, `cyberdeckzero-console unbind` (no console repaint over the launcher) and `chvt 7` (a VT without a login
prompt: the keys typed in the launcher also reach the active VT), then the launcher as the user, with
`APPLAUNCH_DISPLAY=dpi-scaled`, `APPLAUNCH_STD_KEYBOARD=1`, the board profile
(`EnvironmentFile=-/etc/applaunch/board.conf`) and `APPLAUNCH_SESSION=desktop`. `PAMName=login` gives it a login
session, so `/run/user/<uid>` (the launcher's state files) and the user's own services (sound) stay up after the desktop
session has ended.

**Quit:** Settings > System > **Quit to desktop** > Yes. The entry exists only in a launcher started by this unit
(`APPLAUNCH_SESSION=desktop`); never on the deck or the Pi 3A+, whose user service would leave a dark panel. The
launcher ends its main loop, tears down and exits with status 0 (if the teardown takes more than 5 s it exits with 0
anyway). The unit then runs, as root, `cyberdeckzero-console restore-desktop lightdm.service`: the text console is bound
again and lightdm is started (`--no-block`, since that start waits for the unit's own stop to end).

**Any other end gives the desktop back too:** a crash, a kill, or the hold-Esc recovery of a stuck page (exit status
75, which restarts the user service on the appliance boards). `Restart=no`: the unit is never restarted, so there is
no restart loop and never a board without a screen; open the launcher from the menu again.

**The one restart: exit status 76.** A change of Settings > Screen > PDA mode ends the launcher with exit status 76
(the new value is read when it starts). Both units start it again on that status (`RestartForceExitStatus=76`; the
user service also has `Restart=on-failure`); on the desktop unit, `cyberdeckzero-console restore-desktop` sees
`EXIT_STATUS=76` in `ExecStopPost=` and leaves the display manager stopped and the console free, so the desktop does
not come back in between (its start would stop the launcher again through `Conflicts=`). A shutdown under way still
wins: the console is bound and nothing starts.

**Shutdown and reboot** from Settings > System (or anywhere else): the unit is stopped in the poweroff or reboot
transaction itself. `restore-desktop` sees it (`systemctl is-system-running` answers `stopping` as soon as
`shutdown.target` has a start job, which every poweroff, reboot, halt and kexec queues before the units stop; a queued
job for one of those targets counts too) and does not start lightdm: the board powers off or reboots without the
desktop coming back in between.

**Open desktop programs are closed.** Starting the launcher stops the display manager, and with it X (or the
compositor) and the user's desktop session: every open desktop program ends at once, without a prompt, and unsaved
work in it is lost (save first). Programs started with `nohup` or as services keep running. When the launcher gives
the desktop back, the display manager starts a new session (with autologin, the user is logged in again); the programs
open before are not reopened.

**Uninstall:** `./uninstall.sh` (from ssh) stops the unit if it runs (the desktop comes back), removes it, the menu
entry and the PolicyKit rule (see [Uninstall](#uninstall)). The deck profile (`zero2w`) has no desktop variant: on a
deck image with a desktop, `install.sh` stops and changes nothing.

### Switching between appliance and desktop

`install.sh` chooses the variant at every run. Install or remove a desktop (a display manager) later, then run
`sudo ./install.sh` again (this bundle or a newer one); `sudo ./install.sh --dry-run` shows the switch first
(`switch from the appliance variant to the desktop variant` and the files it found). Every install ends with
`session: desktop (<reason>)` or `session: appliance (<reason>)` and the reminder to run it again after a desktop
change.

- **Appliance to desktop** (a display manager is enabled or running now): before the desktop files are written,
  the user service `cyberdeckzero.service` is stopped (if the launcher runs on the panel the install says so first: it
  quits now), disabled and removed, and `cyberdeckzero-console.service` is disabled, stopped (the text console is bound
  again) and removed: the launcher and the display manager never both start at boot, and the desktop unit frees the
  panel itself while the launcher runs. Then the desktop unit, the menu entry and the PolicyKit rule, as above.
  Nothing is started: the desktop comes up at the next boot (or now: `sudo systemctl start lightdm`). Run it from ssh
  or a terminal on the desktop; from the launcher's own Terminal app `install.sh` refuses (stopping the user service
  would end it half way).
- **Desktop to appliance** (no display manager any more: none enabled, its unit removed, or the system booting to
  `multi-user.target`): the desktop unit, the menu entry and the PolicyKit rule are removed, then the appliance
  variant is installed and started as on a fresh board (user service with lingering, console unit, `getty@tty1`
  masked). A launcher started from the menu that still runs is left running (its exit would start the display manager
  again if its unit is still there): the user service then starts at the next boot.
- **Kept** either way: `/etc/applaunch/board.conf` (a PC's `pc-*` profile included, byte for byte), the user's
  settings, app sources and installed apps, the boot configuration. Going to the desktop also keeps lingering (harmless;
  `loginctl disable-linger <user>` turns it off) and `getty@tty1` as it is (masked by the appliance install: lightdm
  uses VT 7; `uninstall.sh` unmasks it).
- **Warnings:** a desktop found running while no display manager is enabled for boot, or while the system boots to
  another target than `graphical.target`, is named with the fix (`sudo systemctl set-default graphical.target`): the
  launcher no longer starts at boot, so the desktop must.
- A second run finds nothing of the other variant and switches nothing. The deck (`zero2w`) has no desktop variant:
  with a desktop there, `install.sh` stops before changing anything, as before.
- `tools/boards/boards.json`: set the board's `kind` to `desktop` after the switch (`service` after the way back).

### Desktop on the FZ-M1 tablet (Armbian XFCE)

The tablet (`pc-fzm1-2`, Armbian 26.8 on Ubuntu 26.04, 3.8 GB RAM) installed as an appliance gets XFCE from Armbian's
own tool, then `install.sh` again. What `armbian-config` > Software > Desktops > XFCE does (configng `module_desktops`,
`xfce.yaml`, minimal tier), as far as the launcher is concerned:

- installs `xfce4`, `xfce4-goodies`, `lightdm` with `slick-greeter`, `xserver-xorg` (an X11 session, no Wayland),
  pulseaudio, blueman, `polkitd`; the lightdm package points `display-manager.service` to `lightdm.service`
  (`/etc/X11/default-display-manager`), which install.sh reads. lightdm runs X on VT 7 (its default; Armbian's
  `lightdm.conf.d` files do not change it);
- sets netplan's renderer to NetworkManager (`/etc/netplan/00-default-use-network-manager.yaml`, removes
  `10-dhcp-all-interfaces.yaml`, runs `netplan apply`, enables NetworkManager and systemd-resolved): the same renderer
  as the tablet's own NetworkManager set-up, no conflict, but the Wi-Fi can reconnect once during the apply;
- starts lightdm at once, sets `graphical.target`, and turns on autologin for the user who ran it with sudo
  (`/etc/lightdm/lightdm.conf.d/22-armbian-autologin.conf`: `autologin-user=osrde`, `user-session=xfce`), so the
  tablet boots into XFCE.

Steps, from ssh as osrde, with this release's amd64 bundle unpacked in `~/bundle`:

```
systemctl --user stop cyberdeckzero.service  # the launcher quits now: no launcher hidden under the desktop (the panel keeps its last picture)
sudo apt install tmux && tmux                # armbian-config goes on if the ssh link drops during the netplan apply (tmux attach)
sudo armbian-config                          # Software > Desktops > XFCE (minimal tier); XFCE appears on the panel
cd ~/bundle && sudo ./install.sh --dry-run   # session: desktop (display manager lightdm.service enabled; running: Xorg+lightdm), the switch
sudo ./install.sh                            # user service and console unit off; desktop unit, menu entry, PolicyKit rule
sudo reboot                                  # boots to XFCE, osrde logged in
```

Then on the tablet: Applications menu > Accessories > **CyberdeckZero Launcher**. XFCE ends (its open programs are
closed, see above) and the launcher takes the panel; Settings > System > **Quit to desktop** brings XFCE back (a fresh
session). On the way, as root, once lightdm has stopped: `cyberdeckzero-console unbind`, `chvt 7` (X switches back,
when it stops, to the VT that was active when it started: VT 1 after the boot, whose login prompt the appliance install
masked; VT 7 is free then, whichever VT X used), the launcher on `/dev/fb0` (i915's fbdev
emulation shows it again once X has let go of the display), then `bind` and `systemctl --no-block start lightdm` at
the exit. `chvt` comes from `kbd`, which install.sh adds on a PC with a desktop.

Optional, in a terminal on the desktop (as osrde):

- **On-screen keyboard for the desktop** (the launcher has its own): `sudo apt install onboard` (Ubuntu universe), then
  Onboard's Preferences > General: "Auto-show when editing text" (it needs the accessibility bus:
  `xfconf-query -c xfce4-session -p /general/StartAssistiveTechnologies -n -t bool -s true`, then log out and in), and
  Settings > Session and Startup > Application Autostart: Onboard.
- **Bigger touch targets** (1280x800 on 7"): `xfconf-query -c xsettings -p /Xft/DPI -n -t int -s 144` (text 1.5x),
  `xfconf-query -c xsettings -p /Gtk/CursorThemeSize -n -t int -s 48`, `xfconf-query -c xfwm4 -p /general/theme -s
  Default-xhdpi` (large window buttons), the panel: right-click > Panel > Panel Preferences > Row size 48, single-click
  icons: `xfconf-query -c xfce4-desktop -p /desktop-icons/single-click -n -t bool -s true` (Thunar: Edit >
  Preferences > Behavior > Single click).

**Without armbian-config** (plain apt): `sudo apt install --no-install-recommends xserver-xorg lightdm
lightdm-gtk-greeter xfce4 xfce4-terminal dbus-x11` (answer `lightdm` if a "Default display manager" question comes;
`sudo dpkg-reconfigure lightdm` changes it later; keep one display manager: `readlink
/etc/systemd/system/display-manager.service` must end in `lightdm.service`), `sudo systemctl set-default
graphical.target`, autologin: `printf '[Seat:*]\nautologin-user=osrde\nautologin-user-timeout=0\nuser-session=xfce\n' |
sudo tee /etc/lightdm/lightdm.conf.d/50-autologin.conf`, then `sudo ./install.sh` again and reboot.

**Back to the appliance:** `sudo armbian-config` > Software > Desktops > remove XFCE (it purges the packages it installed,
lightdm normally among them, removes the autologin file and sets `multi-user.target`; NetworkManager stays; it does not
disable lightdm by name), then `sudo ./install.sh` again from ssh:
it switches back (a `display-manager.service` link left pointing to the removed lightdm counts as no display manager),
and the launcher starts at once and at every boot.

## Network shares helper

`install.sh` installs `/usr/libexec/cyberdeckzero/cyberdeckzero-share` (root:root 0755, a POSIX `sh` script) and the package
`cifs-utils` (apt, no recommended packages; the anchor package keeps it, so `apt autoremove` never takes it away; if it
cannot be installed, for example without a network, the install only prints a warning and goes on). An app (Vinyl is the
first) uses it to add and remove an SMB share from its own screen. **The launcher never grants passwordless sudo for it:
there is no sudoers rule.** The app runs it through the toolkit's shared sudo password prompt (`sudo -k`, then
`sudo <helper> ...`), so the user types the password at every Add and Remove. The contract for apps is in
[`docs/APP-CONTRACT.md`](../../../docs/APP-CONTRACT.md) ("Network shares (SMB)").

**Grammar.** Every argument is checked against an allow-list before anything else; unknown verbs and flags are `err usage`.
No option for `mount` is ever taken from the caller.

```
cyberdeckzero-share version
cyberdeckzero-share list
cyberdeckzero-share add --name N --host H --share S [--subfolder P] [--user U --credfile PATH]
cyberdeckzero-share remove --name N
```

| Argument | Allowed |
| --- | --- |
| `--name` | 1-32 characters of `A-Za-z0-9_-`, not starting with `-` |
| `--host` | an IPv4 address (no leading zeros) or a host name (labels of `A-Za-z0-9-`, dots, 253 characters at most, so `nas.local` works; no IPv6, no port) |
| `--share` | 1-80 characters of `A-Za-z0-9_.$-` and single inner spaces (no leading or trailing space, no two in a row; no tab), not `.`, no `..` (so no `/`, `\` or commas) |
| `--subfolder` | relative path of segments of `A-Za-z0-9_.$@+=-` (each with single inner spaces, as in `--share`) separated by single `/`, no `..`, 200 characters at most |
| `--user` | 1-64 characters of `A-Za-z0-9_.@-`, not starting with `-`; goes with `--credfile` (both or neither) |
| `--credfile` | exactly `/run/user/<SUDO_UID>/cyberdeckzero-share/<random>.cred`, `<random>` 8-64 characters of `A-Za-z0-9_-` |

**Who is calling.** `add` takes the user from `SUDO_UID`, `SUDO_GID` and `SUDO_USER` (it refuses when they are missing,
when the user is root or when `id` does not agree), and mounts with `uid=`/`gid=` of that user. The helper never reads stdin (the
sudo password may arrive there) and never logs, echoes or passes a password as an argument. `list` and `version` need no root;
`remove` needs root but not a calling user.

**The credentials file** (only with `--user`). The app writes `username=`, `password=` and optionally `domain=` lines
(`key=value`, no other key, no empty or control line, at most 1024 bytes, the password 1-256 bytes) into the path above:
owned by the calling user, mode 0600, a regular file, not a symlink. The helper opens it, checks what it opened, copies it into
`/etc/cyberdeckzero/shares/<name>.cred` (0600 root, folder 0700, made on the first use) and deletes the app's file, also when
the add fails later. A file that fails the owner, mode or type check is refused with `err usage` and left where it is.

**What `add` does.** A test mount (10 s limit) with the final options into a private work folder; on failure nothing is left
behind. On success it writes, each with a first line that marks it as the helper's:

- `/etc/systemd/system/mnt-cyberdeck-<escaped>.mount` (`What=//host/share[/subfolder]`, a space written as a plain space, `Where=/mnt/cyberdeck/<name>`, `Type=cifs`,
  `TimeoutSec=10`, `Options=ro,nosuid,nodev,noexec,uid=,gid=,forceuid,forcegid,file_mode=0444,dir_mode=0555,soft,echo_interval=10`
  plus `guest` or `credentials=/etc/cyberdeckzero/shares/<name>.cred`; no `vers=`: the kernel negotiates, SMB1 is not offered),
- `/etc/systemd/system/mnt-cyberdeck-<escaped>.automount` (`TimeoutIdleSec=60`, `WantedBy=remote-fs.target`),
- the mount point `/mnt/cyberdeck/<name>` (the folder `/mnt/cyberdeck` is 0755 root and never a symlink),
- the credentials file, then `systemctl daemon-reload` and `systemctl enable --now` of the **automount** only. The share is
  mounted on first use and again after a reboot; the boot never waits for the server. The unit name is made with `systemd-escape`.

`remove` stops and disables the units, unmounts (lazily when busy), deletes the units and the credentials file, reloads systemd
and removes the mount point when it is an empty folder under `/mnt/cyberdeck`; it is idempotent (a name that is not there is `ok`).
It refuses a name whose unit was not made by the helper (`err other`), and `add` refuses a name that already maps to such a
unit, to a mount point or to a credentials file (`err exists`). `list` shows only units with the marker; other units are never touched.

**Reply** (stdout; stderr stays empty; the **last line** is the answer): `ok` (remove), `ok 1` (version, the protocol level),
`ok path=/mnt/cyberdeck/<name>` (add), for `list` one line `<name> <host> <share> [<subfolder>] <state>` per share (a space inside the share or subfolder is
written `\040`, so each field is one token; state
`mounted`, `idle`, `failed` or `inactive`) and then a line that is exactly `ok`, or `err <word> [reason]`. Exit status 0 only on
`ok`; 2 for `err usage`; 1 for any other `err`.

| Word | Meaning |
| --- | --- |
| `usage` | a bad verb, flag or value, a missing or refused credentials file, `SUDO_*` missing or root |
| `exists` | the name, the mount point or its credentials file is taken (also by something the helper did not make) |
| `nomodule` | the kernel has no `cifs` module (`cifs filesystem not supported`) |
| `nohelper` | `mount.cifs` is not installed (package `cifs-utils`) |
| `unreachable` | the host name does not resolve, no route, connection refused or host down |
| `notfound` | the server names the missing share or subfolder (`NT_STATUS_BAD_NETWORK_NAME`, `NT_STATUS_OBJECT_NAME_NOT_FOUND`, `NT_STATUS_OBJECT_PATH_NOT_FOUND`, or `mount error(6)`) |
| `denied` | the server refused the user, or the guest (`EACCES`, `NT_STATUS_LOGON_FAILURE`, `NT_STATUS_ACCESS_DENIED`); also every `mount error(2)` (No such file or directory) without an `NT_STATUS` word, which a wrong share name and a share that needs a login both cause (the kernel logs the telling line only once per boot, so the helper no longer reads it): reason `wrong share name, or it needs a login`, or `wrong share or folder name, or it needs a login` with `--subfolder` |
| `timeout` | no answer within the 10 s limit, or `ETIMEDOUT` |
| `other` | anything else (also a refused `remove`, a failed `systemctl`), with the first line of the message as the reason (printable ASCII, 160 characters at most, the password and `key=value` credential text removed) |

**Undo by hand** (`<name>` as given to `add`; the escaped unit name is `systemd-escape --path /mnt/cyberdeck/<name>`):

```
sudo systemctl disable --now "$(systemd-escape --path /mnt/cyberdeck/<name>).automount"
sudo systemctl stop "$(systemd-escape --path /mnt/cyberdeck/<name>).mount"
sudo rm "/etc/systemd/system/$(systemd-escape --path /mnt/cyberdeck/<name>)".mount "/etc/systemd/system/$(systemd-escape --path /mnt/cyberdeck/<name>)".automount
sudo rm -f /etc/cyberdeckzero/shares/<name>.cred
sudo systemctl daemon-reload
sudo umount -l /mnt/cyberdeck/<name>; sudo rmdir /mnt/cyberdeck/<name>
```

**Install and uninstall.** `install.sh --dry-run` prints a "network shares" paragraph and `--plan` the keys `share_helper=` and
`optional_packages=`. `uninstall.sh` removes the helper (and its folders when empty) but **leaves the user's shares alone**
by default, says which ones, and prints the way to remove them by hand; `uninstall.sh --remove-shares` removes them first through the
helper (only units with the helper's marker). `cifs-utils` stays installed, as every package `install.sh` adds.

Tests: `tests/test-share-helper.sh` (fake root, stand-ins for `mount`, `systemctl`, ...; see "Files in this folder").

## Uninstall

`uninstall.sh` (in the bundle next to `install.sh`) removes what `install.sh` added. Run it as the deck user over
ssh, not from the launcher's own terminal; it asks for `sudo` only for the system parts and never stores a password.

```
./uninstall.sh --dry-run            # print every action, change nothing
./uninstall.sh                      # summary, one confirmation, then act
./uninstall.sh --yes --purge-config # no prompt; also delete the user's launcher data
```

It stops, disables and deletes `cyberdeckzero.service` (and `APPLaunch.service` of an install made before v0.7.0;
through systemd, no `pkill`), `cyberdeckzero-console.service` (the
panel goes back to the text console; `cyberdeckzero-console bind` is run as well), `cyberdeckzero-desktop.service` (stopped
first if it runs: the desktop comes back) with the menu entry `/usr/share/applications/cyberdeckzero.desktop`, and
`launcher-ntp-default.service`,
removes the binaries, the framebuffer shim, the launcher assets under `/usr/share/APPLaunch` (only the files of the
bundle, listed in `payload/share.manifest`), the udev rules (then `udevadm control --reload`) and the PolicyKit rules,
the board profile `/etc/applaunch/board.conf` (and the directory if empty; a `board.conf.bak` is kept),
unmasks and enables `getty@tty1` if it is masked, and takes the PWM backlight out of the boot configuration: the exact
lines `dtoverlay=waveshare-pwm-backlight` and `dtoverlay=pwm,pin=18,func=2` in `config.txt` (on the Pi 5 + HyperPixel 4,
`dtoverlay=hyperpixel4-pwm-backlight`; a timestamped
`config.txt.bak-uninstall-<time>` is made first and the removed lines are printed), the `.dtbo`, and the
`vt.global_cursor_default=0` / `consoleblank=0` arguments in `cmdline.txt` when the `*.bak-applaunch` backup shows that
`install.sh` added them. Reboot afterwards. The 3.5" SPI panel's `dtparam=backlight-pwm` (a Zero 2 W with
`pi3a-luckfox35`) stays in `config.txt`: the panel stays lit without the launcher ([Boards](#boards) says how to revert it).
The block that `install.sh --spi-panel luckfox35` added to `config.txt` on a Raspberry Pi (between the `# >>> cyberdeckzero spi
panel` and `# <<< cyberdeckzero spi panel` lines) is removed, and so is `/lib/firmware/st7796s.bin` when `install.sh` put it there
and it is unchanged; a panel set up by hand is never touched ([Raspberry Pi: the 3.5" SPI panel](#raspberry-pi-the-35-spi-panel)).
On Armbian it undoes what `install.sh --spi-panel waveshare35f` recorded in `install.conf` (v0.10.9; [Orange Pi Zero 2W: the 3.5" SPI panel](#orange-pi-zero-2w-the-35-spi-panel), "Undo").
It also removes the network share helper (the shares stay unless `--remove-shares`, [Network shares helper](#network-shares-helper)),
the FZ-M1's power-key drop-in when `install.sh` wrote it and it is unedited ([Install on a PC](#install-on-a-pc-amd64)),
and purges the anchor package `cyberdeckzero-runtime` (it installs no file; the packages it names stay).

It keeps by default: `~/.config/cardputerzero`, the Settings > Apps sources and install records
(`~/.local/share/cardputerzero-appstore`, with the default source and its marker), the download cache, every installed
app package and its data (`/opt/...`), the packages `install.sh` pulled in with apt, the user's groups and lingering,
and the backup files. After `--purge-config` a new install offers the default app source again.

| Option | Effect |
| --- | --- |
| `--dry-run` | Print every action and change nothing. |
| `--yes` | No confirmation prompt (without it the script asks once, after printing what it removes and keeps). |
| `--purge-config` | Also delete `~/.config/cardputerzero`, `~/.local/share/cardputerzero-appstore` and `~/.cache/cardputerzero-appstore`, and Settings > Updates' `~/.local/state/cyberdeckzero` and `~/.cache/cyberdeckzero`. |
| `--remove-apps` | Also `apt-get remove` the app packages installed from Settings > Apps / the Store, taken from the launcher's `installed.json` and listed first. No record: skipped. With `--purge-config` the packages are purged. |
| `--restore-config` | Restore `config.txt` from `config.txt.bak-before-pwm` instead of removing the two lines (loses any other later edit; the current file is backed up first). This works only where that file exists (the deck's own backup, made by hand before `install.sh` existed). `install.sh` itself writes `config.txt.bak-applaunch`, which this option does not use; without the file the option stops with an error. |
| `--no-boot-config` | Leave `config.txt` (also the 3.5" SPI panel's block), `cmdline.txt`, the overlay and the panel's init file alone; on Armbian also the `--spi-panel waveshare35f` set-up (the overlay name in `armbianEnv.txt`, the files, the DKMS modules). |
| `--disable-linger` | Also run `loginctl disable-linger` (`install.sh` enabled it; it may have been on before). |
| `--remove-shares` | Also remove the network shares made with the share helper (unmount, units, credentials files). Without it they stay and keep working; the way to remove them by hand is printed ([Network shares helper](#network-shares-helper)). |
| `--user NAME`, `--payload DIR` | The user that ran `install.sh`; where the bundle's `payload/` is, if it is not next to the script. |

## Files in this folder

| File | Purpose |
| --- | --- |
| `build.sh` / `install.sh` / `uninstall.sh` | Build the bundle (host; `--arch arm64` by default, `--arch amd64` natively for PCs) / install it (Pi or PC) / remove it again. |
| `cyberdeckzero-update` | Settings > Updates' root helper, installed in `/usr/share/APPLaunch/bin` (`apply`, `resume`, `plan`, `options`, `status`, `apt-refresh`, `apt-upgrade`, `apt-repair`; [Settings > Updates](#settings--updates)). |
| `cyberdeckzero-share` | The network share helper, installed in `/usr/libexec/cyberdeckzero` (`version`, `list`, `add`, `remove`; run by an app through the sudo password prompt, no sudoers rule; [Network shares helper](#network-shares-helper)). |
| `default-sources.sh` | Adds the apps repo to Settings > Apps once for a user with no source yet (run by `install.sh` as that user; see "Default app source"). |
| `cyberdeckzero.service` | systemd **user** service: display, touch axes, keyboard device, working directory, optional board profile (`EnvironmentFile=-/etc/applaunch/board.conf`). |
| `cyberdeckzero-console`, `cyberdeckzero-console.service` | The text console helper (installed in `/usr/share/APPLaunch/bin`: `unbind`, `bind`, `status`, `restore-desktop UNIT`) and the appliance boards' boot-time unit that frees the panel from the console ([Text console](#text-console)). |
| `cyberdeckzero-desktop.service`, `cyberdeckzero.desktop`, `52-cyberdeckzero-desktop.rules` | Desktop boards: the system unit that runs the launcher in place of the desktop, the applications menu entry, and the PolicyKit rule that lets the launcher's user start and stop that unit; install.sh fills in the user and the display manager ([Desktop boards](#desktop-boards)). |
| `config.txt.snippet` | Panel part of `config.txt` the port was tested with. |
| `waveshare-pwm-backlight.dts` | Replaces the on/off GPIO18 backlight by a PWM one (Settings > Screen > Brightness). |
| `hyperpixel4-pwm-backlight.dts` | Pi 5 + HyperPixel 4, only with `install.sh --pwm-backlight`: its on/off GPIO19 backlight becomes a PWM one with levels (RP1 PWM0). |
| `panels/waveshare35f/` | `install.sh --spi-panel waveshare35f` (Orange Pi Zero 2W, Armbian) and `--spi-panel luckfox35` (a Raspberry Pi: only the init file): the panel's init file, its two overlays, the DKMS sources of `drm-mipi-dbi-chunk` and `pwm-gpio`; their origin and licences in its README ([Orange Pi Zero 2W: the 3.5" SPI panel](#orange-pi-zero-2w-the-35-spi-panel)). Copied to `payload/panels/` by `build.sh`; LF line ends (`.gitattributes`). |
| `90-backlight-unblank.rules` | The PWM backlight starts powered down: unblank it when it appears. |
| `91-applaunch-vkbd.rules` | `uinput` permission and the `/dev/input/applaunch-vkbd` name used for legacy apps. |
| `93-applaunch-rfkill.rules` | `/dev/rfkill` writable by the `netdev` group, so Settings > Bluetooth can run `rfkill unblock bluetooth` as the launcher user. |
| `50-networkmanager-netdev.rules`, `51-launcher-time-power.rules` | PolicyKit: Wi-Fi scan/connect, Network Time / clock, reboot / shutdown from a background session. |
| `launcher-ntp-default.service` | Network Time is on at every boot (a manually set time lasts for the session). |
| `ctl/` | `cyberdeckzero-ctl`, the `launcher.sock` command-line client (`docs/APP-CONTRACT.md` section 14), built by `build.sh`. |
| `vfb/` | Framebuffer shim (`libapplaunch_vfb.so`) and a test app (`fbtest.c`). |
| `tests/` | `test-install-board.sh`: board and desktop detection, profile, desktop unit and uninstall-plan tests on the PC against `fixtures/` (fake `/proc` and `/sys` trees of each board, desktop markers added by the test), `cyberdeckzero-console` against a fake `/sys/class/vtconsole` and a fake `systemctl` (the shutdown guard), and the default app source (`default-sources.sh` with a temp state folder and a fake Store backend). `test-update-helper.sh`: Settings > Updates' root helper `cyberdeckzero-update` and what `install.sh` gives it (`/etc/applaunch/install.conf`, `--plan`) in a fake root (`UPDATE_ROOT`, stand-ins for `systemctl`, `runuser`, `systemd-inhibit`, ...): options replayed or inferred, backup, plan refusals, health check, rollback, the desktop unit's restart, resume after an interruption, uninstall. `test-share-helper.sh`: the network share helper in a fake root (`CDZ_SHARE_SYSROOT`, stand-ins for `mount`, `umount`, `mountpoint`, `systemctl`, `systemd-escape`, `id`): every verb, every allow-list rejection, the calling-user and credentials-file checks, every error word, remove, foreign units, and that stdin is never read and no password leaks. **None of them is part of `tools/dev/check-all.sh`.** Run them as a normal user with `sh projects/APPLaunch/pizero2w/tests/test-install-board.sh` `sh projects/APPLaunch/pizero2w/tests/test-update-helper.sh` and `sh projects/APPLaunch/pizero2w/tests/test-share-helper.sh`, from an LF copy of the **whole repository** (the fixtures' line ends: a Windows checkout with `core.autocrlf=true` has CRLF fixtures, so use WSL on a copy with the CRs stripped; `test-update-helper.sh` also reads `projects/APPLaunch/tests/fixtures/apt/deck-p4`, which a copy of this folder alone does not have). A run on 2026-11-12 (main at v0.10.1, WSL, CRs stripped) passed 501 of 502 checks of `test-install-board.sh` (the failing one: "amd64 config: the device config without the NEON lines") and 159 of 159 of `test-update-helper.sh`; see [`tools/dev/README.md`](../../../tools/dev/README.md) for what `check-all.sh` runs. |

## Sound, battery and brightness per board

Since report 030 (phase c-1) the launcher finds out what the board has each time Settings opens
(`ext_components/cp0_lvgl/include/cp0_capabilities.h`); no build switch and no board name decides these rows:

- **Battery**: a Linux `power_supply` battery (as since v0.8.3): the Battery row and the status strip's level.
- **Screen > Brightness**: the backlight of the profile (`APPLAUNCH_BACKLIGHT`), else the one detected (see
  [Boards](#boards)): levels give the slider, `max_brightness` 1 gives On / Off 10 s, none gives no row.
- **Speaker** (after Screen, a volume slider, 100 % to 0 %): a sound card of the board's own that is not HDMI, with a
  playback volume control (Master, Speaker, PCM or Headphone, through `amixer`), or else a sound server's sink
  (PulseAudio or PipeWire through `pactl`; a Bluetooth speaker counts, `auto_null` and HDMI sinks do not). The volume
  goes through `pactl` when a server answers, else through the card's control (`amixer -M`, the user in the `audio`
  group: install.sh adds it when the board has such a card). A board with HDMI audio only has no Speaker row.
- The **volume and brightness keys** do nothing and show nothing on a board without that hardware.
- The launcher's own interface sounds (start-up, home, lock screen) stay off on our boards; the Speaker slider plays a
  short sound after each change so the new volume can be heard.

What each board gets (from the boards' measured facts, the fake trees of `projects/APPLaunch/tests/test_settings_capabilities.cpp`):

| Board | Battery | Brightness | Speaker |
| --- | --- | --- | --- |
| Deck (Zero 2 W, Waveshare 2.8" DPI) | no row | levels (PWM `backlight`, as before) | no row (no audio output) |
| Pi 3A+ Luckfox SPI | no row | On / Off (`gpio:` profile, as before) | a row when the 3.5 mm jack's card (`bcm2835 Headphones`, PCM) is there; HDMI alone: no row |
| Pi 3A+ Waveshare DSI | no row | levels (`sysfs:` profile, as before) | as the Luckfox |
| Zero 2 W + Waveshare 3.5" (F) (`pi3a-luckfox35`) | no row | levels (`sysfs:` profile, `backlight_pwm` 0..15, set up by install.sh, [Boards](#boards)); On / Off without it | no row (no audio output) |
| Orange Pi Zero 2W + Waveshare 3.5" (F) (`pi3a-luckfox35`, `--spi-panel`) | no row | On / Off (`gpio:` profile); levels with `--soft-pwm-backlight` (`sysfs:` profile, `backlight_pwm` 0..9, owner: smooth) | not checked |
| Pi 5 + HyperPixel 4 | no row | **On / Off** (detected: `gpio-backlight`, max 1; it offered 10 levels that all wrote 1 before); levels after `install.sh --pwm-backlight` ([Boards](#boards)) | no row (vc4-hdmi only, no `pactl`) |
| Pi 5 + 7" DSI Touch Display (`pi5-dsi7`) | no row | levels (`sysfs:` profile, regulator `10-0045`, max 255; checked on the board) | no row (vc4-hdmi only) |
| HackBerry CM5 | no row | **no row** (no backlight device; the row did nothing before) | a row while the case speaker is connected over Bluetooth (its sink); otherwise none |
| FZ-M1 tablet | Battery row | levels (`intel_backlight`, 937) | **Speaker row** (HDA Intel PCH, Master; with or without a sound server) |

## Settings and environment

`cyberdeckzero.service` environment: `APPLAUNCH_DISPLAY=dpi-scaled`, `APPLAUNCH_ROTATE=0`,
`LV_LINUX_KEYBOARD_DEVICE=/dev/input/bt-keyboard`, `APPLAUNCH_TOUCH_SWAP_XY/INVERT_X/INVERT_Y` (touch panel
orientation measured on this deck), `APPLAUNCH_STD_KEYBOARD=1`, optional `APPLAUNCH_FN_KEY=<evdev code>`,
optional `APPLAUNCH_TERM_FONT=<px>` (terminal font, default 20, 14 on a portrait screen under 480 px wide; Settings > Screen > Terminal text size, when not Auto, wins over it), optional `APPLAUNCH_MULTITASK=0|1`
([Multitasking](#multitasking); not set = on from 640x480).

User settings live in `~/.config/cardputerzero/config.json` (DarkTime screensaver timeout, brightness, volume,
per-app touch choice `touch_<app>`, app switches such as `app_Python`: 0 hides the Python tile; the home grid's
`app_order` and, after a move, `home_order`; `pda_mode`, `ssh_indicator`, `apps_auto_check`, `idle_dim_percent`,
`terminal_font_px`, `screen_diag` and `screen_mm`).

Test hooks of the detection (the render harness and the unit tests use them; leave them unset on a board):
`APPLAUNCH_BACKLIGHT_ROOT` (default `/sys/class/backlight`), `APPLAUNCH_ASOUND_ROOT` (default `/proc/asound`),
`APPLAUNCH_POWER_SUPPLY_ROOT` (default `/sys/class/power_supply`), `APPLAUNCH_AMIXER` and `APPLAUNCH_PACTL` (the
programs, default `amixer` and `pactl`), `APPLAUNCH_BLUETOOTHCTL` and `APPLAUNCH_BTMGMT` (the programs of the Bluetooth
audio facts) and `APPLAUNCH_PROC_ROOT` (the process scan for `pipewire` and `bluealsa`, default `/proc`).

## Known limits

- Legacy apps contain their own copy of the inherited matrix keymap, so punctuation typed inside them may
  differ; the launcher's own pages are not affected.
- The Pi Zero 2 W has no audio output: apps log `aplay: audio open error`; add a USB or I2S sound card for sound (a card
  with a playback volume control then brings the Speaker row, see
  [Sound, battery and brightness per board](#sound-battery-and-brightness-per-board)).
- The Pi boards have no battery gauge or hardware RTC; the clock comes from Network Time. A board with a Linux
  `power_supply` battery (a PC) shows it in the status strip and Settings > Battery; the low-battery warning and
  shutdown still follow the upstream bq27220 gauge service only.
- Entries hidden by the hardware profile (Ethernet, the bq27220 pages) are kept in the source and compiled out
  (`#if`), not deleted; Speaker, Battery and Brightness are not switched there but detected (above).

## Reverting the PWM backlight

Remove the `dtoverlay=pwm,...` and `dtoverlay=waveshare-pwm-backlight` lines from `config.txt` (a backup is
`config.txt.bak-applaunch`) and delete `/etc/udev/rules.d/90-backlight-unblank.rules`. `uninstall.sh` does this (and
the rest) for you.
