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
python board.py grab       pi5 C:/tmp/pi5.png [--raw] [--rotate 90]   # framebuffer, turned the way the user sees it
python board.py test-stop  pi5                                   # restore desktop / service, remove test files
python board.py status     deck
```

| Board | Host | Kind | Framebuffer | Fallback turn for the screenshot |
| --- | --- | --- | --- | --- |
| deck | zero7.local | service | /dev/fb0 640x480 32 bpp | none |
| pi3a | 192.168.68.170 | service | /dev/fb1 320x480 RGB565 (Luckfox 3.5" SPI) | 90 deg CCW |
| pi3a-dsi | 192.168.68.170 | service | /dev/fb0 800x480 RGB565 (Waveshare 4.3" DSI) | 90 deg CCW (PDA portrait) |
| pi4b | 192.168.68.167 | service | /dev/fb0 800x480 RGB565 (Pi 4 B, Freenove 5" DSI) | 90 deg CCW (PDA portrait) |
| zero8 | 192.168.68.184 | service | /dev/fb1 320x480 RGB565 (Pi Zero 2 W, Waveshare 3.5" RPi LCD (F), profile `pi3a-luckfox35`) | 90 deg CCW |
| pi5 (RPI53) | 192.168.68.175 | desktop | /dev/fb0 480x800 32 bpp (Pi 5, Pimoroni HyperPixel 4 DPI, profile `pi5-hyperpixel4`) | 90 deg CCW |
| pi5-dsi7 (RPI52) | 192.168.68.160 | desktop | /dev/fb0 800x480 32 bpp (Pi 5, official 7" DSI Touch Display, `drm-rp1-dsidrmf`, profile `pi5-dsi7`) | none |
| hackberry | 192.168.68.214 | desktop | /dev/fb0 720x720 32 bpp | none |
| fzm1 | 192.168.68.183 | desktop, **amd64** | /dev/fb0 1280x800 32 bpp (Panasonic FZ-M1 mk2 tablet, i915) | 90 deg CCW (PDA portrait) |

- **Board names:** the entries are named by what the user tests (`pi5` is the Pi 5 with the HyperPixel 4", `pi5-dsi7` the
  Pi 5 with the 7" DSI panel); the owner's own names for the two Pi 5 boards are RPI53 (`pi5`) and RPI52 (`pi5-dsi7`).
  `zero8` is a Pi Zero 2 W like the deck, but with the 3.5" SPI panel, so it is a different board from `deck`.
- **grab rotation:** `grab` turns the raw framebuffer by the launcher's *current* rotation, read on the board over the
  same ssh session: `/etc/applaunch/board.conf` (`APPLAUNCH_LOGICAL`, `APPLAUNCH_ROTATE`) and the stored Settings >
  Screen > PDA mode (`"pda_mode"` in `~/.config/cardputerzero/config.json`: 0 Off = the landscape canvas, 1 On rotate
  left = +90, 2 On rotate right = +270, applied as `projects/APPLaunch/main/ui/pda_mode.cpp` does, only on a board
  where the setting is offered). It prints the rotation it chose and why. The table's turn (`rotate` in
  `boards.json`) is only the fallback: no board.conf (the deck), or a result that does not fit the framebuffer's
  orientation. `--rotate 0|90|180|270` forces a value, e.g. for a test launcher started with `test-start --env
  APPLAUNCH_ROTATE=...` (the state read is the installed launcher's, not the test run's environment).

- **arch** (`boards.json`, default `arm64`): the bundle a board takes. `upload` refuses a bundle of the other arch:
  by its file name (`-arm64` / `-amd64`), by its launcher's ELF header when the tarball is on this machine, and on the
  board (the payload's ELF header against `uname -m`) right after unpacking. `fzm1` is amd64; its `test_env` is
  install.sh's pc profile. It has Armbian's XFCE with lightdm since 2026-10-22, so it is a desktop board
  (`"kind": "desktop"`; the section "A service board that gets a desktop" below says how a board is flipped).

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
- **Pi 3A+ `/tmp` is a 208 MB tmpfs.** The bundle is about 30 MB (arm64 since v0.9.0) and `upload` unpacks it there, so an earlier bundle or `/tmp/applaunch-bundle` left behind makes `upload` fail with "No space left on device" (or paramiko `OSError: Failure`). Remove the old files (`rm -rf /tmp/applaunch-bundle /tmp/applaunch-upload.tar.gz /tmp/b05 /tmp/bundle*.tar.gz`) and upload again.
- Never `pkill -f` over ssh (kills the ssh shell); `board.py` uses `pkill -x cyberdeckzero` (the whole binary name is
  the process name: 13 characters), and the earlier binary's process name for a board not migrated yet.
- `python -I board.py -h` and `--dry-run` need nothing beyond the standard library. For a real run paramiko must be
  importable: if it is in the user site-packages, run without `-I`.
- Only the device operator (the Controller or the user) touches boards; workers hand off a test plan.

## launcher-probe

`tools/boards/launcher-probe` is a read-only board and screen probe (report 031, phase P0): one POSIX shell file that
prints ONE plain-text report of what a board looks like to the launcher's installer. It is for a board nobody has
looked at yet, or whose screen is wrong. It needs no package, no `sudo` and no network. (The older
`projects/APPLaunch/tools/board-probe/probe-board.sh` is a Markdown report of the whole system and stays; this one
adds the pieces the installer decides on: EDID size in mm, the device-tree panel nodes, touch ranges and contact
counts, what `install.sh` would pick.)

**Run it on a board** (as the normal user; `sudo` is not needed, and adds only read access to `/dev/input` for the
touch ranges):

```
scp launcher-probe pi@BOARD:/tmp/          # or copy it any way you like; it is one file
ssh pi@BOARD sh /tmp/launcher-probe --out /tmp/probe-BOARDNAME.txt
```

then paste the output (or send `/tmp/probe-BOARDNAME.txt`). About 5 seconds. To let it say what the installer would
pick, run it from a checkout or an unpacked release (it finds `pizero2w/install.sh` in the checkout layout), or give
`--installer PATH/install.sh`; it only ever runs `install.sh --dry-run`.

Options: `--out FILE` (also save the report; the only file it writes), `--installer PATH`, `--edid-hex` (also the EDID
as hex, with the serial bytes blanked), `-h`. Exit status 0 always, 2 for a wrong option.

**What it reads** (11 delimited sections): the device-tree model, compatible, SoC, kernel, OS release, DMI (a PC);
the display session (default target, display manager, compositor processes) and which launcher unit files exist
(`cyberdeckzero.service` user unit, `cyberdeckzero-desktop.service`, `cyberdeckzero-console.service`, and the
pre-v0.7.0 names); every DRM connector (status, modes, the first mode as the preferred one, the EDID decoded: vendor,
product code, basic size in cm, the first detailed timing with its size in mm, the monitor name); every framebuffer
(name, size, bpp, stride, rotate, and the pixel layout and driver mm through `FBIOGET_VSCREENINFO` when `python3` is
there); the kernel log lines about panels and touch; the device-tree nodes of panels, touch controllers and
backlights (compatible, `width-mm`, `height-mm`, `reg`); the display lines of `config.txt` (`dtoverlay`, `dtparam`,
`display_auto_detect`, `rotate` and related; comments left out), the display words of `cmdline.txt`, the
`armbianEnv.txt` overlay lines, the overlays shipped; every input device with its kind, capability bits,
INPUT_PROP_DIRECT, ABS axes and, when `python3` is installed and `/dev/input/eventN` is readable (user in the `input`
group, or root), the ABS ranges and the contact count (`ABS_MT_SLOT` max + 1); every backlight (type, max, level,
whether the user may write it, what the profile logic would call it); the `install.sh --dry-run` verdict; and the
installed `board.conf`, `install.conf`, `board.state` and `screen.state` when present.

Optional tools are used only when present and reported when missing (`unavailable: ... not installed`): `kmsprint`,
`modetest`, `edid-decode`, `fbset`, `vcgencmd`, `udevadm`, `systemctl`, `python3`. Nothing in it needs them.

**What it redacts** (printed as `redacted`): MAC addresses, IPv4 addresses, the host name, the user name, CPU, board
and DMI serial numbers, the EDID serial (number and text), and in the installed files any line mentioning a password,
token, secret, psk or SSID. It never reads Wi-Fi settings, ssh keys or the environment. Look the report over before
pasting it anyway.

**Tests.** `sh tools/boards/tests/test-launcher-probe.sh` (also the `probe` step of `tools/dev/check-all.sh`) runs the
real script against recorded fixture trees and compares the output with golden files, in `sh` and in `bash`.
`PROBE_ROOT=DIR` (test only) makes the probe read `/proc`, `/sys`, `/boot`, `/etc`, `/run`, `/home` and
`/usr/share/APPLaunch` below `DIR`, run no external tool, and take what only a device node can give from
`DIR/probe-fixtures/`: `kernel.log` (the kernel log), `fbN.vinfo` (the text the python helper prints for the pixel
layout) and `eventN.absinfo` (the text it prints for the ABS ranges and slots). `PROBE_MACHINE`, `PROBE_DATE`,
`PROBE_USER` and `PROBE_WORD_BITS` replace `uname -m`, the date, the user name and the kernel's bitmap word size.
The fixtures (`tests/fixtures/<name>/{probe.env,root/}`) are written by `tests/gen-launcher-probe-fixtures.py` and are
committed; to add a board, add a function there, run it, run the test with `UPDATE_GOLDEN=1` and read the golden
file. A real board's report is a fixture candidate: its files go under `root/` as they are.

**Real boards as fixtures (`--capture-fixture`).** `sh launcher-probe --capture-fixture DIR` writes no report: it
snapshots exactly what the probe, and the installer's board detection that the probe runs with `--dry-run`, read
(device-tree model and compatible, DMI, DRM connectors with modes and the first 256 EDID bytes, framebuffers and their
pixel layout, the panel lines of the kernel log, boot configuration, input capabilities and touch ranges, backlights,
unit-file names, the installed profile) into a `PROBE_ROOT`-shaped tree below `DIR`, plus `probe.env`
(`PROBE_MACHINE`, `PROBE_USER`, `PROBE_DATE`, `PROBE_WORD_BITS`, `PROBE_GLIBC`) and `probe-fixtures/` (`kernel.log`,
`fbN.vinfo`, `eventN.absinfo`, `capture-notes.txt`, `links`). `DIR` must be new or empty; nothing else is written, no
root is needed, and nothing is changed on the system. To capture a board over ssh:
`scp tools/boards/launcher-probe USER@BOARD:/tmp/lp`, `ssh USER@BOARD sh /tmp/lp --capture-fixture /tmp/lp-cap`,
`ssh USER@BOARD 'cd /tmp/lp-cap && tar cf - .' | tar xf - -C tools/boards/tests/fixtures/NAME/root`, move `probe.env`
up to `fixtures/NAME/`, then `ssh USER@BOARD rm -rf /tmp/lp /tmp/lp-cap`. Touch ranges need read access to
`/dev/input` (the `input` group): an unreadable one is recorded as the "unavailable" line, which is the real result.

Redaction while copying: MAC and IPv4 addresses are replaced by `redacted`, the host name by `probehost`, the user name
(3 characters or more, not root) by `pilot`; serial numbers are never read (their files are recorded empty), the EDID
serial bytes and serial text are blanked (the checksum byte moves so a good EDID stays good), the name of a Bluetooth
input device is not kept, `config.txt` keeps its line numbers but every comment becomes a bare `#`, `armbianEnv.txt`
keeps a list of non-identifying keys, and any line of the installed files that mentions a password, token, secret, psk or
SSID is dropped. Kept as they are: `cmdline.txt` (PARTUUID, the Wi-Fi regulatory country) and the install options.
Symbolic links cannot live in git on Windows: they are listed in `probe-fixtures/links` and the test makes them in a
temporary copy. The kernel log is stored as the lines the report prints plus filler lines up to the real line count.
Not captured (so a replay differs from the board there): `/etc/netplan` and the NetworkManager checks, the retired
files of earlier bundles in `/usr/share/APPLaunch`, `/usr/bin/wireplumber`.

The `*-real` fixtures are recorded boards (2026-11-21 and 22): `hackberry-real`, `palmpi-dsi5-real`, `pi5-spi35-real`,
`fzm1-real`, `pi4b-real`, `pi3a-real`, `zero8-real`, `deck-real`, `pi5-real`, `opi-zero2w-real` (Armbian), `pi4b-dsi7-real` (installed by hand from the README). The test also checks which board
install.sh picks from each of them.
