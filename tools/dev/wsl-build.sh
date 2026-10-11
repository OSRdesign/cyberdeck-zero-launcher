#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# wsl-build.sh: build the Pi bundle (pizero2w-bundle.tar.gz) of this checkout in WSL, under the shared build lock.
#
#   tools/dev/wsl-build.sh [--with-store] [--build-dir DIR] [--log FILE]
#
#   1. takes flock /tmp/wsl-build.lock (one build at a time on the build box; waits when another holds it)
#   2. syncs this checkout (the /mnt/c/... tree the script lives in) into the WSL build tree, default
#      ~/zero7/<checkout folder name> (build/, dist/ and .git excluded; see lib.sh sync_tree)
#   3. source ~/zero7/venv/bin/activate; APPLAUNCH_HW=pizero2w CONFIG_REPO_AUTOMATION=y
#      CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk; bash build.sh in projects/APPLaunch/pizero2w
#   4. prints the last 25 lines of the build, the full log's path, the bundle's path and size
#
#   --with-store     also build the Store (passed to pizero2w/build.sh)
#   --build-dir DIR  WSL build tree (default ~/zero7/<checkout folder name>)
#   --log FILE       full build log (default /tmp/wsl-build-<checkout folder name>.log)
#
# Runs in WSL; started from Git Bash on Windows it re-runs itself in WSL. Exit status: build.sh's (0 = bundle built).
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
log=/tmp/wsl-build-$name.log
store=()
while (($#)); do
    case $1 in
        --with-store) store=(--with-store); shift ;;
        --build-dir) build_dir=${2:?--build-dir needs a directory}; shift 2 ;;
        --log) log=${2:?--log needs a file}; shift 2 ;;
        *) echo "unknown option $1 (see -h)" >&2; exit 2 ;;
    esac
done

[[ -f $HOME/zero7/venv/bin/activate ]] || { echo "missing ~/zero7/venv (python venv with scons)" >&2; exit 2; }

take_build_lock
echo "== sync $src -> $build_dir"
sync_tree "$src" "$build_dir"

echo "== build (log: $log)"
start=$(date +%s)
set +e
(
    # shellcheck disable=SC1091
    source "$HOME/zero7/venv/bin/activate"
    export VENV=$HOME/zero7/venv APPLAUNCH_HW=pizero2w CONFIG_REPO_AUTOMATION=y \
        CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk
    cd "$build_dir/projects/APPLaunch/pizero2w" && bash build.sh "${store[@]}"
) >"$log" 2>&1
rc=$?
set -e
secs=$(($(date +%s) - start))

echo "-- last 25 lines of the build --"
tail -n 25 "$log"
echo "----------------------------------"
echo "full log: $log"
bundle=$build_dir/projects/APPLaunch/pizero2w/pizero2w-bundle.tar.gz
if ((rc != 0)); then
    echo "BUILD FAILED (exit $rc, ${secs}s)"
    exit "$rc"
fi
if [[ ! -f $bundle ]]; then
    echo "BUILD FAILED: no bundle at $bundle (${secs}s)"
    exit 1
fi
echo "BUILD OK (${secs}s)"
echo "bundle: $bundle ($(du -h "$bundle" | cut -f1), $(stat -c %s "$bundle") bytes, $(date -r "$bundle" '+%F %T'))"
if [[ -n ${WSL_DISTRO_NAME:-} ]]; then
    echo "from Windows: \\\\wsl.localhost\\$WSL_DISTRO_NAME${bundle//\//\\}"
fi
