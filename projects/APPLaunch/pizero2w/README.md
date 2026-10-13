# CyberdeckZero Launcher on Raspberry Pi boards

CyberdeckZero Launcher on Raspberry Pi boards with a touch panel and a keyboard. Supported boards: the **Pi Zero 2 W
deck** (Waveshare 2.8" DPI touch LCD, 480x640 portrait used as 640x480 landscape, Bluetooth keyboard), the **Pi 3A+**
with the Luckfox 3.5" SPI panel and the **HackBerry Pi CM5** (720x720). The port is selected at run time with
`APPLAUNCH_DISPLAY=dpi-scaled` and at build time with `APPLAUNCH_HW=pizero2w`; the stock CardputerZero behaviour of the
upstream code is unchanged when neither is set. See [`NAMING.md`](../../../NAMING.md) for the product and technical names.

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
| Release asset | `cyberdeckzero-launcher-<version>-arm64.tar.gz` (the first release after the rename also carries it as `pizero2w-bundle.tar.gz`) | `pizero2w-bundle.tar.gz` |

`install.sh` migrates a board installed with the old names: it disables and removes the old user unit (also when
lingering keeps it running), removes the retired unit, rule and menu files, installs the new ones and keeps the old
binary name as a symlink. `uninstall.sh` knows both the old and the new names. Rolling back to v0.6.0:
`systemctl --user disable --now cyberdeckzero.service` before installing v0.6.0.

## What the port adds

| Area | What it does |
| --- | --- |
| Display | Two LVGL displays share `/dev/fb0`: a **native 640x480** one (home grid, terminal, Calculator) and the stock **320x170** one drawn 2x into a window at the top (`cp0_lvgl_dpi_scaled.c`, `cp0_display.h`). |
| Home | Touch-friendly app grid with clock, Wi-Fi bars and Bluetooth state (`native_ui.cpp`). |
| Terminal | CLI, Python and SSH use a full-screen terminal with touch scrolling (`ui_app_st*.cpp`). |
| Calculator | Native touch + keyboard calculator replaces the external Cardputer program. |
| Stock apps | Apps from the Store run unmodified, scaled 2x: an `LD_PRELOAD` shim (`vfb/`) gives them a virtual 320x170 framebuffer and a virtual keyboard (`native_vkbd.cpp`, uinput) that carries both the physical keys and the on-screen toolbar. |
| Touch | Toolbar (Esc, arrows, Enter) under the scaled window; Settings (native, full screen) takes taps and drags directly, and games are driven by swipe/tap gestures; **Settings > Touch** chooses the behaviour per app. |
| Screensaver | After the DarkTime timeout the backlight dims to 10 % and a big HH:MM clock with the date is shown; any touch or key wakes it (`native_screensaver.cpp`). |
| Settings | Hardware profile (`settings_hw_profile.hpp`) hides what a Pi lacks (speaker, Ethernet, ExtPort, battery, ADB, software update) and adds Shutdown; real PWM brightness; Wi-Fi, Bluetooth, Date & Time work without sudo prompts. |
| Responsive shell | One code base for several screens: `cp0_ui_metrics` computes the layout (screen class, density, tokens) from the logical size and the panel size in mm (`APPLAUNCH_PANEL_MM=WxH`, written by `install.sh` for known panels); the home grid, status bar, toolbar and the native Settings (the default on the Pi boards since task 014 P3e; no switch) use it. A fallback on-screen keyboard (`APPLAUNCH_OSK=force|auto|off`) shows only when no physical keyboard is present. Developer notes: `docs/dev/tasks/014-responsive-shell.md`. |
| Apps | **Settings > Apps** installs/removes apps from GitHub sources (`settings_native_apps_page.cpp`). `install.sh` adds the project's apps repo (`OSRdesign/cyberdeck-zero-apps`: LAN Scan, viz1090, Wi-Fi Survey, Mesh Hop, ...) once for a user who has no source yet, so **Settings > Apps > Available** lists its apps out of the box; add your own under Sources. See `docs/HOSTING-APPS.md`. |
| App contract | Every app the launcher starts learns its mode (`compat` / `fullscreen`), window, canvas and scale, the panel's size, rotation, density, framebuffer and touch device, the design tokens and the on-screen keyboard rule (environment, `$XDG_RUNTIME_DIR/applaunch/screen.state` and `input.state`); `X-MinSize=WxH` in a `.desktop` hides an app on a smaller screen. See `docs/APP-CONTRACT.md`; test app `tools/dev/app-contract-probe`. |
| Launcher | **Settings > Launcher** shows or hides the built-in tiles, the **Store** included (it needs a keyboard, so a touch-only board can hide it; shown by default, `APPLAUNCH_LAUNCHER_STORE_TOGGLE` in `settings_hw_profile.hpp`). Hiding the Store tile does not affect Settings > Apps. |
| Keyboard | A standard keyboard is supported (`APPLAUNCH_STD_KEYBOARD=1` disables the Cardputer matrix keymap); the Bluetooth keyboard re-attaches after sleep. |

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
[Desktop boards](#desktop-boards)). To take it all off again, see [Uninstall](#uninstall).

It refuses to run (and changes nothing) on a system with glibc older than 2.38: the launcher and the apps are built
for Debian 13 trixie. `sudo ./install.sh --dry-run` prints the detected board and what would be done.

**Default app source.** `install.sh` runs `default-sources.sh` as the launcher user (the user's own file, no root, no
password). When that user has no app source yet (no `~/.local/share/cardputerzero-appstore/registries.json`, or one
with only the Store's built-in CardputerZero Hub), it adds `OSRdesign/cyberdeck-zero-apps` in the Store's own format
and syncs it once, so Settings > Apps > Available lists its apps after the install. A list with sources of the user's
own is never changed. The decision is taken once per user (marker file `applaunch-default-source` in that folder):
a source you remove or switch off in Settings > Apps > Sources stays removed after the next `install.sh`. Offline
during the install, the source is added but not synced: Settings > Apps > Sources > Sync my sources (key `S`) fetches
it. `--no-default-source` skips all of this. By hand, as the user (never with sudo):
`sh default-sources.sh --dry-run` shows what it would do, `sh default-sources.sh` does it.

## Boards

`install.sh` detects the board from `/proc/device-tree/model`, the connected DRM connectors (`/sys/class/drm`) and
the framebuffers (`/sys/class/graphics`):

| Board | Detected by | Profile |
| --- | --- | --- |
| `zero2w`: Pi Zero 2 W deck, Waveshare 2.8" DPI | model contains "Zero 2" | none: `cyberdeckzero.service` alone, exactly as before (a stale generated profile is moved to `board.conf.bak`) |
| `pi3a-luckfox35`: Pi 3 Model A+ with the Luckfox 3.5" ST7796S SPI panel | model "Raspberry Pi 3 Model A Plus", connector `SPI-*` connected with mode 320x480, a 320x480 framebuffer named `panel-mipi-dbi*` | `/etc/applaunch/board.conf` (below); the PWM backlight overlay is not added; a udev rule (`92-applaunch-backlight-gpio.rules`) lets the video group switch `backlight_gpio` |
| `pi3a-wsdsi43`: Pi 3 Model A+ with the Waveshare 4.3" DSI panel (800x480; overlay `vc4-kms-dsi-waveshare-800x480`, see below) | model "Raspberry Pi 3 Model A Plus", connector `DSI-*` connected with mode 800x480, an 800x480 framebuffer named `vc4drmfb` (config.txt and the overlay's name are not looked at) | `board.conf`: `/dev/fb0`, `APPLAUNCH_LOGICAL=800x480`, `APPLAUNCH_ROTATE=0`, `APPLAUNCH_COMPAT_SCALE=2`, `APPLAUNCH_PANEL_MM=94x56` (to be confirmed with a ruler), touch `auto` / `buffer`, `APPLAUNCH_BACKLIGHT=sysfs:/sys/class/backlight/10-0045` (the overlay's backlight regulator; the only backlight device if that one is absent, none if there is none); config.txt is not edited (no PWM overlay), cmdline.txt gets `vt.global_cursor_default=0 consoleblank=0` only (no `fbcon=` argument) |
| `pi5-hyperpixel4`: Pi 5 with the Pimoroni HyperPixel 4" DPI panel | model "Raspberry Pi 5", connector `DPI-*` connected with mode 480x800, a 480x800 framebuffer named `drm-rp1-dpi*` | `board.conf`: that framebuffer, `APPLAUNCH_LOGICAL=800x480`, `APPLAUNCH_ROTATE=90`, touch `auto` / `buffer` |
| `hackberry-cm5`: HackBerry Pi CM5 (720x720 DPI panel) | model "Compute Module 5", connector `DPI-*` connected with mode 720x720, a 720x720 framebuffer named `drm-rp1-dpi*` | `board.conf`: that framebuffer, `APPLAUNCH_LOGICAL=720x720`, `APPLAUNCH_ROTATE=0`, touch `auto` / `buffer` |
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

The profile is a systemd `EnvironmentFile` (`EnvironmentFile=-/etc/applaunch/board.conf` in `cyberdeckzero.service`,
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

A board that boots to the Raspberry Pi OS desktop (the HackBerry CM5, the Pi 5: lightdm with labwc) keeps its desktop.
`install.sh` sees it (a display manager enabled for boot: `/etc/systemd/system/display-manager.service`, or one of the
desktop processes `tools/board-probe/probe-board.sh` looks for) and installs the desktop variant
(`--dry-run` prints `session: desktop (...)` and the unit it would write):

- no user service, no lingering, `getty@tty1` left as it is, no change to `config.txt` or `cmdline.txt`; the board
  profile, udev and PolicyKit rules, Network Time service and default app source as on the other boards;
- `/etc/systemd/system/cyberdeckzero-desktop.service`, written for the launcher's user and the display manager found
  (`lightdm.service`). It has no `[Install]` section: it cannot be enabled, the launcher never starts at boot;
- `/usr/share/applications/cyberdeckzero.desktop`: "CyberdeckZero Launcher" in the applications menu (Accessories), which
  runs `systemctl --no-block start cyberdeckzero-desktop.service`;
- `/etc/polkit-1/rules.d/52-cyberdeckzero-desktop.rules`: that user, and only that user, may start, stop or restart that
  one unit without a password. Nothing is opened up for lightdm: the unit stops and starts it as root.

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

**Uninstall:** `./uninstall.sh` (from ssh) stops the unit if it runs (the desktop comes back), removes it, the menu
entry and the PolicyKit rule (see [Uninstall](#uninstall)). The deck profile (`zero2w`) has no desktop variant: on a
deck image with a desktop, `install.sh` stops and changes nothing.

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
lines `dtoverlay=waveshare-pwm-backlight` and `dtoverlay=pwm,pin=18,func=2` in `config.txt` (a timestamped
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
| `build.sh` / `install.sh` / `uninstall.sh` | Build the bundle (host) / install it (Pi) / remove it again (Pi). |
| `default-sources.sh` | Adds the apps repo to Settings > Apps once for a user with no source yet (run by `install.sh` as that user; see "Default app source"). |
| `cyberdeckzero.service` | systemd **user** service: display, touch axes, keyboard device, working directory, optional board profile (`EnvironmentFile=-/etc/applaunch/board.conf`). |
| `cyberdeckzero-console`, `cyberdeckzero-console.service` | The text console helper (installed in `/usr/share/APPLaunch/bin`: `unbind`, `bind`, `status`, `restore-desktop UNIT`) and the appliance boards' boot-time unit that frees the panel from the console ([Text console](#text-console)). |
| `cyberdeckzero-desktop.service`, `cyberdeckzero.desktop`, `52-cyberdeckzero-desktop.rules` | Desktop boards: the system unit that runs the launcher in place of the desktop, the applications menu entry, and the PolicyKit rule that lets the launcher's user start and stop that unit; install.sh fills in the user and the display manager ([Desktop boards](#desktop-boards)). |
| `config.txt.snippet` | Panel part of `config.txt` the port was tested with. |
| `waveshare-pwm-backlight.dts` | Replaces the on/off GPIO18 backlight by a PWM one (Settings > Screen > Brightness). |
| `90-backlight-unblank.rules` | The PWM backlight starts powered down: unblank it when it appears. |
| `91-applaunch-vkbd.rules` | `uinput` permission and the `/dev/input/applaunch-vkbd` name used for stock apps. |
| `93-applaunch-rfkill.rules` | `/dev/rfkill` writable by the `netdev` group, so Settings > Bluetooth can run `rfkill unblock bluetooth` as the launcher user. |
| `50-networkmanager-netdev.rules`, `51-launcher-time-power.rules` | PolicyKit: Wi-Fi scan/connect, Network Time / clock, reboot / shutdown from a background session. |
| `launcher-ntp-default.service` | Network Time is on at every boot (a manually set time lasts for the session). |
| `vfb/` | Framebuffer shim (`libapplaunch_vfb.so`) and a test app (`fbtest.c`). |
| `tests/` | `test-install-board.sh`: board and desktop detection, profile, desktop unit and uninstall-plan tests on the PC against `fixtures/` (fake `/proc` and `/sys` trees of each board, desktop markers added by the test), `cyberdeckzero-console` against a fake `/sys/class/vtconsole` and a fake `systemctl` (the shutdown guard), and the default app source (`default-sources.sh` with a temp state folder and a fake Store backend). |

## Settings and environment

`cyberdeckzero.service` environment: `APPLAUNCH_DISPLAY=dpi-scaled`, `APPLAUNCH_ROTATE=0`,
`LV_LINUX_KEYBOARD_DEVICE=/dev/input/bt-keyboard`, `APPLAUNCH_TOUCH_SWAP_XY/INVERT_X/INVERT_Y` (touch panel
orientation measured on this deck), `APPLAUNCH_STD_KEYBOARD=1`, optional `APPLAUNCH_FN_KEY=<evdev code>`,
optional `APPLAUNCH_TERM_FONT=<px>` (terminal font, default 20).

User settings live in `~/.config/cardputerzero/config.json` (DarkTime screensaver timeout, brightness,
per-app touch choice `touch_<app>`, app switches such as `app_Store`: 0 hides the Store tile).

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
