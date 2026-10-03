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
| Apps | **Settings > Apps** installs/removes apps from GitHub sources you add (`settings_apps_page.cpp`); LAN Scan is the first one (`projects/LanScan`). See `docs/HOSTING-APPS.md`. |
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
`*.bak-applaunch`).

## Files in this folder

| File | Purpose |
| --- | --- |
| `build.sh` / `install.sh` | Build the bundle (host) / install it (Pi). |
| `APPLaunch.service` | systemd **user** service: display, touch axes, keyboard device, working directory. |
| `config.txt.snippet` | Panel part of `config.txt` the port was tested with. |
| `waveshare-pwm-backlight.dts` | Replaces the on/off GPIO18 backlight by a PWM one (Settings > Screen > Brightness). |
| `90-backlight-unblank.rules` | The PWM backlight starts powered down: unblank it when it appears. |
| `91-applaunch-vkbd.rules` | `uinput` permission and the `/dev/input/applaunch-vkbd` name used for stock apps. |
| `50-networkmanager-netdev.rules`, `51-launcher-time-power.rules` | PolicyKit: Wi-Fi scan/connect, Network Time / clock, reboot / shutdown from a background session. |
| `launcher-ntp-default.service` | Network Time is on at every boot (a manually set time lasts for the session). |
| `vfb/` | Framebuffer shim (`libapplaunch_vfb.so`) and a test app (`fbtest.c`). |

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
`config.txt.bak-applaunch`) and delete `/etc/udev/rules.d/90-backlight-unblank.rules`.
