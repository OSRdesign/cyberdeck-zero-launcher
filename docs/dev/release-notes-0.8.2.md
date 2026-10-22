**New since v0.8.1** (pull requests #77 to #79; the apps repository has its own pull requests #82 and #83)

- **Apps for x86 PCs (amd64).** Settings > Apps on a PC reads the apps repository's `registry-amd64.json`, and the
  default source the installer seeds names it; arm64 boards keep reading `registry.json`, unchanged. Five apps are
  published for amd64, at the same versions as the arm64 ones: System Monitor 0.2.5, Serial Console 0.4.4, Mesh Hop
  0.5.2, RSS Reader 0.1.2 and Net Toolbox 0.4.2. They need glibc 2.38 or newer, like the launcher. The two
  CardputerZero hub sources (arm64 packages only) are not seeded on a PC.
- **Fix: apps would not install on a board set to another language.** Settings > Apps answered "Could not prepare:
  unable to determine current Debian package state" on a Raspberry Pi 5 set to French, because the Store backend reads
  dpkg's English messages. The launcher now runs the backend in the C locale (#79).
- Developer tooling: `boards.json` lists the FZ-M1 tablet as a desktop board (#77).

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.2-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.1:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Built from the `release-0.8.2` branch (main 547fed3 plus the release docs), version 0.8.2,
channel `release`.
<!-- branding-allow-end -->

**Tested by the owner** (physical tests): on the FZ-M1 tablet, Settings > Apps lists the amd64 apps and installs
them (test build 0.8.1-xc.1 of the same code; not every app was opened); the four arm64 apps rebuilt for 0.8.1 on the deck. The
Pi 5 fix was installed as a test build; the final 0.8.2 build was checked by the automatic suite (8 steps).

**Known limits**

- **A PC installed with 0.8.1 keeps its old app source** (`registry.json`, the arm64 apps): the installer leaves an
  existing source list alone. Fix by hand: Settings > Apps > Sources, remove the OSRdesign/cyberdeck-zero-apps entry
  and add `OSRdesign/cyberdeck-zero-apps` again (on a PC that now names `registry-amd64.json`).
- The Store adds its own CardputerZero hub to the source list, also on a PC; it lists no apps.
- System Monitor's and Net Toolbox's summaries still say "Raspberry Pi" and "Pi Zero 2 W" in the amd64 registry.
- amd64: no battery in the status strip yet, the tablet's touch panel reports at most 2 contacts (no three-finger
  gesture; Alt+Tab and the clock long-press work), viz1090 is not built for amd64.
- Still true from 0.8.1: the installer does not restart a running launcher on a desktop board; viz1090 is not
  multitasking-capable; the Pi 3A+ DSI touch can stay dead after a soft reboot.
