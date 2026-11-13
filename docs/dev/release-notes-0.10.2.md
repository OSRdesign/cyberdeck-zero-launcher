**New since v0.10.1** (pull requests #163, #164, #165 and #171: Settings > Apps installs no recommended packages, font licence texts)

- **Settings > Apps no longer installs the packages an app only recommends.** When an app needs libraries that are
  not on the board yet, Settings > Apps installs them without apt's recommended packages. The download is much
  smaller: for an app that uses the FFmpeg libraries, such as Vinyl, about 104 MB on a Raspberry Pi OS Lite board
  instead of about 462 MB. Apps that recommend a package for an optional feature now work without that feature until
  you install the package yourself: Mesh Hop and mmWave Radar recommend BlueZ, so their Bluetooth parts work only
  when BlueZ is already on the board. Nothing already installed is removed.
- **For app authors:** list every library and tool your app needs in `depends`. `recommends` is only for an optional
  feature that may be missing: Settings > Apps never installs it. An app whose dependencies are already met is
  installed directly, which never installed recommends either. Vinyl 0.1.0, the music player, was published in the
  apps repository on 2026-11-13 and lists the FFmpeg libraries in its `depends`.
- **Font licences on the board.** The licence text of every font family in the launcher is now installed in
  `/usr/share/doc/cyberdeckzero-launcher/fonts/`: DejaVu, Liberation Mono, Montserrat, Noto Sans CJK, Font Awesome
  and Alibaba PuHuiTi's legal notice are new there, next to JetBrains Mono's. The unused icon font `svgfont.ttf`, which had no licence
  information, is no longer in the bundle.
- **Known limits:** only the install of apps changed, and only boards that run this launcher get it: an app installed
  from Settings > Apps on v0.10.1 or older still brings its recommended packages. Updating from v0.10.1 leaves the old
  `svgfont.ttf` on the board; nothing loads it. Some pages of the apps repository still describe the old behaviour.
- **Updating:** the same as for v0.10.1. Boards on v0.8.13 or later with a release build update from Settings >
  Updates. Boards on older builds or dev builds need one install by hand (`install.sh` from the bundle).

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.2-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.2-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels (a Pi 5 with the official 7-inch DSI
panel is detected by itself). For the portrait PDA
profile on the Pi 3A+, add `--board pi3a-wsdsi43-portrait`. On a Zero 2 W with the Waveshare 3.5" SPI panel, add
`--board pi3a-luckfox35` (it is detected as the deck otherwise); the install sets up the PWM backlight. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
our fork [cyberdeck-zero-store](https://github.com/OSRdesign/cyberdeck-zero-store) of CardputerZero/Store (one change:
apt without recommends). Version 0.10.2, channel `release`.
<!-- branding-allow-end -->
