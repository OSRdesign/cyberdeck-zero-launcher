# CyberdeckZero Launcher on Raspberry Pi boards

CyberdeckZero Launcher on Raspberry Pi boards with a touch panel and a keyboard. Supported boards: the **Pi Zero 2 W
deck** (Waveshare 2.8" DPI touch LCD, 480x640 portrait used as 640x480 landscape, Bluetooth keyboard), the **Pi 3A+**
with the Luckfox 3.5" SPI panel and the **HackBerry Pi CM5** (720x720). Since phase x-b of the x86 port (report 028)
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
| Console unit | `cyberdeckzero-console.service` | `cyberdeckzero-console.service` |
| Desktop unit (desktop boards) | `cyberdeckzero-desktop.service` | `cyberdeckzero-desktop.service` |
| PolicyKit rule (desktop boards) | renamed to match the desktop unit | `52-cyberdeckzero-desktop.rules` |
| Menu entry (desktop boards) | `cyberdeckzero.desktop`, "CyberdeckZero Launcher" | `cyberdeckzero.desktop` |
| Release asset | `cyberdeckzero-launcher-<version>-arm64.tar.gz` (the first release after the rename also carries it as `pizero2w-bundle.tar.gz`); PCs: `cyberdeckzero-launcher-<version>-amd64.tar.gz` | `pizero2w-bundle.tar.gz` |

`install.sh` migrates a board installed with the old names: it disables and removes the old user unit (also when
lingering keeps it running), removes the retired unit, rule and menu files, installs the new ones and keeps the old
binary name as a symlink. `uninstall.sh` knows both the old and the new names. Rolling back to v0.6.0:
`systemctl --user disable --now cyberdeckzero.service` before installing v0.6.0.

## What the port adds

| Area | What it does |
| --- | --- |
| Display | Two LVGL displays share `/dev/fb0`: a **native 640x480** one (home grid, terminal, Calculator) and the legacy **320x170** one drawn 2x into a window at the top (`cp0_lvgl_dpi_scaled.c`, `cp0_display.h`). |
| Home | Touch-friendly app grid with clock, Wi-Fi bars and Bluetooth state (`native_ui.cpp`). |
| Terminal | CLI, Python and SSH use a full-screen terminal with touch scrolling (`ui_app_st*.cpp`). |
| Calculator | Native touch + keyboard calculator (the only one: the external 320x170 calculator program of the inherited code is gone). |
| Legacy apps | Fullscreen 320x170 framebuffer apps (from the Store) run unmodified, scaled 2x: an `LD_PRELOAD` shim (`vfb/`) gives them a virtual 320x170 framebuffer and a virtual keyboard (`native_vkbd.cpp`, uinput) that carries both the physical keys and the on-screen toolbar. |
| Touch | Toolbar (Esc, arrows, Enter) under the scaled window; Settings (native, full screen) takes taps and drags directly, and games are driven by swipe/tap gestures; **Settings > Touch** chooses the behaviour per app. |
| Screensaver | After the DarkTime timeout the backlight dims to 10 % and a big HH:MM clock with the date is shown; any touch or key wakes it (`native_screensaver.cpp`). |
| Settings | **Speaker, Battery and Screen > Brightness are detected at run time** each time Settings opens (`cp0_capabilities`, report 030): a row shows only when the board has that hardware, see [Sound, battery and brightness per board](#sound-battery-and-brightness-per-board). The hardware profile (`settings_hw_profile.hpp`) still hides Ethernet and the upstream bq27220 battery pages and adds Shutdown (the upstream device's ExtPort, ADB and software-update pages were removed, report 029); Wi-Fi, Bluetooth, Date & Time work without sudo prompts. **Date & Time > Time zone** picks the zone (region, then city; independent of Network Time) through timedated: the clock follows at once, no restart (when PolicyKit refuses, the sudo password prompt asks first). **Battery** (after Bluetooth) shows only on a board with a Linux `power_supply` battery (a PC's ACPI `BAT0`/`BAT1`, a gauge driver's; read when Settings opens): charge, status, time left or to full, power, voltage, health, device, refreshed every 2 s; the status strip then shows the battery's level and percentage left of the Bluetooth icon (green while charging). The Pi boards have none: no row, nothing in the strip. |
| Responsive shell | One code base for several screens: `cp0_ui_metrics` computes the layout (screen class, density, tokens) from the logical size and the panel size in mm (`APPLAUNCH_PANEL_MM=WxH`, written by `install.sh` for known panels); the home grid, status bar, toolbar and the native Settings (the default on the Pi boards since task 014 P3e; no switch) use it. A fallback on-screen keyboard (`APPLAUNCH_OSK=force|auto|off`) shows only when no physical keyboard is present (a keyboard listed in `APPLAUNCH_KBD_IGNORE` does not count); in a terminal it has an **Alt** key next to Ctrl (one-shot, double tap locks: Alt then Tab is Alt+Tab). Developer notes: `docs/dev/tasks/014-responsive-shell.md`. |
| Apps | **Settings > Apps** installs/removes apps from GitHub sources (`settings_native_apps_page.cpp`). `install.sh` adds the project's apps repo (`OSRdesign/cyberdeck-zero-apps`: LAN Scan, viz1090, Wi-Fi Survey, Mesh Hop, ...) once for a user who has no source yet, so **Settings > Apps > Available** lists its apps out of the box; add your own under Sources. See `docs/HOSTING-APPS.md`. |
| App contract | Every app the launcher starts learns its mode (`compat` / `fullscreen`), window, canvas and scale, the panel's size, rotation, density, framebuffer and touch device, the design tokens and the on-screen keyboard rule (environment, `$XDG_RUNTIME_DIR/applaunch/screen.state` and `input.state`); `X-MinSize=WxH` in a `.desktop` hides an app on a smaller screen. See `docs/APP-CONTRACT.md`; test app `tools/dev/app-contract-probe`. |
| Launcher | **Settings > Launcher** shows or hides the built-in tiles, the **Store** included (it needs a keyboard, so a touch-only board can hide it; shown by default, `APPLAUNCH_LAUNCHER_STORE_TOGGLE` in `settings_hw_profile.hpp`). Hiding the Store tile does not affect Settings > Apps. |
| Keyboard | A standard keyboard is supported (`APPLAUNCH_STD_KEYBOARD=1` disables the inherited matrix keymap); the Bluetooth keyboard re-attaches after sleep. Volume, mute and brightness keys work from any device that has them, a keyboard or not (a tablet's buttons, the ACPI video bus, a headset's buttons: media-key devices, `docs/APP-CONTRACT.md` section 14), on the launcher's pages and inside apps; such a device never counts as a keyboard. Pointer navigation (Settings > System, off by default) turns a mouse or trackpoint into arrows, Enter and the options key; on the HackBerry a quick tap on the pad does not select, press the pad's button (`APPLAUNCH_POINTER_MIN_CLICK_MS`, see `docs/APP-CONTRACT.md` section 10b). |
| Multitasking | Since v0.8.0: up to 4 apps stay open; three fingers held 1 s, a 0.5 s long-press on the status strip, Alt+Tab, Super tapped twice (within 0.45 s) or held 0.6 s open "Open apps", Super tapped once goes home; apps opt in with `X-Background=run\|freeze` (`docs/APP-CONTRACT.md` section 13). On by default on canvases of 640x480 and more; see [Multitasking](#multitasking). Code: `main/ui/multitask/`. |

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
kept as `*.bak-applaunch`). On a board that boots to a desktop it installs the desktop variant instead (see
[Desktop boards](#desktop-boards)). The end of the install names the session and why (`session: appliance (...)` or
`session: desktop (...)`). **If you install or remove a desktop later, run `install.sh` again**: it switches between
the two variants ([Switching between appliance and desktop](#switching-between-appliance-and-desktop)). To take it all
off again, see [Uninstall](#uninstall).

It refuses to run (and changes nothing) on a system with glibc older than 2.38: the launcher and the apps are built
for Debian 13 trixie. It also refuses a bundle of the other architecture (it reads the launcher's ELF header: arm64 on
a Pi, amd64 on a PC). `sudo ./install.sh --dry-run` prints the detected board and what would be done.

**Default app source.** `install.sh` runs `default-sources.sh` as the launcher user (the user's own file, no root, no
password). When that user has no app source yet (no `~/.local/share/cardputerzero-appstore/registries.json`, or one
with only the Store's built-in CardputerZero Hub), it adds `OSRdesign/cyberdeck-zero-apps` in the Store's own format
and syncs it once, so Settings > Apps > Available lists its apps after the install. A list with sources of the user's
own is never changed. The decision is taken once per user (marker file `applaunch-default-source` in that folder):
a source you remove or switch off in Settings > Apps > Sources stays removed after the next `install.sh`. Offline
during the install, the source is added but not synced: Settings > Apps > Sources > Sync my sources (key `S`) fetches
it. `--no-default-source` skips all of this. By hand, as the user (never with sudo):
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
- **NetworkManager** for Settings > Wi-Fi: install it yourself (see below). install.sh does not: it takes the network
  over from netplan / systemd-networkd. The end of the install prints the recipe when `nmcli` is missing.

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
status strip and in Settings > Battery. Settings > Apps reads `registry-amd64.json` of the apps repository (five apps since v0.8.2). The Store tile is hidden (`APPLAUNCH_HIDE_TILES=app_Store`; the Store's hub has arm64 packages only); Settings >
Launcher shows it again if wanted.

## Multitasking

New in v0.8.0 (task 015, `docs/dev/reports/026-multitasking-study.md`). Nothing to install or configure: it comes
with the bundle and is **on by default** when the launcher's logical canvas is 640x480 or more (the deck, the Pi 3A+
DSI at 800x480, the HackBerry CM5 at 720x720, the Pi 5 HyperPixel at 800x480). On smaller canvases (the Pi 3A+ SPI
panel at 480x320, 320x170) it is off and the launcher behaves as before.

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
| `zero2w`: Pi Zero 2 W deck, Waveshare 2.8" DPI | model contains "Zero 2" | none: `cyberdeckzero.service` alone, exactly as before (a stale generated profile is moved to `board.conf.bak`) |
| `pi3a-luckfox35`: Pi 3 Model A+ with the Luckfox 3.5" ST7796S SPI panel | model "Raspberry Pi 3 Model A Plus", connector `SPI-*` connected with mode 320x480, a 320x480 framebuffer named `panel-mipi-dbi*` | `/etc/applaunch/board.conf` (below); the PWM backlight overlay is not added; a udev rule (`92-applaunch-backlight-gpio.rules`) lets the video group switch `backlight_gpio` |
| `pi3a-wsdsi43`: Pi 3 Model A+ with the Waveshare 4.3" DSI panel (800x480; overlay `vc4-kms-dsi-waveshare-800x480`, see below) | model "Raspberry Pi 3 Model A Plus", connector `DSI-*` connected with mode 800x480, an 800x480 framebuffer named `vc4drmfb` (config.txt and the overlay's name are not looked at) | `board.conf`: `/dev/fb0`, `APPLAUNCH_LOGICAL=800x480`, `APPLAUNCH_ROTATE=0`, `APPLAUNCH_COMPAT_SCALE=2`, `APPLAUNCH_PANEL_MM=94x56` (to be confirmed with a ruler), touch `auto` / `buffer`, `APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045` (the overlay's backlight regulator, brightness levels; the only backlight device if that one is absent, none if there is none) and a udev rule (`92-applaunch-backlight-sysfs.rules`) that lets the video group set it; config.txt is not edited (no PWM overlay), cmdline.txt gets `vt.global_cursor_default=0 consoleblank=0` only (no `fbcon=` argument) |
| `pi5-hyperpixel4`: Pi 5 with the Pimoroni HyperPixel 4" DPI panel | model "Raspberry Pi 5", connector `DPI-*` connected with mode 480x800, a 480x800 framebuffer named `drm-rp1-dpi*` | `board.conf`: that framebuffer, `APPLAUNCH_LOGICAL=800x480`, `APPLAUNCH_ROTATE=90`, touch `auto` / `buffer`; config.txt only with `--pwm-backlight` (below) |
| `hackberry-cm5`: HackBerry Pi CM5 (720x720 DPI panel) | model "Compute Module 5", connector `DPI-*` connected with mode 720x720, a 720x720 framebuffer named `drm-rp1-dpi*` | `board.conf`: that framebuffer, `APPLAUNCH_LOGICAL=720x720`, `APPLAUNCH_ROTATE=0`, touch `auto` / `buffer` |
| `pc-<model>`: a PC (amd64 bundle), e.g. `pc-fzm1-2` (Panasonic FZ-M1 mk2) | x86_64 with no device tree, a framebuffer (the DRM driver's, `*drmfb`, first); `<model>` is the DMI product name in lower case | `board.conf`: `APPLAUNCH_DISPLAY=dpi-scaled`, that framebuffer, `APPLAUNCH_LOGICAL` = its size, `APPLAUNCH_ROTATE=0`, `APPLAUNCH_PANEL_MM` from the connected connector's EDID (eDP first; the detailed timing's mm, else the cm of the basic block), else a table of known models (FZM1-2: 150x93), else none (default density, large controls); touch `auto` / `buffer`; `APPLAUNCH_BACKLIGHT=sysfs:` the first backlight (`intel_backlight` first) and its udev rule; on a tablet (SMBIOS chassis 30 or 32, or a known tablet) `APPLAUNCH_KBD_IGNORE=bus:0011`; `APPLAUNCH_HIDE_TILES=app_Store`. No boot configuration (GRUB, kernel command line), no dtc |
| anything else | - | none: the detected facts are printed and the install stops (`--force` installs without a profile). Run `tools/board-probe/probe-board.sh` on the board and send the report. A Pi 3A+ with both its panels connected is not guessed: choose with `--board`. With `--force`, the deck's PWM backlight overlay is added only on a model that is not one of the boards above (a Pi 3A+, Pi 5 or CM5 whose panel was not recognised keeps its config.txt) |

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

The Pi 5 and HackBerry values are the ones `tools/boards/board.py test-start` used on those boards. Their panels' size
in mm is not measured yet (task 014), so their profiles have no `APPLAUNCH_PANEL_MM` and the layout uses its default
density. These two boards normally run the Raspberry Pi OS desktop: they then get the desktop variant
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
APPLAUNCH_HIDE_TILES=app_Store       # home tiles hidden by default (Settings > Launcher can show them)
```

`APPLAUNCH_BACKLIGHT=sysfs:<dir>` (since x-b; before, it fell back to `/sys/class/backlight/backlight`) writes
`<dir>/brightness` in levels from 0 to `<dir>/max_brightness`: the Settings slider (10 % steps, never under 10 %),
the hotkeys, the screensaver's dim and the sleep / wake all use it. Without the line (the deck, the Pi 5, the
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

The `pi3a-luckfox35` and `pi3a-wsdsi43` profiles also set `LV_LINUX_FBDEV_DEVICE` to the same framebuffer and
`APPLAUNCH_TOUCH_SWAP_XY/INVERT_X/INVERT_Y=0`, so the deck's touch axes from the service do not apply.

- **Force a profile:** `sudo ./install.sh --board pi3a-luckfox35` (or `pi3a-wsdsi43`, `pi5-hyperpixel4`,
  `hackberry-cm5`; the panel's framebuffer must be there), or `--board zero2w` for no profile.
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
lines `dtoverlay=waveshare-pwm-backlight` and `dtoverlay=pwm,pin=18,func=2` in `config.txt` (on the Pi 5,
`dtoverlay=hyperpixel4-pwm-backlight`; a timestamped
`config.txt.bak-uninstall-<time>` is made first and the removed lines are printed), the `.dtbo`, and the
`vt.global_cursor_default=0` / `consoleblank=0` arguments in `cmdline.txt` when the `*.bak-applaunch` backup shows that
`install.sh` added them. Reboot afterwards.

It keeps by default: `~/.config/cardputerzero`, the Settings > Apps sources and install records
(`~/.local/share/cardputerzero-appstore`, with the default source and its marker), the download cache, every installed
app package and its data (`/opt/...`), the packages `install.sh` pulled in with apt, the user's groups and lingering,
and the backup files. After `--purge-config` a new install offers the default app source again.

| Option | Effect |
| --- | --- |
| `--dry-run` | Print every action and change nothing. |
| `--yes` | No confirmation prompt (without it the script asks once, after printing what it removes and keeps). |
| `--purge-config` | Also delete `~/.config/cardputerzero`, `~/.local/share/cardputerzero-appstore` and `~/.cache/cardputerzero-appstore`. |
| `--remove-apps` | Also `apt-get remove` the app packages installed from Settings > Apps / the Store, taken from the launcher's `installed.json` and listed first. No record: skipped. With `--purge-config` the packages are purged. |
| `--restore-config` | Restore `config.txt` from `config.txt.bak-before-pwm` instead of removing the two lines (loses any other later edit; the current file is backed up first). |
| `--no-boot-config` | Leave `config.txt`, `cmdline.txt` and the overlay alone. |
| `--disable-linger` | Also run `loginctl disable-linger` (`install.sh` enabled it; it may have been on before). |
| `--user NAME`, `--payload DIR` | The user that ran `install.sh`; where the bundle's `payload/` is, if it is not next to the script. |

## Files in this folder

| File | Purpose |
| --- | --- |
| `build.sh` / `install.sh` / `uninstall.sh` | Build the bundle (host; `--arch arm64` by default, `--arch amd64` natively for PCs) / install it (Pi or PC) / remove it again. |
| `default-sources.sh` | Adds the apps repo to Settings > Apps once for a user with no source yet (run by `install.sh` as that user; see "Default app source"). |
| `cyberdeckzero.service` | systemd **user** service: display, touch axes, keyboard device, working directory, optional board profile (`EnvironmentFile=-/etc/applaunch/board.conf`). |
| `cyberdeckzero-console`, `cyberdeckzero-console.service` | The text console helper (installed in `/usr/share/APPLaunch/bin`: `unbind`, `bind`, `status`, `restore-desktop UNIT`) and the appliance boards' boot-time unit that frees the panel from the console ([Text console](#text-console)). |
| `cyberdeckzero-desktop.service`, `cyberdeckzero.desktop`, `52-cyberdeckzero-desktop.rules` | Desktop boards: the system unit that runs the launcher in place of the desktop, the applications menu entry, and the PolicyKit rule that lets the launcher's user start and stop that unit; install.sh fills in the user and the display manager ([Desktop boards](#desktop-boards)). |
| `config.txt.snippet` | Panel part of `config.txt` the port was tested with. |
| `waveshare-pwm-backlight.dts` | Replaces the on/off GPIO18 backlight by a PWM one (Settings > Screen > Brightness). |
| `hyperpixel4-pwm-backlight.dts` | Pi 5 + HyperPixel 4, only with `install.sh --pwm-backlight`: its on/off GPIO19 backlight becomes a PWM one with levels (RP1 PWM0). |
| `90-backlight-unblank.rules` | The PWM backlight starts powered down: unblank it when it appears. |
| `91-applaunch-vkbd.rules` | `uinput` permission and the `/dev/input/applaunch-vkbd` name used for legacy apps. |
| `93-applaunch-rfkill.rules` | `/dev/rfkill` writable by the `netdev` group, so Settings > Bluetooth can run `rfkill unblock bluetooth` as the launcher user. |
| `50-networkmanager-netdev.rules`, `51-launcher-time-power.rules` | PolicyKit: Wi-Fi scan/connect, Network Time / clock, reboot / shutdown from a background session. |
| `launcher-ntp-default.service` | Network Time is on at every boot (a manually set time lasts for the session). |
| `vfb/` | Framebuffer shim (`libapplaunch_vfb.so`) and a test app (`fbtest.c`). |
| `tests/` | `test-install-board.sh`: board and desktop detection, profile, desktop unit and uninstall-plan tests on the PC against `fixtures/` (fake `/proc` and `/sys` trees of each board, desktop markers added by the test), `cyberdeckzero-console` against a fake `/sys/class/vtconsole` and a fake `systemctl` (the shutdown guard), and the default app source (`default-sources.sh` with a temp state folder and a fake Store backend). |

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
| Pi 5 + HyperPixel 4 | no row | **On / Off** (detected: `gpio-backlight`, max 1; it offered 10 levels that all wrote 1 before); levels after `install.sh --pwm-backlight` ([Boards](#boards)) | no row (vc4-hdmi only, no `pactl`) |
| HackBerry CM5 | no row | **no row** (no backlight device; the row did nothing before) | a row while the case speaker is connected over Bluetooth (its sink); otherwise none |
| FZ-M1 tablet | Battery row | levels (`intel_backlight`, 937) | **Speaker row** (HDA Intel PCH, Master; with or without a sound server) |

## Settings and environment

`cyberdeckzero.service` environment: `APPLAUNCH_DISPLAY=dpi-scaled`, `APPLAUNCH_ROTATE=0`,
`LV_LINUX_KEYBOARD_DEVICE=/dev/input/bt-keyboard`, `APPLAUNCH_TOUCH_SWAP_XY/INVERT_X/INVERT_Y` (touch panel
orientation measured on this deck), `APPLAUNCH_STD_KEYBOARD=1`, optional `APPLAUNCH_FN_KEY=<evdev code>`,
optional `APPLAUNCH_TERM_FONT=<px>` (terminal font, default 20), optional `APPLAUNCH_MULTITASK=0|1`
([Multitasking](#multitasking); not set = on from 640x480).

User settings live in `~/.config/cardputerzero/config.json` (DarkTime screensaver timeout, brightness, volume,
per-app touch choice `touch_<app>`, app switches such as `app_Store`: 0 hides the Store tile).

Test hooks of the detection (the render harness and the unit tests use them; leave them unset on a board):
`APPLAUNCH_BACKLIGHT_ROOT` (default `/sys/class/backlight`), `APPLAUNCH_ASOUND_ROOT` (default `/proc/asound`),
`APPLAUNCH_POWER_SUPPLY_ROOT` (default `/sys/class/power_supply`), `APPLAUNCH_AMIXER` and `APPLAUNCH_PACTL` (the
programs, default `amixer` and `pactl`).

## Known limits

- Legacy apps contain their own copy of the inherited matrix keymap, so punctuation typed inside them may
  differ; the launcher's own pages are not affected.
- The Pi Zero 2W has no audio output: apps log `aplay: audio open error`; add a USB or I2S sound card for sound (a card
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
