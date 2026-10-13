# tools/boards: deploy and test on the boards

`board.py` (Python 3 + paramiko; Pillow for `grab`) talks to the boards in `boards.json` over ssh. Run it on the
Windows dev machine with `C:/` style paths. The password comes from the `PW` environment variable (`PIPW` is
accepted too); it is never stored, never on a command line (sudo reads it on stdin) and never printed.
`--dry-run` prints every command and connects to nothing.

```
python board.py upload     pi3a C:/path/pizero2w-bundle.tar.gz   # upload, unpack to /tmp/applaunch-bundle, stage ~/applaunch-test
python board.py install    pi3a [--check] [--force]              # sudo install.sh --user <user> (--check = install.sh --dry-run)
python board.py test-start pi5 --env APPLAUNCH_OSK=force
python board.py grab       pi5 C:/tmp/pi5.png [--raw]            # framebuffer, turned the way the user sees it
python board.py test-stop  pi5                                   # restore desktop / service, remove test files
python board.py status     deck
```

| Board | Host | Kind | Framebuffer | Turn for the screenshot |
| --- | --- | --- | --- | --- |
| deck | zero7.local | service | /dev/fb0 640x480 32 bpp | none |
| pi3a | 192.168.68.170 | service | /dev/fb1 320x480 RGB565 | 90 deg CCW |
| pi5 | 192.168.68.175 | desktop | /dev/fb0 480x800 32 bpp | 90 deg CCW |
| hackberry | 192.168.68.214 | desktop | /dev/fb0 720x720 32 bpp | none |

- **service** boards run the installed launcher as the user service `APPLaunch.service`. `test-start` stops it and
  runs the staged launcher from `~/applaunch-test` with the board's `test_env`; `test-stop` kills it, removes the test
  files and restarts the service detached (`setsid nohup systemctl --user restart ... &`). `status` also shows
  `applaunch-console.service` and whether the framebuffer text console is bound (`bind=1`) or not (`bind=0`).
- **desktop** boards (Raspberry Pi OS desktop: lightdm + labwc own the screen). `install` puts the desktop variant
  there (install.sh detects the display manager: `applaunch-desktop.service`, the menu entry "CardputerZero Launcher",
  nothing starts at boot; see `projects/APPLaunch/pizero2w/README.md`, "Desktop boards"); no `--force` needed.
  `test-start` stops `applaunch-desktop.service` if it runs, stops lightdm, links `/usr/share/APPLaunch` to
  `~/applaunch-test` (only when nothing real is there, so not after an install) and runs the staged launcher;
  `test-stop` removes that link, starts lightdm again and removes the test files. `status` shows lightdm, the unit and
  the text console.
- `install --force` passes `--force` to install.sh (an unknown board without a profile, or replacing a hand-edited
  `/etc/applaunch/board.conf`). Other install.sh options: `--install-arg=--no-default-source` (with `=`).
- **Pi 3A+:** a command string containing `environ` drops the ssh connection; `board.py` refuses to send one (quirk
  `no_environ` in `boards.json`), before anything on the board is stopped.
- **Pi 3A+ `/tmp` is a 208 MB tmpfs.** The bundle is about 54 MB and `upload` unpacks it there, so an earlier bundle or `/tmp/applaunch-bundle` left behind makes `upload` fail with "No space left on device" (or paramiko `OSError: Failure`). Remove the old files (`rm -rf /tmp/applaunch-bundle /tmp/applaunch-upload.tar.gz /tmp/b05 /tmp/bundle*.tar.gz`) and upload again.
- Never `pkill -f` over ssh (kills the ssh shell); `board.py` uses `pkill -x M5CardputerZero`.
- `python -I board.py -h` and `--dry-run` need nothing beyond the standard library. For a real run paramiko must be
  importable: if it is in the user site-packages, run without `-I`.
- Only the device operator (the Controller or the user) touches boards; workers hand off a test plan.
