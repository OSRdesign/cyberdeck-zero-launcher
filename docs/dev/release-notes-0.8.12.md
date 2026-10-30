**New since v0.8.11** (pull requests #111, #112 and #113: grab rotation, a reorderable home grid, the SSH indicator; docs #110)

- **Home grid: arrange the apps.** Hold a tile for 3 seconds (a progress bar shows the hold), or press Ctrl+O on a
  selected tile, to enter move mode. Arrow keys move the tile, a tap on another position places it, Enter drops it and
  Esc cancels. The order is saved. Settings > Launcher > Reset app order puts the default order back.
- **SSH indicator on the status strip.** A glyph shows while an SSH session is connected (it has an idle and an active
  look). Switch it in Settings > System > SSH indicator; default On. The strip slot is reserved, so the Esc
  button and the page titles shift left by 49 px (35 px on narrow strips). The status feed gains `ssh=` and `ssh_act=`.
- **Apps need a rebuild to show the indicator.** Apps that copied the strip code keep the old strip until rebuilt;
  9 apps were republished on 2026-10-30 with it.
- **Shorter Settings titles.** Three page titles are shortened so they fit next to the glyph: Key test, Screen
  keyboard, Paired.
- **Dev tool: `board.py grab` rotation.** The capture now turns by the launcher's current rotation (PDA mode on or
  off), so the Pi 3A+, Pi 4 B and tablet screenshots are no longer sideways in PDA mode; `--rotate` forces a value.
- **Docs.** `docs/PDA-PROFILE.md` has a section "Apps with the band" with renders of the handwriting band.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.12-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.11:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.8.12, channel `release`.
<!-- branding-allow-end -->

**Verified:** `tools/dev/check-all.sh` passed (all nine steps) for #113, with the new unit tests for the home order,
the move mode and the SSH watch and indicator. The owner tested the home grid move mode and the SSH indicator on the
deck, a Pi 4 B and the tablet.

**Known limits**

- Apps built before this release keep the old strip (no indicator, old title positions) until they are rebuilt.
- Landscape PDA stays rejected (portrait only); apps shown turned keep the standard keyboard.
- Still true from 0.8.9 to 0.8.11: see their notes' known limits.
