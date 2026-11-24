**New since v0.10.10** (pull requests #212 and #213: Quick controls tile, installer keeps your panel size)

- **Quick controls tile (#213).** A built-in tile "Quick controls" on the home grid opens a page "Controls" with four
  big buttons in a 2 x 2 grid: Shutdown, Reboot, Wi-Fi and Bluetooth. There is **no confirmation**: a tap or Enter on
  Shutdown or Reboot acts at once (the page then says "Shutting down..." or "Restarting..."). Wi-Fi and Bluetooth
  switch the radio at once and show a dot with On, Off or Switching...; the real state is read again afterwards. A
  board without that adapter shows Not available and the button does nothing. Opening the page or moving the focus
  never acts, the first focus is on Wi-Fi, and a button acts only when the press began on it. Arrows (no wrap), Tab and
  Shift+Tab, Enter, Esc and the touch screen all work, in the 2 x 2 layout at every size including portrait and PDA.
  Shutdown and Reboot are the same calls as Settings > System, run as the launcher's user; a refused call shows
  "Shutdown failed" or "Reboot failed". The tile can be hidden in Settings > Launcher (`app_QuickControls`) or with
  `APPLAUNCH_HIDE_TILES`.
- **Installer keeps your panel size (#212).** Reproduction: on a Pi 4B (`pi4b-dsi`, 7-inch, `PANEL_MM=154x86`) a
  re-install replaced the size with the profile default (`108x65`) when `install.conf` had no `screen_size=` or
  `panel_mm=` (for example after installing with an `install.sh` older than #181) and `board.conf` was a generated,
  unedited profile with a different size. Now `install.sh` keeps that `PANEL_MM` and records it as `panel_mm=`. Fresh
  installs still get the profile defaults, a hand-edited `board.conf` is still left alone, and a PC still takes its
  size from the screen. A board that was already hit needs its size given once: `--screen-size 7` (or `--panel-mm
  154x86`); a board that was not hit needs nothing.
- **Tested:** on a Pi 4B with a 7-inch 800x480 screen (seveninch): install, the tile, the 2 x 2 page, focus on Wi-Fi,
  arrows, Tab, Bluetooth switched by touch and Enter, Esc, hiding the tile; Wi-Fi and Reboot were tried at the board.
  The same board kept `PANEL_MM=154x86` in a normal re-install and in one with the size keys removed. The installer
  suites pass (978 and 174 checks) and the unit and render scenes of the page pass. **Not tested:** other boards
  (HackBerry, tablet, Orange Pi, deck, Pi 5), whether logind allows passwordless poweroff and reboot on each board,
  Not available on real hardware, and Shutdown at the board.
- **Known failures on main** (also before these changes): the release-notes count of the `app-unit` step and two
  `probe` goldens of `check-all.sh`.
- **Upgrade notes.** Boards on v0.8.13 or later with a release build update from Settings > Updates, as before.
  Boards on older builds or dev builds need one install by hand.

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.11-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.11-arm64.tar.gz && cd bundle      # amd64 on a PC
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
apt without recommends). Version 0.10.11, channel `release`.
<!-- branding-allow-end -->
