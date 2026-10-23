#!/bin/sh
# Build cyberdeckzero-ctl (the command-line client of launcher.sock). Run from this directory: CC=<compiler> ./build.sh
set -e
CC=${CC:-cc}
INC=../../../../ext_components/cp0_lvgl
$CC -std=c99 -O2 -Wall -Wextra -o cyberdeckzero-ctl cyberdeckzero_ctl.c \
    $INC/src/cp0_app_client.c $INC/src/cp0_app_contract.c $INC/src/cp0_ui_metrics.c -I$INC/include
echo "built $(pwd)/cyberdeckzero-ctl"
