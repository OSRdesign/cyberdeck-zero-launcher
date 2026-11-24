#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Draws the Quick controls tile icon (projects/APPLaunch/APPLaunch/share/images/quickctl_100.png).

100x100 transparent PNG like the other built-in tiles: one coral badge (hue about 350, the free pink-red range of
docs/ICON-PALETTE.md in the apps repo), a switch plate silhouette, one white power symbol (ring with a gap on top and a
bar), strokes 10 px. Drawn at 8x and reduced with LANCZOS. Pillow only.

    python tools/icons/make_quickctl_icon.py [out.png]
"""
import math
import sys

from PIL import Image, ImageDraw

SCALE = 8
SIZE = 100
BADGE = (255, 77, 109, 255)   # coral pink #FF4D6D
INK = (255, 255, 255, 255)


def main(path):
    s = SCALE
    img = Image.new("RGBA", (SIZE * s, SIZE * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # the plate: 88 x 78, soft corners, centred a little above the middle (room under it for a two-line label)
    x0, y0, x1, y1 = 6, 8, 94, 86
    d.rounded_rectangle([x0 * s, y0 * s, x1 * s, y1 * s], radius=24 * s, fill=BADGE)
    # the power symbol: a ring open at the top and a bar through the gap
    cx, cy, r, w = 50, 49, 21, 10
    d.arc([(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s], start=-60, end=240, fill=INK, width=w * s)
    for ang in (-60, 240):  # round caps on the ring's two ends
        a = math.radians(ang)
        ex, ey = cx + (r - w / 2) * math.cos(a), cy + (r - w / 2) * math.sin(a)
        d.ellipse([(ex - w / 2) * s, (ey - w / 2) * s, (ex + w / 2) * s, (ey + w / 2) * s], fill=INK)
    d.rounded_rectangle([(cx - w / 2) * s, (cy - r - 5) * s, (cx + w / 2) * s, (cy - 3) * s], radius=(w / 2) * s,
                        fill=INK)
    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    img.save(path)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "projects/APPLaunch/APPLaunch/share/images/quickctl_100.png")
