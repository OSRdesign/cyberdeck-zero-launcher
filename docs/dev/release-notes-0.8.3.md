**New since v0.8.2** (pull request #81; the apps repository has its own pull requests #84 and #85)

- **Battery on any board that has one.** The launcher reads a Linux `power_supply` battery at run time (the FZ-M1
  tablet's ACPI `BAT1`): the status strip shows the charge and a level icon (green while charging), and Settings >
  Battery (native Settings, after Bluetooth) opens a read-only table: charge, status, time left or to full, power,
  voltage, health. Boards without a battery (the Pi boards) show nothing new; the bq27 gauge block and its calibration
  page are unchanged. The low-battery auto-shutdown flow is not touched.
- **System Monitor 0.2.6, published for arm64 and amd64.** On a PC it shows the DMI model and the CPU name, picks the
  CPU temperature from the package sensor, and adds a battery tile and rows (percent, state, power, time left or to
  full). The Pi-only rows (firmware flags, `vcgencmd`) stay on the Pi.
- Apps get the new status strip only when they move their launcher pin.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.3-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.2:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Built from the `release-0.8.3` branch (main 8feeaec plus the release docs), version 0.8.3,
channel `release`.
<!-- branding-allow-end -->

**Tested by the owner** (physical tests): the battery test build 0.8.2-xd.1 of the same code is installed on the
FZ-M1 tablet; System Monitor 0.2.6 (arm64) is installed on the deck. The final 0.8.3 build was checked by the
automatic suite (8 steps).

**Known limits**

- **A PC installed with 0.8.1 keeps its old app source** (`registry.json`, the arm64 apps): the installer leaves an
  existing source list alone. Fix by hand: Settings > Apps > Sources, remove the OSRdesign/cyberdeck-zero-apps entry
  and add `OSRdesign/cyberdeck-zero-apps` again (on a PC that now names `registry-amd64.json`).
- The Store adds its own CardputerZero hub to the source list, also on a PC; it lists no apps.
- Charging is shown by colour only (no bolt icon); only the first battery is shown; the amd64 System Monitor 0.2.6 has
  not been run on the tablet.
- Net Toolbox's summary still says "Pi Zero 2 W" in the amd64 registry. The tablet's touch panel reports at most 2
  contacts (no three-finger gesture; Alt+Tab and the clock long-press work). viz1090 is not built for amd64.
- Still true from 0.8.1: the installer does not restart a running launcher on a desktop board; viz1090 is not
  multitasking-capable; the Pi 3A+ DSI touch can stay dead after a soft reboot.
