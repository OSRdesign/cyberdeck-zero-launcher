**New since v0.8.3** (pull requests #83 to #85, report 030 "board capabilities"; the apps repository has its own pull
request #86)

- **The launcher finds out what the board has.** Battery, backlight (kind and number of levels) and sound output are
  now detected at run time instead of by compile-time switches, and Settings follows them: Brightness shows levels,
  or On / Off when the backlight has a single step (the Pi 5 + HyperPixel 4 by default), and no row at all without a
  backlight; a Speaker row appears only when there is a non-HDMI sound card or a sound-server output (Bluetooth
  included). The volume goes through `pactl`, else `amixer`. System sounds are off by default on our boards. The
  volume and brightness keys do nothing, and show no hint, on a board without the capability. `install.sh` adds
  `alsa-utils` and the `audio` group when a non-HDMI sound card exists.
- **HackBerry CM5: battery and Bluetooth speaker.** The battery is read from the board's MAX17048 gauge over i2c
  (`APPLAUNCH_GAUGE=max17048:auto:0x36` in its profile; percent, voltage, and charging from the charge rate; a kernel
  battery still wins); the status strip and Settings > Battery show it. The HackBerry profile notes that brightness is
  the case button (no software brightness) and names the case Bluetooth speaker `XWF-M18-M28-M38`. On every board a
  device is marked `Trusted=true` after each successful Bluetooth pair, so it reconnects by itself.
- **Bluetooth audio on desktop boards.** The launcher stops the display manager, and WirePlumber then dropped
  Bluetooth audio ("a2dp-sink profile connect failed: Protocol not available"). `install.sh` now writes a per-user
  WirePlumber drop-in (`~/.config/wireplumber/wireplumber.conf.d/50-cyberdeckzero-bluez-no-seat.conf`); it applies
  after the next login or `systemctl --user restart wireplumber`. `uninstall.sh` removes it.
- **Pi 5 + HyperPixel 4: brightness levels (opt-in).** `sudo ./install.sh --pwm-backlight` (only on the
  `pi5-hyperpixel4` board) installs an overlay that turns the backlight into a PWM one (RP1 PWM0 channel 3, GPIO19,
  10 kHz) with levels. Measured on the panel: 1 % duty is black, 2 % is readable, the backlight is near full from about
  15 %, so levels 1 to 9 are 2 % to 14 % and level 10 is 100 %. It needs a reboot; the hand revert is in
  `projects/APPLaunch/pizero2w/README.md`. Without the flag nothing changes (On / Off).
- **The backlight level is corrected at launcher start** on every board with levels: the saved Settings > Brightness
  level, or full when nothing is saved and the panel came up below half. The system restores the last, possibly dim,
  level at boot, and the launcher used to skip this step on dpi-scaled boards; the deck, the Pi 3A+ and the tablet now
  also come back to their saved level at start.
- Apps repository: the README's app table has a Packages column (arm64 / amd64) and the app guidelines have the FZ-M1
  tablet row.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.4-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels, then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.3:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Built from the `release-0.8.4` branch (main 98a79db plus the release docs), version 0.8.4,
channel `release`.
<!-- branding-allow-end -->

**Tested by the owner** (physical tests): test build 0.8.3-c4.1 of the same code on all five boards (deck, Pi 3A+,
Pi 5, HackBerry CM5, FZ-M1 tablet): the saved brightness comes back at launcher start, the dark-time dim and wake, and
each board's rows; on the Pi 5 + HyperPixel 4 (overlay installed by hand) every brightness level was checked by eye;
on the HackBerry the battery in the status strip and the case speaker connecting. The final 0.8.4 build was checked by
the automatic suite (8 steps).

**Known limits**

- The `--pwm-backlight` flag itself has been tried only on fake board trees (on the real Pi 5 the overlay was installed
  by hand). On the Pi 5, level 10 looks like level 9, and the screensaver's dim stays at the 2 % floor (level 1).
- The Pi 5 has no Speaker row (its only sound output is HDMI).
- Trusted-after-pair is covered by the build only (no board test).
- The HackBerry gauge is read with a register write then a separate read (no i2c repeated start) and was tried on
  one unit; its i2c bus is found through the touch controller (`auto`). A HackBerry already installed gets the new
  profile keys when `install.sh` rewrites its generated `board.conf` (the old one kept as `board.conf.bak`); a
  profile edited by hand is kept unless `--force`.
- Still true from 0.8.3: a PC installed with 0.8.1 keeps its old app source (fix by hand in Settings > Apps >
  Sources, see the 0.8.3 notes); the Store adds its own CardputerZero hub to the source list, which lists no apps; the
  tablet's touch panel reports at most 2 contacts; viz1090 is not built for amd64; the installer does not restart a
  running launcher on a desktop board; charging is shown by colour only (no bolt icon) and only the first battery is
  shown; Net Toolbox's summary still says "Pi Zero 2 W" in the amd64 registry; viz1090 is not multitasking-capable; the
  Pi 3A+ DSI touch can stay dead after a soft reboot.
