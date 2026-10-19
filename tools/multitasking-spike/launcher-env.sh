#!/bin/sh
# SPDX-License-Identifier: MIT
#
# launcher-env.sh: print the launcher's environment, one KEY=VALUE per line, for `fbhandoff --env-file` and
# `memprobe.py --env-file`, so an app started over ssh sees what it would see under the launcher (touch axes, fb
# device, keyboard node, board profile). Works while the launcher is stopped: it reads the unit, not the process.
#
#   sh launcher-env.sh > ~/mta-results/launcher.env
#
# Sources, later lines win (as in systemd, where EnvironmentFile= overrides Environment=):
#   1. `systemctl --user show cyberdeckzero.service -p Environment` (the unit's Environment= lines)
#   2. /etc/applaunch/board.conf (the board profile; the deck has none)
# Values containing spaces are not supported (none of the launcher's have any).
set -u
systemctl --user show cyberdeckzero.service -p Environment --value 2> /dev/null | tr ' ' '\n' |
    grep -E '^[A-Za-z_][A-Za-z0-9_]*=' || true
if [ -r /etc/applaunch/board.conf ]; then
    grep -E '^[A-Za-z_][A-Za-z0-9_]*=' /etc/applaunch/board.conf || true
fi
