#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Build the app contract probe (see README.md). Linux / WSL; run from anywhere.
#
#   sh build.sh [host|aarch64|all]     default: all
#
#   host     gcc (or $HOST_CC)                    -> build/host/app-contract-probe
#   aarch64  aarch64-linux-gnu-gcc (or $CC)       -> build/aarch64/app-contract-probe  (the Pi boards)
#
# Sources: probe.c + the contract SDK copied by path from ext_components/cp0_lvgl (cp0_app_contract.c,
# cp0_ui_metrics.c, src/cp0/cp0_fb_output.c). C99 + POSIX + Linux fbdev/evdev headers; no LVGL, no other library.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cp0=$here/../../../ext_components/cp0_lvgl
srcs="$here/probe.c $cp0/src/cp0_app_contract.c $cp0/src/cp0_ui_metrics.c $cp0/src/cp0/cp0_fb_output.c"
flags="-std=c99 -O2 -Wall -Wextra -Werror -I$here -I$cp0/include -I$cp0/src/cp0"

build() { # name compiler
    mkdir -p "$here/build/$1"
    # shellcheck disable=SC2086
    $2 $flags -o "$here/build/$1/app-contract-probe" $srcs
    echo "built $here/build/$1/app-contract-probe"
}

target=${1:-all}
case $target in
    host) build host "${HOST_CC:-gcc}" ;;
    aarch64) build aarch64 "${CC:-aarch64-linux-gnu-gcc}" ;;
    all) build host "${HOST_CC:-gcc}"; build aarch64 "${CC:-aarch64-linux-gnu-gcc}" ;;
    *) echo "usage: sh build.sh [host|aarch64|all]" >&2; exit 2 ;;
esac
