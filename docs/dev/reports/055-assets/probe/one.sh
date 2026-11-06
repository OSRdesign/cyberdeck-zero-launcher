#!/bin/sh
# SPDX-License-Identifier: MIT
# P0 spike (report 055), second board run: one probe, wrapped in a health line before and after.
#   sh one.sh TIMEOUT_S COMMAND ARGS...      (the command runs under nice -n 19 and timeout)
h() {
    echo "H up_s=$(cut -d. -f1 /proc/uptime) avail_mb=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)" \
         "thr=$(vcgencmd get_throttled 2>/dev/null | cut -d= -f2) temp=$(vcgencmd measure_temp 2>/dev/null | cut -d= -f2)" \
         "volt=$(vcgencmd measure_volts core 2>/dev/null | cut -d= -f2) load=$(cut -d' ' -f1 /proc/loadavg)"
}
t=$1; shift
h
if [ "$#" -gt 0 ]; then
    timeout "$t" nice -n 19 "$@"
    echo "RC $?"
    h
fi
