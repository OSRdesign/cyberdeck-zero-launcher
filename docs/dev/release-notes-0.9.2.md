**New since v0.9.1** (pull requests #128, #129 and #130: apt updates in Settings > Updates, apps that fit the screen)

- **Settings > Updates > Packages: the pending apt updates.** A new Packages page lists them in groups: Safe to
  upgrade, Launcher libraries, Kernel, firmware and boot, and Needs the terminal. Refresh lists asks for sudo and
  shows how old the lists are. The Store's own apps are never offered as Debian upgrades. The App updates row opens
  Settings > Apps > Installed.
- **Upgrade from the screen.** Upgrade N packages asks for a confirmation, then shows the progress (Cancel works
  during the download) and a result page. The Launcher libraries switch is off by default and warns before use.
  Kernel, firmware, bootloader and EEPROM updates stay in the terminal. The launcher never reboots by itself: it
  says when a reboot is advised. It refuses on low battery (under 50 percent and not charging), under-voltage, low
  disk space, held packages, or when apt would do more than the list shows.
- **A small anchor package.** install.sh now installs cyberdeckzero-runtime, which keeps apt from removing the
  libraries the launcher needs. uninstall.sh removes it.
- **Every long wait shows a box** with a progress bar and an elapsed timer, so a slow board never looks frozen.
- **Settings > Apps lists only apps that fit the screen.** A 480x320 board hides the 640x480 apps. Installed apps
  always stay listed. The app page shows Screen (Landscape, or Landscape and portrait) and Minimum size.
- **Updating:** boards on v0.8.13 or later with a release build update from Settings > Updates. Boards on older
  builds or dev builds need one install by hand.
- **Known limits:** the apt upgrade has been run on a Pi 4B only. Slow boards (Zero 2 W) take
  longer: the plan step took 18 to 31 seconds on a Pi 4B. A package whose upgrade pulls in launcher libraries moves
  to the Launcher libraries group. Apps from the Hub source are not filtered by screen (they carry no size). The
  filter reads the Store's cached app lists, so new apps appear after a Sync.

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.9.2-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels. For the portrait PDA profile on the
Pi 3A+, add `--board pi3a-wsdsi43-portrait`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Version 0.9.2, channel `release`.
<!-- branding-allow-end -->
