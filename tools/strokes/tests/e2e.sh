#!/bin/sh
# SPDX-License-Identifier: MIT
#
# e2e.sh BUILD_DIR: synthetic strokes -> evtest-style text -> strokes-capture -> corpus -> strokes-replay, compared
# with strokes-eval on the same samples. Proves the capture tool's evtest path and the corpus format end to end.
set -eu
b=${1:-build/host}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$b/strokes-eval" --n 20 --seed 5 --gen drop_p=0.3 --evtest "$tmp/ev.txt" --write "$tmp/direct.txt" --conf 0 \
    > "$tmp/eval.out"
"$b/strokes-capture" --evtest "$tmp/ev.txt" --labels-file "$tmp/ev.txt.labels" --ppmm 8.54 --out "$tmp/cap.txt" \
    --quiet --no-recognise
"$b/strokes-replay" "$tmp/cap.txt" --conf 0 > "$tmp/cap.out"
"$b/strokes-replay" "$tmp/direct.txt" --conf 0 > "$tmp/direct.out"
n_cap=$(grep -vc '^#' "$tmp/cap.txt")
n_dir=$(grep -vc '^#' "$tmp/direct.txt")
all_eval=$(grep '^ALL' "$tmp/eval.out" | sed 's/^ALL: //')
all_cap=$(grep '^ALL' "$tmp/cap.out" | sed 's/^ALL: //')
all_dir=$(grep '^ALL' "$tmp/direct.out" | sed 's/^ALL: //')
echo "e2e: eval   $all_eval"
echo "e2e: corpus $all_dir"
echo "e2e: capture $all_cap"
[ "$n_cap" = "$n_dir" ] || { echo "e2e: FAIL $n_cap captured samples, $n_dir written" >&2; exit 1; }
[ "$all_eval" = "$all_dir" ] || { echo "e2e: FAIL the corpus replay differs from the direct evaluation" >&2; exit 1; }
[ "$all_cap" = "$all_dir" ] || { echo "e2e: FAIL the captured corpus replays differently" >&2; exit 1; }
echo "e2e: OK ($n_cap samples)"
