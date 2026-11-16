**New since v0.10.3** (pull requests #185: the `screen off|on` request for apps, and #181: the installer asks for the screen size and has a generic profile)

- **Apps can switch the screen off and on.** A new `launcher.sock` request, `screen off` / `screen on`, switches the
  backlight fully off and brings it back without saving anything (`brightness set` snaps to the launcher's steps and
  is saved, so it never reaches 0). Replies: `ok`, `err busy` (another app holds the screen off) or `err absent` (no
  backlight, or it cannot be switched). `board.state` lists `screen` in `requests=` when the backlight is `levels` or
  `onoff`, not on boards without one (the HackBerry). cp0_lvgl apps use `cp0_app_backlight(int on)` (0 ok, 1 refused,
  -1 no service). Details: APP-CONTRACT.md, the `screen` row of the requests table.
- **The launcher never leaves the screen black.** It restores the backlight by itself when the app that switched it
  off sends `screen on`, is hidden or replaced in front, quits, crashes or is killed (the launcher checks the process
  about once a second), and when the launcher stops. The saved brightness is never touched. While the screen is off
  the brightness keys and idle dim do nothing, `brightness set` answers `err busy`, and `brightness get` reports the
  level that comes back. Touch and keys still reach the app, which ignores them itself.
- **Known limits:** a launcher crash while the screen is off leaves an on/off (GPIO) backlight dark until the launcher
  starts again; levels backlights are restored at startup. The Waveshare 4.3" DSI panel on the Pi 3A+ cannot reach
  zero backlight and only dims, so an app should paint the screen black as well. First user: Vinyl's "Knock-knock"
  setting (Vinyl 0.6.0, apps repository): the screen went fully dark on the Pi 5 boards, the Pi 4B, the Pi 3A+ with
  the 5" panel, zero8 and the deck.
- **The installer asks for the screen size.** The 4.3", 5" and 7" DSI panels (and the 640x480 panels of 2.8" to 5")
  look the same to software, so a board could get the wrong physical size (`APPLAUNCH_PANEL_MM`). New options:
  `--screen-size INCHES` (diagonal, 1.5 to 15) and `--panel-mm WxH` (exact size). A first install from a terminal
  asks "What is the screen's diagonal in inches?" (Enter keeps the profile's size). The choice is kept in
  `install.conf`, and Settings > Updates passes it back without asking. Boards that do not use the options stay
  exactly as before. Not on the deck (Zero 2 W) or unknown boards.
- **A screen without a profile.** `--board generic` (or `--force` on an unknown board with exactly one panel) writes a
  board profile from what is detected: framebuffer, canvas, rotation, touch, backlight, and the size from
  `--screen-size`. The installer prints what it assumed; please send the probe report. Not yet tried on real 2.8",
  3.5", 4" or 5" panels. README of the installer: "Screen size" and "My screen is not listed".
- **Updating:** the same as for v0.10.3. Boards on v0.8.13 or later with a release build update from Settings >
  Updates. Boards on older builds or dev builds need one install by hand (`install.sh` from the bundle).

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.4-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.4-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels (a Pi 5 with the official 7-inch DSI
panel is detected by itself). For the portrait PDA
profile on the Pi 3A+, add `--board pi3a-wsdsi43-portrait`. On a Zero 2 W with the Waveshare 3.5" SPI panel, add
`--board pi3a-luckfox35` (it is detected as the deck otherwise); the install sets up the PWM backlight. A board whose
DSI panel is not the size of its profile (for example a 5" panel on a Pi 3A+): add `--screen-size 5`. A screen with no
profile: `--board generic`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
our fork [cyberdeck-zero-store](https://github.com/OSRdesign/cyberdeck-zero-store) of CardputerZero/Store (one change:
apt without recommends). Version 0.10.4, channel `release`.
<!-- branding-allow-end -->
