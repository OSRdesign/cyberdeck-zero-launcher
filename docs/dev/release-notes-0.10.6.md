**New since v0.10.5** (pull requests #193, #194 and #195: installer for boards that are not a Raspberry Pi; the terminal grid fills the screen)

- **Installer for boards that are not a Raspberry Pi (#193, #194).** An Orange Pi Zero 2W was taken for the deck
  (`zero2w`) because "Zero 2" is in its device-tree name, and the install stopped at `/boot/config.txt`. Now:
  - Only a real Raspberry Pi gets a Raspberry Pi board name; detection reads the device-tree `compatible`. Any other
    board is "unknown" to `install.sh`, which asks for `--force` or `--board <profile>` (for example `--board
    pi3a-luckfox35` or `--board generic`).
  - With no `config.txt` / `cmdline.txt` (Armbian and others) the Raspberry Pi boot configuration is skipped with a
    message, and the install goes on and enables the service. A failing `udevadm` reload no longer stops the install.
  - **Backlight with levels on non-Pi boards:** a backlight under `/sys/class/backlight` is used for Brightness and the
    idle dim (the sysfs line in `board.conf`, dim level 10, a udev rule so the launcher user can write it); an on/off
    backlight keeps working as before, and no backlight writes no line.
  - **`--spi-panel waveshare35f [--soft-pwm-backlight]`**: sets up the Waveshare 3.5" RPi LCD (F) on Allwinner H616 /
    H618 boards running Armbian: the panel's init file, the device-tree overlay in `armbianEnv.txt`, `modules-load`,
    the DKMS module `drm-mipi-dbi-chunk` (the panel's DMA transfers must stay under 4 GB) and, with
    `--soft-pwm-backlight`, a software PWM backlight (`pwm-gpio`). It never sets `mem=` and refuses on a Raspberry Pi.
  - **`--network-manager [--yes]`**: installs NetworkManager and switches the network set-up from netplan to it
    (Armbian and Ubuntu server images have none, and Settings > Wi-Fi needs it). When the network goes over Wi-Fi it
    asks first on a terminal; without one, `--yes` goes ahead. Reboot afterwards.
  - **Board-tested** on an Orange Pi Zero 2W, from a clean state (the install and the options above). Other H616 /
    H618 boards and other SPI panels are not tested.
- **Terminal grid fills the screen on every board (not yet tested on a board).** The terminal no longer stops at 80
  columns x 24 rows: the grid is the whole screen divided by the text cell (columns = width / cell width, rows =
  height / cell height), at least 20 x 8 and at most 255 x 255. Before, the small text sizes left a blank strip at the
  bottom of a 720x720 screen. The terminal tells the shell, ssh and full-screen programs the real size, and Settings >
  Screen > Terminal text size shows the same grid in its notes (for example Small 14 px: 90 x 40 on 720x720, 100 x 26
  on 800x480, 160 x 40 on 1280x720). The F6 80 x 24 view is unchanged. **This is covered by unit tests and screen
  renders only; it has not been tested on a board yet.**
- **Upgrade notes:**
  - On a board that is not a Raspberry Pi, `install.sh` now needs `--force` or `--board <profile>` (it used to guess
    the deck wrongly). Raspberry Pi installs are unchanged.
  - `--spi-panel waveshare35f` needs a network connection (firmware, DKMS), the kernel headers of the running kernel,
    and a reboot; DKMS rebuilds the module for later kernels if their headers are installed.
  - Boards on v0.8.13 or later with a release build update from Settings > Updates (release builds only); the new
    installer options apply when `install.sh` of this bundle is run by hand. Boards on older builds or dev builds need
    one install by hand.

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.6-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.6-arm64.tar.gz && cd bundle      # amd64 on a PC
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
apt without recommends). Version 0.10.6, channel `release`.
<!-- branding-allow-end -->
