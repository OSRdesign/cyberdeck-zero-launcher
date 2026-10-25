#!/bin/sh
# SPDX-License-Identifier: MIT
#
# check_names.sh: the Palm-era trademark of the alphabet our shapes resemble must not appear in tools/strokes (code,
# data, tests, README, file names); report 038 section 5. The pattern is written with a character class so that this
# file does not contain the word itself.
set -eu
here=$(cd -- "$(dirname -- "$0")/.." && pwd)
pattern='g[r]affiti'
hits=$(grep -rniI --exclude-dir=build -e "$pattern" "$here" || true)
names=$(find "$here" -path "$here/build" -prune -o -print | grep -i -e "$pattern" || true)
if [ -n "$hits$names" ]; then
    printf '%s\n%s\n' "$hits" "$names"
    echo "names: the trademark word appears above (keep it out of tools/strokes; docs/dev may cite it)" >&2
    exit 1
fi
echo "names: OK"
