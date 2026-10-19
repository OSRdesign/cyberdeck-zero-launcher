#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mta_synth: synthetic touch recordings for the P-a spike tests (no board needed).

Builds protocol B event streams the way the kernel sends them (slot, tracking id, positions, SYN_REPORT; equal
values are not resent) from finger stories in display millimetres, and writes them as an mtrec CSV recording (with
labelled segments) or as a raw stream of struct input_event (24 bytes each, little-endian, 64-bit timeval) that
`mtrec --raw` and `fbhapp --input FIFO` read.

  python3 mta_synth.py demo --csv OUT.csv [--raw OUT.bin] [--board deck|pi3a]

As a module: Synth(board).hold(...), .palm(...), .segment(...), then to_csv() / to_raw().
"""
import argparse
import math
import random
import struct
import sys

from mta_analyse import (ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_SLOT, ABS_MT_TOUCH_MAJOR,
                         ABS_MT_TRACKING_ID, EV_ABS, EV_KEY, EV_SYN, SYN_REPORT, Geom)

BOARDS = {
    # raw ranges, panel mm (display orientation), swap, slots, reports TOUCH_MAJOR
    "deck": dict(x1=639, y1=479, mm_w=56.6, mm_h=42.5, swap=1, slots=5, major=True,
                 name="11-005d Goodix Capacitive TouchScreen"),
    "pi3a": dict(x1=799, y1=479, mm_w=94.0, mm_h=56.0, swap=0, slots=10, major=False,
                 name="10-0038 generic ft5x06 (79)"),
}

EVENT = struct.Struct("<qqHHi")


class Synth:
    def __init__(self, board="deck", seed=1):
        b = BOARDS[board]
        self.board = board
        self.b = b
        self.geom = Geom(0, b["x1"], 0, b["y1"], b["mm_w"], b["mm_h"], b["swap"])
        self.frames = {}  # t_us -> {slot: dict(tid=, x=, y=, major=)} changes
        self.segments = []
        self.rng = random.Random(seed)
        self.next_tid = 100

    # -- low level: per-slot changes at a time (ms), merged into frames
    def _put(self, t_ms, slot, **kv):
        f = self.frames.setdefault(int(round(t_ms * 1000)), {})
        f.setdefault(slot, {}).update(kv)

    def down(self, t_ms, slot, x_mm, y_mm, major=None):
        x, y = self.geom.from_mm(x_mm, y_mm)
        self.next_tid += 1
        kv = dict(tid=self.next_tid, x=x, y=y)
        if major is not None and self.b["major"]:
            kv["major"] = major
        self._put(t_ms, slot, **kv)

    def move(self, t_ms, slot, x_mm, y_mm, major=None):
        x, y = self.geom.from_mm(x_mm, y_mm)
        kv = dict(x=x, y=y)
        if major is not None and self.b["major"]:
            kv["major"] = major
        self._put(t_ms, slot, **kv)

    def up(self, t_ms, slot):
        self._put(t_ms, slot, tid=-1)

    # -- finger stories
    def hold(self, t0_ms, pts_mm, dur_ms, stagger_ms=40, jitter_mm=0.2, major=None, rate_hz=100, slot0=0,
             lift_stagger_ms=20):
        """fingers land one after the other (stagger), hold with jitter, lift"""
        period = 1000.0 / rate_hz
        for i, (x, y) in enumerate(pts_mm):
            self.down(t0_ms + i * stagger_ms, slot0 + i, x, y, major)
        t = t0_ms + (len(pts_mm) - 1) * stagger_ms + period
        end = t0_ms + dur_ms
        while t < end:
            for i, (x, y) in enumerate(pts_mm):
                if t >= t0_ms + i * stagger_ms:
                    self.move(t, slot0 + i, x + self.rng.uniform(-jitter_mm, jitter_mm),
                              y + self.rng.uniform(-jitter_mm, jitter_mm), major)
            t += period
        for i in range(len(pts_mm)):
            self.up(end + i * lift_stagger_ms, slot0 + i)
        return end + len(pts_mm) * lift_stagger_ms

    def drag(self, t0_ms, slot, a_mm, b_mm, dur_ms, rate_hz=100, major=None):
        period = 1000.0 / rate_hz
        self.down(t0_ms, slot, a_mm[0], a_mm[1], major)
        n = max(1, int(dur_ms / period))
        for k in range(1, n + 1):
            f = k / n
            self.move(t0_ms + k * period, slot, a_mm[0] + (b_mm[0] - a_mm[0]) * f, a_mm[1] + (b_mm[1] - a_mm[1]) * f,
                      major)
        self.up(t0_ms + dur_ms + period, slot)
        return t0_ms + dur_ms + period

    def tap(self, t_ms, slot, x_mm, y_mm, dur_ms=80, major=None):
        self.down(t_ms, slot, x_mm, y_mm, major)
        self.up(t_ms + dur_ms, slot)

    def palm(self, t0_ms, center_mm, dur_ms, n=4, spread_mm=10, major=150):
        pts = [(center_mm[0] + spread_mm * math.cos(2 * math.pi * k / n),
                center_mm[1] + spread_mm * math.sin(2 * math.pi * k / n)) for k in range(n)]
        return self.hold(t0_ms, pts, dur_ms, stagger_ms=15, jitter_mm=0.8, major=major)

    def segment(self, sid, t0_ms, t1_ms, text=""):
        self.segments.append((int(t0_ms * 1000), int(t1_ms * 1000), sid, text))

    # -- output
    def events(self):
        """(t_us, type, code, value) in kernel order, without resending equal values"""
        out = []
        last = {}  # slot -> {key: value}
        cur_slot = None
        for t in sorted(self.frames):
            changes = self.frames[t]
            emitted = False
            for slot in sorted(changes):
                kv = changes[slot]
                prev = last.setdefault(slot, {})
                todo = []
                if "tid" in kv and kv["tid"] != prev.get("tid"):
                    todo.append((ABS_MT_TRACKING_ID, kv["tid"]))
                for key, code in (("x", ABS_MT_POSITION_X), ("y", ABS_MT_POSITION_Y), ("major", ABS_MT_TOUCH_MAJOR)):
                    if key in kv and kv[key] != prev.get(key):
                        todo.append((code, kv[key]))
                if not todo:
                    continue
                if slot != cur_slot:
                    out.append((t, EV_ABS, ABS_MT_SLOT, slot))
                    cur_slot = slot
                for code, v in todo:
                    out.append((t, EV_ABS, code, v))
                prev.update(kv)
                emitted = True
            if emitted:
                out.append((t, EV_SYN, SYN_REPORT, 0))
        return out

    def to_csv(self, launcher="stopped"):
        b = self.b
        lines = [
            "# mtrec v1",
            "# host=synth kernel=none started=synthetic clock=raw",
            f"# device path=synthetic bus=0x18 vendor=0x0 product=0x0 name={b['name']}",
            f"# mode=mt-b slots={b['slots']} props=direct",
            f"# range x0=0 x1={b['x1']} y0=0 y1={b['y1']}",
            f"# geom board={self.board} panel_mm={b['mm_w']:.1f}x{b['mm_h']:.1f} panel_src=synth swap={b['swap']} "
            f"swap_src=synth ppmm=0",
            f"# launcher={launcher}",
        ]
        rows = [(t, 1, f"ev,{t},{ty},{c},{v},") for t, ty, c, v in self.events()]
        for t0, t1, sid, text in self.segments:
            rows.append((t0, 0, f"seg,{t0},begin,{sid},{text}"))
            rows.append((t1, 2, f"seg,{t1},end,{sid},"))
        rows.sort(key=lambda r: (r[0], r[1]))
        return "\n".join(lines + [r[2] for r in rows]) + "\n"

    def to_raw(self, base_us=0):
        return b"".join(raw_event(base_us + t, ty, c, v) for t, ty, c, v in self.events())


def raw_event(t_us, ty, code, value):
    return EVENT.pack(t_us // 1000000, t_us % 1000000, ty, code, value)


def key_events(t_us, code, press=True):
    """a key press (or release) plus SYN_REPORT, as a keyboard sends it"""
    return raw_event(t_us, EV_KEY, code, 1 if press else 0) + raw_event(t_us, EV_SYN, SYN_REPORT, 0)


def demo(board="deck"):
    """a short labelled session: every class of the guided script once or twice"""
    s = Synth(board)
    w, h = s.b["mm_w"], s.b["mm_h"]
    t = 1000
    s.segment("idle.1", t, t + 2000)
    t += 3000
    s.segment("tap1.1", t, t + 3000)
    for k in range(3):
        s.tap(t + 200 + k * 900, 0, w * 0.5, h * 0.5)
    t += 4000
    s.segment("pinch2.1", t, t + 3000)
    s.down(t + 100, 0, w * 0.4, h * 0.5)
    s.down(t + 120, 1, w * 0.6, h * 0.5)
    for k in range(1, 100):
        f = 0.5 + 0.4 * math.sin(k / 15.0)
        s.move(t + 120 + k * 20, 0, w * (0.5 - 0.2 * f), h * 0.5)
        s.move(t + 120 + k * 20, 1, w * (0.5 + 0.2 * f), h * 0.5)
    s.up(t + 2200, 0)
    s.up(t + 2210, 1)
    t += 4000
    for rep in range(1, 4):
        s.segment(f"hold3.{rep}", t, t + 3500)
        s.hold(t + 300, [(w * 0.3, h * 0.5), (w * 0.5, h * 0.45), (w * 0.7, h * 0.5)], 2000, major=12)
        t += 5000
    s.segment("palm.1", t, t + 4000)
    s.palm(t + 300, (w * 0.5, h * 0.5), 3000, n=5, spread_mm=9, major=160)
    t += 5000
    s.segment("grip.1", t, t + 5000)
    s.hold(t + 300, [(0.5, h * 0.3), (0.8, h * 0.6), (w - 0.6, h * 0.5)], 4200, stagger_ms=60, major=40)
    t += 6000
    s.segment("osk.1", t, t + 4000)
    for k in range(20):
        side = k % 2
        s.tap(t + 200 + k * 160, side, w * (0.25 if side else 0.75), h * 0.8, dur_ms=90)
    t += 5000
    return s


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=["demo"])
    ap.add_argument("--csv")
    ap.add_argument("--raw")
    ap.add_argument("--board", choices=sorted(BOARDS), default="deck")
    ap.add_argument("--base-us", type=int, default=0, help="add to every raw timestamp (e.g. a monotonic time)")
    a = ap.parse_args(argv)
    s = demo(a.board)
    if a.csv:
        with open(a.csv, "w", encoding="utf-8") as f:
            f.write(s.to_csv())
    if a.raw:
        with open(a.raw, "wb") as f:
            f.write(s.to_raw(a.base_us))
    return 0


if __name__ == "__main__":
    sys.exit(main())
