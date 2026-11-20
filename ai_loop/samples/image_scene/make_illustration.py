#!/usr/bin/env python3
"""The sample specification illustration for B-1221, drawn procedurally
(standard library only) so its provenance is exact: a dusk village street,
960x540 -- gradient sky, a low sun, a mountain ridge, a row of houses with
lit windows, a lantern, a big tree, and in the foreground a house front
with a door and one lit window, on a path through dark ground.

    python3 make_illustration.py            # writes illustration.png here

The scene is composed so the B-1221 analysis has something to find: bright
blobs (sun, windows, lantern) for glow candidates, a tall distinct door
for a door candidate, ground-standing silhouettes (houses, tree) for
collision candidates, and a clear ground strip to walk on.
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
from image_scene import write_png  # noqa: E402

W, H = 960, 540
HORIZON = 335  # Where the ground starts.


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 4)

    def set(self, x, y, rgb, alpha=1.0):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 4
            if alpha >= 1.0:
                self.px[i:i + 3] = bytes(rgb)
            else:
                for k in range(3):
                    self.px[i + k] = int(self.px[i + k] * (1 - alpha) + rgb[k] * alpha)
            self.px[i + 3] = 255

    def rect(self, x0, y0, x1, y1, rgb):
        for y in range(int(y0), int(y1)):
            for x in range(int(x0), int(x1)):
                self.set(x, y, rgb)

    def circle(self, cx, cy, r, rgb, alpha=1.0):
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    self.set(x, y, rgb, alpha)

    def glow(self, cx, cy, r, rgb):
        """A radial falloff disc, alpha-blended: a light halo."""
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                d = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
                if d < r:
                    self.set(x, y, rgb, (1 - d / r) ** 2 * 0.55)

    def polygon(self, points, rgb):
        ys = [p[1] for p in points]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            nodes = []
            for i in range(len(points)):
                x0, y0 = points[i]
                x1, y1 = points[(i + 1) % len(points)]
                if (y0 <= y < y1) or (y1 <= y < y0):
                    nodes.append(x0 + (y - y0) / (y1 - y0) * (x1 - x0))
            nodes.sort()
            for i in range(0, len(nodes) - 1, 2):
                for x in range(int(nodes[i]), int(nodes[i + 1]) + 1):
                    self.set(x, y, rgb)


def lerp(a, b, t):
    return tuple(int(a[k] + (b[k] - a[k]) * t) for k in range(3))


def main():
    c = Canvas(W, H)
    # Sky: deep indigo to warm orange at the horizon.
    top, low = (38, 42, 84), (216, 138, 78)
    for y in range(HORIZON):
        c.rect(0, y, W, y + 1, lerp(top, low, (y / HORIZON) ** 1.6))
    # Ground: dark green-brown, lighter toward the bottom.
    gtop, gbot = (44, 50, 30), (64, 68, 42)
    for y in range(HORIZON, H):
        c.rect(0, y, W, y + 1, lerp(gtop, gbot, (y - HORIZON) / (H - HORIZON)))
    # The path: a lighter trapezoid widening toward the viewer.
    c.polygon([(430, HORIZON + 6), (520, HORIZON + 6), (640, H), (300, H)], (96, 88, 62))
    # Sun: halo first, then the disc, low over the ridge.
    c.glow(300, 296, 90, (255, 190, 110))
    c.circle(300, 296, 30, (255, 226, 168))
    # Mountain ridge across the whole width.
    c.polygon([(0, 330), (140, 268), (300, 318), (470, 262), (640, 322), (800, 278), (960, 326), (960, 372), (0, 372)], (56, 50, 88))
    # Midground houses: dark silhouettes, gable roofs, lit windows.
    wall, roof = (46, 36, 42), (60, 42, 46)
    for hx, hw, hh in ((120, 110, 74), (268, 92, 62), (560, 120, 82), (716, 96, 66)):
        top_y = 428 - hh
        c.rect(hx, top_y + 18, hx + hw, 428, wall)
        c.polygon([(hx - 8, top_y + 20), (hx + hw / 2, top_y - 14), (hx + hw + 8, top_y + 20)], roof)
        for wx in range(int(hx + 14), int(hx + hw - 16), 30):
            c.rect(wx, top_y + 34, wx + 12, top_y + 50, (255, 206, 110))
    # A lantern by the path: pole and a bright head.
    c.rect(614, 356, 620, 428, (30, 26, 26))
    c.glow(617, 348, 26, (255, 196, 110))
    c.rect(608, 338, 626, 356, (255, 214, 130))
    # Foreground house, front-left: wall, roof, a lit window and the door.
    c.rect(56, 318, 306, 486, (40, 32, 36))
    c.polygon([(44, 322), (181, 262), (318, 322)], (54, 38, 40))
    c.rect(84, 356, 116, 388, (255, 206, 110))
    c.rect(196, 372, 228, 452, (98, 66, 42))  # The door: tall, warm, distinct.
    c.circle(220, 414, 3, (255, 220, 140))
    # Big tree, foreground right: trunk and a dark canopy of blobs.
    c.rect(838, 330, 862, 486, (34, 26, 22))
    for cx, cy, r in ((850, 268, 92), (786, 306, 58), (916, 300, 62), (850, 330, 66)):
        c.circle(cx, cy, r, (22, 44, 26))
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "illustration.png")
    write_png(out, W, H, c.px)
    print("wrote", out)


if __name__ == "__main__":
    main()
