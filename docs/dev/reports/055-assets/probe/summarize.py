#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""P0 spike: compact summary of the RESULT/H lines of a board_seq.py log."""
import re
import sys

for path in sys.argv[1:]:
    print("==", path)
    step = ""
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line.startswith("STEP"):
            step = line.split("bin/", 1)[-1].replace("~/p0-probe/tiles/", "").replace("~/p0-probe/work/", "")
            continue
        if line.startswith(("STOP", "DONE")):
            print(line)
            continue
        if not line.startswith("RESULT"):
            continue
        kv = dict(re.findall(r"(\S+?)=(\S+)", line))
        kind = line.split()[1]
        if kind == "decode":
            print("decode %-32s n=%s med=%s p95=%s dense=%s/%s first=%s 565=%s" % (
                kv.get("file", "").split("/")[-1] + ":" + kv.get("lib", ""), kv.get("n"), kv.get("warm_med"),
                kv.get("warm_p95"), kv.get("dense_med"), kv.get("dense_p95"), kv.get("first_med"), kv.get("to565_med")))
        elif kind == "magnify":
            print("magnify", " ".join("%s=%s" % (k, v) for k, v in kv.items()))
        elif kind == "view":
            print("view %s %s x%s mag=%s tiles=%s med=%s p95=%s max=%s" % (
                kv["file"].split("/")[-1], kv["screen"], kv["scale"], kv.get("mag"), kv["tiles_mean"], kv["med"],
                kv["p95"], kv["max"]))
        elif kind == "read":
            print("read %s open=%s cold=%s/%s/%s warm=%s/%s" % (
                kv["method"], kv["open_ms"], kv["cold_med"], kv["cold_p95"], kv["cold_mean"], kv["warm_med"], kv["warm_p95"]))
        elif kind == "mem" and kv.get("tag") in ("read_after_cold",):
            print("   mem %s rss=%s anon=%s file=%s" % (kv["tag"], kv["VmRSS_kb"], kv["RssAnon_kb"], kv["RssFile_kb"]))
        elif kind == "lru":
            print("lru slots=%s bpp=%s scale=%s cache=%s rss=%s hwm=%s dec=%s" % (
                kv["slots"], kv["bpp"], kv["scale"], kv["cache_mb"], kv["VmRSS_kb"], kv["VmHWM_kb"], kv["decode_ms_mean"]))
        elif kind == "convert":
            print("convert journal=%s n=%s ms=%s tiles_s=%s write=%s read=%s out=%s" % (
                kv["journal"], kv["n"], kv["ms"], kv["tiles_s"], kv["write_mb"], kv["read_mb"], kv["out_mb"]))
        elif kind == "stubmem":
            print("stub %-28s %s rss=%s anon=%s file=%s" % (step.split()[0] + " " + step.split()[-1], kv["tag"],
                                                           kv["VmRSS_kb"], kv["RssAnon_kb"], kv["RssFile_kb"]))
    hs = re.findall(r"H up_s=(\d+) avail_mb=(\d+) thr=(\S+) temp=(\S+) volt=(\S+)", open(path, encoding="utf-8").read())
    if hs:
        av = [int(h[1]) for h in hs]
        tp = [float(h[3].rstrip("'C")) for h in hs]
        print("health: %d checks, avail min %d max %d MB, temp %.1f-%.1f C, throttled %s, uptime %s -> %s s" % (
            len(hs), min(av), max(av), min(tp), max(tp), sorted(set(h[2] for h in hs)), hs[0][0], hs[-1][0]))
