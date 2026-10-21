#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# wsl-build.sh: build the bundle of this checkout in WSL, under the shared build lock: the tarball
# cyberdeckzero-launcher-<version>-<arch>.tar.gz (<version>: APPLAUNCH_VERSION, default "dev"; <arch>: arm64 for the Pi
# boards, the default, or amd64 for PCs) and, for arm64, its copy pizero2w-bundle.tar.gz (the earlier quick-start name;
# build.sh leaves it out with LEGACY_BUNDLE_NAME=0).
#
#   tools/dev/wsl-build.sh [--arch arm64|amd64] [--with-store] [--build-dir DIR] [--log FILE]
#
#   1. takes flock /tmp/wsl-build.lock (one build at a time on the build box; waits when another holds it)
#   2. syncs this checkout (the /mnt/c/... tree the script lives in) into the WSL build tree, default
#      ~/zero7/<checkout folder name> (amd64: ~/zero7/<checkout folder name>-amd64, so the two architectures do not
#      rebuild each other's objects) (build/, dist/ and .git excluded; see lib.sh sync_tree)
#   3. source ~/zero7/venv/bin/activate; APPLAUNCH_HW=pizero2w CONFIG_REPO_AUTOMATION=y; bash build.sh --arch <arch>
#      in projects/APPLaunch/pizero2w (it picks the config: linux_x86_cross_cp0_config_defaults.mk for arm64,
#      linux_x86_64_native_cp0_config_defaults.mk for amd64, which needs the -dev packages of build.sh's help)
#   4. prints the last 25 lines of the build, the full log's path, the bundle's path and size (and the copy's)
#
#   APPLAUNCH_CHANNEL in the environment sets the channel (default development here; build.sh alone defaults to release);
#   APPLAUNCH_VERSION and LEGACY_BUNDLE_NAME go through to build.sh as they are
#
#   --arch ARCH      arm64 (default) or amd64 (passed to pizero2w/build.sh)
#   --with-store     also build the Store (passed to pizero2w/build.sh)
#   --build-dir DIR  WSL build tree (default ~/zero7/<checkout folder name>, amd64: with "-amd64")
#   --log FILE       full build log (default /tmp/wsl-build-<checkout folder name>[-amd64].log)
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
build_dir=
log=
arch=arm64
store=()
while (($#)); do
    case $1 in
        --arch) arch=${2:?--arch needs arm64 or amd64}; shift 2 ;;
        --with-store) store=(--with-store); shift ;;
        --build-dir) build_dir=${2:?--build-dir needs a directory}; shift 2 ;;
        --log) log=${2:?--log needs a file}; shift 2 ;;
        *) echo "unknown option $1 (see -h)" >&2; exit 2 ;;
    esac
done
case $arch in
    arm64) suffix= ;;
    amd64) suffix=-amd64 ;;
    *) echo "--arch $arch: arm64 or amd64" >&2; exit 2 ;;
esac
build_dir=${build_dir:-$(default_build_dir)$suffix}
log=${log:-/tmp/wsl-build-$name$suffix.log}

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
    export VENV=$HOME/zero7/venv APPLAUNCH_HW=pizero2w CONFIG_REPO_AUTOMATION=y
    # a developer's build: development-only pages show (pizero2w/build.sh defaults to release); the caller may override
    export APPLAUNCH_CHANNEL=${APPLAUNCH_CHANNEL:-development}
    cd "$build_dir/projects/APPLaunch/pizero2w" && bash build.sh --arch "$arch" "${store[@]}"
) >"$log" 2>&1
rc=$?
set -e
secs=$(($(date +%s) - start))

echo "-- last 25 lines of the build --"
tail -n 25 "$log"
echo "----------------------------------"
echo "full log: $log"
out_dir=$build_dir/projects/APPLaunch/pizero2w
bundle=$out_dir/cyberdeckzero-launcher-${APPLAUNCH_VERSION:-dev}-$arch.tar.gz
legacy=$out_dir/pizero2w-bundle.tar.gz
[[ $arch == arm64 ]] || legacy=/nonexistent   # the earlier name is an arm64 copy only
if ((rc != 0)); then
    echo "BUILD FAILED (exit $rc, ${secs}s)"
    exit "$rc"
fi
if [[ ! -f $bundle ]]; then
    echo "BUILD FAILED: no bundle at $bundle (${secs}s)"
    exit 1
fi
echo "BUILD OK (${secs}s)"
for f in "$bundle" "$legacy"; do
    [[ -f $f ]] || continue
    [[ $f == "$legacy" ]] && label="bundle (copy, earlier name)" || label=bundle
    echo "$label: $f ($(du -h "$f" | cut -f1), $(stat -c %s "$f") bytes, $(date -r "$f" '+%F %T'))"
    if [[ -n ${WSL_DISTRO_NAME:-} ]]; then
        echo "  from Windows: \\\\wsl.localhost\\$WSL_DISTRO_NAME${f//\//\\}"
    fi
done
