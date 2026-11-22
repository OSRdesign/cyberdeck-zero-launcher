**New since v0.10.9** (pull request #207: network share names with spaces)

- **Network share helper (#207).** The helper now accepts a single space inside a share name and inside a folder name
  (for example the share `share test`): no leading, trailing or double space, no tab. `list` writes such a space as
  ` ` so every line keeps one token per field; apps decode it. The folder name on the board (`--name`), the host and
  the user name keep their strict rules. An app built for the earlier helper never sends a space, so nothing changes
  for it. Vinyl 0.8.1 uses this.
- **Tested:** the helper suite passes 282 checks and the installer suite 969. On a real board (Pi Zero 2 W class,
  Debian 13) the helper added the share `share test` from a ZimaOS server, the automount came up, the files read, `list`
  showed `share test` and remove left nothing behind. A folder with a space inside a share was tested only with the
  fake root.
- **Upgrade notes:**
  - Boards on v0.8.13 or later with a release build update from Settings > Updates, as before. Nothing else changes.
  - Boards on older builds or dev builds need one install by hand.

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.10-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.10-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels (a Pi 5 with the official 7-inch DSI
panel is detected by itself). For the portrait PDA
profile on the Pi 3A+, add `--board pi3a-wsdsi43-portrait`. On a Zero 2 W with the Waveshare 3.5" SPI panel, add
`--board pi3a-luckfox35` only if it is not detected by itself; the install sets up the PWM backlight. On any Raspberry Pi with the Luckfox or Waveshare 3.5" SPI panel (a Pi 5 too), run `sudo ./install.sh --spi-panel luckfox35`, reboot, then run `install.sh` once more. A board whose
DSI panel is not the size of its profile (for example a 5" panel on a Pi 3A+): add `--screen-size 5`. A screen with no
profile: `--board generic`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
our fork [cyberdeck-zero-store](https://github.com/OSRdesign/cyberdeck-zero-store) of CardputerZero/Store (one change:
apt without recommends). Version 0.10.10, channel `release`.
<!-- branding-allow-end -->
