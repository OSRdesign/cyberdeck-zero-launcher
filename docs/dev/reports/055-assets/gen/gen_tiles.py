#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""P0 spike (report 055): synthetic map-like raster tiles, NO third-party map data.

Draws an imaginary area (towns, street grids, roads, a river, lakes, woods, buildings, street-name labels at 12 px on
the 256 px scale, each tile's z/x/y in a corner) in world coordinates, so tiles join up across their borders. Zoom 0-14,
2106 tiles per tile size. For 256 px and 512 px ("@2x": the same picture at twice the resolution, labels 24 px) it writes
three formats: RGB PNG, 8-bit palette PNG, JPEG q75; each as a z/x/y folder, an MBTiles 1.3 file (flat `tiles` table,
TMS rows) and a PMTiles v3 file (official BSD-3 `pmtiles` writer, gzip directories, tiles uncompressed).

  python gen_tiles.py OUTDIR [--sizes 256,512] [--jobs 8]

Fonts: DejaVu Sans from the launcher repo (free licence), only used to draw the test pictures.
"""
import io
import math
import os
import random
import sqlite3
import sys
import time
from multiprocessing import Pool

from PIL import Image, ImageDraw, ImageFont

FONT = r"C:\CLAUDE\zero7\launcher-study-maps\projects\APPLaunch\APPLaunch\share\font\DejaVuSans.ttf"

# The area: 39 x 39 tiles at zoom 14 (about 64 x 64 km at 48.6 N), every lower zoom down to 0.
ZMAX = 14
X0, X1, Y0, Y1 = 8283, 8321, 5654, 5692          # inclusive, zoom 14 XYZ
WX0, WY0 = X0 * 256, Y0 * 256                     # world px at zoom 14 (256 px tiles)
WX1, WY1 = (X1 + 1) * 256, (Y1 + 1) * 256
SS = 2                                            # supersampling for antialiased geometry

C_LAND = (242, 239, 233)
C_WATER = (170, 211, 223)
C_FOREST = (173, 209, 158)
C_PARK = (200, 250, 204)
C_RESID = (224, 223, 223)
C_BUILD = (217, 208, 201)
C_BUILD_EDGE = (196, 182, 171)
C_STREET, C_STREET_CASE = (255, 255, 255), (187, 187, 187)
C_PRIM, C_PRIM_CASE = (252, 214, 164), (160, 107, 0)
C_MOTOR, C_MOTOR_CASE = (232, 146, 162), (220, 42, 103)

SYL = ["lar", "kem", "mor", "vel", "tan", "bri", "sol", "dun", "ash", "fen", "hal", "rik", "mon", "ter", "pell",
       "gar", "lin", "orn", "wes", "cal", "nor", "bel", "stav", "quin", "rod", "mar", "eld", "tor", "hev", "ful"]
STREET_KIND = ["Street", "Road", "Lane", "Avenue", "Way", "Close", "Row", "Place", "Grove", "Hill"]


def tile_ranges():
    out = []
    for z in range(ZMAX + 1):
        s = ZMAX - z
        out.append((z, X0 >> s, X1 >> s, Y0 >> s, Y1 >> s))
    return out


# ---------------------------------------------------------------- features (deterministic)
def _name(rng, n=2):
    w = "".join(rng.choice(SYL) for _ in range(n))
    return w[0].upper() + w[1:]


def _blob(rng, cx, cy, r, n=18, rough=0.35):
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        rr = r * (1 + rng.uniform(-rough, rough))
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts


def _wiggle(rng, p0, p1, n, amp):
    (x0, y0), (x1, y1) = p0, p1
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy) or 1
    nx, ny = -dy / L, dx / L
    pts = []
    for i in range(n + 1):
        t = i / n
        o = 0 if i in (0, n) else rng.uniform(-amp, amp)
        pts.append((x0 + dx * t + nx * o, y0 + dy * t + ny * o))
    return pts


def _bbox(pts, pad=0):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return (min(xs) - pad, min(ys) - pad, max(xs) + pad, max(ys) + pad)


def build_features():
    rng = random.Random(55)
    W, H = WX1 - WX0, WY1 - WY0
    F = {"resid": [], "forest": [], "park": [], "lake": [], "river": [], "street": [], "prim": [], "motor": [],
         "build": {}, "place": []}
    towns = []
    for i in range(3):
        cx, cy = WX0 + rng.uniform(0.2, 0.8) * W, WY0 + rng.uniform(0.2, 0.8) * H
        towns.append((cx, cy, rng.uniform(1200, 1800), _name(rng, 2), 1))
    for i in range(9):
        cx, cy = WX0 + rng.uniform(0.05, 0.95) * W, WY0 + rng.uniform(0.05, 0.95) * H
        towns.append((cx, cy, rng.uniform(280, 520), _name(rng, 2), 0))
    for (cx, cy, r, nm, big) in towns:
        F["resid"].append(_blob(rng, cx, cy, r, 22, 0.15))
        F["place"].append((cx, cy, nm, big))
    for i in range(28):
        cx, cy = WX0 + rng.uniform(0, 1) * W, WY0 + rng.uniform(0, 1) * H
        F["forest"].append(_blob(rng, cx, cy, rng.uniform(200, 800), 20, 0.4))
    for i in range(6):
        cx, cy = WX0 + rng.uniform(0.1, 0.9) * W, WY0 + rng.uniform(0.1, 0.9) * H
        F["lake"].append(_blob(rng, cx, cy, rng.uniform(80, 380), 22, 0.3))
    # river across the area
    pts = _wiggle(rng, (WX0 - 300, WY0 + 0.3 * H), (WX1 + 300, WY0 + 0.75 * H), 40, 260)
    F["river"].append((pts, _name(rng, 2)))
    # motorways
    for (a, b) in [((WX0 - 500, WY0 + 0.1 * H), (WX1 + 500, WY0 + 0.6 * H)),
                   ((WX0 + 0.35 * W, WY0 - 500), (WX0 + 0.6 * W, WY1 + 500))]:
        F["motor"].append((_wiggle(rng, a, b, 16, 180), "A %d" % rng.randint(1, 99)))
    # primary roads between places
    places = [(t[0], t[1]) for t in towns]
    for i, p in enumerate(places):
        dists = sorted(((math.hypot(p[0] - q[0], p[1] - q[1]), j) for j, q in enumerate(places) if j != i))
        for _, j in dists[:2]:
            if j > i or rng.random() < 0.3:
                F["prim"].append((_wiggle(rng, p, places[j], 10, 140), "D %d" % rng.randint(100, 999)))
    # street grids and buildings in each place
    for (cx, cy, r, nm, big) in towns:
        ang = math.radians(rng.uniform(-18, 18))
        ca, sa = math.cos(ang), math.sin(ang)
        sp = rng.uniform(42, 64)
        n = int(r / sp)

        def rot(u, v):
            return (cx + u * ca - v * sa, cy + u * sa + v * ca)
        for axis in (0, 1):
            for k in range(-n, n + 1):
                off = k * sp
                half = math.sqrt(max(r * r - off * off, 0)) * 0.95
                if half < sp * 0.5:
                    continue
                if axis == 0:
                    a, b = rot(-half, off), rot(half, off)
                else:
                    a, b = rot(off, -half), rot(off, half)
                F["street"].append((_wiggle(rng, a, b, max(2, int(half / 70)), 3),
                                    _name(rng, rng.choice((1, 2))) + " " + rng.choice(STREET_KIND)))
        cell = 13
        m = int(r / cell)
        for iu in range(-m, m + 1):
            for iv in range(-m, m + 1):
                u, v = iu * cell, iv * cell
                if u * u + v * v > r * r:
                    continue
                du = abs(((u + sp / 2) % sp) - sp / 2)
                dv = abs(((v + sp / 2) % sp) - sp / 2)
                if du < 9 or dv < 9 or rng.random() < 0.2:
                    continue
                w2, h2 = rng.uniform(3, 5.5), rng.uniform(3, 5.5)
                poly = [rot(u - w2, v - h2), rot(u + w2, v - h2), rot(u + w2, v + h2), rot(u - w2, v + h2)]
                key = (int(u * ca - v * sa + cx) // 256, int(u * sa + v * ca + cy) // 256)
                F["build"].setdefault(key, []).append(poly)
        if big:
            for i in range(3):
                F["park"].append(_blob(rng, cx + rng.uniform(-0.6, 0.6) * r, cy + rng.uniform(-0.6, 0.6) * r,
                                       rng.uniform(60, 140), 14, 0.3))
    # bounding boxes once
    for k in ("resid", "forest", "park", "lake"):
        F[k] = [(p, _bbox(p)) for p in F[k]]
    for k in ("river", "street", "prim", "motor"):
        F[k] = [(p, nm, _bbox(p, 40)) for (p, nm) in F[k]]
    return F


FEAT = None
FONTS = {}


def font(px):
    f = FONTS.get(px)
    if f is None:
        f = FONTS[px] = ImageFont.truetype(FONT, px)
    return f


# ---------------------------------------------------------------- rendering
def _inter(b, v):
    return not (b[2] < v[0] or b[0] > v[2] or b[3] < v[1] or b[1] > v[3])


def _width(base, z, minw):
    return max(minw, base * (0.75 ** (ZMAX - z)))


LABEL_CACHE = {}


def _label(text, px, angle, fill, halo):
    key = (text, px, round(angle))
    im = LABEL_CACHE.get(key)
    if im is None:
        f = font(px)
        sw = max(1, px // 6)
        l, t, r, b = f.getbbox(text, stroke_width=sw)
        im = Image.new("RGBA", (r - l + 2, b - t + 2), (0, 0, 0, 0))
        ImageDraw.Draw(im).text((1 - l, 1 - t), text, font=f, fill=fill, stroke_width=sw, stroke_fill=halo)
        if abs(angle) > 0.5:
            im = im.rotate(angle, resample=Image.BICUBIC, expand=True)
        LABEL_CACHE[key] = im
    return im


def _along(pts, step, first):
    """Label anchors every `step` world px along a polyline: (x, y, angle)."""
    out = []
    want = first
    run = 0.0
    for a, b in zip(pts, pts[1:]):
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        while want <= run + L and L > 0:
            t = (want - run) / L
            ang = -math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
            while ang > 90:
                ang -= 180
            while ang < -90:
                ang += 180
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, ang))
            want += step
        run += L
    return out


def _mid_angle(pts):
    i = len(pts) // 2
    a, b = pts[i - 1], pts[i]
    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    ang = -math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    while ang > 90:
        ang -= 180
    while ang < -90:
        ang += 180
    return mx, my, ang


def render(z, x, y, tpx):
    global FEAT
    if FEAT is None:
        FEAT = build_features()
    F = FEAT
    S = tpx / 256.0
    k = (2.0 ** (z - ZMAX)) * S                   # world px (z14, 256) -> zoom-z px at this tile size
    ox, oy = x * tpx, y * tpx
    pad = 60 / k
    view = (ox / k - pad, oy / k - pad, (ox + tpx) / k + pad, (oy + tpx) / k + pad)
    cw = tpx * SS
    im = Image.new("RGB", (cw, cw), C_LAND)
    d = ImageDraw.Draw(im)

    def T(pts):
        return [((px * k - ox) * SS, (py * k - oy) * SS) for (px, py) in pts]

    def poly(layer, color, zmin, outline=None):
        if z < zmin:
            return
        for pts, bb in F[layer]:
            if _inter(bb, view):
                d.polygon(T(pts), fill=color, outline=outline)

    def lines(layer, fill, case, base, zmin, minw):
        if z < zmin:
            return
        w = _width(base, z, minw) * S * SS
        sel = [p for (p, nm, bb) in F[layer] if _inter(bb, view)]
        if case is not None:
            for p in sel:
                d.line(T(p), fill=case, width=max(1, int(round(w + 2 * S * SS))), joint="curve")
        for p in sel:
            d.line(T(p), fill=fill, width=max(1, int(round(w))), joint="curve")

    poly("forest", C_FOREST, 7)
    poly("resid", C_RESID, 9)
    poly("park", C_PARK, 12)
    poly("lake", C_WATER, 6)
    lines("river", C_WATER, None, 14, 7, 1.0)
    if z >= 14:
        tx0, ty0 = int(view[0]) // 256, int(view[1]) // 256
        tx1, ty1 = int(view[2]) // 256, int(view[3]) // 256
        for bx in range(tx0, tx1 + 1):
            for by in range(ty0, ty1 + 1):
                for pts in F["build"].get((bx, by), ()):
                    d.polygon(T(pts), fill=C_BUILD, outline=C_BUILD_EDGE)
    if z >= 12:
        lines("street", C_STREET, C_STREET_CASE if z >= 13 else None, 5, 12, 1.0)
    lines("prim", C_PRIM, C_PRIM_CASE if z >= 10 else None, 7, 8, 1.0)
    lines("motor", C_MOTOR, C_MOTOR_CASE if z >= 9 else None, 8, 5, 1.0)
    im = im.resize((tpx, tpx), Image.LANCZOS)

    # labels at the final resolution
    def put(lbl, wx, wy):
        cx, cy = wx * k - ox, wy * k - oy
        x0, y0 = int(round(cx - lbl.width / 2)), int(round(cy - lbl.height / 2))
        if x0 > tpx or y0 > tpx or x0 + lbl.width < 0 or y0 + lbl.height < 0:
            return
        im.paste(lbl.convert("RGB"), (x0, y0), lbl)

    lp = int(round(12 * S))
    if z >= 14:
        for i, (p, nm, bb) in enumerate(F["street"]):
            if _inter(bb, view):
                for (mx, my, ang) in _along(p, 260, 60 + (i * 37) % 120):
                    put(_label(nm, lp, ang, (51, 51, 51), (255, 255, 255)), mx, my)
    if z >= 13:
        for (p, nm, bb) in F["prim"] + F["motor"]:
            if _inter(bb, view):
                for (mx, my, ang) in _along(p, 900 / (2 ** (z - 13)) * 2, 300):
                    put(_label(nm, lp, ang, (80, 50, 0), (255, 255, 255)), mx, my)
    if z >= 12:
        for (p, nm, bb) in F["river"]:
            if _inter(bb, view):
                for i in range(4, len(p) - 1, 9):
                    mx, my, ang = _mid_angle(p[i - 1:i + 1])
                    put(_label(nm, lp, ang, (74, 111, 165), (255, 255, 255)), mx, my)
    for (cx, cy, nm, big) in F["place"]:
        if (big and z >= 9) or (not big and z >= 12):
            size = int(round((17 if big and z >= 11 else 13) * S))
            put(_label(nm, size, 0, (0, 0, 0), (255, 255, 255)), cx, cy - (40 if z >= 13 else 0) / k * S)
    # tile id in the top-left corner
    ImageDraw.Draw(im).text((2 * S, 1 * S), "%d/%d/%d" % (z, x, y), font=font(int(round(9 * S))), fill=(140, 140, 140))
    return im


def encode(job):
    z, x, y, tpx = job
    im = render(z, x, y, tpx)
    b_png = io.BytesIO()
    im.save(b_png, format="PNG", compress_level=6)
    pal = im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    b_pal = io.BytesIO()
    pal.save(b_pal, format="PNG", compress_level=6)
    b_jpg = io.BytesIO()
    im.save(b_jpg, format="JPEG", quality=75)
    return (z, x, y, b_png.getvalue(), b_pal.getvalue(), b_jpg.getvalue())


# ---------------------------------------------------------------- containers
def lonlat(z, x, y):
    n = 2.0 ** z
    lon = x / n * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * y / n))))
    return lon, lat


def mbtiles_open(path, fmt, tpx):
    if os.path.exists(path):
        os.remove(path)
    db = sqlite3.connect(path, isolation_level=None)
    db.executescript("""
        CREATE TABLE metadata (name text, value text);
        CREATE TABLE tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob);
        CREATE UNIQUE INDEX tile_index on tiles (zoom_level, tile_column, tile_row);
    """)
    lon0, lat1 = lonlat(ZMAX, X0, Y0)
    lon1, lat0 = lonlat(ZMAX, X1 + 1, Y1 + 1)
    meta = {"name": "p0-synthetic-%s-%d" % (fmt, tpx), "format": "jpg" if fmt == "jpg" else "png",
            "bounds": "%.6f,%.6f,%.6f,%.6f" % (lon0, lat0, lon1, lat1),
            "center": "%.6f,%.6f,12" % ((lon0 + lon1) / 2, (lat0 + lat1) / 2),
            "minzoom": "0", "maxzoom": str(ZMAX), "type": "baselayer", "version": "1.0",
            "description": "P0 synthetic test tiles (no map data), %d px" % tpx, "attribution": "synthetic"}
    db.executemany("INSERT INTO metadata VALUES (?,?)", list(meta.items()))
    db.execute("BEGIN")
    return db


def main():
    out = sys.argv[1]
    sizes = [256, 512]
    jobs_n = 8
    a = sys.argv[2:]
    if "--sizes" in a:
        sizes = [int(s) for s in a[a.index("--sizes") + 1].split(",")]
    if "--jobs" in a:
        jobs_n = int(a[a.index("--jobs") + 1])
    from pmtiles.tile import zxy_to_tileid, TileType, Compression
    from pmtiles.writer import Writer
    os.makedirs(out, exist_ok=True)
    jobs0 = []
    for (z, xa, xb, ya, yb) in tile_ranges():
        for x in range(xa, xb + 1):
            for y in range(ya, yb + 1):
                jobs0.append((zxy_to_tileid(z, x, y), z, x, y))
    jobs0.sort()
    print("tiles per set:", len(jobs0))
    lon0, lat1 = lonlat(ZMAX, X0, Y0)
    lon1, lat0 = lonlat(ZMAX, X1 + 1, Y1 + 1)
    for tpx in sizes:
        t0 = time.time()
        sets = {"png": "png", "pal": "png", "jpg": "jpg"}
        names = {f: "%s%d" % (f, tpx) for f in sets}
        dbs = {f: mbtiles_open(os.path.join(out, names[f] + ".mbtiles"), f, tpx) for f in sets}
        pmf = {f: open(os.path.join(out, names[f] + ".pmtiles"), "wb") for f in sets}
        pmw = {f: Writer(pmf[f]) for f in sets}
        sizes_sum = {f: 0 for f in sets}
        with Pool(jobs_n) as pool:
            for n, (z, x, y, bpng, bpal, bjpg) in enumerate(
                    pool.imap(encode, [(z, x, y, tpx) for (_, z, x, y) in jobs0], chunksize=4)):
                tid = zxy_to_tileid(z, x, y)
                for f, data in (("png", bpng), ("pal", bpal), ("jpg", bjpg)):
                    dd = os.path.join(out, names[f], str(z), str(x))
                    os.makedirs(dd, exist_ok=True)
                    with open(os.path.join(dd, "%d.%s" % (y, sets[f])), "wb") as fh:
                        fh.write(data)
                    dbs[f].execute("INSERT INTO tiles VALUES (?,?,?,?)", (z, x, (1 << z) - 1 - y, data))
                    pmw[f].write_tile(tid, data)
                    sizes_sum[f] += len(data)
                if n % 500 == 0:
                    print("  %d px: %d tiles, %.0f s" % (tpx, n, time.time() - t0), flush=True)
        for f in sets:
            dbs[f].execute("COMMIT")
            dbs[f].close()
            pmw[f].finalize({
                "tile_type": TileType.JPEG if f == "jpg" else TileType.PNG,
                "tile_compression": Compression.NONE,
                "min_lon_e7": int(lon0 * 1e7), "min_lat_e7": int(lat0 * 1e7),
                "max_lon_e7": int(lon1 * 1e7), "max_lat_e7": int(lat1 * 1e7),
                "center_zoom": 12, "center_lon_e7": int((lon0 + lon1) / 2 * 1e7),
                "center_lat_e7": int((lat0 + lat1) / 2 * 1e7)},
                {"name": names[f], "attribution": "synthetic", "description": "P0 synthetic test tiles"})
            pmf[f].close()
            print("  %s: %d tiles, %.1f MB of tile data, mean %.1f KB" % (
                names[f], len(jobs0), sizes_sum[f] / 1e6, sizes_sum[f] / len(jobs0) / 1024))
        print("%d px done in %.0f s" % (tpx, time.time() - t0))


if __name__ == "__main__":
    main()
