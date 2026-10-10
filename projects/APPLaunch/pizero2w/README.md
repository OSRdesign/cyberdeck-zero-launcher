# CardputerZero launcher on a Raspberry Pi Zero 2W (Waveshare 2.8" DPI LCD cyberdeck)

A port of the CardputerZero launcher to a Raspberry Pi Zero 2W with the Waveshare 2.8" DPI touch LCD
(480x640 portrait, used as 640x480 landscape) and a Bluetooth keyboard. It is selected at run time with
`APPLAUNCH_DISPLAY=dpi-scaled` and at build time with `APPLAUNCH_HW=pizero2w`; the stock CardputerZero
behaviour is unchanged when neither is set.

## What the port adds

| Area | What it does |
| --- | --- |
| Display | Two LVGL displays share `/dev/fb0`: a **native 640x480** one (home grid, terminal, Calculator) and the stock **320x170** one drawn 2x into a window at the top (`cp0_lvgl_dpi_scaled.c`, `cp0_display.h`). |
| Home | Touch-friendly app grid with clock, Wi-Fi bars and Bluetooth state (`native_ui.cpp`). |
| Terminal | CLI, Python and SSH use a full-screen terminal with touch scrolling (`ui_app_st*.cpp`). |
| Calculator | Native touch + keyboard calculator replaces the external Cardputer program. |
| Stock apps | Apps from the Store run unmodified, scaled 2x: an `LD_PRELOAD` shim (`vfb/`) gives them a virtual 320x170 framebuffer and a virtual keyboard (`native_vkbd.cpp`, uinput) that carries both the physical keys and the on-screen toolbar. |
| Touch | Toolbar (Esc, arrows, Enter) under the scaled window; Settings pages and games are driven by swipe/tap gestures; **Settings > Touch** chooses the behaviour per app. |
| Screensaver | After the DarkTime timeout the backlight dims to 10 % and a big HH:MM clock with the date is shown; any touch or key wakes it (`native_screensaver.cpp`). |
| Settings | Hardware profile (`settings_hw_profile.hpp`) hides what a Pi lacks (speaker, Ethernet, ExtPort, battery, ADB, software update) and adds Shutdown; real PWM brightness; Wi-Fi, Bluetooth, Date & Time work without sudo prompts. |
| Apps | **Settings > Apps** installs/removes apps from GitHub sources you add (`settings_apps_page.cpp`); LAN Scan is the first one (source in the apps repo, `apps/lanscan`). See `docs/HOSTING-APPS.md`. |
| Keyboard | A standard keyboard is supported (`APPLAUNCH_STD_KEYBOARD=1` disables the Cardputer matrix keymap); the Bluetooth keyboard re-attaches after sleep. |

## Install on a Pi

1. **Prepare the Pi**: Raspberry Pi OS Lite 64-bit (Debian 13), Wi-Fi configured, SSH enabled.
   Add the panel configuration from `config.txt.snippet` to `/boot/firmware/config.txt` and reboot
   (`/dev/fb0` must be 640x480 at 32 bpp). Pair the Bluetooth keyboard with `bluetoothctl`.
2. **Build the bundle** on a Linux x86_64 host or WSL: `projects/APPLaunch/pizero2w/build.sh [--with-store]`
   (needs `gcc-aarch64-linux-gnu g++-aarch64-linux-gnu pkg-config libffi-dev libfreetype6-dev python3-venv`).
3. **Copy and install**: copy `pizero2w-bundle.tar.gz` to the Pi, unpack it, then
   `cd bundle && sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"`.
   Reboot if the script says so (PWM backlight, console cursor).

`install.sh` is idempotent. It installs the launcher and assets under `/usr/share/APPLaunch`, the udev
and PolicyKit rules, the user service (with lingering, so it starts at boot), the boot-time Network Time
service, masks the text console on tty1 and adds the PWM backlight overlay (original files are kept as
`*.bak-applaunch`). To take it all off again, see [Uninstall](#uninstall).

It refuses to run (and changes nothing) on a system with glibc older than 2.38: the launcher and the apps are built
for Debian 13 trixie. `sudo ./install.sh --dry-run` prints the detected board and what would be done.

## Boards

`install.sh` detects the board from `/proc/device-tree/model`, the connected DRM connectors (`/sys/class/drm`) and
the framebuffers (`/sys/class/graphics`):

| Board | Detected by | Profile |
| --- | --- | --- |
| `zero2w`: Pi Zero 2 W deck, Waveshare 2.8" DPI | model contains "Zero 2" | none: `APPLaunch.service` alone, exactly as before (a stale generated profile is moved to `board.conf.bak`) |
| `pi3a-luckfox35`: Pi 3 Model A+ with the Luckfox 3.5" ST7796S SPI panel | model "Raspberry Pi 3 Model A Plus", connector `SPI-*` connected with mode 320x480, a 320x480 framebuffer named `panel-mipi-dbi*` | `/etc/applaunch/board.conf` (below); the PWM backlight overlay is not added; a udev rule (`92-applaunch-backlight-gpio.rules`) lets the video group switch `backlight_gpio` |
| anything else | - | none: the detected facts are printed and the install stops (`--force` installs without a profile). Run `tools/board-probe/probe-board.sh` on the board and send the report |

The profile is a systemd `EnvironmentFile` (`EnvironmentFile=-/etc/applaunch/board.conf` in `APPLaunch.service`,
`-` = optional). systemd lets its values override the service's `Environment=` lines, so the deck's values
(`/dev/fb0`, rotation 0, touch swap/invert) stay in the service and a profile replaces the ones it names:

```
APPLAUNCH_BOARD=pi3a-luckfox35       # label, logged at start, shown in Settings > About
APPLAUNCH_FB=/dev/fb1                # framebuffer device (default /dev/fb0); install.sh writes the detected one
APPLAUNCH_LOGICAL=480x320            # landscape canvas the UI draws on (default 640x480)
APPLAUNCH_ROTATE=90                  # 0|90|180|270 clockwise pre-rotation into the buffer (default 0)
APPLAUNCH_COMPAT_SCALE=1             # 1 or 2: scale of the 320x170 stock-app window (default 2)
APPLAUNCH_TOUCH_DEV=auto             # auto = pick by capability, or /dev/input/eventN
APPLAUNCH_TOUCH_ORIENT=buffer        # buffer = device axes follow the framebuffer; legacy = SWAP_XY/INVERT_* env (default legacy)
APPLAUNCH_BACKLIGHT=gpio:/sys/class/backlight/backlight_gpio   # gpio:<dir> on/off | sysfs:<dir> brightness | pwm
```

The `pi3a-luckfox35` profile also sets `LV_LINUX_FBDEV_DEVICE` to the same framebuffer and
`APPLAUNCH_TOUCH_SWAP_XY/INVERT_X/INVERT_Y=0`, so the deck's touch axes from the service do not apply.

- **Force a profile:** `sudo ./install.sh --board pi3a-luckfox35` (or `--board zero2w` for no profile).
  `sudo ./install.sh --board-only` only (re)writes the profile and the backlight rule.
- **Hand edits** are kept: a re-run leaves an edited `board.conf` alone; `--force` replaces it and keeps the old one
  as `board.conf.bak`. Restart the launcher after an edit: `systemctl --user restart APPLaunch.service`.
- **Add a board:** probe it with `tools/board-probe/probe-board.sh`, then in `install.sh` add its detection to the
  `case "$MODEL"` block, its values to `profile_body()` and its name to `--board`; add a fixture under `tests/fixtures/`
  (fake `proc/` and `sys/` trees) and cases to `tests/test-install-board.sh` (run it with `sh`, no Pi needed).

## Uninstall

`uninstall.sh` (in the bundle next to `install.sh`) removes what `install.sh` added. Run it as the deck user over
ssh, not from the launcher's own terminal; it asks for `sudo` only for the system parts and never stores a password.

```
./uninstall.sh --dry-run            # print every action, change nothing
./uninstall.sh                      # summary, one confirmation, then act
./uninstall.sh --yes --purge-config # no prompt; also delete the user's launcher data
```

It stops, disables and deletes `APPLaunch.service` (through systemd, no `pkill`) and `launcher-ntp-default.service`,
removes the binaries, the framebuffer shim, the launcher assets under `/usr/share/APPLaunch` (only the files of the
bundle, listed in `payload/share.manifest`), the udev rules (then `udevadm control --reload`) and the PolicyKit rules,
the board profile `/etc/applaunch/board.conf` (and the directory if empty; a `board.conf.bak` is kept),
unmasks and enables `getty@tty1` if it is masked, and takes the PWM backlight out of the boot configuration: the exact
lines `dtoverlay=waveshare-pwm-backlight` and `dtoverlay=pwm,pin=18,func=2` in `config.txt` (a timestamped
`config.txt.bak-uninstall-<time>` is made first and the removed lines are printed), the `.dtbo`, and the
`vt.global_cursor_default=0` / `consoleblank=0` arguments in `cmdline.txt` when the `*.bak-applaunch` backup shows that
`install.sh` added them. Reboot afterwards.

It keeps by default: `~/.config/cardputerzero`, the Settings > Apps sources and install records
(`~/.local/share/cardputerzero-appstore`), the download cache, every installed app package and its data (`/opt/...`),
the packages `install.sh` pulled in with apt, the user's groups and lingering, and the backup files.

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
| `build.sh` / `install.sh` / `uninstall.sh` | Build the bundle (host) / install it (Pi) / remove it again (Pi). |
| `APPLaunch.service` | systemd **user** service: display, touch axes, keyboard device, working directory, optional board profile (`EnvironmentFile=-/etc/applaunch/board.conf`). |
| `config.txt.snippet` | Panel part of `config.txt` the port was tested with. |
| `waveshare-pwm-backlight.dts` | Replaces the on/off GPIO18 backlight by a PWM one (Settings > Screen > Brightness). |
| `90-backlight-unblank.rules` | The PWM backlight starts powered down: unblank it when it appears. |
| `91-applaunch-vkbd.rules` | `uinput` permission and the `/dev/input/applaunch-vkbd` name used for stock apps. |
| `50-networkmanager-netdev.rules`, `51-launcher-time-power.rules` | PolicyKit: Wi-Fi scan/connect, Network Time / clock, reboot / shutdown from a background session. |
| `launcher-ntp-default.service` | Network Time is on at every boot (a manually set time lasts for the session). |
| `vfb/` | Framebuffer shim (`libapplaunch_vfb.so`) and a test app (`fbtest.c`). |
| `tests/` | `test-install-board.sh`: board detection, profile and uninstall-plan tests on the PC against `fixtures/` (fake `/proc` and `/sys` trees of each board). |

## Settings and environment

`APPLaunch.service` environment: `APPLAUNCH_DISPLAY=dpi-scaled`, `APPLAUNCH_ROTATE=0`,
`LV_LINUX_KEYBOARD_DEVICE=/dev/input/bt-keyboard`, `APPLAUNCH_TOUCH_SWAP_XY/INVERT_X/INVERT_Y` (touch panel
orientation measured on this deck), `APPLAUNCH_STD_KEYBOARD=1`, optional `APPLAUNCH_FN_KEY=<evdev code>`,
optional `APPLAUNCH_TERM_FONT=<px>` (terminal font, default 20).

User settings live in `~/.config/cardputerzero/config.json` (DarkTime screensaver timeout, brightness,
per-app touch choice `touch_<app>`, app switches).

## Known limits

- Stock apps contain their own copy of the Cardputer's matrix keymap, so punctuation typed inside them may
  differ; the launcher's own pages are not affected.
- The Pi Zero 2W has no audio output: apps log `aplay: audio open error`; add a USB or I2S sound card for sound.
- There is no battery gauge or hardware RTC; the clock comes from Network Time.
- Entries hidden by the hardware profile are kept in the source and compiled out (`#if`), not deleted.

## Reverting the PWM backlight

Remove the `dtoverlay=pwm,...` and `dtoverlay=waveshare-pwm-backlight` lines from `config.txt` (a backup is
`config.txt.bak-applaunch`) and delete `/etc/udev/rules.d/90-backlight-unblank.rules`. `uninstall.sh` does this (and
the rest) for you.
