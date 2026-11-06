#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fake tile server for the map engine's fetch tests (tests/test_map_fetch.c, run by run_tests.sh). Python stdlib only,
127.0.0.1 only: no test ever contacts a real tile server.

Usage: fake_tile_server.py <port-file> <log-file>. Binds an ephemeral port on 127.0.0.1, writes the port number to
<port-file> and serves until it is killed. Every request is appended to <log-file> as one JSON line:
  {"path", "ua", "inm" (If-None-Match), "ims" (If-Modified-Since), "conc" (requests in progress, this one included),
   "t" (seconds)}
so a test can check the User-Agent, the parallel cap, conditional requests and that nothing else was asked.

Tiles: /<mode>/<z>/<x>/<y>.png[?query], a 256 px greyscale PNG that shows its z/x/y (every tile different). Modes:
  normal     200, Cache-Control: max-age=86400, ETag "z-x-y", Last-Modified; 304 when If-None-Match matches
  short      the same with max-age=0 (the engine's freshness floor applies)
  expires    Expires and Date instead of Cache-Control (one hour)
  nostore    Cache-Control: no-store
  slow       waits 0.6 s, then normal          slow3   waits 3 s, then normal
  drop       closes the connection without an answer
  r429       429 with Retry-After: 1           r503    503 with Retry-After as an HTTP date (now + 1 s)
  e500       500
  404, gone  404, 410
  odd        404 when x + y is odd, else normal
  blocked    one identical "BLOCKED" picture for every tile, HTTP 200, image/png
  sea        one identical plain blue picture for every tile (open sea: not a blocked answer)
  big        a 3 MB image/png                   html    200 text/html          liar    200 image/png, HTML bytes
  redir-osmf 302 to https://tile.openstreetmap.org/...   redir-http 302 to http://example.invalid/...
  redir-ok   302 to /normal/... (same host)     redir-loop 302 to itself
  sw/<name>  behaves as the mode set for <name> by GET /_sw?name=<name>&mode=<mode> (default normal)
"""
import json
import struct
import sys
import threading
import time
import zlib
from email.utils import formatdate
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

LOCK = threading.Lock()
STATE = {"conc": 0}
SWITCH = {}
LOG = None

# 3 x 5 digits, rows top to bottom, bit 2 = left
FONT = {
    "0": [7, 5, 5, 5, 7], "1": [2, 6, 2, 2, 7], "2": [7, 1, 7, 4, 7], "3": [7, 1, 7, 1, 7], "4": [5, 5, 7, 1, 1],
    "5": [7, 4, 7, 1, 7], "6": [7, 4, 7, 5, 7], "7": [7, 1, 1, 1, 1], "8": [7, 5, 7, 5, 7], "9": [7, 5, 7, 1, 7],
    "/": [1, 1, 2, 4, 4], "B": [6, 5, 6, 5, 6], "L": [4, 4, 4, 4, 7], "O": [7, 5, 5, 5, 7], "C": [7, 4, 4, 4, 7],
    "K": [5, 5, 6, 5, 5], "E": [7, 4, 6, 4, 7], "D": [6, 5, 5, 5, 6],
}


def png(rows, w, h, ctype=0, bitdepth=8, plte=None):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + r for r in rows)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bitdepth, ctype, 0, 0, 0))
    if plte:
        out += chunk(b"PLTE", plte)
    return out + chunk(b"IDAT", zlib.compress(raw, 1)) + chunk(b"IEND", b"")


def text_tile(text, bg):
    w = h = 256
    img = [bytearray([bg]) * w for _ in range(h)]
    scale, x0 = 4, 8
    for ch in text:
        if x0 + 3 * scale > w:
            break
        glyph = FONT.get(ch)
        if glyph:
            for gy, bits in enumerate(glyph):
                for gx in range(3):
                    if bits & (4 >> gx):
                        for yy in range(scale):
                            row = img[8 + gy * scale + yy]
                            for xx in range(scale):
                                row[x0 + gx * scale + xx] = 0
        x0 += 4 * scale
    return png([bytes(r) for r in img], w, h)


BLOCKED = text_tile("BLOCKED", 200)
SEA = png([b"\0" * 256 for _ in range(256)], 256, 256, ctype=3, plte=b"\x40\x80\xc0")


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send(self, status, body=b"", ctype="image/png", extra=()):
        self.send_response(status)
        if status not in (304,):
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD" and status != 304:
            self.wfile.write(body)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path == "/_sw":
            q = parse_qs(url.query)
            with LOCK:
                SWITCH[q.get("name", [""])[0]] = q.get("mode", ["normal"])[0]
            return self.send(200, b"ok", "text/plain")
        with LOCK:
            STATE["conc"] += 1
            conc = STATE["conc"]
            if LOG:
                LOG.write(json.dumps({"path": self.path, "ua": self.headers.get("User-Agent", ""),
                                      "inm": self.headers.get("If-None-Match", ""),
                                      "ims": self.headers.get("If-Modified-Since", ""),
                                      "conc": conc, "t": time.time()}) + "\n")
                LOG.flush()
        try:
            self.tile(url)
        finally:
            with LOCK:
                STATE["conc"] -= 1

    def tile(self, url):
        parts = url.path.strip("/").split("/")
        if len(parts) >= 2 and parts[0] == "sw":
            with LOCK:
                mode = SWITCH.get(parts[1], "normal")
            parts = parts[1:]
        else:
            mode = parts[0] if parts else ""
        try:
            z, x, y = int(parts[1]), int(parts[2]), int(parts[3].split(".")[0])
        except (IndexError, ValueError):
            return self.send(400, b"bad tile path", "text/plain")
        etag = '"%d-%d-%d"' % (z, x, y)
        lm = "Sun, 01 Nov 2026 10:00:00 GMT"
        if mode in ("slow", "slow3"):
            time.sleep(0.6 if mode == "slow" else 3.0)
            mode = "normal"
        if mode == "odd":
            if (x + y) % 2:
                return self.send(404, b"no tile", "text/plain")
            mode = "normal"
        if mode in ("normal", "short", "expires", "nostore"):
            if self.headers.get("If-None-Match") == etag:
                return self.send(304, extra=[("ETag", etag), ("Cache-Control", "max-age=86400")])
            body = text_tile("%d/%d/%d" % (z, x, y), 160 + (x + y) % 2 * 40)
            if mode == "normal":
                extra = [("Cache-Control", "max-age=86400"), ("ETag", etag), ("Last-Modified", lm)]
            elif mode == "short":
                extra = [("Cache-Control", "public, max-age=0"), ("ETag", etag)]
            elif mode == "expires":
                now = time.time()
                extra = [("Date", formatdate(now, usegmt=True)), ("Expires", formatdate(now + 3600, usegmt=True))]
            else:
                extra = [("Cache-Control", "no-store")]
            return self.send(200, body, "image/png", extra)
        if mode == "drop":
            self.close_connection = True
            try:
                self.connection.shutdown(2)
            except OSError:
                pass
            return
        if mode == "r429":
            return self.send(429, b"slow down", "text/plain", [("Retry-After", "1")])
        if mode == "r503":
            return self.send(503, b"busy", "text/plain", [("Retry-After", formatdate(time.time() + 1, usegmt=True))])
        if mode == "e500":
            return self.send(500, b"boom", "text/plain")
        if mode == "404":
            return self.send(404, b"no tile", "text/plain")
        if mode == "gone":
            return self.send(410, b"gone", "text/plain")
        if mode == "blocked":
            return self.send(200, BLOCKED, "image/png", [("Cache-Control", "max-age=86400")])
        if mode == "sea":
            return self.send(200, SEA, "image/png", [("Cache-Control", "max-age=86400")])
        if mode == "big":
            return self.send(200, BLOCKED + b"\0" * (3 * 1024 * 1024), "image/png")
        if mode == "html":
            return self.send(200, b"<html>not a tile</html>", "text/html")
        if mode == "liar":
            return self.send(200, b"<html>not a tile</html>", "image/png")
        if mode == "redir-osmf":
            return self.send(302, b"", "text/plain", [("Location", "https://tile.openstreetmap.org/%d/%d/%d.png" % (z, x, y))])
        if mode == "redir-http":
            return self.send(302, b"", "text/plain", [("Location", "http://example.invalid/%d/%d/%d.png" % (z, x, y))])
        if mode == "redir-ok":
            return self.send(302, b"", "text/plain", [("Location", "/normal/%d/%d/%d.png" % (z, x, y))])
        if mode == "redir-loop":
            return self.send(302, b"", "text/plain", [("Location", url.path)])
        return self.send(404, b"unknown mode", "text/plain")


class Quiet(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):  # clients that hang up early (cancel, size cap) are normal here
        pass


if __name__ == "__main__":
    LOG = open(sys.argv[2], "a", encoding="utf-8")
    srv = Quiet(("127.0.0.1", 0), H)
    with open(sys.argv[1] + ".tmp", "w") as f:
        f.write(str(srv.server_address[1]))
    import os
    os.replace(sys.argv[1] + ".tmp", sys.argv[1])
    srv.serve_forever()
