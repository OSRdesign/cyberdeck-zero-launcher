#!/bin/sh
# SPDX-License-Identifier: MIT
#
# All host tests of the P-a spike tools (Linux / WSL; about 2 minutes): the C tracker unit tests, the analyser and
# memprobe tests (Python unittest), the end-to-end runs of fbhandoff, fbhapp and mtrec (tests/host_e2e.sh).
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
