**New since v0.10.0** (pull requests #154 and #155: Bluetooth audio facts for apps, Raspberry Pi 5 with the 7-inch DSI panel)

- **Apps can see which Bluetooth audio devices are paired.** The launcher still only pairs devices; it adds no audio
  control and never changes the default sound source or output. It now tells apps, read-only, which paired devices are
  headsets, headphones or speakers, whether each is connected, and whether the music (A2DP) or call (hands-free)
  profile is up. It also tells which sound stack owns Bluetooth audio (PipeWire, PulseAudio, BlueALSA or none), whether
  the adapter offers hands-free calls (a headset microphone) and whether it offers LE Audio. A recorder can use this to
  tell a headset from a speaker and to say that Bluetooth audio is not set up. Nothing changes on the launcher's own
  screens; the details are in the app contract (`docs/APP-CONTRACT.md`).
- **New board: Raspberry Pi 5 with the official Raspberry Pi 7-inch Touch Display** on the DSI port (800x480).
  `install.sh` now detects it (before, it stopped with "board: unknown" and needed `--force` and a hand-written
  `board.conf`). The profile gives the touch screen, brightness levels with the panel's own backlight, idle dim and the
  panel size for the layout. Touch and brightness were checked on the author's board. `install.sh` changes no boot
  configuration for it. An existing hand-written `board.conf` is kept; `--force` replaces it and keeps the old one as
  `board.conf.bak`. To force the profile: `sudo ./install.sh --board pi5-dsi7`.
- **Updating:** the same as for v0.10.0. Boards on v0.8.13 or later with a release build update from Settings >
  Updates. Boards on older builds or dev builds need one install by hand (`install.sh` from the bundle).

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.1-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.1-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels (a Pi 5 with the official 7-inch DSI
panel is detected by itself). For the portrait PDA
profile on the Pi 3A+, add `--board pi3a-wsdsi43-portrait`. On a Zero 2 W with the Waveshare 3.5" SPI panel, add
`--board pi3a-luckfox35` (it is detected as the deck otherwise); the install sets up the PWM backlight. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.10.1, channel `release`.
<!-- branding-allow-end -->
