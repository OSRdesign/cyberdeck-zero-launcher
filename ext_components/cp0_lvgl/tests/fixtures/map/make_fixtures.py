#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# make_fixtures.py: writes the small binary fixtures of the map engine's tests (tests/test_map_*.c) into this folder.
# Not run by the tests: the files it writes are committed. Re-run only to change them, then update the tests.
#
#   python make_fixtures.py            (needs Pillow and the BSD-3 `pmtiles` package: pip install pillow pmtiles)
#
# Every picture is a formula, so the tests check decoded pixels without keeping copies of the source images:
#   rgb256.png     RGB PNG 256 px, pixel (x, y) = (x, y, (x + y) & 255)
#   pal256.png     8-bit palette PNG 256 px, 4 x 4 blocks of 64 px, block i = PALETTE[i]
#   pal512.png     the same at 512 px (blocks of 128 px)
#   la256.png      grey + alpha PNG 256 px: left half alpha 0 (grey 0), right half grey 100 alpha 255
#   q75_256.jpg    JPEG q75 256 px, quadrants red / green / blue / white (top left, top right, bottom left, bottom right)
#   q75_512.jpg    the same at 512 px
#   ref_gzip.pmtiles    PMTiles v3 from the reference writer: zoom 0-2, gzip directories and metadata, palette PNG tiles
#                       of one colour each, TILE_RGB(z, x, y); at zoom 2 every tile but (0, 0) has the colour of (1, 1),
#                       so the writer stores them once with run lengths
#   ref_leaves.pmtiles  the same tiles with leaf directories forced (4 entries per leaf)
#   ref_view.mbtiles    MBTiles in the deduplicated layout of mbutil (map + images tables, a `tiles` view), zoom 0-2,
#                       the same tiles, TMS rows, metadata name / format / bounds / minzoom / maxzoom / attribution
import io
import os
import sqlite3

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

PALETTE = [(0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255),
           (255, 0, 255), (128, 128, 128), (64, 32, 16), (16, 64, 32), (32, 16, 64), (200, 100, 50), (50, 200, 100),
           (100, 50, 200), (240, 240, 230)]


def tile_rgb(z, x, y):
    if z == 2 and (x, y) != (0, 0):
        x, y = 1, 1
    return (40 * z + 20, (x * 50 + 10) & 255, (y * 50 + 30) & 255)


def save(img, name, **kw):
    path = os.path.join(HERE, name)
    img.save(path, **kw)
    print(name, os.path.getsize(path), "bytes")


def png_bytes(img):
    b = io.BytesIO()
    img.save(b, format="PNG", optimize=True)
    return b.getvalue()


def solid_tile(z, x, y):
    img = Image.new("P", (256, 256), 0)
    img.putpalette(list(tile_rgb(z, x, y)) + [0] * (255 * 3))
    return png_bytes(img)


def main():
    img = Image.new("RGB", (256, 256))
    img.putdata([(x, y, (x + y) & 255) for y in range(256) for x in range(256)])
    save(img, "rgb256.png", optimize=True)

    for px in (256, 512):
        img = Image.new("P", (px, px))
        b = px // 4
        img.putdata([(x // b) + 4 * (y // b) for y in range(px) for x in range(px)])
        img.putpalette([c for rgb in PALETTE for c in rgb])
        save(img, "pal%d.png" % px, optimize=True)

    img = Image.new("LA", (256, 256))
    img.putdata([(0, 0) if x < 128 else (100, 255) for y in range(256) for x in range(256)])
    save(img, "la256.png", optimize=True)

    for px in (256, 512):
        img = Image.new("RGB", (px, px))
        h = px // 2
        quad = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]
        img.putdata([quad[(x >= h) + 2 * (y >= h)] for y in range(px) for x in range(px)])
        save(img, "q75_%d.jpg" % px, quality=75)

    tiles = [(z, x, y) for z in range(3) for x in range(1 << z) for y in range(1 << z)]
    data = {t: solid_tile(*t) for t in tiles}

    from pmtiles.tile import Compression, TileType, zxy_to_tileid
    import pmtiles.writer as pw

    header = {"tile_type": TileType.PNG, "tile_compression": Compression.NONE,
              "min_lon_e7": -1800000000, "min_lat_e7": -850511287, "max_lon_e7": 1800000000,
              "max_lat_e7": 850511287, "center_zoom": 1, "center_lon_e7": 0, "center_lat_e7": 0}
    meta = {"name": "Reference gzip", "attribution": "© Test \"data\"", "description": "map engine fixture"}
    order = sorted(tiles, key=lambda t: zxy_to_tileid(*t))

    def write_pm(name, metadata):
        with open(os.path.join(HERE, name), "wb") as f:
            w = pw.Writer(f)
            for t in order:
                w.write_tile(zxy_to_tileid(*t), data[t])
            w.finalize(dict(header), metadata)
        print(name, os.path.getsize(os.path.join(HERE, name)), "bytes")

    write_pm("ref_gzip.pmtiles", meta)
    original = pw.optimize_directories
    pw.optimize_directories = lambda entries, target: pw.build_roots_leaves(entries, 4)
    try:
        write_pm("ref_leaves.pmtiles", dict(meta, name="Reference leaves"))
    finally:
        pw.optimize_directories = original

    path = os.path.join(HERE, "ref_view.mbtiles")
    if os.path.exists(path):
        os.remove(path)
    db = sqlite3.connect(path)
    db.executescript(
        "CREATE TABLE metadata (name text, value text);"
        "CREATE TABLE map (zoom_level integer, tile_column integer, tile_row integer, tile_id text);"
        "CREATE TABLE images (tile_data blob, tile_id text);"
        "CREATE UNIQUE INDEX map_index ON map (zoom_level, tile_column, tile_row);"
        "CREATE UNIQUE INDEX images_id ON images (tile_id);"
        "CREATE VIEW tiles AS SELECT map.zoom_level AS zoom_level, map.tile_column AS tile_column,"
        " map.tile_row AS tile_row, images.tile_data AS tile_data FROM map JOIN images ON images.tile_id = map.tile_id;")
    db.executemany("INSERT INTO metadata VALUES (?, ?)", [
        ("name", "Reference view"), ("format", "png"), ("bounds", "-180,-85.0511,180,85.0511"),
        ("minzoom", "0"), ("maxzoom", "2"), ("attribution", "© Test view"), ("type", "baselayer")])
    ids = {}
    for t in tiles:
        tid = "%02x%02x%02x" % tile_rgb(*t)
        if tid not in ids:
            ids[tid] = True
            db.execute("INSERT INTO images VALUES (?, ?)", (data[t], tid))
        z, x, y = t
        db.execute("INSERT INTO map VALUES (?, ?, ?, ?)", (z, x, (1 << z) - 1 - y, tid))
    db.commit()
    db.execute("VACUUM")
    db.close()
    print("ref_view.mbtiles", os.path.getsize(path), "bytes")


if __name__ == "__main__":
    main()
