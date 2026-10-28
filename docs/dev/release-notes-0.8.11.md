**New since v0.8.10** (pull request #108: PDA S4, apps get the handwriting band)

- **PDA mode: apps shown upright can use the handwriting band.** In portrait (PDA mode), an app that is shown upright
  (it declares an `X-Portrait` size that fits) can now show the same handwriting band as the launcher. The band shows
  only when a text field asks for input, and its keys behave like the on-screen keyboard's. It follows the choice in
  Settings > System > On-screen keyboard (Handwriting + keyboard); with a physical keyboard there is no band.
  Glyphs you train in Settings > System > Strokes training count in an app at its next start; a glyph trained while
  the app is open is used there after it is restarted.
- **Which apps show it.** The app needs a toolkit built at this release. The first one is Serial Console; the next
  apps repo toolkit pin is v0.8.11, so other apps get it when they are rebuilt. Older apps keep the on-screen
  keyboard. Landscape apps and apps shown turned are unchanged.

**For app developers** (`docs/APP-CONTRACT.md`, report `docs/dev/reports/044-apps-with-the-band.md`)

- The band runs inside the app's own process (the shared code is in `cp0_lvgl`, with the rule "when is the band the
  text input" in `cp0::ui::zone_host`). The launcher only says "you may" and "the user wants it".
- The launcher passes `APPLAUNCH_PDA=1` to a fullscreen app run upright (an `X-Portrait` that fits, on a launcher with
  the writing zone). Landscape or turned apps get `APPLAUNCH_PDA=0`, as since 0.8.9.
- `input.state` gains `hand=0|1`: 1 when the user chose Handwriting + keyboard and the band exists. A file without
  `hand` means 0.
- There is no `.desktop` key to opt in; an upright portrait app gets the flag on its own.
- A page that already handles the on-screen keyboard's inset handles the band; the keys arrive as ordinary key
  events.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.11-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.10:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.8.11, channel `release`.
<!-- branding-allow-end -->

**Verified:** `tools/dev/check-all.sh` passed for #108: the 196 render goldens are byte-identical, and the zone rule,
`input.state` round trip and environment decision have unit tests. The owner tested a test bundle (0.8.10-s4.1) with
the apps toolkit change on the tablet and on a Pi 4 B: the band works in Serial Console.

**Known limits**

- Only apps built with a toolkit from this release show the band; none other than Serial Console is converted yet.
- Landscape PDA stays rejected (portrait only); apps shown turned keep the standard keyboard.
- Still true from 0.8.9 and 0.8.10: see their notes' known limits.
