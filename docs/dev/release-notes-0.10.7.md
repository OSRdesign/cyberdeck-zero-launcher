**New since v0.10.6** (pull requests #197 and #198: the 3.5" SPI panel on a Raspberry Pi; the FZ-M1 power key)

- **The 3.5" SPI panel on a Raspberry Pi, set up by the installer (#197).** The Luckfox 3.5" RPi LCD (CTP) and the
  Waveshare 3.5" RPi LCD (F) (ST7796S display, GT911 touch, 320x480) used to need `config.txt` edited by hand. Now:
  - **`sudo ./install.sh --spi-panel luckfox35`** (`waveshare35f` is accepted as the same name) on a Raspberry Pi of any
    model (a Pi 5, a Zero 2 W, a Pi 3A+, ...): it puts the panel's init file in `/lib/firmware`, adds one marked block
    to `[all]` in `/boot/firmware/config.txt` (a backup is made once; also on a desktop board, because you asked for
    it), writes the `pi3a-luckfox35` profile and asks for a reboot. Run `install.sh` once more after the reboot so the
    profile finds the panel's framebuffer. `--dry-run` shows the plan and the exact lines first.
  - **Detection:** a Raspberry Pi other than the Zero 2 W and the Pi 3A+ with the panel connected (an `SPI-*` connector
    at 320x480 and a `panel-mipi-dbi*` framebuffer, and no DSI or DPI panel connected as well) now gets the
    `pi3a-luckfox35` profile by itself, without `--board`.
  - **Brightness with 15 steps on a Pi 5** (tested by hand on a Pi 5: screen, touch, launcher and 15-step
    brightness): the PWM backlight uses channel 2 there (GPIO18), written for you.
  - `uninstall.sh` removes the marked block and the init file that `install.sh` put there.
  - **Not tested:** other Raspberry Pi models than the Pi 5 and the Zero 2 W with this option; other SPI panels.
- **Panasonic FZ-M1: the power key survives the installer (#198).** On the FZ-M1 only, `install.sh` writes the logind
  drop-in `/etc/systemd/logind.conf.d/50-cyberdeckzero-powerkey.conf`: a short press of the power key suspends, a long
  press powers off (the settings used by hand since v0.8.7). logind gets a reload signal, never a restart. A file with
  the same settings stays as it is; one with other settings is kept (`--force` replaces it, the old one is kept as
  `.bak`). `uninstall.sh` removes the file only when the installer made it and it has not been edited. **Not yet
  tested on the tablet.**
- **Upgrade notes:**
  - `--spi-panel luckfox35` needs a reboot, and a second `install.sh` run after it. It edits `config.txt`.
  - Nothing changes on other boards unless you pass the new option; the power key file is written on the FZ-M1 only.
  - Boards on v0.8.13 or later with a release build update from Settings > Updates; the new installer options apply
    when `install.sh` of this bundle is run by hand. Boards on older builds or dev builds need one install by hand.

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.7-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.7-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels (a Pi 5 with the official 7-inch DSI
panel is detected by itself). For the portrait PDA
profile on the Pi 3A+, add `--board pi3a-wsdsi43-portrait`. On a Zero 2 W with the Waveshare 3.5" SPI panel, add
`--board pi3a-luckfox35` (it is detected as the deck otherwise); the install sets up the PWM backlight. On any Raspberry Pi with the Luckfox or Waveshare 3.5" SPI panel (a Pi 5 too), run `sudo ./install.sh --spi-panel luckfox35`, reboot, then run `install.sh` once more. A board whose
DSI panel is not the size of its profile (for example a 5" panel on a Pi 3A+): add `--screen-size 5`. A screen with no
profile: `--board generic`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
our fork [cyberdeck-zero-store](https://github.com/OSRdesign/cyberdeck-zero-store) of CardputerZero/Store (one change:
apt without recommends). Version 0.10.7, channel `release`.
<!-- branding-allow-end -->
