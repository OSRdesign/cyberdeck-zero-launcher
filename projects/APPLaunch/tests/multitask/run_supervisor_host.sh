#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Multitasking P-b host process tests (task 015): builds the spike's fake app (tools/multitasking-spike fbhapp) and
# test_supervisor_host (the real supervisor and its Linux port, AddressSanitizer + UndefinedBehaviorSanitizer when the
# compiler has them), then runs every scenario on a file-backed framebuffer with FIFOs as input nodes. Linux / WSL,
# about 40 s. Called by projects/APPLaunch/tests/run_tests.sh.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../../.." && pwd)
build="${TMPDIR:-/tmp}/applaunch-tests/multitask"
work=$(mktemp -d "${TMPDIR:-/tmp}/mt-host.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM
mkdir -p "$build"

spike="$root/tools/multitasking-spike/src"
# P-e: fbhapp --helper runs the toolkit's app-side lifecycle helper (cp0_app_lifecycle.c) against the real supervisor
${CC:-gcc} -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-format-truncation \
    -I"$spike" -I"$root/tools/dev/app-contract-probe" -I"$root/ext_components/cp0_lvgl/include" \
    -o "$build/fbhapp" "$spike/fbhapp.c" "$spike/fb_util.c" "$spike/mta_util.c" "$spike/evdev_util.c" \
    "$root/ext_components/cp0_lvgl/src/cp0_app_lifecycle.c" -lm

san="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
if ! printf 'int main(void){return 0;}\n' | ${CXX:-g++} -x c++ $san - -o "$build/san_probe" 2>/dev/null; then
    echo "NOTE supervisor host test without sanitizers (no ASan/UBSan runtime)"
    san=""
fi
ui="$root/projects/APPLaunch/main/ui"
cp0="$root/ext_components/cp0_lvgl"
# P-c: the touch tracker, its device reader and G1 (C, compiled as C)
for c in "$cp0/src/cp0_touch_slots.c" "$cp0/src/cp0_gesture_g1.c" "$cp0/src/cp0/cp0_touch_reader.c" \
    "$cp0/src/cp0/cp0_media_keys.c"; do
    ${CC:-gcc} -std=c11 -O1 -g -Wall -Wextra -Werror $san -I"$cp0/include" -c "$c" -o "$build/$(basename "$c" .c).o"
done
${CXX:-g++} -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread $san \
    -I"$ui" -I"$cp0/include" -I"$cp0/src/cp0" \
    "$here/test_supervisor_host.cpp" \
    "$ui/multitask/supervisor.cpp" "$ui/multitask/supervisor_linux.cpp" "$ui/multitask/multitask_model.cpp" \
    "$ui/multitask/switch_keys.cpp" "$ui/multitask/lifecycle_protocol.cpp" "$ui/multitask/switcher_model.cpp" \
    "$cp0/src/cp0/cp0_esc_key_watch.cpp" \
    "$build/cp0_touch_slots.o" "$build/cp0_gesture_g1.o" "$build/cp0_touch_reader.o" "$build/cp0_media_keys.o" -lm \
    -o "$build/test_supervisor_host"
# leaks of the forked children (guardian, apps between fork and exec) are not ours to report
ASAN_OPTIONS=detect_leaks=0 "$build/test_supervisor_host" "$build/fbhapp" "$work"
