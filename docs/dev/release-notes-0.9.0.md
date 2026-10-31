**New since v0.8.13** (pull requests #115 to #124: the first release you can install from Settings > Updates on a
v0.8.13 board, and the split from the `small-screen` branch)

- **Two branches from now on.** This branch keeps screens of 640x480 and up (and the portrait PDA). The new
  `small-screen` branch is for 480x320 only, has no release for now, and keeps what was removed here.
- **Settings > Apps shows each app's description.** Every app's page has a Description row that opens a text page with
  the full text. Apps you have not installed yet now open their page first (Description, Install), so installing takes
  two taps instead of one. Descriptions are capped at 500 characters in the launcher; every app of ours now carries a
  condensed descriptor (a summary line of at most 80 characters plus the description, 500 characters in all), and the
  apps repo checks it. The long documentation stays in each app's README.
- **Removed from this branch:** the Store tile and its switch in Settings > Launcher (the Store program stays in the
  bundle as the back end of Settings > Apps: installing and updating apps works as before), and Snake, Tank and IP
  Panel (320x170 apps; they stay on the `small-screen` branch). The apps of ours that fit 640x480 stay.
- **Smaller bundle:** about 27 MB smaller (arm64 57 MB to 30 MB), because two unused large fonts were dropped.
  Upgrading removes them and the retired images.
- **Safer self-update:** updating from Settings > Updates now backs up every file the installer can delete or
  replace, so an automatic rollback also brings back the retired images and fonts. An update needs about 330 MB free.
- **After upgrading,** remove packages that no longer have a screen: `2048` (an old Hub package; Debian's own 2048
  would replace it on an apt upgrade) and, if present, `meshzero`, `lanscan` and `wifi-survey`:
  `sudo apt remove <name>`.

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.9.0-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.9.0, channel `release`.
<!-- branding-allow-end -->

**Known limits**

- Self-update works on the release channel only.
- apt updates in the Updates page are not part of this release (next).
- On a 480-wide portrait panel the Description row preview is short (about 8 characters); the text page shows it all.
