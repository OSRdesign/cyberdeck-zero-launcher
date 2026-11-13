# tools/dev: build and check the launcher

Scripts for the Windows dev machine + WSL build box, used by workers and by the Controller. Run them from WSL or
from Git Bash (they re-run themselves in WSL through `wsl.exe`). `-h` prints the usage of each.

| Script | What it does |
| --- | --- |
| `wsl-build.sh [--arch arm64\|amd64] [--with-store]` | Syncs this checkout into its WSL build tree, takes the build lock, builds the bundle with `projects/APPLaunch/pizero2w/build.sh` (arm64 for the Pi boards by default; `--arch amd64` builds natively for PCs in its own tree `~/zero7/<checkout>-amd64` and needs the -dev packages listed by `build.sh -h`). Prints the last 25 build lines, the full log's path, and the bundle's path and size (also as a `\\wsl.localhost\...` path for Windows tools such as `tools/boards/board.py upload`). Exit status = the build's. Environment: `APPLAUNCH_VERSION` (default `dev`), `APPLAUNCH_CHANNEL` (default `development` here, `release` in `build.sh` alone), `LEGACY_BUNDLE_NAME`; set them inside WSL, they do not cross the re-run through `wsl.exe`. A release bundle: [release checklist](../../docs/dev/checklists/release.md), step 5. |
| `check-all.sh` | Syncs, takes the lock, then runs `cp0-unit` (`ext_components/cp0_lvgl/tests/run_tests.sh`), `app-unit` (`projects/APPLaunch/tests/run_tests.sh`), `strokes` (`make -C tools/strokes check`), `render` (`tests/render/run.sh render`), `compare`, `twice`, `header`, `contract` (same script; the app contract probe, `app-contract-probe/`), `branding` (`check-branding.sh` on the checkout): 9 steps. It does not run the installer tests (see [Running the checks](#running-the-checks)). One line per step: `PASS|FAIL <step> (<seconds>s)`; a failing step also shows its last 40 lines. Logs in `/tmp/check-all-<checkout>/`. Exit 1 if any step failed. |
| `check-branding.sh [--allow FILE] [ROOT]` | Fails (exit 1, prints `file:line: text`) when a tracked file names the upstream brand outside the technical names of `NAMING.md`, the `branding-allow-begin/end` blocks (credits) and the paths of `branding-allow.txt` (one glob per line, with the reason). POSIX sh, runs anywhere git does (in WSL it uses `git.exe` for a worktree made on Windows). For the apps repo: copy it to `tools/branding/check-branding.sh` there, with `branding-allow.apps-template.txt` as `tools/branding/branding-allow.txt`. |
| `x86-smoke.sh [--size WxH] [--seconds N] [ENV=VALUE ...]` | Starts the amd64 launcher of `~/zero7/<checkout>-amd64` headless on a virtual framebuffer (the vfb shim on a `/dev/shm` file, 1280x800 by default, the pc profile's environment, its `share/` seen at `/usr/share/APPLaunch` through a private overlay: no root), then prints its display lines, whether it still runs and its peak RSS, and writes a PNG (`/tmp/x86-smoke-<W>x<H>.png`). Exit 0 when it still ran at the shot. |
| `lib.sh` | Shared helpers (sourced): WSL re-exec, build lock, tree sync. |

`wsl-build.sh` and `check-all.sh` use the same recipe as the scripts the earlier sessions ran by hand:
`source ~/zero7/venv/bin/activate`, `APPLAUNCH_HW=pizero2w CONFIG_REPO_AUTOMATION=y
CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk`, everything under `flock /tmp/wsl-build.lock`.

## Running the checks

**`check-all.sh`** runs 9 steps, in this order: `cp0-unit`, `app-unit`, `strokes`, `render`, `compare`, `twice`,
`header`, `contract`, `branding` (the table above says what each one runs). `app-unit` includes the release notes test
(`test_settings_update_model.cpp`, rules in the [release checklist](../../docs/dev/checklists/release.md#release-notes-rules)).

**Not in `check-all.sh`: the installer tests.** `projects/APPLaunch/pizero2w/tests/test-install-board.sh` (board
detection, profiles, whole install, migration, desktop variant, uninstall) and `test-update-helper.sh` (Settings >
Updates' root helper `cyberdeckzero-update` and what `install.sh` gives it) run on their own, in WSL, as a normal user
(`test-update-helper.sh` refuses to run as root), from an **LF copy of the whole repository** (cloned from any local repository that has the commit, for example after
a `git fetch github`):

```
git -c core.autocrlf=false clone -q --no-checkout /mnt/c/CLAUDE/zero7/launcher/.git /tmp/lf && cd /tmp/lf &&
git -c core.autocrlf=false checkout -q <commit>
sh projects/APPLaunch/pizero2w/tests/test-install-board.sh
sh projects/APPLaunch/pizero2w/tests/test-update-helper.sh
```

- LF: the checkouts on Windows have `core.autocrlf=true`, and `.gitattributes` keeps only scripts and unit files LF,
  so the fixtures (fake `/proc` and `/sys` trees, for example `fixtures/zero2w/proc/device-tree/model`) are CRLF
  there (`Raspberry Pi Zero 2 W Rev 1.0\r`), which no board's `/proc` gives: a run from such a tree does not count.
- The whole repository: some checks read files outside `pizero2w/`. A copy of `pizero2w/` alone fails 2 checks of
  `test-install-board.sh` ("build.sh: the earlier name is an arm64 copy only, ..." and "amd64 config: the device
  config without the NEON lines": they read `projects/APPLaunch/*config_defaults.mk`) and the 5 "deck case" checks of
  `test-update-helper.sh` (they read the apt fixture `projects/APPLaunch/tests/fixtures/apt/deck-p4`).
- Time on the build box: about 40 s for `test-install-board.sh` (502 checks), about 4 minutes for
  `test-update-helper.sh` (159 checks). Each prints `ok` or `FAIL` per check and `N passed, M failed` at the end; exit
  status 0 only when nothing failed.

**Known failures and flakes** (last full run of the installer tests: 2026-11-12 on main `08b48238`, both 0 failed):

- No known failure on main when the installer tests run as above. The 2 installer checks and the 5 "deck case"
  checks that were noted as failing on main fail only from a copy of `pizero2w/` alone (checked 2026-11-12: 500/2 and
  154/5 from such a copy, 502/0 and 159/0 from the whole LF copy). Not a release blocker: rerun from a whole LF copy.
- `twice` can differ once in the map scenes: the map widget's tile-load thread is not a tracked worker of the harness
  (`projects/APPLaunch/tests/render/README.md`, `mapdemo wait`). For v0.10.1, 2 of 4315 PNGs differed once and the
  rerun passed. Rerun `twice` before treating it as a failure; a difference that comes back is a real one.
- `cp0-unit` prints `SKIP test_keyboard_keymap` without `libxkbcommon-dev` (see Prerequisites); that is not a
  failure unless `CI=1` or `REQUIRE_XKBCOMMON=1`.

## The WSL build tree

- One tree per checkout: `~/zero7/<checkout folder name>` (`~/zero7/launcher` for `C:\CLAUDE\zero7\launcher`,
  `~/zero7/launcher-tooling` for a worktree of that name). Two worktrees never share a tree, so one branch's files
  cannot leak into another's build. Override with `--build-dir`.
- The sync copies the checkout without `build/`, `dist/`, `.git`, the harness `out/` and the Pi bundle, and mirrors
  `projects/APPLaunch/` and `ext_components/` with `--delete` (a file that is not on this branch is removed from the
  tree; build products stay, so builds are incremental).
- `SDK/` (about 2 GB of downloaded LVGL and libraries; empty in a fresh git worktree) is copied once from
  `~/zero7/launcher/SDK` (override with `DEV_SEED`) when the tree has none. The render harness compiles LVGL from it,
  which is why `check-all.sh` runs in the WSL tree and not on `/mnt/c`.
- The first sync of a new tree takes a minute or two (copy from `/mnt/c`), later ones a few seconds.

## Prerequisites

WSL Ubuntu with `rsync`, `flock`, `gcc`/`g++`, `gcc-aarch64-linux-gnu g++-aarch64-linux-gnu`, `pkg-config`,
FreeType and libpng headers (render harness), `python3`; the venv `~/zero7/venv` with SCons (`build.sh` reuses it
through `VENV`); `~/zero7/launcher/SDK` populated by one earlier build.
`libxkbcommon-dev` (`sudo apt install libxkbcommon-dev`) for the `cp0-unit` keymap test: without it that one test prints
`SKIP test_keyboard_keymap (libxkbcommon-dev missing...)` and the step goes on; with `CI=1` or `REQUIRE_XKBCOMMON=1` the
missing library is a hard failure. `~/zero7/launcher/projects/AppStore` (the Store submodule, needed by the `app-unit`
registry-mirror test) is copied into a new tree on its first sync, like `SDK/`, when the checkout has no Store sources.
For `--with-store` run `git submodule update --init projects/AppStore` in the checkout first: the Store is our fork
with one patch (decisions 2026-11-13), a seed copy may be the old upstream one, and `build.sh` refuses it.

## Facts that cost time

- **LVGL is 9.5.0** (`CONFIG_V9_5_LVGL_VERSION_MAJOR=9`, `_MINOR=5`, `_PATCH=0` in the build's
  `build/config/global_config.mk`; sources in `SDK/github_source/lvgl/lvgl_9_5`). Read the 9.5 API, not 8.x.
- **One build at a time** on the build box: everything that compiles takes `/tmp/wsl-build.lock`. When a script says
  it is waiting, another worker is building: wait, do not remove the lock.
- **Pi 3A+: no "environ" in an ssh command.** An ssh command string that contains the word `environ` drops the
  connection (`/proc/PID/environ`, but also `EnvironmentFile=`). `tools/boards/board.py` refuses such commands.
- **Pi 3A+ user service restart:** start it detached so the ssh call returns:
  `setsid nohup systemctl --user restart cyberdeckzero.service > /dev/null 2>&1 < /dev/null &` (`APPLaunch.service` before v0.7.0).
- Deploying to boards: see `tools/boards/` (`board.py`, `boards.json`).
