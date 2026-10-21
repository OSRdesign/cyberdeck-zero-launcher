# Dev guide (for the agents and for humans)

Read this first. It holds the facts every agent needs, so nobody has to rediscover them.

## The two repositories

| Repo | Local path | GitHub | Holds |
| --- | --- | --- | --- |
| launcher | `C:\CLAUDE\zero7\launcher` | `OSRdesign/cyberdeck-zero-launcher` (remote `github`, branch `main`; changes go through a short-lived branch and a pull request) | The launcher (C++/LVGL/SCons), the Pi bundle and installer, generic capabilities, the dev docs |
| apps | `C:\CLAUDE\zero7\cyberdeck-zero-apps` | `OSRdesign/cyberdeck-zero-apps` (remote `origin`, branch `main`) | One folder per app under `apps/<id>/` (`app.json`, `icon.png`, `root/` tree, optional `src/` and `build/`), `tools/` (pure Python `build_deb.py`, `make_registry.py`), `packages/`, `registry.json` |

**The rule:** apps live in the apps repo and are installed from the deck's Settings > Apps. The launcher only gets
*generic* capabilities (for example `X-Fullscreen=true` in a `.desktop` file, or the shared top-bar renderer).
Never put app-specific code, UI or icons into the launcher. If an app needs something the launcher lacks, file a
request with the Controller.

## The deck (the only physical device)

- Raspberry Pi Zero 2W, Raspberry Pi OS Lite 64-bit (Debian 13), Waveshare 2.8" DPI touch LCD (`/dev/fb0` is
  640x480, 32 bpp), Bluetooth keyboard, RTL-SDR dongle on the OTG port. Host `zero7.local`, user `osrde`.
- The launcher runs as the user service `cyberdeckzero.service` (`systemctl --user ...`; `APPLaunch.service` before v0.7.0). Installed apps are
  `.deb` packages; their `.desktop` files are in `/usr/share/APPLaunch/applications/`.
- Passwords are never written to a file, a commit or a prompt that is saved: the Controller gives the password to
  the device operator through the `PIPW` environment variable.
- Helpers: `docs/dev/deck/deck.py` (run, sudo, put, get, install, shot); `docs/dev/deck/check_statusbar_drift.py` (no deck needed: are the apps' copies of the top bar identical to the launcher's). **Only the device operator touches the deck.**
- Screens: the launcher draws two displays. Legacy apps run in a 320x170 window scaled 2x with a toolbar; a
  full-screen app (`X-Fullscreen=true`) owns all 640x480. The top bar (clock, Wi-Fi, Bluetooth) is one shared
  renderer (`ext_components/cp0_lvgl/.../cp0_statusbar.*`); use it, never redraw your own.
- Other boards have other panels: an app learns its mode, canvas, the panel's size, rotation, density and the
  keyboard rule from the app contract (`docs/APP-CONTRACT.md`; test app `tools/dev/app-contract-probe`).

## Building

All builds run in WSL (Ubuntu) because the launcher cross-compiles to aarch64. **One build at a time**: wrap every
build in `flock /tmp/wsl-build.lock ...`. Use your own git worktree so build directories never collide.

```
wsl -e sh -c 'cd /mnt/c/CLAUDE/zero7/launcher/projects/APPLaunch && export PATH=/mnt/c/CLAUDE/zero7/launcher/.venv-pizero2w/bin:$PATH \
  APPLAUNCH_HW=pizero2w CONFIG_REPO_AUTOMATION=y CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk && scons -j$(nproc)'
```
The binary is `dist/cyberdeckzero`. An LVGL app project builds the same way and produces `dist/<Name>`; app sources
live in the apps repo (`apps/<id>/src`, built in a scratch project under `projects/`: native apps by `apps/<id>/build.sh`
through `apps/_common/build_app.sh` (`host`, `cross`, `all`, `--strict`, `--sync`; pinned to the launcher commit in
`apps/_common/LAUNCHER_REF`), the older apps by `apps/<id>/build/build.sh`). The full bundle is `projects/APPLaunch/pizero2w/build.sh --with-store`.

Packages: write `apps/<id>/app.json`, put the files in `apps/<id>/root/` exactly as they must land on the device,
then `python tools/make_registry.py --owner OSRdesign --repo cyberdeck-zero-apps` (builds every `.deb` and
`registry.json`). A local `.deb` of one app (a draft too), without touching `packages/` or `registry.json`:
`--only <id> --out <folder>`. Publishing puts the tested `.deb` in `packages/` and writes the registry with
`--no-build`, so only that app's entry changes. Check a package with `dpkg-deb -I` and `dpkg-deb -c` in WSL.

## Build, test, deploy (launcher)

**LVGL is 9.5.0** (`CONFIG_V9_5_LVGL_VERSION_*` in the build's `build/config/global_config.mk`, sources in
`SDK/github_source/lvgl/lvgl_9_5`): use the 9.5 API.

- `tools/dev/wsl-build.sh`: syncs your checkout into its own WSL tree (`~/zero7/<checkout folder>`), builds the Pi
  bundle under the build lock, prints the tail of the log and the bundle path. `tools/dev/check-all.sh`: unit tests,
  render, compare, twice, header, one PASS/FAIL line each. Details and prerequisites: `tools/dev/README.md`.
- `tools/boards/board.py` (`boards.json`): upload, install, test-start / test-stop, grab, status on the deck, the
  Pi 3A+, the Pi 5 and the HackBerry, with each board's quirks built in (`--dry-run` prints the commands). Only the
  device operator runs it against a board. See `tools/boards/README.md`.
- Building a native Settings page: `docs/dev/native-settings-page-howto.md`.

**Definition of done for a launcher UI task** (before the hand-off report):

1. scons build green (`tools/dev/wsl-build.sh`), no new warnings in the files you touched;
2. unit tests green (`ext_components/cp0_lvgl/tests/run_tests.sh`, `projects/APPLaunch/tests/run_tests.sh`), new
   logic covered by a test registered there;
3. compat goldens 640x480 and 480x320 byte-identical (`run.sh compare`), unless the task is a deliberate baseline
   change agreed with the Controller;
4. `run.sh twice` identical; `run.sh header` passes;
5. new UI: a harness scene, rendered at the five sizes 480x320, 640x480, 800x480, 720x720, 1280x720 (contact sheet in
   the report);
6. docs and the file map updated;
7. no commit and no push by workers.

Steps 2-4 are one command: `tools/dev/check-all.sh`.

## Gotchas that cost hours (all learned the hard way)

- The shell tool mangles backslash escapes inside heredocs (`\n`, `\b`, quotes). Write files with the Write/Edit
  tools, or build such strings from byte values (`bytes([92, 98])`). Check the result when it contains escapes.
- Windows Python needs `C:/...` paths; Git Bash `/c/...` paths do not work there.
- Never `pkill -f` over ssh (it kills your own shell): use `pkill -x <name>`.
- `sudo` over ssh needs the password (`deck.py sudo` sends it on stdin, never in the command line); `~` inside `sudo sh -c` is root's home: use
  absolute paths.
- A backgrounded remote process must have stdin, stdout and stderr redirected or the ssh call never returns.
- Line endings: the repos use LF (`.gitattributes`); upstream viz1090 uses CRLF, so patch it with `sed`.
- Commit identity: `git -c user.name=OSRdesign -c user.email=oliviersr@gmail.com commit ...` and end the message with
  the `Co-Authored-By: Claude ...` trailer the session was given. Do not rewrite published history.
- The launcher only repaints what changes: anything else writing to `/dev/fb0` leaves residue. The service waits
  for the boot to finish for that reason. The user wants the normal boot lines visible: do not add `quiet` or
  `fbcon=map` to the boot line.
- The Bluetooth keyboard can be asleep (device node missing): nothing may depend on it being present.

## Approval gates (ask the Controller, which asks the user)

`git push`, a release, a change to the boot configuration, services or udev/polkit rules on the deck, installing or
removing system packages, anything that deletes user data, anything that publishes outside the two repos.

## Definition of done (every task)

Builds clean; verified on the deck (screenshots in the report); the Verifier's checklist passes; docs updated;
the user has tried it on the deck; only then commit and push, on the user's go-ahead.
