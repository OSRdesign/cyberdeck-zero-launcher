# tools/dev: build and check the launcher

Two scripts for the Windows dev machine + WSL build box, used by workers and by the Controller. Run them from WSL or
from Git Bash (they re-run themselves in WSL through `wsl.exe`). `-h` prints the usage of each.

| Script | What it does |
| --- | --- |
| `wsl-build.sh [--with-store]` | Syncs this checkout into its WSL build tree, takes the build lock, builds the Pi bundle with `projects/APPLaunch/pizero2w/build.sh`. Prints the last 25 build lines, the full log's path, and the bundle's path and size (also as a `\\wsl.localhost\...` path for Windows tools such as `tools/boards/board.py upload`). Exit status = the build's. |
| `check-all.sh` | Syncs, takes the lock, then runs `cp0-unit` (`ext_components/cp0_lvgl/tests/run_tests.sh`), `app-unit` (`projects/APPLaunch/tests/run_tests.sh`), `render` (`tests/render/run.sh render`), `compare`, `twice`, `header`, `contract` (same script; the app contract probe, `app-contract-probe/`). One line per step: `PASS|FAIL <step> (<seconds>s)`; a failing step also shows its last 40 lines. Logs in `/tmp/check-all-<checkout>/`. Exit 1 if any step failed. |
| `lib.sh` | Shared helpers (sourced): WSL re-exec, build lock, tree sync. |

Both use the same recipe as the scripts the earlier sessions ran by hand:
`source ~/zero7/venv/bin/activate`, `APPLAUNCH_HW=pizero2w CONFIG_REPO_AUTOMATION=y
CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk`, everything under `flock /tmp/wsl-build.lock`.

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

## Facts that cost time

- **LVGL is 9.5.0** (`CONFIG_V9_5_LVGL_VERSION_MAJOR=9`, `_MINOR=5`, `_PATCH=0` in the build's
  `build/config/global_config.mk`; sources in `SDK/github_source/lvgl/lvgl_9_5`). Read the 9.5 API, not 8.x.
- **One build at a time** on the build box: everything that compiles takes `/tmp/wsl-build.lock`. When a script says
  it is waiting, another worker is building: wait, do not remove the lock.
- **Pi 3A+: no "environ" in an ssh command.** An ssh command string that contains the word `environ` drops the
  connection (`/proc/PID/environ`, but also `EnvironmentFile=`). `tools/boards/board.py` refuses such commands.
- **Pi 3A+ user service restart:** start it detached so the ssh call returns:
  `setsid nohup systemctl --user restart APPLaunch.service > /dev/null 2>&1 < /dev/null &`.
- Deploying to boards: see `tools/boards/` (`board.py`, `boards.json`).
