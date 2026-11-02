**New since v0.9.2** (pull requests #132 and #133: home grid tile names)

- **Home grid: a tile name is one line and never runs over the icon.** A long name such as "Home Assistant" used
  to wrap onto a second line and overlap the icon on small screens. The name now stays on one line: the font steps
  down to 80 percent, and if it still does not fit the name ends in an ellipsis.
- **Updating:** the same as for v0.9.2. Boards on v0.8.13 or later with a release build update from Settings >
  Updates. Boards on older builds or dev builds need one install by hand.

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.9.3-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.9.3, channel `release`.
<!-- branding-allow-end -->
