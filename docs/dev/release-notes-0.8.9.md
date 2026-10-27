**New since v0.8.8** (pull request #102: the PDA mode setting, Settings > Screen > PDA mode)

- **PDA mode setting, Settings > Screen > PDA mode.** Three values: **Off**, **On rotate left** and **On rotate right**.
  The entry is shown on panels where portrait fits: a short side of at least 480 px and room for the writing zone. It
  is not shown on square panels such as the HackBerry 720x720, nor on the deck. It makes the PDA mode (portrait, with
  the handwriting band) a choice for the user on any compatible screen, not only on the Pi 3A+ / Pi 4 B install
  profile: the 7-inch tablet, for one, runs 800x1280 portrait. The band stays one third of the height on every panel.
  The stored choice wins over `board.conf` and over the install profile, so a plain `install.sh` does not revert it;
  the `pi3a-wsdsi43-portrait` profile stays the "PDA by default" install choice. A change asks "Restart the
  launcher?" (it closes the open apps), then the launcher restarts (exit code 76; both service files restart on it;
  the desktop variant keeps the screen).
- **Apps no longer inherit the PDA flag.** The launcher now passes `APPLAUNCH_PDA=0` to the apps it starts.

## The setting in pictures

Renders made by the launcher's own render harness, not photographs of the board.

| Settings > Screen > PDA mode | Restart question |
| --- | --- |
| ![PDA mode choice](../images/pda/pda-mode-choice.png) | ![Restart the launcher? dialog](../images/pda/pda-mode-dialog.png) |
| Off, On rotate left, On rotate right | A change asks first; open apps are closed |

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.9-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.8:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.8.9, channel `release`.
<!-- branding-allow-end -->

**Verified:** `tools/dev/check-all.sh` passes, with 383 install tests and 14 render goldens at 480x800 and 800x1280.
The setting was tried by the owner on the 7-inch tablet (FZ-M1): all three values and the restart.

**Known limits**

- Not measured: "On rotate right" on a DSI board, and pen accuracy in the tablet's portrait mode.
- Still true from 0.8.8: see the 0.8.8 notes' known limits.
