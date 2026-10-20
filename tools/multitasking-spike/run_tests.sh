#!/bin/sh
# SPDX-License-Identifier: MIT
#
# All host tests of the P-a spike tools (Linux / WSL; about 2 minutes): the C tracker unit tests, the analyser and
# memprobe tests (Python unittest), the end-to-end runs of fbhandoff, fbhapp and mtrec (tests/host_e2e.sh), and (P-c)
# every mtplay pattern replayed through the launcher's G1 (cp0_lvgl tests/test_g1_replay.c) on the deck and the Pi 3A+.
#
#   sh tools/multitasking-spike/run_tests.sh
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$here"
sh build.sh host
echo "== C: mt_track"
build/host/test_mt_track
echo "== Python: mta_analyse, memprobe"
python3 -m unittest discover -s tests -p 'test_*.py'
echo "== end to end"
bash tests/host_e2e.sh
echo "== mtplay patterns through the launcher's G1 (P-c)"
cp0=$here/../../ext_components/cp0_lvgl
${HOST_CC:-gcc} -std=c11 -O1 -Wall -Wextra -Werror -I"$cp0/include" -o build/host/test_g1_replay \
    "$cp0/src/cp0_touch_slots.c" "$cp0/src/cp0_gesture_g1.c" "$cp0/src/cp0_ui_metrics.c" "$cp0/tests/test_g1_replay.c" \
    -lm
patterns="g1; g1short; g1close; g1slow; g1move; g1edge; four; pinch; strip; tap 20 20; g1 1200"
build/host/mtplay --dry-run --script "$patterns" | build/host/test_g1_replay - 640x480 2/2 0
build/host/mtplay --dry-run --board pi3a --script "$patterns" | build/host/test_g1_replay - 800x480 2/2 0
