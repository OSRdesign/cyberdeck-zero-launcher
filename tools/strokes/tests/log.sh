#!/bin/sh
# SPDX-License-Identifier: MIT
#
# log.sh BUILD_DIR: strokes-log on a small capture log of the Strokes training page (report 042): the recorded numbers
# (before / after, a recorded confusion), the replay, and --merge keeping every sample and its comment line.
set -eu
b=${1:-build/host}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
pts='0,250,500 10,250,520 20,250,540 30,250,560 40,250,580 50,250,600 60,250,620 ^70'
{
    echo '# strokes-corpus 1'
    echo '# training phase=train target=i set=letters attempt=1/4 verdict=ok got=i score=0.990 margin=0.500 reject=none size_mm=0.1x14.1 at=2026-10-25T21:00:00'
    printf 'i\tletters\t8.540\t%s\n' "$pts"
    echo '# training phase=train target=i set=letters attempt=2/4 verdict=refused got=l score=0.700 margin=0.010 reject=low-score size_mm=0.1x14.1 at=2026-10-25T21:00:05'
    printf 'i\tletters\t8.540\t%s\n' "$pts"
    echo '# training phase=retest target=i set=letters attempt=1/1 verdict=ok got=i score=0.990 margin=0.500 reject=none size_mm=0.1x14.1 at=2026-10-25T21:01:00'
    printf 'i\tletters\t8.540\t%s\n' "$pts"
} > "$tmp/log.txt"
"$b/strokes-log" "$tmp/log.txt" > "$tmp/out.txt"
grep -q 'before (training attempts) 1/2 = 50.0 %, after (re-test) 1/1 = 100.0 %' "$tmp/out.txt" ||
    { cat "$tmp/out.txt"; echo "log: FAIL the session numbers" >&2; exit 1; }
grep -q '^  i *-> ~l *1$' "$tmp/out.txt" || { cat "$tmp/out.txt"; echo "log: FAIL the recorded confusion" >&2; exit 1; }
grep -q '^ALL' "$tmp/out.txt" || { cat "$tmp/out.txt"; echo "log: FAIL no replay table" >&2; exit 1; }
"$b/strokes-log" --phase retest "$tmp/log.txt" | grep -q 'after (re-test) 1/1' ||
    { echo "log: FAIL --phase" >&2; exit 1; }
"$b/strokes-log" --merge "$tmp/merged.txt" "$tmp/log.txt" "$tmp/log.txt" > /dev/null
[ "$(grep -vc '^#' "$tmp/merged.txt")" = 6 ] || { echo "log: FAIL --merge samples" >&2; exit 1; }
[ "$(grep -c '^# training ' "$tmp/merged.txt")" = 6 ] || { echo "log: FAIL --merge comments" >&2; exit 1; }
"$b/strokes-replay" "$tmp/merged.txt" --conf 0 | grep -q '^ALL' || { echo "log: FAIL the merged corpus" >&2; exit 1; }
echo "log: OK"
