#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""P0 spike (report 055), second board run: one probe per ssh session, a pause between probes, stop on bad health.

  python board_seq.py HOST MIN_AVAIL_MB STEPS_FILE LOG [--pause 25]

STEPS_FILE: one remote shell command per line ($B, $T, $W are replaced by ~/p0-probe/bin, tiles, work); the commands
run `sh $B/one.sh ...`, which prints "H up_s=... avail_mb=... thr=... temp=..." before and after the probe.
Stops (exit 3) when throttled is non-zero, MemAvailable is under MIN_AVAIL_MB, uptime went backwards (a reset), or the
board cannot be reached (exit 4: no retry). The password comes only from PIPW (brd.connect)."""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brd  # noqa: E402

H = re.compile(r"^H up_s=(\d+) avail_mb=(\d+) thr=(\S*) temp=(\S*)")


def main():
    host, min_av, steps_f, log_f = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    pause = 25
    if "--pause" in sys.argv:
        pause = int(sys.argv[sys.argv.index("--pause") + 1])
    steps = [s.strip() for s in open(steps_f, encoding="utf-8") if s.strip() and not s.startswith("#")]
    log = open(log_f, "a", encoding="utf-8")

    def out(s):
        log.write(s + "\n")
        log.flush()
        print(s, flush=True)

    last_h, last_up = "none", -1
    for i, st in enumerate(steps):
        cmd = st.replace("$B", "~/p0-probe/bin").replace("$T", "~/p0-probe/tiles").replace("$W", "~/p0-probe/work")
        out("STEP %d/%d %s %s" % (i + 1, len(steps), time.strftime("%H:%M:%S"), cmd))
        m = re.search(r"one\.sh (\d+)", cmd)
        tmo = (int(m.group(1)) if m else 120) + 60
        try:
            c = brd.connect(host, 15)
        except Exception as e:  # noqa: BLE001
            out("STOP connect failed at step %d (%s): %s; last health: %s" % (i + 1, cmd, e, last_h))
            sys.exit(4)
        try:
            stdin, stdout, stderr = c.exec_command(cmd, timeout=tmo)
            stdin.close()
            text = stdout.read().decode("utf-8", "replace") + stderr.read().decode("utf-8", "replace")
        except Exception as e:  # noqa: BLE001
            out("STOP session lost during step %d (%s): %s; last health: %s" % (i + 1, cmd, e, last_h))
            c.close()
            sys.exit(4)
        c.close()
        for line in text.splitlines():
            out("  " + line)
            hm = H.match(line)
            if hm:
                up, av, thr = int(hm.group(1)), int(hm.group(2)), hm.group(3)
                last_h = line
                if last_up >= 0 and up < last_up:
                    out("STOP uptime went backwards (%d -> %d): the board reset" % (last_up, up))
                    sys.exit(3)
                last_up = up
                if thr and thr != "0x0":
                    out("STOP throttled=%s" % thr)
                    sys.exit(3)
                if av < min_av:
                    out("STOP MemAvailable %d MB < %d MB" % (av, min_av))
                    sys.exit(3)
        if i + 1 < len(steps):
            time.sleep(pause)
    out("DONE %s" % time.strftime("%H:%M:%S"))


if __name__ == "__main__":
    main()
