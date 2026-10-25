# tools/boards: deploy and test on the boards

`board.py` (Python 3 + paramiko; Pillow for `grab`) talks to the boards in `boards.json` over ssh. Run it on the
Windows dev machine with `C:/` style paths. The password comes from the `PW` environment variable (`PIPW` is
accepted too); it is never stored, never on a command line (sudo reads it on stdin) and never printed.
`--dry-run` prints every command and connects to nothing.

```
python board.py upload     pi3a C:/path/cyberdeckzero-launcher-dev-arm64.tar.gz   # upload, unpack to /tmp/applaunch-bundle, stage ~/applaunch-test
python board.py upload     fzm1 C:/path/cyberdeckzero-launcher-dev-amd64.tar.gz   # the PC tablet takes the amd64 bundle
python board.py install    pi3a [--check] [--force]              # sudo install.sh --user <user> (--check = install.sh --dry-run)
python board.py test-start pi5 --env APPLAUNCH_OSK=force
python board.py grab       pi5 C:/tmp/pi5.png [--raw]            # framebuffer, turned the way the user sees it
python board.py test-stop  pi5                                   # restore desktop / service, remove test files
python board.py status     deck
```

| Board | Host | Kind | Framebuffer | Turn for the screenshot |
| --- | --- | --- | --- | --- |
| deck | zero7.local | service | /dev/fb0 640x480 32 bpp | none |
| pi3a | 192.168.68.170 | service | /dev/fb1 320x480 RGB565 (Luckfox 3.5" SPI) | 90 deg CCW |
| pi3a-dsi | 192.168.68.170 | service | /dev/fb0 800x480 RGB565 (Waveshare 4.3" DSI) | none |
| pi4b | 192.168.68.167 | service | /dev/fb0 800x480 RGB565 (Pi 4 B, Freenove 5" DSI) | none |
| pi5 | 192.168.68.175 | desktop | /dev/fb0 480x800 32 bpp | 90 deg CCW |
| hackberry | 192.168.68.214 | desktop | /dev/fb0 720x720 32 bpp | none |
| fzm1 | 192.168.68.183 | service, **amd64** | /dev/fb0 1280x800 32 bpp (Panasonic FZ-M1 mk2 tablet, i915) | none |

- **arch** (`boards.json`, default `arm64`): the bundle a board takes. `upload` refuses a bundle of the other arch:
  by its file name (`-arm64` / `-amd64`), by its launcher's ELF header when the tarball is on this machine, and on the
  board (the payload's ELF header against `uname -m`) right after unpacking. `fzm1` is amd64; its `test_env` is
  install.sh's pc profile. It boots to the text console with no display manager, so it is a service board (until a
  desktop is installed on it: see below).

- **service** boards run the installed launcher as the user service `cyberdeckzero.service` (`APPLaunch.service` up to
  v0.6.0; install.sh migrates it). `test-start` stops it (and `APPLaunch.service` on a board not migrated yet) and
  runs the staged launcher (`bin/cyberdeckzero`) from `~/applaunch-test` with the board's `test_env`; `test-stop` kills
  it, removes the test files and restarts the installed service detached (`cyberdeckzero.service`, else
  `APPLaunch.service`; `setsid nohup systemctl --user restart ... &`). `status` also shows `cyberdeckzero-console.service`
  (and the earlier `applaunch-console.service`) and whether the framebuffer text console is bound (`bind=1`) or not
  (`bind=0`).
- **desktop** boards (a display manager owns the screen: Raspberry Pi OS desktop, lightdm + labwc; or a PC with a
  desktop, lightdm + Xorg with XFCE). `install` puts the desktop variant
  there (install.sh detects the display manager: `cyberdeckzero-desktop.service`, the menu entry "CyberdeckZero
  Launcher", nothing starts at boot; see `projects/APPLaunch/pizero2w/README.md`, "Desktop boards"); no `--force`
  needed. `test-start` stops `cyberdeckzero-desktop.service` (and the earlier `applaunch-desktop.service`) if it runs,
  stops the display manager, links `/usr/share/APPLaunch` to `~/applaunch-test` (only when nothing real is there, so
  not after an install) and runs the staged launcher; `test-stop` removes that link, starts the display manager again
  and removes the test files. `status` shows the display manager, the units, whether labwc or Xorg runs and the text
  console. The display manager is `dm` in `boards.json` (the unit name without `.service`; default `lightdm`, so the
  Pi 5 and HackBerry entries need none).
- **A service board that gets a desktop** (the FZ-M1 after `sudo armbian-config` > Software > Desktops > XFCE): run
  `install` again (install.sh switches to the desktop variant: the user service goes, the desktop unit and the menu
  entry come; `install --check` shows the switch first), then set `"kind": "desktop"` for that board in `boards.json`
  (add `"dm"` only when the display manager is not lightdm: `gdm3`, `sddm`). Until then `test-start` would stop a user
  service that is no longer there and leave the desktop on the screen. Back to `"service"` when the desktop is removed
  and `install` ran again (install.sh switches back).
- `install --force` passes `--force` to install.sh (an unknown board without a profile, or replacing a hand-edited
  `/etc/applaunch/board.conf`). Other install.sh options: `--install-arg=--no-default-source` (with `=`).
- **Pi 3A+ (and Pi 4 B, below):** a command string containing `environ` drops the ssh connection; `board.py` refuses to send one (quirk
  `no_environ` in `boards.json`), before anything on the board is stopped. `pi3a` and `pi3a-dsi` are the same board
  with its SPI panel or its DSI panel (install.sh profiles `pi3a-luckfox35` and `pi3a-wsdsi43`): use the entry of the
  panel that is mounted (the DSI panel since 2026-10-13).
- **Pi 4 B (`pi4b`, install.sh profile `pi4b-dsi`):** the same `environ` quirk as the Pi 3A+ (verified 2026-10-25:
  a command string containing the word in any case, `EnvironmentFile` for one, drops the ssh connection), so its entry
  has `no_environ` too. It logs undervoltage warnings with its current supply.
- **Pi 3A+ `/tmp` is a 208 MB tmpfs.** The bundle is about 54 MB and `upload` unpacks it there, so an earlier bundle or `/tmp/applaunch-bundle` left behind makes `upload` fail with "No space left on device" (or paramiko `OSError: Failure`). Remove the old files (`rm -rf /tmp/applaunch-bundle /tmp/applaunch-upload.tar.gz /tmp/b05 /tmp/bundle*.tar.gz`) and upload again.
- Never `pkill -f` over ssh (kills the ssh shell); `board.py` uses `pkill -x cyberdeckzero` (the whole binary name is
  the process name: 13 characters), and the earlier binary's process name for a board not migrated yet.
- `python -I board.py -h` and `--dry-run` need nothing beyond the standard library. For a real run paramiko must be
  importable: if it is in the user site-packages, run without `-I`.
- Only the device operator (the Controller or the user) touches boards; workers hand off a test plan.
