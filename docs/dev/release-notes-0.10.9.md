**New since v0.10.8** (pull request #205: network share helper answer for a wrong share name, backlight wait for apps, Orange Pi uninstall, installer size note)

- **Network share helper (#205).** A `mount` that fails with `error(2)` and no `NT_STATUS` word (a wrong share name, or
  a share that needs a login; `mount.cifs` gives the same answer for both on a ZimaOS server) now always answers
  `denied`, with the reason "wrong share name, or it needs a login" ("wrong share or folder name, or it needs a
  login" when a folder was given). The 0.10.8 kernel-log check is gone: the kernel logs the wrong-name line only once
  per boot, so later wrong names were answered differently.
- **`cp0_app_backlight` waits longer (#205).** Apps built on cp0_lvgl (for example Vinyl's knock-knock) now wait up to
  1500 ms for the launcher's answer to the screen on/off request instead of 200 ms, so a slow board no longer shows a
  refusal for a request that was about to be done. `beep` and `toast` keep 200 ms. Documented in `docs/APP-CONTRACT.md`.
- **Orange Pi Zero 2W uninstall (#205).** On Armbian with `install.sh --spi-panel waveshare35f`, the installer now
  records what it made (the overlay name it put in `armbianEnv.txt`, the files, the DKMS modules) and `uninstall.sh`
  undoes exactly that. Installed packages stay. `uninstall.sh --no-boot-config` leaves all of it. Boards installed
  before 0.10.9 have no such record, so nothing is removed on them.
- **Installer size note (#205).** When a hand-edited `board.conf` is kept, the note about the 4.3", 5" or 7" DSI
  screen size now says that the kept file decides.
- **Developer:** the branding check allows the probe fixtures and golden reports (captured board data).
- **Tested:** the helper, installer and update-helper test suites pass (254, 969 and 172 checks). The Orange Pi
  uninstall and the share helper change were tested with a fake root only, not yet on a board (the helper change is the
  same logic as the 0.10.8 helper tests).
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
sha256sum -c cyberdeckzero-launcher-0.10.9-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.9-arm64.tar.gz && cd bundle      # amd64 on a PC
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
apt without recommends). Version 0.10.9, channel `release`.
<!-- branding-allow-end -->
