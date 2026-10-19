#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mta_analyse: turn mtrec recordings into the P-a numbers of study 026 (section 5, risk 3).

  python3 mta_analyse.py REC.csv [REC2.csv ...] [--json OUT.json] [--sweep none|one|grid]
                         [--panel-mm WxH] [--swap 0|1] [--t-hold MS] [--major-max N]

For every recording (one board each):
  * per labelled step (mtrec --guided): most fingers at once, contacts, short-lived contacts (< 30 ms), closest two
    fingers (mm), how far fingers moved, TOUCH_MAJOR of each contact (Goodix only);
  * per step class (hold3, palm, grip, osk, ...): the same, pooled over the repetitions;
  * the three-finger-hold recogniser G1 exactly as study section 5.2 specifies it, with the owner's D2 value
    (hold 1000 ms) and the study's thresholds: for each class, how many repetitions fired. For the hold3* classes that
    is the hit rate; for every other class it is the false-positive rate per labelled segment;
  * the spacing of three fingers when they land (minimum pairwise distance), and why missed holds were rejected;
  * a threshold sweep (one parameter at a time by default) so the thresholds can be chosen from data.

Standard library only (Python 3.9+). The recogniser class G1 is the reference the C recogniser of phase P-c can be
checked against.
"""
import argparse
import itertools
import json
import math
import os
import statistics
import sys
from dataclasses import asdict, dataclass, field, replace
from typing import Dict, List, Optional, Tuple

GHOST_US = 30000
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
SYN_REPORT, SYN_MT_REPORT, SYN_DROPPED = 0, 2, 3
ABS_X, ABS_Y, ABS_PRESSURE = 0x00, 0x01, 0x18
ABS_MT_SLOT, ABS_MT_TOUCH_MAJOR, ABS_MT_WIDTH_MAJOR = 0x2F, 0x30, 0x32
ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID, ABS_MT_PRESSURE = 0x35, 0x36, 0x39, 0x3A
BTN_TOUCH = 0x14A

POSITIVE_PREFIX = "hold3"

# board presets (panel size in display orientation, mm; raw X along the display height?)
PRESETS = {
    "deck": (56.6, 42.5, 1),
    "pi3a": (94.0, 56.0, 0),
    "pi3a-wsdsi43": (94.0, 56.0, 0),
    "hackberry": (72.0, 72.0, 0),
    "hackberry-cm5": (72.0, 72.0, 0),
}


# ----------------------------------------------------------------------------------------------- parsing
@dataclass
class Segment:
    id: str
    label: str
    text: str
    t0: int
    t1: int


@dataclass
class Recording:
    path: str = ""
    header: Dict[str, str] = field(default_factory=dict)
    abs: Dict[str, Dict[str, int]] = field(default_factory=dict)
    events: List[Tuple[int, int, int, int]] = field(default_factory=list)
    segments: List[Segment] = field(default_factory=list)
    notes: List[Tuple[int, str]] = field(default_factory=list)


def _kv(tokens):
    out = {}
    for tok in tokens:
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def parse_recording(lines, path=""):
    """Parses mtrec CSV lines (an iterable of str)."""
    rec = Recording(path=path)
    open_segs = {}
    for raw in lines:
        line = raw.rstrip("\r\n")
        if not line:
            continue
        if line.startswith("#"):
            body = line[1:].strip()
            if body.startswith("stat ") or body.startswith("columns"):
                continue
            if body.startswith("device "):
                rest = body[len("device "):]
                head, _, name = rest.partition(" name=")
                rec.header.update(_kv(head.split()))
                rec.header["name"] = name
            elif body.startswith("abs "):
                kv = _kv(body.split()[1:])
                rec.abs[kv.get("name", kv.get("code", "?"))] = {
                    k: int(v, 0) for k, v in kv.items() if k not in ("name",)
                }
            else:
                first = body.split()[0] if body.split() else ""
                kv = _kv(body.split())
                if first in ("geom", "range"):
                    kv = {first + "." + k: v for k, v in kv.items()}
                rec.header.update(kv)
                if first.startswith("mtrec"):
                    rec.header["version"] = first
            continue
        parts = line.split(",", 5)
        kind = parts[0]
        try:
            t = int(parts[1])
        except (IndexError, ValueError):
            continue
        if kind == "ev" and len(parts) >= 5:
            rec.events.append((t, int(parts[2]), int(parts[3]), int(parts[4])))
        elif kind == "seg" and len(parts) >= 4:
            edge, sid = parts[2], parts[3]
            text = ",".join(parts[4:]) if len(parts) > 4 else ""
            if edge == "begin":
                open_segs[sid] = (t, text)
            elif edge == "end" and sid in open_segs:
                t0, txt = open_segs.pop(sid)
                rec.segments.append(Segment(sid, sid.split(".")[0], txt, t0, t))
        elif kind == "note":
            rec.notes.append((t, ",".join(parts[2:])))
    last_t = rec.events[-1][0] if rec.events else 0
    for sid, (t0, txt) in open_segs.items():  # a recording stopped inside a step
        rec.segments.append(Segment(sid, sid.split(".")[0], txt, t0, max(t0, last_t)))
    rec.segments.sort(key=lambda s: s.t0)
    return rec


def load_recording(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return parse_recording(f, path)


# ----------------------------------------------------------------------------------------------- geometry
@dataclass
class Geom:
    x0: int
    x1: int
    y0: int
    y1: int
    mm_w: float
    mm_h: float
    swap: int

    def to_mm(self, x, y):
        sx = (x - self.x0) / (self.x1 - self.x0) if self.x1 > self.x0 else 0.0
        sy = (y - self.y0) / (self.y1 - self.y0) if self.y1 > self.y0 else 0.0
        if self.swap:
            return sy * self.mm_w, sx * self.mm_h
        return sx * self.mm_w, sy * self.mm_h

    def from_mm(self, mx, my):
        """display mm -> raw (for synthetic recordings)"""
        fx, fy = mx / self.mm_w, my / self.mm_h
        if self.swap:
            fx, fy = fy, fx
        return (round(self.x0 + fx * (self.x1 - self.x0)), round(self.y0 + fy * (self.y1 - self.y0)))


def geom_of(rec, panel_mm=None, swap=None):
    h = rec.header
    x0 = int(h.get("range.x0", 0))
    x1 = int(h.get("range.x1", 639))
    y0 = int(h.get("range.y0", 0))
    y1 = int(h.get("range.y1", 479))
    board = h.get("geom.board", "unknown")
    w = hh = None
    if panel_mm:
        w, hh = (float(v) for v in panel_mm.lower().split("x"))
    elif "geom.panel_mm" in h:
        w, hh = (float(v) for v in h["geom.panel_mm"].lower().split("x"))
    elif board in PRESETS:
        w, hh = PRESETS[board][0], PRESETS[board][1]
    else:
        w, hh = (x1 - x0 + 1) / 10.0, (y1 - y0 + 1) / 10.0
    if swap is None:
        if "geom.swap" in h:
            swap = int(h["geom.swap"])
        else:
            swap = PRESETS.get(board, (0, 0, 0))[2]
    return Geom(x0, x1, y0, y1, w, hh, int(swap))


# ----------------------------------------------------------------------------------------------- contacts
@dataclass
class Contact:
    id: int
    slot: int
    t_start: int
    t_end: int = -1
    x0: float = 0.0  # landing position, mm (display orientation)
    y0: float = 0.0
    landed: bool = False
    path_mm: float = 0.0
    max_disp_mm: float = 0.0
    major_max: int = -1
    major_min: int = -1
    seg: int = -1
    _lx: float = 0.0
    _ly: float = 0.0

    @property
    def duration_us(self):
        return self.t_end - self.t_start


@dataclass
class Frame:
    t: int
    pts: Dict[int, Tuple[float, float, int]]  # contact id -> (x_mm, y_mm, major or -1)


def reconstruct(rec, geom):
    """Replays the events: frames (one per SYN_REPORT, with every contact down) and contacts (protocol B, single
    touch; protocol A gives frames with anonymous points)."""
    mode = rec.header.get("mode", "mt-b")
    frames: List[Frame] = []
    contacts: List[Contact] = []
    nslots = max(1, int(rec.header.get("slots", 10)))
    sx = [0] * max(nslots, 16)
    sy = [0] * max(nslots, 16)
    smaj = [-1] * max(nslots, 16)
    active: Dict[int, Contact] = {}  # slot -> contact
    cur = 0
    in_drop = False
    next_id = 1
    a_pts = []
    a_has = False
    a_x = a_y = 0

    def end(slot, t):
        c = active.pop(slot, None)
        if c is None:
            return
        if not c.landed:
            c.x0, c.y0 = geom.to_mm(sx[slot], sy[slot])
            c.landed = True
        c.t_end = t
        contacts.append(c)

    def start(slot, t):
        nonlocal next_id
        c = Contact(id=next_id, slot=slot, t_start=t)
        next_id += 1
        active[slot] = c

    st_down = False
    for t, typ, code, val in rec.events:
        if typ == EV_SYN:
            if code == SYN_DROPPED:
                in_drop = True
                for s in list(active):
                    end(s, t)
                st_down = False
            elif code == SYN_REPORT:
                if in_drop:
                    in_drop = False
                    continue
                if mode == "mt-a":
                    frames.append(Frame(t, {i: (p[0], p[1], -1) for i, p in enumerate(a_pts)}))
                    a_pts = []
                    continue
                pts = {}
                for s, c in active.items():
                    mx, my = geom.to_mm(sx[s], sy[s])
                    if not c.landed:
                        c.x0, c.y0, c._lx, c._ly, c.landed = mx, my, mx, my, True
                    else:
                        c.path_mm += math.hypot(mx - c._lx, my - c._ly)
                        c._lx, c._ly = mx, my
                        c.max_disp_mm = max(c.max_disp_mm, math.hypot(mx - c.x0, my - c.y0))
                    m = smaj[s]
                    if m >= 0:
                        c.major_max = max(c.major_max, m)
                        c.major_min = m if c.major_min < 0 else min(c.major_min, m)
                    pts[c.id] = (mx, my, m)
                frames.append(Frame(t, pts))
            elif code == SYN_MT_REPORT and mode == "mt-a":
                if a_has:
                    a_pts.append(geom.to_mm(a_x, a_y))
                a_has = False
            continue
        if in_drop:
            continue
        if mode == "mt-a":
            if typ == EV_ABS and code == ABS_MT_POSITION_X:
                a_x, a_has = val, True
            elif typ == EV_ABS and code == ABS_MT_POSITION_Y:
                a_y, a_has = val, True
            continue
        if mode == "single":
            if typ == EV_KEY and code == BTN_TOUCH:
                if val and not st_down:
                    start(0, t)
                if not val and st_down:
                    end(0, t)
                st_down = bool(val)
            elif typ == EV_ABS and code == ABS_X:
                sx[0] = val
            elif typ == EV_ABS and code == ABS_Y:
                sy[0] = val
            continue
        if typ != EV_ABS:
            continue
        if code == ABS_MT_SLOT:
            cur = val if 0 <= val < len(sx) else -1
            continue
        if cur < 0:
            continue
        if code == ABS_MT_TRACKING_ID:
            if val < 0:
                end(cur, t)
            else:
                end(cur, t)
                start(cur, t)
        elif code == ABS_MT_POSITION_X:
            sx[cur] = val
        elif code == ABS_MT_POSITION_Y:
            sy[cur] = val
        elif code == ABS_MT_TOUCH_MAJOR:
            smaj[cur] = val
    t_last = rec.events[-1][0] if rec.events else 0
    for s in list(active):
        end(s, t_last)
    contacts.sort(key=lambda c: c.t_start)
    return frames, contacts


def assign_segments(rec, contacts):
    """A contact belongs to the step it started in; one that started outside any step and is still down when a
    step begins belongs to that step (fingers put down a little before GO), as mtrec counts it."""
    segs = rec.segments
    for c in contacts:
        c.seg = -1
        for i, s in enumerate(segs):
            if s.t0 <= c.t_start <= s.t1:
                c.seg = i
                break
        if c.seg < 0:
            for i, s in enumerate(segs):
                if c.t_start < s.t0 <= c.t_end:
                    c.seg = i
                    break


# ----------------------------------------------------------------------------------------------- statistics
def pct(values, p):
    if not values:
        return None
    v = sorted(values)
    k = (len(v) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return v[lo] if lo == hi else v[lo] + (v[hi] - v[lo]) * (k - lo)


def min_pair(pts):
    best = None
    items = list(pts.values())
    for a, b in itertools.combinations(items, 2):
        d = math.hypot(a[0] - b[0], a[1] - b[1])
        best = d if best is None or d < best else best
    return best


def window_stats(frames, contacts, t0, t1, seg_index=None):
    fr = [f for f in frames if t0 <= f.t <= t1]
    cs = [c for c in contacts if (c.seg == seg_index if seg_index is not None else t0 <= c.t_start <= t1)]
    max_f = max((len(f.pts) for f in fr), default=0)
    pairs = [d for d in (min_pair(f.pts) for f in fr) if d is not None]
    majors = [c.major_max for c in cs if c.major_max >= 0]
    return {
        "max_fingers": max_f,
        "contacts": len(cs),
        "short_lt30ms": sum(1 for c in cs if c.duration_us < GHOST_US),
        "min_pair_mm": round(min(pairs), 2) if pairs else None,
        "max_disp_mm": round(max((c.max_disp_mm for c in cs), default=0.0), 2) if cs else None,
        "major_max": max(majors) if majors else None,
        "major_min": min(c.major_min for c in cs if c.major_min >= 0) if majors else None,
        "contact_majors": majors,
        "durations_ms": [round(c.duration_us / 1000.0, 1) for c in cs],
    }


# ----------------------------------------------------------------------------------------------- G1 recogniser
@dataclass(frozen=True)
class G1Params:
    t_land_ms: float = 250
    t_hold_ms: float = 1000  # owner decision D2 (study proposed 700)
    d_still_mm: float = 4
    d_sep_mm: float = 8
    e_edge_mm: float = 2
    major_max: Optional[int] = None  # Goodix only; None = not used
    need: int = 3

    def short(self):
        mm = "off" if self.major_max is None else str(self.major_max)
        return (f"hold={self.t_hold_ms:g} land={self.t_land_ms:g} still={self.d_still_mm:g} sep={self.d_sep_mm:g} "
                f"edge={self.e_edge_mm:g} major={mm}")


class G1:
    """Three-finger hold, study 026 section 5.2. Feed frames in order (t in microseconds, points in display mm);
    call finish() at the end. Outcomes are appended to self.sessions."""

    IDLE, TRACKING, ARMED, DONE = "idle", "tracking", "armed", "done"

    def __init__(self, p: G1Params, mm_w: float, mm_h: float):
        self.p, self.w, self.h = p, mm_w, mm_h
        self.state = self.IDLE
        self.sessions = []
        self._s = None

    def _close(self, outcome, t, reason=None):
        self._s.update(outcome=outcome, t_outcome=t, reason=reason)
        self.state = self.DONE

    def _edge(self, x, y):
        e = self.p.e_edge_mm
        return x < e or y < e or x > self.w - e or y > self.h - e

    def frame(self, t, pts):
        p = self.p
        if self.state == self.IDLE:
            if not pts:
                return
            self._s = {"t0": t, "landing": {}, "majors": {}, "max_n": 0, "outcome": None, "t_outcome": None,
                       "reason": None, "t3": None, "arm_min_pair_mm": None, "t_end": None}
            self.sessions.append(self._s)
            self.state = self.TRACKING
        s = self._s
        s["max_n"] = max(s["max_n"], len(pts))
        new_ids = [cid for cid in pts if cid not in s["landing"]]
        for cid, (x, y, m) in pts.items():
            if cid not in s["landing"]:
                s["landing"][cid] = (x, y)
            if m is not None and m >= 0:
                s["majors"][cid] = max(s["majors"].get(cid, -1), m)
        if self.state == self.ARMED and t >= s["t3"] + p.t_hold_ms * 1000:
            self._close("fired", int(s["t3"] + p.t_hold_ms * 1000))
        if self.state == self.TRACKING:
            lifted = [cid for cid in s["landing"] if cid not in pts]
            if len(s["landing"]) > p.need:
                self._close("rejected", t, "extra")
            elif lifted:
                self._close("rejected", t, "lift")
            elif len(pts) == p.need:
                t3 = t
                s["t3"] = t3
                ps = list(pts.values())
                mp = min(math.hypot(a[0] - b[0], a[1] - b[1]) for a, b in itertools.combinations(ps, 2))
                s["arm_min_pair_mm"] = mp
                if t3 - s["t0"] > p.t_land_ms * 1000:
                    self._close("rejected", t, "land")
                elif mp < p.d_sep_mm:
                    self._close("rejected", t, "sep")
                elif any(self._edge(x, y) for (x, y, _m) in ps):
                    self._close("rejected", t, "edge")
                elif p.major_max is not None and any(s["majors"].get(cid, -1) > p.major_max for cid in pts):
                    self._close("rejected", t, "major")
                else:
                    self.state = self.ARMED
            elif t - s["t0"] > p.t_land_ms * 1000:
                self._close("rejected", t, "land")
        elif self.state == self.ARMED:
            if new_ids or len(pts) > p.need:
                self._close("rejected", t, "extra")
            elif len(pts) < p.need:
                self._close("rejected", t, "lift")
            else:
                for cid, (x, y, _m) in pts.items():
                    lx, ly = s["landing"][cid]
                    if math.hypot(x - lx, y - ly) > p.d_still_mm:
                        self._close("rejected", t, "move")
                        break
        if not pts and self.state in (self.DONE, self.TRACKING, self.ARMED):
            if self.state != self.DONE:
                self._close("none", t)
            s["t_end"] = t
            self.state = self.IDLE

    def finish(self, t):
        if self.state == self.ARMED and t >= self._s["t3"] + self.p.t_hold_ms * 1000:
            self._close("fired", int(self._s["t3"] + self.p.t_hold_ms * 1000))
        if self._s is not None and self._s["t_end"] is None:
            if self._s["outcome"] is None:
                self._s["outcome"] = "none"
            self._s["t_end"] = t


def run_g1(frames, params, geom):
    g = G1(params, geom.mm_w, geom.mm_h)
    for f in frames:
        g.frame(f.t, f.pts)
    g.finish(frames[-1].t if frames else 0)
    return g.sessions


def evaluate(rec, frames, params, geom):
    """G1 outcome per labelled segment: {label: {reps, fired, rate, positive}}, plus fires outside segments."""
    sessions = run_g1(frames, params, geom)
    fires = [s["t_outcome"] for s in sessions if s["outcome"] == "fired"]
    per = {}
    covered = set()
    for seg in rec.segments:
        hit = [t for t in fires if seg.t0 <= t <= seg.t1]
        covered.update(hit)
        d = per.setdefault(seg.label, {"reps": 0, "fired": 0, "positive": seg.label.startswith(POSITIVE_PREFIX)})
        d["reps"] += 1
        d["fired"] += 1 if hit else 0
    for d in per.values():
        d["rate"] = round(d["fired"] / d["reps"], 3) if d["reps"] else None
    reasons = {}
    for seg in rec.segments:
        if not seg.label.startswith(POSITIVE_PREFIX):
            continue
        ss = [s for s in sessions if seg.t0 - 300000 <= s["t0"] <= seg.t1]
        if any(s["outcome"] == "fired" for s in ss):
            continue
        for s in ss:
            r = s["reason"] or s["outcome"]
            reasons[r] = reasons.get(r, 0) + 1
    return {
        "params": asdict(params),
        "per_label": per,
        "fires_outside_segments": len([t for t in fires if t not in covered]),
        "hold3_miss_reasons": reasons,
        "sessions": sessions,
    }


def summarise_eval(ev):
    pos = [d for d in ev["per_label"].values() if d["positive"]]
    neg = [d for d in ev["per_label"].values() if not d["positive"]]
    pr, pf = sum(d["reps"] for d in pos), sum(d["fired"] for d in pos)
    nr, nf = sum(d["reps"] for d in neg), sum(d["fired"] for d in neg)
    return {
        "hold3_hits": pf, "hold3_reps": pr, "hit_rate": round(pf / pr, 3) if pr else None,
        "negative_fired": nf, "negative_reps": nr, "fp_rate": round(nf / nr, 3) if nr else None,
        "fires_outside_segments": ev["fires_outside_segments"],
    }


def sweep_configs(base, kind, majors):
    if kind == "none":
        return []
    axes = {
        "t_hold_ms": [500, 700, 1000, 1500],
        "t_land_ms": [150, 250, 400, 600],
        "d_still_mm": [2, 3, 4, 6, 8],
        "d_sep_mm": [5, 6, 8, 10, 12],
        "e_edge_mm": [0, 1, 2, 3],
        "major_max": [None] + sorted(set(majors)),
    }
    if kind == "one":
        out = []
        for k, vals in axes.items():
            for v in vals:
                c = replace(base, **{k: v})
                if c != base and c not in out:
                    out.append(c)
        return out
    keys = list(axes)
    return [G1Params(**dict(zip(keys, combo))) for combo in itertools.product(*(axes[k] for k in keys))]


# ----------------------------------------------------------------------------------------------- analysis
def corner_check(rec, contacts, geom):
    seg = next((s for s in rec.segments if s.label == "corners"), None)
    if seg is None:
        return None
    cs = [c for c in contacts if seg.t0 <= c.t_start <= seg.t1 and c.duration_us >= 200000][:4]
    if len(cs) < 4:
        return {"ok": None, "note": f"{len(cs)} corner touches found (4 expected)"}
    names = ["top-left", "top-right", "bottom-right", "bottom-left"]
    pts = [(round(c.x0, 1), round(c.y0, 1)) for c in cs]
    # TL->TR must move along display X, TR->BR along display Y (inversion does not matter for distances)
    dx1, dy1 = abs(pts[1][0] - pts[0][0]), abs(pts[1][1] - pts[0][1])
    dx2, dy2 = abs(pts[2][0] - pts[1][0]), abs(pts[2][1] - pts[1][1])
    ok = dx1 > dy1 and dy2 > dx2
    return {
        "ok": ok,
        "points_mm": dict(zip(names, pts)),
        "note": "axes match the swap setting" if ok else "TL->TR did not run along the display width: try --swap "
                                                         + ("0" if geom.swap else "1"),
    }


def analyse_recording(rec, panel_mm=None, swap=None, base=None, sweep="one", keep_sessions=False):
    geom = geom_of(rec, panel_mm, swap)
    frames, contacts = reconstruct(rec, geom)
    assign_segments(rec, contacts)
    base = base or G1Params()
    out = {
        "file": rec.path,
        "board": rec.header.get("geom.board", "unknown"),
        "device": rec.header.get("name", ""),
        "mode": rec.header.get("mode", "mt-b"),
        "slots": int(rec.header.get("slots", 0) or 0),
        "panel_mm": [geom.mm_w, geom.mm_h],
        "swap": geom.swap,
        "has_major": any(c.major_max >= 0 for c in contacts),
        "duration_s": round((rec.events[-1][0] if rec.events else 0) / 1e6, 1),
        "launcher": rec.header.get("launcher", "?"),
        "total": {k: v for k, v in window_stats(frames, contacts, -10**15, 10**15).items()
                  if k not in ("contact_majors", "durations_ms")},
        "segments": [],
        "labels": {},
    }
    for i, seg in enumerate(rec.segments):
        st = window_stats(frames, contacts, seg.t0, seg.t1, seg_index=i)
        out["segments"].append({"id": seg.id, "label": seg.label, "t0_s": round(seg.t0 / 1e6, 2),
                                "dur_s": round((seg.t1 - seg.t0) / 1e6, 2),
                                **{k: v for k, v in st.items() if k not in ("contact_majors", "durations_ms")}})
        lab = out["labels"].setdefault(seg.label, {"reps": 0, "max_fingers": [], "contacts": 0, "short_lt30ms": 0,
                                                  "min_pair_mm": [], "max_disp_mm": [], "majors": []})
        lab["reps"] += 1
        lab["max_fingers"].append(st["max_fingers"])
        lab["contacts"] += st["contacts"]
        lab["short_lt30ms"] += st["short_lt30ms"]
        if st["min_pair_mm"] is not None:
            lab["min_pair_mm"].append(st["min_pair_mm"])
        if st["max_disp_mm"] is not None:
            lab["max_disp_mm"].append(st["max_disp_mm"])
        lab["majors"].extend(st["contact_majors"])
    for lab in out["labels"].values():
        mj = lab.pop("majors")
        lab["major_p50"] = pct(mj, 50)
        lab["major_p95"] = pct(mj, 95)
        lab["major_max"] = max(mj) if mj else None
        lab["max_fingers_max"] = max(lab["max_fingers"]) if lab["max_fingers"] else 0
        lab["min_pair_mm_min"] = min(lab["min_pair_mm"]) if lab["min_pair_mm"] else None
        lab["max_disp_mm_p95"] = pct(lab["max_disp_mm"], 95)

    # three fingers landing: spacing at the moment the third lands (all hold3* steps)
    sessions = run_g1(frames, replace(base, t_land_ms=10**9, d_sep_mm=0, e_edge_mm=0, major_max=None), geom)
    spacing = []
    for seg in rec.segments:
        if not seg.label.startswith(POSITIVE_PREFIX):
            continue
        for s in sessions:
            if seg.t0 - 300000 <= s["t0"] <= seg.t1 and s["arm_min_pair_mm"] is not None:
                spacing.append((seg.label, s["arm_min_pair_mm"], (s["t3"] - s["t0"]) / 1000.0))
                break
    out["three_finger_landing"] = {
        "count": len(spacing),
        "min_pair_mm_min": round(min(v for _, v, _ in spacing), 2) if spacing else None,
        "min_pair_mm_p10": round(pct([v for _, v, _ in spacing], 10), 2) if spacing else None,
        "min_pair_mm_median": round(pct([v for _, v, _ in spacing], 50), 2) if spacing else None,
        "land_spread_ms_median": round(pct([w for _, _, w in spacing], 50), 1) if spacing else None,
        "land_spread_ms_max": round(max(w for _, _, w in spacing), 1) if spacing else None,
        "by_label": {lab: [round(v, 2) for l2, v, _ in spacing if l2 == lab] for lab in {l for l, _, _ in spacing}},
    }
    out["corners"] = corner_check(rec, contacts, geom)

    ev = evaluate(rec, frames, base, geom)
    if not keep_sessions:
        ev.pop("sessions")
    out["g1_base"] = {**ev, "summary": summarise_eval(ev), "label": base.short()}
    finger_majors = [c.major_max for c in contacts if c.major_max >= 0 and c.seg >= 0
                     and rec.segments[c.seg].label.startswith(POSITIVE_PREFIX)]
    cand = [int(v) for v in (pct(finger_majors, 95), pct(finger_majors, 100)) if v is not None]
    if cand:
        cand += [int(cand[-1] * 1.5)]
    rows = []
    for p in sweep_configs(base, sweep, cand if out["has_major"] else []):
        e = evaluate(rec, frames, p, geom)
        rows.append({"params": asdict(p), "label": p.short(), **summarise_eval(e),
                     "fp_by_label": {k: v["fired"] for k, v in e["per_label"].items() if not v["positive"] and v["fired"]}})
    out["sweep"] = rows
    return out


# ----------------------------------------------------------------------------------------------- report
def _f(v, nd=1):
    if v is None:
        return "-"
    if isinstance(v, float):
        return f"{v:.{nd}f}"
    return str(v)


def text_report(res):
    L = []
    L.append(f"== {os.path.basename(res['file'])}: board {res['board']}, \"{res['device']}\" ({res['mode']}, "
             f"{res['slots']} slots), panel {res['panel_mm'][0]:.1f} x {res['panel_mm'][1]:.1f} mm, swap {res['swap']},"
             f" {res['duration_s']} s, launcher {res['launcher']}")
    if not res["has_major"]:
        L.append("   no TOUCH_MAJOR on this controller: palm rejection must use contact count and spacing")
    t = res["total"]
    L.append(f"   total: max {t['max_fingers']} fingers, {t['contacts']} contacts, {t['short_lt30ms']} short (<30 ms), "
             f"closest pair {_f(t['min_pair_mm'])} mm")
    if res["corners"]:
        L.append(f"   corners: {res['corners'].get('note')}")
    L.append("")
    L.append(f"   {'class':<12} {'reps':>4} {'fingers':>7} {'contacts':>8} {'short':>5} {'gap_min':>7} "
             f"{'move_p95':>8} {'major p50/p95/max':>18}")
    for name, lab in res["labels"].items():
        mj = (f"{_f(lab['major_p50'], 0)}/{_f(lab['major_p95'], 0)}/{_f(lab['major_max'])}"
              if lab["major_max"] is not None else "-")
        L.append(f"   {name:<12} {lab['reps']:>4} {lab['max_fingers_max']:>7} {lab['contacts']:>8} "
                 f"{lab['short_lt30ms']:>5} {_f(lab['min_pair_mm_min']):>7} {_f(lab['max_disp_mm_p95']):>8} {mj:>18}")
    tf = res["three_finger_landing"]
    L.append("")
    L.append(f"   three fingers landing (hold3*): {tf['count']} holds, closest pair min {_f(tf['min_pair_mm_min'])} / "
             f"p10 {_f(tf['min_pair_mm_p10'])} / median {_f(tf['min_pair_mm_median'])} mm; spread between first and "
             f"third finger median {_f(tf['land_spread_ms_median'])} ms, max {_f(tf['land_spread_ms_max'])} ms")
    g = res["g1_base"]
    s = g["summary"]
    L.append("")
    L.append(f"   G1 three-finger hold [{g['label']}]:")
    L.append(f"     hold3* hit {s['hold3_hits']}/{s['hold3_reps']} ({_f(s['hit_rate'], 2)}); false positives "
             f"{s['negative_fired']}/{s['negative_reps']} labelled segments ({_f(s['fp_rate'], 3)}); fires between "
             f"steps {s['fires_outside_segments']}")
    for name, d in g["per_label"].items():
        L.append(f"     {name:<12} {'HIT ' if d['positive'] else 'FP  '} {d['fired']}/{d['reps']}")
    if g["hold3_miss_reasons"]:
        L.append(f"     missed holds rejected by: {g['hold3_miss_reasons']}")
    if res["sweep"]:
        L.append("")
        L.append("   sweep (one parameter at a time around the line above unless --sweep grid):")
        L.append(f"     {'params':<60} {'hit':>7} {'FP':>7}  FP classes")
        for r in res["sweep"]:
            L.append(f"     {r['label']:<60} {r['hold3_hits']:>3}/{r['hold3_reps']:<3} {r['negative_fired']:>3}/"
                     f"{r['negative_reps']:<3} {r['fp_by_label'] or ''}")
    return "\n".join(L)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recordings", nargs="+")
    ap.add_argument("--json", help="write the full results as JSON")
    ap.add_argument("--sweep", choices=["none", "one", "grid"], default="one")
    ap.add_argument("--panel-mm", help="override the panel size WxH (display orientation)")
    ap.add_argument("--swap", type=int, choices=[0, 1], help="override the axis swap")
    ap.add_argument("--t-hold", type=float, default=1000, help="G1 hold time, ms (owner D2: 1000)")
    ap.add_argument("--t-land", type=float, default=250)
    ap.add_argument("--d-still", type=float, default=4)
    ap.add_argument("--d-sep", type=float, default=8)
    ap.add_argument("--e-edge", type=float, default=2)
    ap.add_argument("--major-max", type=int, default=None)
    a = ap.parse_args(argv)
    base = G1Params(t_land_ms=a.t_land, t_hold_ms=a.t_hold, d_still_mm=a.d_still, d_sep_mm=a.d_sep,
                    e_edge_mm=a.e_edge, major_max=a.major_max)
    results = []
    for path in a.recordings:
        rec = load_recording(path)
        res = analyse_recording(rec, a.panel_mm, a.swap, base, a.sweep)
        results.append(res)
        print(text_report(res))
        print()
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"tool": "mta_analyse", "version": 1, "results": results}, f, indent=1)
        print(f"wrote {a.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
