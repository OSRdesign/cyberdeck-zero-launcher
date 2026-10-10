#!/usr/bin/env python3
"""Draw a test pattern on a Linux framebuffer and (optionally) check the touch panel.

Usage:
  python3 fb-test-pattern.py [--fb /dev/fbN] [--seconds N] [--rotate-hint]
  python3 fb-test-pattern.py --touch auto|/dev/input/eventN [--seconds N]
  python3 fb-test-pattern.py --calibrate [--touch /dev/input/eventN]

The pattern shows orientation (a big "F" and "TOP-LEFT" in the top-left corner, TOP and RIGHT
arrows), colour order (R G B W K bars, labelled), depth (grey gradient: visible steps = 16 bpp),
and cropping (1 px white border on the whole virtual screen, red frame 20 px inside, tick marks
every 100 px with their coordinate). Stop the launcher first: systemctl --user stop APPLaunch.service
Python 3.9+ standard library only. The previous screen contents are restored on exit when possible.
"""
import argparse
import fcntl
import mmap
import os
import select
import signal
import struct
import subprocess
import sys
import time

FBIOGET_VSCREENINFO = 0x4600
FBIOGET_FSCREENINFO = 0x4602
FIX_FMT = "@16sL4I3HIL2IH2H"           # struct fb_fix_screeninfo, native alignment
EV_FMT = "llHHi"                       # struct input_event (timeval of two C longs)
EV_SIZE = struct.calcsize(EV_FMT)
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
ABS_X, ABS_Y, ABS_MT_X, ABS_MT_Y, ABS_MT_TRACKING_ID = 0, 1, 53, 54, 57
BTN_TOUCH = 330
RESTORE_LIMIT = 64 * 1024 * 1024

# 5x7 font: 7 rows per glyph as hex bytes (top row first), bit 4 is the leftmost column.
FONT = {
    "0": "0E11131519110E", "1": "040C040404040E", "2": "0E11010204081F", "3": "1F02040201110E", "4": "02060A121F0202",
    "5": "1F101E0101110E", "6": "0608101E11110E", "7": "1F010204080808", "8": "0E11110E11110E", "9": "0E11110F01020C",
    "A": "0E11111F111111", "B": "1E11111E11111E", "C": "0E11101010110E", "D": "1C12111111121C", "E": "1F10101E10101F",
    "F": "1F10101E101010", "G": "0E11101711110F", "H": "1111111F111111", "I": "0E04040404040E", "J": "0702020202120C",
    "K": "11121418141211", "L": "1010101010101F", "M": "111B1515111111", "N": "11111915131111", "O": "0E11111111110E",
    "P": "1E11111E101010", "Q": "0E11111115120D", "R": "1E11111E141211", "S": "0F10100E01011E", "T": "1F040404040404",
    "U": "1111111111110E", "V": "11111111110A04", "W": "1111111515150A", "X": "11110A040A1111", "Y": "1111110A040404",
    "Z": "1F01020408101F", "x": "0000110A040A11", "-": "0000001F000000", ":": "000C0C000C0C00", ".": "00000000000C0C",
    "/": "00010204081000", "=": "00001F001F0000", "+": "0004041F040400", "(": "02040808080402", ")": "08040202020408",
    "?": "0E110102040004", " ": "00000000000000",
}

WHITE, BLACK, RED, GREEN, BLUE = (255, 255, 255), (0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)
YELLOW, GREY, CYAN = (255, 255, 0), (128, 128, 128), (0, 255, 255)

PERM_HINT = ("hint: add your user to the groups video and input (sudo usermod -aG video,input $USER, "
             "then log out and in), or run this script with sudo")


def die(msg, hint=None):
    print("ERROR: " + msg, file=sys.stderr)
    if hint:
        print(hint, file=sys.stderr)
    sys.exit(1)


class Framebuffer:
    def __init__(self, path):
        try:
            self.fd = os.open(path, os.O_RDWR)
        except PermissionError:
            die("no permission to open %s" % path, PERM_HINT)
        except OSError as e:
            die("cannot open %s: %s" % (path, e), "available: %s" % ", ".join(
                sorted("/dev/" + n for n in os.listdir("/dev") if n.startswith("fb")) or ["none"]))
        var = bytearray(160)
        fix = bytearray(256)
        try:
            fcntl.ioctl(self.fd, FBIOGET_VSCREENINFO, var)
            fcntl.ioctl(self.fd, FBIOGET_FSCREENINFO, fix)
        except OSError as e:
            die("%s is not a framebuffer (ioctl failed: %s)" % (path, e))
        v = struct.unpack_from("=40I", var)
        f = struct.unpack_from(FIX_FMT, fix)
        self.xres, self.yres, self.xv, self.yv, self.xoff, self.yoff, self.bpp = v[0:7]
        self.red, self.green, self.blue, self.transp = v[8:11], v[11:14], v[14:17], v[17:20]
        self.rotate = v[34]
        self.id = f[0].split(b"\0")[0].decode("ascii", "replace")
        self.smem_len, self.line_length = f[2], f[10]
        if self.bpp not in (16, 24, 32):
            die("unsupported depth: %d bits per pixel (only 16, 24 and 32)" % self.bpp)
        self.Bpp = self.bpp // 8
        if not self.line_length:
            self.line_length = self.xv * self.Bpp
        self.size = min(self.line_length * self.yv, self.smem_len)
        self.yv = min(self.yv, self.size // self.line_length)
        try:
            self.mm = mmap.mmap(self.fd, self.size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        except OSError as e:
            die("mmap of %s failed: %s" % (path, e))

    def describe(self):
        def bf(b):
            return "off %d len %d" % (b[0], b[1])
        return ("%s id=%r visible %dx%d virtual %dx%d offset %d,%d  %d bpp  line_length %d  smem_len %d  rotate %d\n"
                "  red %s, green %s, blue %s, transp %s" % (
                    "framebuffer", self.id, self.xres, self.yres, self.xv, self.yv, self.xoff, self.yoff, self.bpp,
                    self.line_length, self.smem_len, self.rotate, bf(self.red), bf(self.green), bf(self.blue),
                    bf(self.transp)))

    def pack(self, rgb):
        def chan(val, bitf):
            off, ln = bitf[0], bitf[1]
            return ((val >> (8 - ln)) << off) if ln else 0
        p = chan(rgb[0], self.red) | chan(rgb[1], self.green) | chan(rgb[2], self.blue)
        if self.transp[1]:
            p |= ((1 << self.transp[1]) - 1) << self.transp[0]   # opaque alpha
        return p.to_bytes(self.Bpp, sys.byteorder)


class Canvas:
    """Drawing in visible-screen coordinates into a buffer laid out like the framebuffer memory."""

    def __init__(self, fb, buf):
        self.fb, self.buf = fb, buf
        self.W, self.H, self.ox, self.oy = fb.xres, fb.yres, fb.xoff, fb.yoff
        self.cache = {}

    def color(self, rgb):
        if rgb not in self.cache:
            self.cache[rgb] = self.fb.pack(rgb)
        return self.cache[rgb]

    def rect_abs(self, x, y, w, h, rgb):
        fb = self.fb
        x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, fb.xv), min(y + h, fb.yv)
        if x1 <= x0 or y1 <= y0:
            return
        row = self.color(rgb) * (x1 - x0)
        for yy in range(y0, y1):
            off = yy * fb.line_length + x0 * fb.Bpp
            self.buf[off:off + len(row)] = row

    def rect(self, x, y, w, h, rgb):
        self.rect_abs(x + self.ox, y + self.oy, w, h, rgb)

    def frame(self, x, y, w, h, t, rgb):
        self.rect(x, y, w, t, rgb)
        self.rect(x, y + h - t, w, t, rgb)
        self.rect(x, y, t, h, rgb)
        self.rect(x + w - t, y, t, h, rgb)

    def line(self, x0, y0, x1, y1, rgb, t=2):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            self.rect(x0 + (x1 - x0) * i // n, y0 + (y1 - y0) * i // n, t, t, rgb)

    def text(self, x, y, s, scale, rgb):
        for ch in s:
            glyph = bytes.fromhex(FONT.get(ch, FONT["?"]))
            for r, bits in enumerate(glyph):
                for c in range(5):
                    if bits & (0x10 >> c):
                        self.rect(x + c * scale, y + r * scale, scale, scale, rgb)
            x += 6 * scale

    @staticmethod
    def text_w(s, scale):
        return len(s) * 6 * scale - scale


def draw_pattern(cv, fb):
    W, H = cv.W, cv.H
    m = min(W, H)
    ts = max(1, m // 200)                       # text scale
    cv.rect_abs(0, 0, fb.xv, fb.yv, BLACK)
    cv.frame(-cv.ox, -cv.oy, fb.xv, fb.yv, 1, WHITE)   # 1 px border on the whole virtual screen
    cv.frame(20, 20, W - 40, H - 40, 2, RED)
    cv.line(0, 0, W - 2, H - 2, YELLOW)
    cv.line(W - 2, 0, 0, H - 2, YELLOW)
    for x in range(100, W, 100):                # ticks every 100 px with their coordinate
        cv.rect(x, 0, 1, 10, WHITE)
        cv.rect(x, H - 10, 1, 10, WHITE)
        cv.text(x + 2, 11, str(x), 1, WHITE)
        cv.text(x + 2, H - 19, str(x), 1, WHITE)
    for y in range(100, H, 100):
        cv.rect(0, y, 10, 1, WHITE)
        cv.rect(W - 10, y, 10, 1, WHITE)
        cv.text(11, y + 2, str(y), 1, WHITE)
        cv.text(W - 11 - cv.text_w(str(y), 1), y + 2, str(y), 1, WHITE)
    # colour bars in the centre, labelled, then a grey gradient
    bw, bh = max(W // 2 // 5, 6), max(H // 7, 12)
    bx, by = (W - 5 * bw) // 2, H // 2 - bh
    for i, (rgb, lab) in enumerate(((RED, "R"), (GREEN, "G"), (BLUE, "B"), (WHITE, "W"), (BLACK, "K"))):
        cv.rect(bx + i * bw, by, bw, bh, rgb)
        cv.text(bx + i * bw + (bw - cv.text_w(lab, ts)) // 2, by - 9 * ts, lab, ts, WHITE)
    cv.frame(bx + 4 * bw, by, bw, bh, 1, GREY)
    gw = 5 * bw
    for i in range(gw):
        g = i * 255 // max(gw - 1, 1)
        cv.rect(bx + i, by + bh + 4, 1, bh // 2, (g, g, g))
    # asymmetric F and the description in the top-left corner
    s = max(2, m // 60)
    cv.text(30, 30, "F", s, WHITE)
    lines = ["TOP-LEFT", "%dx%d %dBPP" % (W, H, fb.bpp), "VIRT %dx%d ROT %d" % (fb.xv, fb.yv, fb.rotate)]
    cv.rect(28, 28 + 8 * s, max(cv.text_w(t, ts) for t in lines) + 4, len(lines) * 9 * ts + 2, BLACK)
    for i, t in enumerate(lines):
        cv.text(30, 30 + 8 * s + i * 9 * ts, t, ts, WHITE)
    # TOP arrow (top centre, pointing up) and RIGHT arrow (right middle, pointing right)
    a = max(16, m // 10)
    cx, top = W // 2, 30
    for i in range(a):
        cv.rect(cx - i // 2, top + i, i + 1, 1, GREEN)
    cv.rect(cx - a // 8, top + a, a // 4, a // 2, GREEN)
    cv.text(cx - cv.text_w("TOP", ts) // 2, top + a + a // 2 + 4, "TOP", ts, GREEN)
    cy, right = H // 2 + bh, W - 30
    for i in range(a):
        cv.rect(right - i, cy - i // 2, 1, i + 1, CYAN)
    cv.rect(right - a - a // 2, cy - a // 8, a // 2, a // 4, CYAN)
    cv.text(right - a - a // 2 - 4 - cv.text_w("RIGHT", ts), cy - 3 * ts, "RIGHT", ts, CYAN)


ROTATE_HINT = """How to read the picture (compare with the photo you take of the screen):
- The big F is upright and readable in the top-left corner, TOP points up: no rotation, no mirror.
- F in the top-right corner, lying on its side, TOP points right: the panel turns the picture
  90 deg clockwise (the app must draw rotated 90 deg counter-clockwise to compensate).
- F in the bottom-left corner, TOP points left: the panel turns it 90 deg counter-clockwise.
- F upside down in the bottom-right corner: rotated 180 deg.
- F reads backwards (mirror image): the panel mirrors the picture; note which arrow is reversed.
- The bar labelled R is not red: colour order is swapped (R and B swapped = BGR panel).
- The grey gradient shows clear steps: 16 bpp (RGB565) or a low-depth panel.
- A side of the 1 px white border is missing, or the red frame is not 20 px from an edge: the
  picture is cropped (or overscanned) on that side. The tick numbers give the visible size.
"""


def find_touch():
    """First device in /proc/bus/input/devices with ABS_MT_POSITION_X or ABS_X and DIRECT or BTN_TOUCH."""
    wb = 64 if os.uname().machine.endswith("64") else 32

    def bits(s):
        v = 0
        for w in s.split():
            v = (v << wb) | int(w, 16)
        return v
    try:
        text = open("/proc/bus/input/devices").read()
    except OSError:
        return None
    for block in text.split("\n\n"):
        name, node, b = "", None, {}
        for ln in block.splitlines():
            if ln.startswith("N: Name="):
                name = ln[8:].strip('"')
            elif ln.startswith("H: Handlers="):
                node = next((h for h in ln[12:].split() if h.startswith("event")), None)
            elif ln.startswith("B: ") and "=" in ln:
                k, val = ln[3:].split("=", 1)
                b[k] = bits(val)
        ev, ab, key, prop = b.get("EV", 0), b.get("ABS", 0), b.get("KEY", 0), b.get("PROP", 0)
        if node and ev >> EV_ABS & 1 and (ab >> ABS_MT_X & 1 or ab & 1) and (prop >> 1 & 1 or key >> BTN_TOUCH & 1) \
                and not name.startswith("applaunch-"):
            return "/dev/input/" + node, name
    return None


class Touch:
    def __init__(self, path):
        try:
            self.fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        except PermissionError:
            die("no permission to read %s" % path, PERM_HINT)
        except OSError as e:
            die("cannot open %s: %s" % (path, e))
        self.path = path
        for ax, ay in ((ABS_MT_X, ABS_MT_Y), (ABS_X, ABS_Y)):
            ix, iy = self.absinfo(ax), self.absinfo(ay)
            if ix and iy and ix[2] > ix[1]:
                self.ax, self.ay, self.rx, self.ry = ax, ay, ix, iy
                break
        else:
            die("%s reports no ABS_X / ABS_MT_POSITION_X range" % path)
        self.x = self.y = None
        self.down = False

    def absinfo(self, code):
        buf = bytearray(24)
        try:
            fcntl.ioctl(self.fd, 0x80184540 + code, buf)    # EVIOCGABS(code)
        except OSError:
            return None
        return struct.unpack("6i", buf)                      # value, min, max, fuzz, flat, resolution

    def describe(self):
        n = "ABS_MT_POSITION" if self.ax == ABS_MT_X else "ABS"
        return "%s: %s_X %d..%d, %s_Y %d..%d" % (self.path, n, self.rx[1], self.rx[2], n, self.ry[1], self.ry[2])

    def norm(self):
        return ((self.x - self.rx[1]) / float(self.rx[2] - self.rx[1]),
                (self.y - self.ry[1]) / float(max(self.ry[2] - self.ry[1], 1)))

    def poll(self, timeout):
        """Read pending events; returns 'move', 'up' or None (at most one per SYN_REPORT)."""
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return None
        try:
            data = os.read(self.fd, EV_SIZE * 64)
        except BlockingIOError:
            return None
        result = None
        for i in range(0, len(data) - EV_SIZE + 1, EV_SIZE):
            _, _, typ, code, val = struct.unpack_from(EV_FMT, data, i)
            if typ == EV_ABS and code == self.ax:
                self.x = val
            elif typ == EV_ABS and code == self.ay:
                self.y = val
            elif (typ == EV_KEY and code == BTN_TOUCH) or (typ == EV_ABS and code == ABS_MT_TRACKING_ID):
                was = self.down
                self.down = (val != 0) if typ == EV_KEY else (val >= 0)
                if was and not self.down:
                    result = "up"
            elif typ == EV_SYN and code == 0 and self.down and self.x is not None and self.y is not None:
                result = result or "move"
        return result


def mark(cv, t, rgb=CYAN):
    nx, ny = t.norm()
    px, py = int(nx * (cv.W - 1)), int(ny * (cv.H - 1))
    cv.rect(px - 4, py - 4, 9, 9, rgb)
    return px, py


def touch_loop(cv, t, seconds):
    print("Touch the screen for %d s: a square is drawn where the RAW coordinates land (no swap/invert)." % seconds)
    end = time.time() + seconds
    while time.time() < end:
        ev = t.poll(0.2)
        if ev == "move":
            px, py = mark(cv, t)
            print("raw x=%d y=%d -> fb %d,%d" % (t.x, t.y, px, py))


def solve3(m, v):
    """Solve a 3x3 linear system by Gaussian elimination (None if singular)."""
    a = [list(m[i]) + [v[i]] for i in range(3)]
    for c in range(3):
        p = max(range(c, 3), key=lambda r: abs(a[r][c]))
        if abs(a[p][c]) < 1e-9:
            return None
        a[c], a[p] = a[p], a[c]
        for r in range(3):
            if r != c:
                f = a[r][c] / a[c][c]
                a[r] = [a[r][k] - f * a[c][k] for k in range(4)]
    return [a[i][3] / a[i][i] for i in range(3)]


def calibrate(cv, t):
    targets = [(0.1, 0.1, "TOP-LEFT"), (0.9, 0.1, "TOP-RIGHT"), (0.9, 0.9, "BOTTOM-RIGHT"), (0.1, 0.9, "BOTTOM-LEFT")]
    for i, (fx, fy, _) in enumerate(targets):
        x, y = int(fx * cv.W), int(fy * cv.H)
        cv.rect(x - 15, y, 31, 1, WHITE)
        cv.rect(x, y - 15, 1, 31, WHITE)
        cv.text(x + 4, y + 4, str(i + 1), max(1, min(cv.W, cv.H) // 200), WHITE)
    raw = []
    for i, (_, _, label) in enumerate(targets):
        print("Tap crosshair %d near the %s corner of the picture (30 s)..." % (i + 1, label))
        end, got = time.time() + 30, False
        while time.time() < end and not got:
            ev = t.poll(0.2)
            if ev == "move":
                mark(cv, t, YELLOW)
            elif ev == "up" and t.x is not None:
                raw.append(t.norm())
                print("  raw x=%d y=%d" % (t.x, t.y))
                got = True
        if not got:
            die("no tap received, calibration aborted")
    (u0, v0), (u1, v1), (u2, v2), (u3, v3) = raw
    dxu, dxv = (u1 + u2 - u0 - u3) / 2, (v1 + v2 - v0 - v3) / 2     # raw change when moving right
    dyu, dyv = (u3 + u2 - u0 - u1) / 2, (v3 + v2 - v0 - v1) / 2     # raw change when moving down
    swap = abs(dxv) > abs(dxu)
    inv_x = (dxv if swap else dxu) < 0
    inv_y = (dyu if swap else dyv) < 0
    if swap != (abs(dyu) > abs(dyv)):
        print("WARNING: the taps are not consistent with a pure swap/invert (diagonal mounting or bad taps?)")
    print("\nResult, same convention as the launcher service (swap first, then invert):")
    print("  APPLAUNCH_TOUCH_SWAP_XY=%d\n  APPLAUNCH_TOUCH_INVERT_X=%d\n  APPLAUNCH_TOUCH_INVERT_Y=%d"
          % (swap, inv_x, inv_y))
    print("  (relative to this framebuffer as drawn, i.e. with APPLAUNCH_ROTATE=0)")
    snap = []
    for src_is_v, inv in ((swap, inv_x), (not swap, inv_y)):
        k = -1 if inv else 1
        snap += [0, k, 1 if inv else 0] if src_is_v else [k, 0, 1 if inv else 0]
    print("libinput matrix from swap/invert:  LIBINPUT_CALIBRATION_MATRIX=\"%s\"" % " ".join("%g" % c for c in snap))
    # least-squares affine fit: screen_norm = [a b c; d e f] * [u v 1]
    mtm = [[0.0] * 3 for _ in range(3)]
    rhs_x, rhs_y = [0.0] * 3, [0.0] * 3
    for (u, v), (fx, fy, _) in zip(raw, targets):
        row = (u, v, 1.0)
        for r in range(3):
            for c in range(3):
                mtm[r][c] += row[r] * row[c]
            rhs_x[r] += row[r] * fx
            rhs_y[r] += row[r] * fy
    px, py = solve3(mtm, rhs_x), solve3(mtm, rhs_y)
    if px and py:
        err = max(max(abs((px[0] * u + px[1] * v + px[2]) - fx) * cv.W, abs((py[0] * u + py[1] * v + py[2]) - fy) * cv.H)
                  for (u, v), (fx, fy, _) in zip(raw, targets))
        print("libinput matrix, fitted:           LIBINPUT_CALIBRATION_MATRIX=\"%s\"   (max error %.1f px)"
              % (" ".join("%.4f" % c for c in px + py), err))
    else:
        print("fitted matrix: not computable (taps too close together)")


def on_sigterm(*_):
    raise KeyboardInterrupt


def launcher_running():
    try:
        out = subprocess.run(["systemctl", "--user", "is-active", "APPLaunch.service"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=5)
        return out.stdout.strip() == b"active"
    except (OSError, subprocess.SubprocessError):
        return False


DESKTOP_PROCS = ("labwc", "wayfire", "sway", "weston", "Xorg", "Xwayland", "lightdm", "sddm", "gdm", "gdm3")


def desktop_found():
    """Names of running compositor / display-manager processes, plus the systemd default target."""
    found = set()
    try:
        pids = [d for d in os.listdir("/proc") if d.isdigit()]
    except OSError:
        pids = []
    for pid in pids:
        try:
            with open("/proc/%s/comm" % pid) as f:
                c = f.read().strip()
        except OSError:
            continue
        if c in DESKTOP_PROCS:
            found.add(c)
    target = None
    try:
        out = subprocess.run(["systemctl", "get-default"], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, timeout=3)
        target = out.stdout.decode("ascii", "replace").strip() or None
    except (OSError, subprocess.SubprocessError):
        pass
    return [n for n in DESKTOP_PROCS if n in found], target


def desktop_warning():
    names, target = desktop_found()
    if not names and target != "graphical.target":
        return
    print("=" * 72)
    print("WARNING: a desktop session may own the display.")
    print("  processes found: %s" % (", ".join(names) or "none"))
    print("  systemd default target: %s" % (target or "?"))
    print("  The compositor is DRM master, so the pattern written to the framebuffer")
    print("  may NOT be visible (or may be overwritten at once).")
    print("  Stop the desktop first (this blanks the screen and ends the desktop")
    print("  session, so run this test from an ssh session):")
    print("      sudo systemctl stop lightdm")
    print("  and start it again after the test:")
    print("      sudo systemctl start lightdm")
    print("  (use --no-desktop-check to skip this check)")
    print("=" * 72)


def main():
    ap = argparse.ArgumentParser(description="Framebuffer test pattern and touch check.")
    ap.add_argument("--fb", default="/dev/fb0", help="framebuffer device (default /dev/fb0)")
    ap.add_argument("--seconds", type=int, default=None, help="how long to show the pattern (20) or read touch (10)")
    ap.add_argument("--rotate-hint", action="store_true", help="print how to read the pattern")
    ap.add_argument("--touch", metavar="DEV", help="/dev/input/eventN or 'auto': show raw touch points")
    ap.add_argument("--calibrate", action="store_true", help="4-tap corner test: prints SWAP_XY/INVERT_X/INVERT_Y")
    ap.add_argument("--no-desktop-check", action="store_true", help="skip the running-desktop (lightdm/labwc/Xorg) check")
    args = ap.parse_args()

    if not args.no_desktop_check:
        desktop_warning()

    print("NOTE: the launcher owns the screen; stop it first:  systemctl --user stop APPLaunch.service")
    if launcher_running():
        print("APPLaunch.service is RUNNING and will draw over the pattern. Continuing in 3 s (Ctrl-C to abort).")
        time.sleep(3)
    if args.rotate_hint:
        print(ROTATE_HINT)

    touch = None
    if args.touch or args.calibrate:
        path = args.touch or "auto"
        if path == "auto":
            found = find_touch()
            if not found:
                die("no touch device found in /proc/bus/input/devices (use --touch /dev/input/eventN)")
            path = found[0]
            print("touch device: %s (%s)" % found)
        touch = Touch(path)
        print("touch ranges: " + touch.describe())

    fb = Framebuffer(args.fb)
    print(fb.describe())
    saved = bytes(fb.mm[:fb.size]) if fb.size <= RESTORE_LIMIT else None
    if saved is None:
        print("framebuffer is %d bytes: too large to save, the old picture will NOT be restored" % fb.size)
    signal.signal(signal.SIGTERM, on_sigterm)
    try:
        buf = bytearray(fb.size)
        draw_pattern(Canvas(fb, buf), fb)
        fb.mm[:fb.size] = bytes(buf)
        live = Canvas(fb, fb.mm)              # touch marks go straight to the screen
        if args.calibrate:
            calibrate(live, touch)
        elif touch:
            touch_loop(live, touch, args.seconds or 10)
        else:
            secs = args.seconds or 20
            print("Pattern shown for %d s (Ctrl-C to stop). Take a photo of the screen." % secs)
            time.sleep(secs)
    except KeyboardInterrupt:
        print("\ninterrupted")
    finally:
        if saved is not None:
            fb.mm[:fb.size] = saved
            print("previous screen contents restored")
        fb.mm.close()
        os.close(fb.fd)


if __name__ == "__main__":
    main()
