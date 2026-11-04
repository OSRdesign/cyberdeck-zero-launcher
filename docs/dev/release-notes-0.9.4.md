**New since v0.9.3** (pull requests #137 and #138: PDA mode on 480x320 panels, brightness on the 3.5" SPI panel)

- **PDA mode on small landscape panels.** On a 480x320 screen such as the 3.5" panel, Settings > Screen > PDA mode
  (Off / On rotate left / On rotate right) is now there. The row appears when the short side is 320 px or more. The
  screen then runs upright at 320x480 with the normal on-screen keyboard. There is no handwriting band at this size.
- **Compact layout for narrow portrait.** The top bar is 40 px high, with a smaller Esc button and no page title.
  Dialogs and text are smaller. Toasts and the volume and brightness display are sized for the screen. Landscape and
  the other boards are unchanged.
- **Fix: terminals on 320-wide portrait screens.** CLI, Python, SSH sessions and apps with Terminal=true now open the
  full-screen terminal with the on-screen keyboard. Before, they showed the old small layout with no keyboard and no
  way out.
- **Brightness on the Waveshare 3.5" SPI panel on a Zero 2 W** (profile pi3a-luckfox35). Brightness has 10 real
  steps, the idle screen dims and the level comes back at start-up, through the panel's PWM backlight. A full install
  on that board switches config.txt to the PWM backlight and asks for a reboot. A hand-edited board.conf is kept. The
  Pi 3A+ keeps On / Off.
- **Updating:** the same as for v0.9.3. Boards on v0.8.13 or later with a release build update from Settings >
  Updates. Boards on older builds or dev builds need one install by hand. A Zero 2 W with the 3.5" SPI panel
  (pi3a-luckfox35) stops in Settings > Updates on this profile change: run install.sh --board pi3a-luckfox35 by hand
  once, then reboot.
- **Known:** the Brightness label is cut on the narrow portrait Settings > Screen page.

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.9.4-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.9.4-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. On a Zero 2 W with the Waveshare 3.5" SPI panel, add
`--board pi3a-luckfox35` (it is detected as the deck otherwise); the install sets up the PWM backlight. Then reboot.

**Zero 2 W with the 3.5" SPI panel, already on a release:** Settings > Updates stops at the new profile ("it would
change /etc/applaunch/board.conf"). Run `sudo ./install.sh --board pi3a-luckfox35` from the bundle once (the old
profile is kept as `board.conf.bak`), then reboot. A board changed by hand already keeps its files and updates as
before.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.9.4, channel `release`.
<!-- branding-allow-end -->
