**New since v0.9.4** (pull requests #145, #146 and #149: Settings > Apps checks for app updates, map engine for Deck Atlas)

- **Settings > Apps checks for app updates when you open it.** The page shows the saved lists at once, then refreshes
  every enabled source whose list is older than 10 minutes, in the background. A newer app version shows as the usual
  yellow marker, and the highlight and scroll stay where they are. A status line tells the result: up to date, updated
  (with the number of sources that failed), or offline with the age of the list shown. Without a network connection
  nothing is lost and no popup appears.
- **New setting "Auto-check updates"** at the end of Sources: On (default) or Off. Off brings back the old behaviour.
  "Sync my sources" always downloads every enabled source, whatever the setting.
- **Map engine for Deck Atlas.** The offline map engine in the shared UI library handles converted map folders per
  zoom level, refreshes its cache when a map changes, reports the map source and notifies the app when the view
  moves. Deck Atlas uses this; nothing changes on the launcher's own screens.
- **Updating:** the same as for v0.9.4. Boards on v0.8.13 or later with a release build update from Settings >
  Updates. Boards on older builds or dev builds need one install by hand (`install.sh` from the bundle).

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.0-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.0-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. On a Zero 2 W with the Waveshare 3.5" SPI panel, add
`--board pi3a-luckfox35` (it is detected as the deck otherwise); the install sets up the PWM backlight. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.10.0, channel `release`.
<!-- branding-allow-end -->
