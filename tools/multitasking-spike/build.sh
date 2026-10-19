#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Build the P-a spike tools (study 026). Linux / WSL; run from anywhere.
#
#   sh build.sh [host|aarch64|all]     default: all
#
#   host     gcc (or $HOST_CC)                          -> build/host/{mtrec,fbhandoff,fbhapp,test_mt_track}
#   aarch64  aarch64-linux-gnu-gcc (or $CC), -static    -> build/aarch64/{mtrec,fbhandoff,fbhapp}  (the Pi boards)
#
# The aarch64 binaries are static, so they do not depend on the boards' glibc (the WSL cross toolchain has 2.39;
# the boards run Debian 13 with 2.41, uConsole-era bookworm would have 2.36). C11 + POSIX + Linux headers only; the
# 5x7 font comes from tools/dev/app-contract-probe/font5x7.h.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
src=$here/src
font=$here/../dev/app-contract-probe
flags="-std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-format-truncation -I$src -I$font"
common="$src/mta_util.c $src/evdev_util.c"

build() { # dir compiler extra-flags
    out=$here/build/$1
    mkdir -p "$out"
    # shellcheck disable=SC2086
    $2 $flags $3 -o "$out/mtrec" "$src/mtrec.c" "$src/mt_track.c" $common -lm
    # shellcheck disable=SC2086
    $2 $flags $3 -o "$out/fbhandoff" "$src/fbhandoff.c" "$src/fb_util.c" $common -lm
    # shellcheck disable=SC2086
    $2 $flags $3 -o "$out/fbhapp" "$src/fbhapp.c" "$src/fb_util.c" $common -lm
    echo "built $out/mtrec $out/fbhandoff $out/fbhapp"
}

target=${1:-all}
case $target in
    host)
        build host "${HOST_CC:-gcc}" ""
        # shellcheck disable=SC2086
        ${HOST_CC:-gcc} $flags -o "$here/build/host/test_mt_track" "$here/tests/test_mt_track.c" "$src/mt_track.c" -lm
        ;;
    aarch64) build aarch64 "${CC:-aarch64-linux-gnu-gcc}" "-static" ;;
    all)
        sh "$0" host
        sh "$0" aarch64
        ;;
    *) echo "usage: sh build.sh [host|aarch64|all]" >&2; exit 2 ;;
esac
