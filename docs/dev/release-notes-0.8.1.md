**New since v0.8.0** (pull requests #69 to #75)

- **x86 PCs (amd64).** The launcher now also runs on x86_64 PCs with a touch panel and no display server (report 028).
  Reference machine: the Panasonic FZ-M1 mk2 tablet. The release carries a second bundle,
  `cyberdeckzero-launcher-0.8.1-amd64.tar.gz`; `install.sh` detects a PC, sizes the screen from its EDID, finds the
  backlight (`sysfs:`) and the touch panel, and ignores the tablet's non-keyboard input bus so the on-screen keyboard
  shows. It needs glibc 2.38 or newer (Debian 13, Ubuntu 24.04 or later, Armbian on them), NetworkManager and BlueZ.
  The installer adds `polkitd` and `bluez` when they are missing (reboot and shutdown from Settings need polkit).
  Apps for amd64 come later; the Store tile is hidden on a PC until then.
- **A desktop on the same machine.** If the PC has a desktop (for example XFCE with lightdm), `install.sh` installs the
  desktop variant: the launcher takes the screen from the display manager, Settings gets **Quit to desktop**, and a
  hardware keyboard behaves as on a normal PC. Re-running the installer switches between the two variants (#74).
- **Time zone picker in Settings** (#71), independent of the network time setting: region, then city.
- **The same Settings options on every board** (#72). About (with the launcher version) is on every board and the
  rows no longer depend on the board name.
- **HackBerry CM5 pad:** pointer navigation ignores a single touch of the pad and relies on its physical button (#73).
- **Storage page** works on a PC's SSD and not only on an SD card (#70); the Pi 3A+ DSI backlight dims and wakes
  again (#70).
- **Upstream decoupling** (#75): the launcher no longer carries code, CI or docs of the project it was derived from
  that it does not use (LoRa, the ExtPort, Developer and Software rows, upstream demo projects). MIT notices and credits are kept, and so are the names the Store, the apps
  and the app contract depend on.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.1-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.0:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Built from the `release-0.8.1` branch (main b996720 plus the release docs), version 0.8.1,
channel `release`.
<!-- branding-allow-end -->

**Tested by the owner** (physical tests): the Panasonic FZ-M1 tablet (amd64: install, touch, brightness, Wi-Fi,
Bluetooth, reboot and shutdown, time zone, Storage, desktop variant with XFCE and lightdm), the deck and the Pi 3A+
DSI (dim and wake), and the HackBerry CM5 (pointer navigation with the pad button). The final 0.8.1 build itself was
checked by the automatic suite (8 steps); the boards ran the test builds of the same changes.

**Known limits**

- amd64: no apps built for it yet and the Store tile is hidden; the tablet's touch panel reports at most 2
  contacts, so the three-finger gesture does not work there (Alt+Tab and the clock long-press do).
- HackBerry CM5: same 2-contact limit; its first-run hint still says "Hold three fingers".
- The installer does not restart a running launcher on a desktop board; start it again from the desktop after an
  upgrade.
- Still true from 0.8.0: viz1090 is not multitasking-capable; Net Toolbox stays a legacy app; the Pi 3A+ DSI touch
  can stay dead after a soft reboot (a full power-off and on fixes it).
