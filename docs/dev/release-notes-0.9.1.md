**New since v0.9.0** (pull request #126: a wake-from-sleep fix)

- **Fix: the screen after a wake from sleep.** On a tablet or PC that suspends (the Panasonic tablet's power key), the
  launcher now repaints the whole screen after the wake and leaves the clock screen, so the home grid shows at once at
  normal brightness. Before, the old clock screen or nothing stayed until the first touch. The first touch after a wake
  is no longer swallowed. Boards that never suspend are unaffected.
- **Updating:** boards on v0.8.13 or later with a release build can install it from Settings > Updates (v0.9.0 and
  v0.9.1 are the first releases installed that way). Boards on v0.8.12 or older need one install by hand.
- **Known limits:** an external app that owns the whole panel is not repainted after a suspend (this needs a separate
  app SDK event); self-update works on the release channel only; apt updates in the Updates page are not part of this
  release (next).

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.9.1-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.9.1, channel `release`.
<!-- branding-allow-end -->
