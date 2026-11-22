**New since v0.10.7** (pull requests #200 to #203: a network share helper for apps; the board probe and its fixtures; installer findings from real boards)

- **Network share helper for apps (#203).** The launcher now ships a small root helper,
  `/usr/libexec/cyberdeckzero/cyberdeckzero-share`, that lets an app add and remove a Windows/Samba (SMB) network share.
  Verbs: `version`, `list`, `add`, `remove`. Apps call it through `sudo`, so the user types their password at every add
  and remove; there is no sudoers rule. `list` needs no password.
  - **What it does:** after a test mount it makes read-only systemd mount and automount units under
    `/mnt/cyberdeck/<name>` (mounted on first use, again after a reboot). A share that needs a login (or that refuses
    the guest) answers `denied`.
  - **Install:** `install.sh` installs the helper and `cifs-utils` on every board (it warns and carries on if the
    package cannot be installed). `uninstall.sh` leaves the shares alone; `sudo ./uninstall.sh --remove-shares` removes
    them too.
  - **First app to use it:** Vinyl 0.8.0 (coming, not yet published). Developers: `docs/APP-CONTRACT.md`, "Network shares (SMB)".
  - **Known behaviour:** a share does not unmount when idle; with a dead server, reads fail after 20 to 45 seconds and
    the mount recovers by itself.
  - **Tested** on a real Pi (add a guest share, the error answers, remove, automount) with the helper copied by hand.
    **Not yet tested:** delivery by `install.sh` on a board.
- **Installer findings from real board fixtures (#202).** `install.sh --dry-run` lacked a closing parenthesis on the
  "boot config: none on a desktop board" line. A Pi Zero 2 W with a connected SPI panel (320x480, `panel-mipi-dbi`
  framebuffer) is now detected as `pi3a-luckfox35`; a Zero 2 W without one stays the deck, also when a DSI or DPI panel
  is connected. The dry run and the real run now say that a 4.3", 5" or 7" DSI panel cannot be told apart and which size
  is assumed (unless `--screen-size` was given).
- **Developer tool: `launcher-probe` (#201).** `tools/boards` has a read-only board and screen probe with
  `--capture-fixture DIR`, which snapshots what the installer's detection reads; nine real boards are kept as test
  fixtures. No change on a board.
- **Roadmap (#200):** the FZ-M1 power key is marked released in v0.10.7. No user-visible change.
- **Upgrade notes:**
  - Boards on v0.8.13 or later with a release build update from Settings > Updates, as before. The share helper is
    installed when `install.sh` of this bundle runs. Nothing else changes.
  - Boards on older builds or dev builds need one install by hand.

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.8-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.8-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels (a Pi 5 with the official 7-inch DSI
panel is detected by itself). For the portrait PDA
profile on the Pi 3A+, add `--board pi3a-wsdsi43-portrait`. On a Zero 2 W with the Waveshare 3.5" SPI panel, add
`--board pi3a-luckfox35` only if it is not detected by itself; the install sets up the PWM backlight. On any Raspberry Pi with the Luckfox or Waveshare 3.5" SPI panel (a Pi 5 too), run `sudo ./install.sh --spi-panel luckfox35`, reboot, then run `install.sh` once more. A board whose
DSI panel is not the size of its profile (for example a 5" panel on a Pi 3A+): add `--screen-size 5`. A screen with no
profile: `--board generic`. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
our fork [cyberdeck-zero-store](https://github.com/OSRdesign/cyberdeck-zero-store) of CardputerZero/Store (one change:
apt without recommends). Version 0.10.8, channel `release`.
<!-- branding-allow-end -->
