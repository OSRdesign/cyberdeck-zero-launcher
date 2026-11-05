#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""P0 spike (report 055), legibility harness with the generated tiles (no real tiles yet).

Builds what each panel would show of a town centre at zoom 14, with the display scales of D5:
  1x      the 256 px tile 1:1
  1.5x    256 px tile magnified (bilinear; the engine's fast 2->3 path looks the same, report 055)
  2x      256 px tile magnified nearest (the cheapest path)
  @2x     the 512 px tile 1:1 (crisp labels drawn for 2x)
  @2x/.75 the 512 px tile scaled to 1.5x (crisp labels at 1.5x)
and prints the physical label size per board from the panel density (px/mm).

  python legibility.py TILEDIR OUTDIR
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONT = r"C:\CLAUDE\zero7\launcher-study-maps\projects\APPLaunch\APPLaunch\share\font\DejaVuSans.ttf"
LABEL_PX = 12                      # street labels at the 256 px scale (gen_tiles.py)
CX, CY = 8292, 5684                # a town-centre tile at zoom 14 (gen_tiles.py)

# panel facts: canvas, px/mm, where it comes from, the launcher's text tokens on that canvas (screen.state text=)
BOARDS = [
    ("deck", 640, 480, 11.3, "screen.state ppmm=1130 density=default (APP-CONTRACT example; report 054 V)", (32, 28, 24, 18)),
    ("zero8-480x320", 480, 320, 6.3, "zero8 screen.state ppmm=630 density=panel_mm (79x49 mm), read 2026-11-05", None),
    ("zero8-320x480", 320, 480, 6.3, "zero8 screen.state ppmm=630, PDA mode on, read 2026-11-05", (24, 20, 16, 14)),
]


def mosaic(tdir, tpx, n=4):
    m = Image.new("RGB", (tpx * n, tpx * n))
    for i in range(n):
        for j in range(n):
            p = os.path.join(tdir, "14", str(CX - 1 + i), "%d.png" % (CY - 1 + j))
            m.paste(Image.open(p).convert("RGB"), (i * tpx, j * tpx))
    return m


def view(m, w, h, scale, resample):
    """Centre crop of the mosaic showing w x h panel px at the given magnification."""
    sw, sh = int(round(w / scale)), int(round(h / scale))
    x0, y0 = (m.width - sw) // 2, (m.height - sh) // 2
    crop = m.crop((x0, y0, x0 + sw, y0 + sh))
    if scale == 1:
        return crop
    return crop.resize((w, h), resample)


def glyph_px(px):
    f = ImageFont.truetype(FONT, px)
    cap = f.getbbox("H")
    xh = f.getbbox("x")
    return cap[3] - cap[1], xh[3] - xh[1]


def main():
    tdir, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    m256 = mosaic(os.path.join(tdir, "png256"), 256)
    m512 = mosaic(os.path.join(tdir, "png512"), 512)
    cap12, x12 = glyph_px(LABEL_PX)
    cap24, x24 = glyph_px(2 * LABEL_PX)
    print("label font %d px at the 256 px scale: cap height %d px, x-height %d px (DejaVu Sans)" % (LABEL_PX, cap12, x12))
    print("| Board | px/mm | Option | Label font px on panel | Font size mm | Cap height mm | x-height mm | Smallest launcher text mm |")
    print("|---|---|---|---|---|---|---|---|")
    sheet_rows = []
    for (name, w, h, ppmm, src, text) in BOARDS:
        opts = [
            ("1x", view(m256, w, h, 1, None), 1.0, cap12, x12),
            ("1.5x", view(m256, w, h, 1.5, Image.BILINEAR), 1.5, cap12, x12),
            ("2x", view(m256, w, h, 2, Image.NEAREST), 2.0, cap12, x12),
            ("@2x", view(m512, w, h, 1, None), 1.0, cap24, x24),
            ("@2x-at-1.5x", view(m512, w, h, 0.75, Image.LANCZOS), 0.75, cap24, x24),
        ]
        small = ("%.2f (%d px)" % (text[-1] / ppmm, text[-1])) if text else "not read"
        row = []
        for (opt, im, f, cap, xh) in opts:
            font_px = (LABEL_PX * 2 if opt.startswith("@2x") else LABEL_PX) * f
            im.save(os.path.join(out, "%s_%s.png" % (name, opt)))
            print("| %s | %.1f | %s | %.1f | %.2f | %.2f | %.2f | %s |" % (
                name, ppmm, opt, font_px, font_px / ppmm, cap * f / ppmm, xh * f / ppmm, small))
            row.append((opt, im))
        sheet_rows.append((name, w, h, row))
    # one contact sheet per board: the options side by side, labelled
    lf = ImageFont.truetype(FONT, 14)
    for (name, w, h, row) in sheet_rows:
        sheet = Image.new("RGB", (len(row) * (w + 10) + 10, h + 34), (255, 255, 255))
        d = ImageDraw.Draw(sheet)
        for k, (opt, im) in enumerate(row):
            x = 10 + k * (w + 10)
            sheet.paste(im, (x, 28))
            d.text((x, 6), "%s  %s" % (name, opt), font=lf, fill=(0, 0, 0))
        sheet.save(os.path.join(out, "sheet_%s.png" % name))
    print("written to", out)


if __name__ == "__main__":
    main()
