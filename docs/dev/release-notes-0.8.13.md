**New since v0.8.12** (pull requests #115, #116 and #117: a crash fix and Settings > Updates)

- **Settings > Updates: update the launcher from the device.** The launcher checks GitHub for a newer release when you
  open the page and once a day, and shows What's new (these release notes). It can download the new version, verify
  its sha256, install it and restart itself, with a progress bar and a sudo password prompt. A backup is made
  first, and the old version is put back automatically if the new one does not start within 90 seconds. Only boards
  running a release build offer the install. A short toast announces a new version once.
- **Fix: home grid crash.** The home grid could crash at start when an app's `.desktop` had no icon. This only
  affected custom or third-party entries.
- **Installer.** `install.sh` now records its options in `/etc/applaunch/install.conf`, so a self-update installs
  with the same options.

<!-- launcher-notes-end -->

**Installing this version:** the first install of v0.8.13 on a board running v0.8.12 or older must be done by hand
(`install.sh` from the bundle, below), because the installed `install.sh` of those versions cannot start a
self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.13-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.12:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.8.13, channel `release`.
<!-- branding-allow-end -->

**Verified:** `tools/dev/check-all.sh` passed for the Settings > Updates work (#117), with the new unit tests for
the update model and the fake release server. The crash fix (#115) has a new review scene (`home_icon_missing`).

**Known limits**

- Self-update works on the release channel only.
- apt updates in the Updates page are not part of this release (next).
- Still true from 0.8.12: apps built before 0.8.12 keep the old status strip until they are rebuilt; landscape PDA stays
  rejected.
