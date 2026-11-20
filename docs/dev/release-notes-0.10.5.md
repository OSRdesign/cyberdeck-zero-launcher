**New since v0.10.4** (pull request #191: three new rows in Settings > Screen)

- **Idle dim level.** Settings > Screen > Idle dim level sets how bright the screen stays while it is dimmed by the
  idle timer: Default (the board profile's level, 10 % or 20 %), 5, 10, 20, 30 or 50 %. It applies at once and is kept
  in config.json. Boards whose backlight only switches on and off (such as the HackBerry) do not show the row.
- **Terminal text size.** Settings > Screen > Terminal text size: Auto (today's size: 20 px, 14 px on a portrait screen
  under 480 px wide), 14, 17, 20 or 24 px. Each choice shows the resulting grid as a grey note (columns x rows; the grid is
  clamped like the terminal itself to at most 80 x 24). It applies the next time the terminal opens, and only to the
  terminal page, which the CLI and SSH sessions started from the launcher share.
- **Screen size.** Settings > Screen > Screen size: Auto or a diagonal from 2.8 to 10.1 inches, for panels that look the
  same to software (the 4.3", 5" and 7" DSI panels). It applies at the next start of the launcher, is kept in
  config.json, and shows where the value comes from. The installer's `--screen-size` still works; this is the same
  choice without running `install.sh` again. A custom diagonal is not available yet.
- **Tested** on the HackBerry and on palmpi. No other changes.
- **Updating:** the same as for v0.10.4. Boards on v0.8.13 or later with a release build update from Settings >
  Updates. Boards on older builds or dev builds need one install by hand (`install.sh` from the bundle).

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.5-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.5-arm64.tar.gz && cd bundle      # amd64 on a PC
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
apt without recommends). Version 0.10.5, channel `release`.
<!-- branding-allow-end -->
