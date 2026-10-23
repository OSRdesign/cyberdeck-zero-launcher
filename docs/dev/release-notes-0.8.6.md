**New since v0.8.5** (pull request #89, report 035 "board state and app SDK")

- **`board.state`: fixed board facts for apps.** Next to `status.state`, the launcher writes `board.state` with the
  board, arch, RAM, battery source, backlight kind and levels, sound kind (guesses are flagged) and the `requests=`
  verbs the launcher accepts. APP-CONTRACT section 5c.
- **`status.state` gains `volume`, `mute` and `brightness_pct`.**
- **Apps can ask the launcher for things over `launcher.sock`.** Verbs: `hello`, `caps`, `beep`, `toast`, `volume`,
  `brightness`, `dim allow|deny`. The socket is mode 0600, same user only, with a peer check and rate limits; every
  request is logged. APP-CONTRACT section 14.
- **`cyberdeckzero-ctl`**, a small command-line client for the socket, is installed by `install.sh`.
- **`cp0_app_client.[ch]`**, copy-ready into an app, wraps the socket requests.
- **Volume and brightness keys act while an app is in front**, without rebuilding the app. The launcher draws nothing
  over a fullscreen app; apps may draw their own level bar from `status.state`.
- The launcher's own UI sounds stay off; there is no new setting.
- The contract version stays 1 (the additions are backwards compatible); `X-Permissions` is reserved for later.

**Apps do not change with this release.** Mesh Hop notifications adopt the socket next.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.6-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels, then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.5:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Built from the `release-0.8.6` branch (main b4dea1c plus the release docs), version 0.8.6,
channel `release`.
<!-- branding-allow-end -->

**Verified:** unit tests including ASan/UBSan; builds for arm64 and amd64 with 0 warnings; installed on the HackBerry
CM5 and the FZ-M1 tablet, where the owner checked `board.state`, the `cyberdeckzero-ctl` verbs, `beep` and `toast`.

**Known limits**

- Not verified on boards: volume and brightness keys inside apps, idle dim with `dim allow`, the `install.sh` and
  `uninstall.sh` paths for the new files, and thread safety of the audio service.
- Still true from 0.8.5: see the 0.8.5 notes' known limits.
