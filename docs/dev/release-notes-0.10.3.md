**New since v0.10.2** (pull request #177: the installer lets the launcher user mount removable drives through udisks without a password)

- **Apps can mount removable drives without a password.** The launcher and its apps run in a background login
  session, which polkit does not treat as the active seat, so `udisksctl mount` was refused for them ("session not
  allowed to mount drives"). `install.sh` now writes `/etc/polkit-1/rules.d/53-cyberdeckzero-udisks.rules` for the
  user it installs for. The rule allows two actions only, `org.freedesktop.udisks2.filesystem-mount` and
  `filesystem-mount-other-seat`. Every board that installs or updates to v0.10.3 gets it. The uninstaller removes it,
  the bundle and the update helper's backup list carry it, so Settings > Updates installs it and a rollback restores
  it. A rule added by hand on the Pi 4B does the same.
- **What it does not do.** It does not mount anything by itself, and it does not install udisks2: Raspberry Pi OS
  Lite images may lack it, desktop images have it. Fixed disks, encryption and every other udisks action are
  unchanged. An app mounts a drive with `udisksctl mount -b <device>` (APP-CONTRACT.md, "Removable drives (udisks)").
  Vinyl 0.3.0, published in the apps repository on 2026-11-14, is the first app that uses it: it mounts a plugged-in
  USB drive on request, has Eject, and unmounts at exit.
- **Known limits:** only boards updated to v0.10.3 have the rule, so a board on v0.10.2 or older still refuses the
  mount until it is updated. Where udisks2 is missing, an app shows its own message.
- **Updating:** the same as for v0.10.2. Boards on v0.8.13 or later with a release build update from Settings >
  Updates. Boards on older builds or dev builds need one install by hand (`install.sh` from the bundle).

<!-- launcher-notes-end -->

**Installing this version:** boards on v0.8.13 or later can update from Settings > Updates (release builds only).
Boards on v0.8.12 or older need one install by hand (`install.sh` from the bundle, below), because the installed
`install.sh` of those versions cannot start a self-update. Later updates can be done from Settings > Updates.

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle). Check the download against its `.sha256` asset first:

```
sha256sum -c cyberdeckzero-launcher-0.10.3-arm64.tar.gz.sha256
tar xzf cyberdeckzero-launcher-0.10.3-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels (a Pi 5 with the official 7-inch DSI
panel is detected by itself). For the portrait PDA
profile on the Pi 3A+, add `--board pi3a-wsdsi43-portrait`. On a Zero 2 W with the Waveshare 3.5" SPI panel, add
`--board pi3a-luckfox35` (it is detected as the deck otherwise); the install sets up the PWM backlight. Then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.13 or older:** run the same command; settings, app sources and installed apps carry over. To
preview: `sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
our fork [cyberdeck-zero-store](https://github.com/OSRdesign/cyberdeck-zero-store) of CardputerZero/Store (one change:
apt without recommends). Version 0.10.3, channel `release`.
<!-- branding-allow-end -->
