#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# check-all.sh: the PC-side checks of a launcher change, one line each, in WSL under the shared build lock.
#
#   tools/dev/check-all.sh [--build-dir DIR] [--no-sync] [--logs DIR]
#
#   Syncs this checkout into the WSL build tree (as wsl-build.sh; the render harness compiles LVGL 9.5 from that
#   tree's SDK/, which a fresh worktree lacks), takes flock /tmp/wsl-build.lock, then runs in order:
#     cp0-unit        sh ext_components/cp0_lvgl/tests/run_tests.sh
#     app-unit        sh projects/APPLaunch/tests/run_tests.sh
#     render          bash projects/APPLaunch/tests/render/run.sh render   (builds the harness, renders every scene)
#     compare         run.sh compare   (640x480 and 480x320 against golden/, byte-identical)
#     twice           run.sh twice     (everything rendered twice, PNGs byte-identical)
#     header          run.sh header    (page headers against the home grid)
#     contract        run.sh contract  (app contract: the host app-contract-probe against the metrics scene)
#   and prints "PASS|FAIL <step> (<seconds>s)" per step; for a failing step also the last 40 lines of its output.
#   Every step runs even after a failure. Exit status 1 when any step failed.
#
#   --build-dir DIR  WSL build tree (default ~/zero7/<checkout folder name>)
#   --no-sync        test the build tree as it is (no copy from the checkout)
#   --logs DIR       one log per step (default /tmp/check-all-<checkout folder name>/)
#
# Runs in WSL; started from Git Bash on Windows it re-runs itself in WSL.
set -euo pipefail

usage() { awk 'NR > 3 && /^#/ { sub(/^# ?/, ""); print; next } NR > 3 { exit }' "$0"; }

for a in "$@"; do
    case $a in -h | --help) usage; exit 0 ;; esac
done

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=lib.sh
. "$here/lib.sh"
reexec_in_wsl "${BASH_SOURCE[0]}" "$@"

src=$(checkout_root)
name=$(basename -- "$src")
build_dir=$(default_build_dir)
sync=1
logs=/tmp/check-all-$name
while (($#)); do
    case $1 in
        --build-dir) build_dir=${2:?--build-dir needs a directory}; shift 2 ;;
        --no-sync) sync=0; shift ;;
        --logs) logs=${2:?--logs needs a directory}; shift 2 ;;
        *) echo "unknown option $1 (see -h)" >&2; exit 2 ;;
    esac
done

take_build_lock
if ((sync)); then
    echo "sync $src -> $build_dir"
    sync_tree "$src" "$build_dir"
fi
[[ -d $build_dir/projects/APPLaunch ]] || { echo "no build tree at $build_dir" >&2; exit 2; }
mkdir -p "$logs"

render=$build_dir/projects/APPLaunch/tests/render/run.sh
failed=0
step() { # name command...
    local name=$1 start rc secs
    shift
    start=$(date +%s)
    set +e
    (cd "$build_dir" && "$@") >"$logs/$name.log" 2>&1
    rc=$?
    set -e
    secs=$(($(date +%s) - start))
    if ((rc == 0)); then
        echo "PASS $name (${secs}s)"
    else
        echo "FAIL $name (${secs}s)"
        tail -n 40 "$logs/$name.log" | sed 's/^/    /'
        failed=1
    fi
}

step cp0-unit sh ext_components/cp0_lvgl/tests/run_tests.sh
step app-unit sh projects/APPLaunch/tests/run_tests.sh
step render bash "$render" render
step compare bash "$render" compare
step twice bash "$render" twice
step header bash "$render" header
step contract bash "$render" contract
echo "logs: $logs/"
exit "$failed"
