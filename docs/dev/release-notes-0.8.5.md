**New since v0.8.4** (pull request #87, report 032 "apps status strip")

- **The launcher publishes a status feed for apps.** Next to `screen.state`, the launcher now writes `status.state`
  with `wifi`, `wifi_pct`, `eth`, `bt`, `battery` and `battery_state`. It is written from the launcher's own thread,
  and only when a value changes. Apps read it with `cp0_app_status_read()` (no LVGL dependency, ready to copy into an
  app). The apps' status strip uses the feed while the launcher process is alive, else it reads sysfs as before. The
  feed is documented in APP-CONTRACT section 5b; the contract version is unchanged (the addition is backwards
  compatible).
- **Wi-Fi bars on PCs.** The sysfs Wi-Fi lookup now finds the wireless interface instead of assuming `wlan0`, so the
  bars are no longer grey on PCs whose interface is named like `wlpXsY`.
- `check_statusbar_drift.py`: Mesh Hop is off the copy list. The check still fails on viz1090's stale copies; that is
  fixed in the apps repository.

**Apps do not change with this release.** Published apps keep their old status strip until they are rebuilt at the
v0.8.5 pin.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.5-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels, then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.4:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Built from the `release-0.8.5` branch (main c1967d8 plus the release docs), version 0.8.5,
channel `release`.
<!-- branding-allow-end -->

**Verified:** unit tests in WSL; builds for arm64 and amd64 with 0 warnings; installed on the HackBerry CM5 and the
FZ-M1 tablet, where `status.state` is written with correct values.

**Known limits**

- Not verified: apps reading the feed (the apps need a re-pin and rebuild, next step), and the Wi-Fi lookup fix on the
  tablet inside an app.
- Still true from 0.8.4: see the 0.8.4 notes' known limits.
