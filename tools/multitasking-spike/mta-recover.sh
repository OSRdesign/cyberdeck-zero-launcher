#!/bin/sh
# SPDX-License-Identifier: MIT
#
# mta-recover.sh: the panic button for the P-a spike tools. Use it when the panel is stuck on a test card or an
# app stays frozen (for example after the ssh connection dropped and the guardian could not finish).
#
#   sh ~/mta-spike/mta-recover.sh
#
#   1. sends SIGCONT to every stopped process of this user (a frozen app continues);
#   2. ends the spike tools politely (SIGTERM: fbhandoff and memprobe restore the panel, end their apps and start
#      the launcher again if they had stopped it);
#   3. starts the launcher (a no-op when it already runs). The launcher repaints the whole panel when it starts;
#      if it was running, `systemctl --user restart cyberdeckzero` repaints it.
set -u
me=$(id -u)
for pid in $(ps -u "$me" -o pid=,stat= | awk '$2 ~ /^T/ { print $1 }'); do
    kill -CONT "$pid" 2> /dev/null && echo "continued $pid ($(cat /proc/"$pid"/comm 2> /dev/null))"
done
for name in fbhandoff fbhapp mtrec; do
    pkill -TERM -u "$me" -x "$name" && echo "ended $name"
done
# memprobe runs under python3: match its script name (this script's own command line does not contain it)
pkill -TERM -u "$me" -f 'memprobe[.]py' && echo "ended memprobe.py"
sleep 4
systemctl --user start cyberdeckzero.service && echo "launcher: $(systemctl --user is-active cyberdeckzero.service)"
