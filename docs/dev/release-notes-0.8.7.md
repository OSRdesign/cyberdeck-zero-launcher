**New since v0.8.6** (pull request #92, c-7: media-key devices, Windows key, sound-player fix)

- **Volume, mute and brightness keys from devices that are not keyboards.** The launcher also reads input devices
  that carry these keys but cannot type: on the FZ-M1 "Panasonic Laptop Support", "Video Bus" and "Intel Virtual
  Buttons". Only the five media keys are taken from them. They never count as a keyboard (`input.state` is unchanged),
  are never grabbed, and `APPLAUNCH_KBD_IGNORE` is honoured. A press seen on two devices counts once (80 ms).
  APP-CONTRACT section 14.
- **Boards where new nodes start being read:** the HackBerry "Consumer Control", the AVRCP node of a connected
  Bluetooth speaker, and the consumer node of the deck's 2.4 GHz keyboard (only while it is plugged in).
- **Windows key opens the app switcher.** A double tap (second press within 450 ms) or a hold of 600 ms or more. The
  first tap is Home as before. The FZ-M1's Windows button is a 1 ms pulse, so on that tablet the double tap is the
  way. Win+F14 is reserved. APP-CONTRACT sections 5 and 13.
- **Fix: sound-player startup race.** The launcher could die with SIGSEGV at start when the sound worker thread ran
  before the player was ready. It was in v0.8.6 too and could hit any board; with c-7's timing it hit the tablet every
  time.

**FZ-M1 notes** (settings of the tablet, not part of the bundle)

- The power key suspends through a logind drop-in: `HandlePowerKey=suspend`, `HandlePowerKeyLongPress=poweroff`. It
  was applied by hand; the owner checked standby and wake.
- BIOS: the volume buttons are set to volume (before they sent brightness keys). In mode "Application" the A button
  and the rotation lock send nothing. In mode "Rotation lock" the rotation lock sends Win+F14, which the launcher does
  not use.

**Apps do not change with this release.**

**Install** (Raspberry Pi OS Lite 64-bit, Debian 13 trixie, the screen overlays already in `config.txt`; or an x86_64
PC with the amd64 bundle):

```
tar xzf cyberdeckzero-launcher-0.8.7-arm64.tar.gz && cd bundle      # amd64 on a PC
sudo ./install.sh --keyboard-name "<name from bluetoothctl devices>"   # on a PC: sudo ./install.sh
```

On a Pi 5 with the HyperPixel 4, add `--pwm-backlight` for brightness levels, then reboot.

<!-- branding-allow-begin: credit for the bundled Store -->
**Upgrading from 0.8.6:** run the same command; settings, app sources and installed apps carry over. To preview:
`sudo ./install.sh --dry-run`. To remove: `sudo ./uninstall.sh`. The bundle includes a prebuilt Store from
CardputerZero/Store. Built from the `release-0.8.7` branch (main 47bde0b plus the release docs), version 0.8.7,
channel `release`.
<!-- branding-allow-end -->

**Verified:** unit suites including ASan/UBSan: switch_keys (444 checks), supervisor_host (377), media_keys (225),
keyboard presence and the Esc watcher; the new sound-player test crashes the old code 20 runs out of 20. Builds for
arm64 and amd64 with no new warnings. The amd64 launcher was run in a harness with the tablet's real device list. On
the FZ-M1 the owner checked the volume buttons and the double tap.

**Known limits**

- Not verified on boards: the HackBerry and the deck with the new devices, and the `install.sh` and `uninstall.sh`
  paths.
- Not done: the power-key drop-in is not in `install.sh` yet.
- Brightness keys exist only on the HackBerry.
- Still true from 0.8.6: see the 0.8.6 notes' known limits.

**Next**

- `install.sh` writes the power-key drop-in for the PC profile.
- The PDA profile (report 038).
- Launcher themes (colours); an idea, no study yet.
