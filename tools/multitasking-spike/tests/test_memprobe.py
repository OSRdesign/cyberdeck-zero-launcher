#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of memprobe: the /proc parsers (pure) and, on Linux, whole runs with stand-in apps (sleep, a busy loop):
CPU running vs frozen, the resident series, nothing left stopped after a normal end, a SIGTERM or a SIGKILL of
memprobe itself, the launcher refusal and --stop-launcher with a fake systemctl.
  python3 -m unittest discover -s tests -p test_memprobe.py     (from tools/multitasking-spike)"""
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

import memprobe as M  # noqa: E402

MEMINFO = """MemTotal:         424100 kB
MemFree:          172000 kB
MemAvailable:     275500 kB
Cached:           150000 kB
SwapTotal:        423932 kB
SwapFree:         405236 kB
Shmem:              3000 kB
"""
SMAPS = """00400000-ffffe000 ---p 00000000 00:00 0                                  [rollup]
Rss:               13612 kB
Pss:                9876 kB
Pss_Anon:           5120 kB
Pss_File:           4700 kB
Pss_Shmem:            56 kB
Swap:                128 kB
SwapPss:             128 kB
"""
TCP = """  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode
   0: 0100007F:7535 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 1 1
   1: 0100007F:7535 0100007F:A1B2 01 00012000:00000000 00:00000000 00000000  1000        0 2 1
   2: 0100007F:0016 0100007F:A1B3 01 00000000:00000000 00:00000000 00000000  1000        0 3 1
"""


class Parsers(unittest.TestCase):
    def test_meminfo_smaps_zram(self):
        mi = M.parse_meminfo(MEMINFO)
        self.assertEqual(mi["MemAvailable"], 275500)
        sm = M.parse_smaps_rollup(SMAPS)
        self.assertEqual((sm["Rss"], sm["Pss_Anon"], sm["Swap"]), (13612, 5120, 128))
        z = M.parse_zram("19140608 4730880 6000640 0 6000640 1200 0 0 0")
        self.assertAlmostEqual(z["orig_mb"], 18.25, places=2)

    def test_stat_with_spaces_in_comm(self):
        comm, state, pgrp, ticks = M.parse_stat("123 (my app) S 1 456 456 0 -1 0 0 0 0 0 70 30 0 0 20 0 1 0")
        self.assertEqual((comm, state, pgrp, ticks), ("my app", "S", 456, 100))

    def test_desktop_and_tcp(self):
        self.assertEqual(M.parse_desktop_exec("[Desktop Entry]\nName=x\nExec=/opt/viz1090/run_viz1090.sh %f\n"),
                         "/opt/viz1090/run_viz1090.sh")
        rows = M.parse_tcp(TCP, 30005)
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[1]["tx_queue"], 0x12000)

    def test_fit(self):
        fe = M.fit_estimate(270, [12.0, 14.0, 40.0, 14.0])
        self.assertEqual(fe["mean_cost_mb"], 20.0)
        self.assertEqual(fe["apps_that_fit"], 10)
        self.assertIsNone(M.fit_estimate(270, []))

    def test_resolve_apps(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "system-monitor.desktop"), "w") as f:
                f.write("[Desktop Entry]\nExec=/usr/share/APPLaunch/bin/system-monitor\n")
            apps = M.resolve_apps("system-monitor,busy=sh -c 'x'", d)
            self.assertEqual(apps, [("system-monitor", "/usr/share/APPLaunch/bin/system-monitor"),
                                    ("busy", "sh -c 'x'")])


BUSY = "busy=python3 -c 'while True: pass'"


def stopped_or_alive(marker):
    """processes whose command line holds marker"""
    out = []
    for d in os.listdir("/proc"):
        if d.isdigit() and marker in M.read(f"/proc/{d}/cmdline").replace("\0", " "):
            out.append((int(d), M.read(f"/proc/{d}/stat").rpartition(")")[2].split()[:1]))
    return out


@unittest.skipUnless(sys.platform.startswith("linux"), "needs /proc")
class Runs(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.env = dict(os.environ)

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def run_probe(self, *args, timeout=120):
        return subprocess.run([sys.executable, os.path.join(ROOT, "memprobe.py"), "--out-dir", self.d, *args],
                              capture_output=True, text=True, timeout=timeout, env=self.env)

    def result(self):
        js = [f for f in os.listdir(self.d) if f.endswith(".json")]
        self.assertEqual(len(js), 1, os.listdir(self.d))
        with open(os.path.join(self.d, js[0])) as f:
            return json.load(f)

    def test_full_run(self):
        marker = "sleep 301"
        r = self.run_probe("--apps", f"{BUSY},idle={marker}", "--settle", "0.5", "--cpu-window", "1.5",
                           "--resident", "2", "--oom-test")
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        data = self.result()
        busy = data["apps"]["busy"]
        self.assertGreater(busy["cpu_running"]["total_pct"], 50)
        self.assertLess(busy["cpu_frozen"]["total_pct"], 5)
        self.assertTrue(busy["thawed_alive"])
        self.assertGreater(busy["running"]["groups"]["busy"]["rss_mb"], 1)
        self.assertEqual([x["n"] for x in data["resident"]], [1, 2])
        self.assertTrue(data["oom_test"]["writes"][0]["ok"])  # raising is always allowed
        self.assertIn("resident series", r.stdout)
        self.assertEqual(stopped_or_alive(marker), [])

    def test_sigterm_and_sigkill_leave_nothing_stopped(self):
        for sig in (signal.SIGTERM, signal.SIGKILL):
            marker = f"sleep {400 + sig}"
            p = subprocess.Popen([sys.executable, os.path.join(ROOT, "memprobe.py"), "--out-dir", self.d, "--apps",
                                  f"{BUSY},idle={marker}", "--settle", "0.3", "--cpu-window", "20",
                                  "--skip-resident"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:  # wait for the first app's frozen phase
                if any(s == ["T"] for _, s in stopped_or_alive("while True")):
                    break
                time.sleep(0.1)
            self.assertTrue(any(s == ["T"] for _, s in stopped_or_alive("while True")), "busy app never frozen")
            p.send_signal(sig)
            p.wait(timeout=30)
            time.sleep(4.5)  # the guardian's SIGTERM grace is 3 s
            self.assertEqual(stopped_or_alive("while True"), [], f"left behind after {sig}")
            self.assertEqual(stopped_or_alive(marker), [])

    def test_launcher_refusal_and_stop(self):
        fake = os.path.join(self.d, "fake")
        os.makedirs(fake)
        shutil.copy(shutil.which("sleep"), os.path.join(fake, "cyberdeckzero"))
        launcher = subprocess.Popen([os.path.join(fake, "cyberdeckzero"), "300"])
        try:
            time.sleep(0.3)
            r = self.run_probe("--apps", "x=sleep 5", "--skip-resident")
            self.assertEqual(r.returncode, 3, r.stderr)
            log = os.path.join(self.d, "systemctl.log")
            sc = os.path.join(fake, "systemctl")
            with open(sc, "w") as f:
                f.write(f"#!/bin/sh\necho \"$*\" >> {log}\n"
                        f"case \"$2\" in stop) pkill -x cyberdeckzero ;; esac\nexit 0\n")
            os.chmod(sc, 0o755)
            self.env["MTA_SYSTEMCTL"] = sc
            r = self.run_probe("--apps", "x=sleep 5", "--settle", "0.2", "--cpu-window", "0.5", "--skip-resident",
                               "--stop-launcher")
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
            with open(log) as f:
                self.assertEqual(f.read().split("\n")[:2], ["--user stop cyberdeckzero.service",
                                                            "--user start cyberdeckzero.service"])
        finally:
            launcher.kill()
            launcher.wait()


if __name__ == "__main__":
    unittest.main()
