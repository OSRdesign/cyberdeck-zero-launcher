#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Unit tests of mta_analyse (parser, contacts, statistics, the G1 recogniser of study 026 section 5.3) on synthetic
recordings from mta_synth. Host only: python3 -m unittest discover -s tests (from tools/multitasking-spike)."""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import mta_analyse as A  # noqa: E402
import mta_synth as S  # noqa: E402

PPMM_DECK = 11.3  # study 5.3 uses deck pixels


def px(v):
    return v / PPMM_DECK


def run(s, params=None):
    rec = A.parse_recording(s.to_csv().splitlines())
    geom = A.geom_of(rec)
    frames, contacts = A.reconstruct(rec, geom)
    return A.run_g1(frames, params or A.G1Params(t_hold_ms=700), geom), contacts, rec


def positive_case(board="deck", third_at=90, move=None, fourth_at=None, edge=False, close=False, lift2_at=None,
                  major=None):
    """study 5.3: slot 0 at t=0 (200,240), slot 1 at 40 (300,250), slot 2 at 90 (400,245), jitter <= 10 px, lifted
    at 1100; the variants break one rule each."""
    s = S.Synth(board)
    xs = [(200, 240), (300, 250), (400, 245)]
    if edge:
        xs[0] = (10, 240)
    if close:
        xs[1] = (260, 240)  # 60 px from slot 0
    times = [0, 40, third_at]
    for i, ((x, y), t) in enumerate(zip(xs, times)):
        s.down(t, i, px(x), px(y), major)
    t = 10
    while t < 1100:
        for i, ((x, y), t_down) in enumerate(zip(xs, times)):
            if t <= t_down or (lift2_at is not None and i == 2 and t >= lift2_at):
                continue
            j = ((t // 10) % 3 - 1) * 5  # +-5 px jitter
            dx = 50 if (move is not None and i == 1 and t >= move) else 0
            s.move(t, i, px(x + j + dx), px(y - j), major)
        if fourth_at is not None and t == fourth_at:
            s.down(t, 3, px(320), px(400), major)
        t += 10
    if lift2_at is not None:
        s.up(lift2_at, 2)
    for i in range(4):
        s.up(1100, i)
    return s


class Parsing(unittest.TestCase):
    def test_header_and_segments(self):
        text = "\n".join([
            "# mtrec v1",
            "# host=zero7 kernel=6.18 started=x clock=monotonic",
            "# device path=/dev/input/event0 bus=0x18 vendor=0x416 product=0x38f name=11-005d Goodix Capacitive "
            "TouchScreen",
            "# mode=mt-b slots=5 props=direct",
            "# abs code=0x30 name=ABS_MT_TOUCH_MAJOR min=0 max=255 fuzz=0 flat=0 res=0",
            "# range x0=0 x1=639 y0=0 y1=479",
            "# geom board=deck panel_mm=56.6x42.5 panel_src=screen.state swap=1 swap_src=launcher-env ppmm=1130",
            "# launcher=stopped",
            "seg,100,begin,corners.1,ONE finger: touch the TOP-LEFT corner, then TOP-RIGHT",
            "ev,150,3,47,0,EV_ABS:ABS_MT_SLOT",
            "ev,150,3,57,5,EV_ABS:ABS_MT_TRACKING_ID",
            "ev,150,0,0,0,EV_SYN:SYN_REPORT",
            "seg,900,end,corners.1,",
            "# stat rec=total id=total",
        ])
        rec = A.parse_recording(text.splitlines())
        self.assertEqual(rec.header["name"], "11-005d Goodix Capacitive TouchScreen")
        self.assertEqual(rec.header["geom.board"], "deck")
        self.assertEqual(rec.header["range.x1"], "639")
        self.assertEqual(rec.abs["ABS_MT_TOUCH_MAJOR"]["max"], 255)
        self.assertEqual(len(rec.events), 3)
        self.assertEqual(rec.segments[0].label, "corners")
        self.assertIn("TOP-RIGHT", rec.segments[0].text)
        g = A.geom_of(rec)
        self.assertEqual((g.mm_w, g.mm_h, g.swap), (56.6, 42.5, 1))

    def test_geom_roundtrip(self):
        for board in ("deck", "pi3a"):
            g = S.Synth(board).geom
            x, y = g.from_mm(20.0, 30.0)
            mx, my = g.to_mm(x, y)
            self.assertAlmostEqual(mx, 20.0, delta=0.15)
            self.assertAlmostEqual(my, 30.0, delta=0.15)


class Contacts(unittest.TestCase):
    def test_counts_ghosts_major(self):
        s = S.Synth("deck")
        s.tap(100, 0, 20, 20, dur_ms=100, major=10)
        s.tap(400, 0, 25, 20, dur_ms=20, major=30)  # short-lived
        rec = A.parse_recording(s.to_csv().splitlines())
        frames, contacts = A.reconstruct(rec, A.geom_of(rec))
        self.assertEqual(len(contacts), 2)
        self.assertEqual(sum(1 for c in contacts if c.duration_us < A.GHOST_US), 1)
        self.assertEqual([c.major_max for c in contacts], [10, 30])
        st = A.window_stats(frames, contacts, 0, 10**9)
        self.assertEqual(st["max_fingers"], 1)
        self.assertEqual(st["short_lt30ms"], 1)

    def test_spacing_and_travel(self):
        s = S.Synth("pi3a")
        s.hold(0, [(10, 10), (22, 10)], 500, jitter_mm=0)
        s.drag(1000, 0, (10, 30), (40, 30), 300)
        rec = A.parse_recording(s.to_csv().splitlines())
        frames, contacts = A.reconstruct(rec, A.geom_of(rec))
        st = A.window_stats(frames, contacts, 0, 900000)
        self.assertAlmostEqual(st["min_pair_mm"], 12.0, delta=0.2)
        drag = contacts[-1]
        self.assertAlmostEqual(drag.max_disp_mm, 30.0, delta=0.3)


class G1Cases(unittest.TestCase):
    """study 026 section 5.3, on both coordinate layouts (deck: swapped axes; Pi 3A+: buffer)"""

    def outcome(self, s, params=None):
        sessions, _, _ = run(s, params)
        self.assertEqual(len(sessions), 1, sessions)
        return sessions[0]

    def test_positive_fires_at_790(self):
        for board in ("deck", "pi3a"):
            o = self.outcome(positive_case(board))
            self.assertEqual(o["outcome"], "fired", (board, o))
            self.assertEqual(o["t_outcome"], 790000)

    def test_owner_d2_one_second(self):
        o = self.outcome(positive_case(), A.G1Params(t_hold_ms=1000))
        self.assertEqual((o["outcome"], o["t_outcome"]), ("fired", 1090000))

    def test_negatives(self):
        cases = {
            "land": dict(third_at=300),
            "move": dict(move=400),
            "extra": dict(fourth_at=500),
            "edge": dict(edge=True),
            "sep": dict(close=True),
            "lift": dict(lift2_at=600),
        }
        for board in ("deck", "pi3a"):
            for reason, kw in cases.items():
                o = self.outcome(positive_case(board, **kw))
                self.assertEqual((o["outcome"], o["reason"]), ("rejected", reason), (board, reason, o))

    def test_palm_major(self):
        s = positive_case("deck", major=200)
        o = self.outcome(s, A.G1Params(t_hold_ms=700, major_max=100))
        self.assertEqual(o["reason"], "major")
        o = self.outcome(positive_case("deck", major=40), A.G1Params(t_hold_ms=700, major_max=100))
        self.assertEqual(o["outcome"], "fired")

    def test_pinch_osk_ghost_never_fire(self):
        for board in ("deck", "pi3a"):
            s = S.Synth(board)
            s.down(0, 0, 20, 20)
            s.down(30, 1, 35, 20)
            for k in range(1, 100):
                s.move(30 + k * 10, 0, 20 - k * 0.05, 20)
                s.move(30 + k * 10, 1, 35 + k * 0.05, 20)
            s.tap(500, 2, 28, 30, dur_ms=20)  # ghost during the pinch
            s.up(1100, 0)
            s.up(1100, 1)
            for k in range(18):  # two thumbs, 80 ms taps, alternating, 3 s
                s.tap(2000 + k * 160, k % 2, 12 if k % 2 else 40, 35, dur_ms=80)
            sessions, _, _ = run(s)
            self.assertFalse([x for x in sessions if x["outcome"] == "fired"], (board, sessions))


class Segments(unittest.TestCase):
    def test_demo_rates_and_sweep(self):
        for board in ("deck", "pi3a"):
            s = S.demo(board)
            rec = A.parse_recording(s.to_csv().splitlines())
            res = A.analyse_recording(rec, sweep="one")
            per = res["g1_base"]["per_label"]
            self.assertEqual(per["hold3"]["fired"], 3, (board, per))
            for lab in ("idle", "tap1", "pinch2", "palm", "grip", "osk"):
                self.assertEqual(per[lab]["fired"], 0, (board, lab, per))
            summ = res["g1_base"]["summary"]
            self.assertEqual((summ["hit_rate"], summ["fp_rate"]), (1.0, 0.0))
            # the grip lies on the edge: without the edge rule it fires (a false positive the sweep must show)
            edge0 = [r for r in res["sweep"] if r["params"]["e_edge_mm"] == 0][0]
            self.assertEqual(edge0["fp_by_label"].get("grip"), 1, edge0)
            # the palm has 5 contacts: rejected as "extra"
            self.assertEqual(res["labels"]["palm"]["max_fingers_max"], 5)
            # spacing of the three landing fingers (0.3/0.5/0.7 of the width, 0.05 of the height apart)
            w, h = s.b["mm_w"], s.b["mm_h"]
            expect = ((0.2 * w) ** 2 + (0.05 * h) ** 2) ** 0.5
            self.assertAlmostEqual(res["three_finger_landing"]["min_pair_mm_min"], expect, delta=0.6)
            if board == "deck":
                self.assertEqual(res["labels"]["palm"]["major_max"], 160)
                self.assertEqual(res["labels"]["hold3"]["major_max"], 12)
            else:
                self.assertFalse(res["has_major"])
            self.assertIn("hold3", A.text_report(res))

    def test_corner_check(self):
        s = S.Synth("deck")
        w, h = s.b["mm_w"], s.b["mm_h"]
        for k, (x, y) in enumerate([(3, 3), (w - 3, 3), (w - 3, h - 3), (3, h - 3)]):
            s.hold(1000 + k * 1000, [(x, y)], 600)
        s.segment("corners.1", 900, 5000)
        rec = A.parse_recording(s.to_csv().splitlines())
        self.assertTrue(A.analyse_recording(rec, sweep="none")["corners"]["ok"])
        self.assertFalse(A.analyse_recording(rec, swap=0, sweep="none")["corners"]["ok"])

    def test_main_writes_json(self):
        import json
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "r.csv")
            with open(p, "w") as f:
                f.write(S.demo("deck").to_csv())
            out = os.path.join(d, "r.json")
            import contextlib
            import io
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(A.main([p, "--json", out, "--sweep", "none"]), 0)
            with open(out) as f:
                data = json.load(f)
            self.assertEqual(data["results"][0]["board"], "deck")


if __name__ == "__main__":
    unittest.main()
