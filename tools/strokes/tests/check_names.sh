#!/bin/sh
# SPDX-License-Identifier: MIT
#
# check_names.sh: the trademark of the handheld alphabet our shapes resemble must not appear in the Strokes files
# (code, data, tests, README, file names): tools/strokes, the library in ext_components/cp0_lvgl (src/strokes,
# include/strokes.h) and the shipped tables (projects/APPLaunch/APPLaunch/strokes); report 038 section 5. The pattern
# is written with a character class so that this file does not contain the word itself. The whole repository is
# checked by tools/dev/check-branding.sh (docs/dev may cite the word).
set -eu
here=$(cd -- "$(dirname -- "$0")/.." && pwd)
repo=$(cd -- "$here/../.." && pwd)
pattern='g[r]affiti'
set -- "$here" "$repo/ext_components/cp0_lvgl/src/strokes" "$repo/ext_components/cp0_lvgl/include/strokes.h" \
    "$repo/projects/APPLaunch/APPLaunch/strokes"
hits=$(grep -rniI --exclude-dir=build -e "$pattern" "$@" || true)
names=$(for d in "$@"; do find "$d" -path "$here/build" -prune -o -print; done | grep -i -e "$pattern" || true)
if [ -n "$hits$names" ]; then
    printf '%s\n%s\n' "$hits" "$names"
    echo "names: the trademark word appears above (keep it out of the Strokes files; docs/dev may cite it)" >&2
    exit 1
fi
echo "names: OK"
