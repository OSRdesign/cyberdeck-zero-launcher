**New since v0.8.7** (pull requests #96, #97, #98 and #99: the PDA profile S1 to S3, the Strokes training, the board
profile `pi4b-dsi`, the idle dim key)

- **The PDA profile: a portrait launcher for small DSI screens.** The same launcher now runs upright on a 480x800
  panel (the Pi 3A+ with the 4.3" DSI screen turned to portrait) or a 480x640 panel (3.5 to 4 inch). Reports 038 (the
  study) and 039 to 042. It is a profile you choose, never detected: `sudo ./install.sh --board pi3a-wsdsi43-portrait`,
  and pass the same flag on every update. The landscape profile `pi3a-wsdsi43` is unchanged, byte for byte.
- **Portrait shell (S2, #98).** On a panel under 600 px wide the status strip is drawn smaller and the page headers use
  an 18 px title that shows only the last part of a path ("Screen", not "Settings > Screen"), so every title fits. The
  home grid has three columns and there is a number pad. Tiles are hidden for apps that do not fit portrait: a
  landscape app whose minimum size fits the landscape canvas is shown and run turned by a quarter turn; compat apps
  and apps that do not fit are hidden. Multitasking is on where the short side is 480 px or more, so the 480x640 and
  480x800 panels have the app switcher; a switcher card whose first word does not fit beside its X now starts the name
  under it. The 640x480 and 480x320 screens draw exactly as before.
- **Strokes: a single-stroke handwriting alphabet (S1, #97).** A recogniser library in C with no LVGL dependency, plain
  text glyph tables (letters, digits, gestures, punctuation, accents) and a user overlay of personal templates.
  About 12.6 us per stroke on a PC, 0.13 to 0.25 ms estimated on the Pi Zero 2W and Pi 3A+. Tools to generate,
  capture, replay and measure strokes are in `tools/strokes/`.
- **The handwriting band (S3, #99).** Where a text field asks for input and the keyboard setting allows it, a band
  comes up at the bottom of the portrait panel: a key strip (Tab, Ctrl, Alt, the arrows, Enter) over two writing
  boxes, letters and digits. Pen strokes are recognised and typed as keys; touches in the band never reach the app;
  when the band is hidden the pages get the whole panel. A physical keyboard hides it, as it hides the on-screen
  keyboard. The strip has a **Keyb** key that puts the standard on-screen keyboard in the band's place until the
  keyboard's close cross is pressed. `input.state` reports `vkb=zone` while the band shows.
- **On-screen keyboard setting.** Settings > System > On-screen keyboard is now **Auto**, **On** or, on the PDA
  profile, **Handwriting + keyboard** (its default there). The old Off is gone; a stored Off reads as Auto.
- **Strokes training (Settings > System > Strokes training, PDA profile only).** A page that keeps the band shown and
  takes its strokes without typing. It shows the glyph to write with the chart stroke as a guide, judges four
  attempts per glyph (recognised, refused, or read as another glyph), turns them into your own templates in the user
  overlay and reloads the recogniser. A glyph picker chooses which glyph to train. Back, Skip, Redo, Reset all (asks
  first) and Finish (a re-test of each trained glyph, then a summary) are on the page, and it resumes where it
  stopped. Every attempt is logged to `~/.config/applaunch/strokes-training.txt`; the PC tool `strokes-log` reads it.
- **Board profile `pi4b-dsi` (#96).** A Raspberry Pi 4 B (1 GB) with the Freenove 5" 800x480 DSI panel is detected
  from the model, the DSI connector, the framebuffer and the panel's backlight regulator, and gets its profile
  automatically: 800x480, compat scale 2, backlight through sysfs, as the 4.3" panel. Its physical panel size is a
  guess to check with a ruler. This board logs undervoltage warnings with a weak supply: use the official 5.1 V 3 A one.
- **Idle dim level per board: `APPLAUNCH_DIM_PERCENT` (#96).** A board profile can set the brightness the screen dims
  to when idle (default 10). The `pi4b-dsi` profile sets 20, because 10 % is black on that panel. There is no setting
  for it in Settings yet.

**Apps do not change with this release.** An app that declares `X-Portrait=WxH` is shown natively on a portrait
launcher; the others run turned or are hidden, as above.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.8-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.7:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.8.8, channel `release`.
<!-- branding-allow-end -->

**Verified:** `tools/dev/check-all.sh` passes: unit suites, the Strokes suite, the render harness (158 goldens; the
goldens of the existing screens, 640x480 and 480x320 included, are byte-identical) and the install tests (375 passed).
The portrait launcher and the band were tried by the owner on the Pi 3A+ with the 4.3" panel in test bundles
`0.8.8-pda.4` and `0.8.8-pda.5` (the training page, the glyph picker and the Keyb key, bundles `.7` and `.8`, are
checked by tests and renders only); the `pi4b-dsi` profile was tried on a Pi 4 B with the Freenove panel.

**Known limits**

- Handwriting accuracy has only been measured on synthetic strokes made from the table's own templates (99.5 % correct
  on them, about 82 % under harsh noise). On the first board test 11 of 26 letters were refused before the training
  page existed. A capture session with a real stylus is planned and will say how well it reads.
- Keys from the band do not reach compat apps (they are hidden on portrait). Turned fullscreen apps keep the standard
  keyboard rule.
- `screen.state` still describes the portrait panel for a turned app.
- A turned app at rotation 180 has not run on a board.
- The final release bundle (as opposed to the test bundles) has not been installed on a board.
- Still true from 0.8.7: see the 0.8.7 notes' known limits.

**Next**

- PDA S4: apps with their own band (`APPLAUNCH_ZONE`, `screen.state` orientation and zone, toolkit).
- PDA S6 follow-ups: tuning from real stylus captures.
- A Settings switch to turn Strokes off, and a Settings entry for the idle dim level.
- `install.sh` writes the power-key drop-in for the PC profile.
- Launcher themes (colours); an idea, no study yet.
