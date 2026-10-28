**New since v0.8.9** (pull requests #105 and #106: the sudo password prompt in PDA mode, and executable scripts in git)

- **PDA mode: the sudo password prompt gets the handwriting band.** The prompt is a dialog on the top layer, and it was
  the one text field without an input method in PDA mode (portrait). Before, there was nothing to type with on the
  password prompt of Settings > Apps install, update and remove, of Time zone when PolicyKit refuses, and of the RTC
  step. Now it gets the handwriting band like every other text field. Landscape was not affected.
- **Scripts are executable in git.** 28 scripts that start with a shebang are now marked executable in git (mode only,
  no content change), so a native Linux checkout builds without a `chmod` step. Installed bundles are not affected.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.10-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.9:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.8.10, channel `release`.
<!-- branding-allow-end -->

**Verified:** `tools/dev/check-all.sh` passed for #106: the 196 render goldens are identical, and the new `zone_sudo`
scene (the password prompt in PDA mode) at 480x800 and 800x1280 fails without the fix. The fixed prompt was installed
on a Pi 4 B as a test build; it was not otherwise tested on boards.

**Known limits**

- The small 320x170 legacy prompt has no text field, so it gets no band.
- Apps that ask for sudo in their own process were not examined.
- Still true from 0.8.9: see the 0.8.9 notes' known limits.
