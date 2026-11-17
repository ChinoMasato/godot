#!/usr/bin/env python3
"""Joins six cube faces (from ai_loop/panorama_capture.gd) into an
equirectangular panorama PNG (BACKLOG B-713): longitude across, latitude down,
the camera's forward direction (-Z) in the middle.

Usage: ai_loop/panorama.py <faces-dir> <out.png> [--width 1024]
       ai_loop/panorama.py --self-test
Standard library only.
"""

import json
import math
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image_diff import read_png  # noqa: E402
from palette_check import write_png  # noqa: E402


def join(faces_dir, out, width=1024):
    meta = json.load(open(os.path.join(faces_dir, "faces.json")))
    faces = []
    for i, basis in enumerate(meta["bases"]):
        w, h, c, rows = read_png(os.path.join(faces_dir, f"face_{i}.png"))
        faces.append((basis, w, h, c, rows))
    half = math.tan(math.radians(meta["fov"]) / 2)
    height = width // 2

    def sample(d):
        # Pick the face whose forward direction (-z of its basis) is closest.
        best = max(faces, key=lambda f: -(f[0][2][0] * d[0] + f[0][2][1] * d[1] + f[0][2][2] * d[2]))
        (bx, by, bz), w, h, c, rows = best
        cx = bx[0] * d[0] + bx[1] * d[1] + bx[2] * d[2]
        cy = by[0] * d[0] + by[1] * d[1] + by[2] * d[2]
        cz = bz[0] * d[0] + bz[1] * d[1] + bz[2] * d[2]
        u, v = cx / (-cz * half), cy / (-cz * half)
        x = min(w - 1, max(0, int((u + 1) * 0.5 * w)))
        y = min(h - 1, max(0, int((1 - v) * 0.5 * h)))
        row = rows[y]
        return (row[x * c], row[x * c + 1], row[x * c + 2])

    def pixel(px, py):
        lon = (px + 0.5) / width * 2 * math.pi - math.pi  # -pi..pi, 0 is forward (-Z).
        lat = math.pi / 2 - (py + 0.5) / height * math.pi
        d = (math.sin(lon) * math.cos(lat), math.sin(lat), -math.cos(lon) * math.cos(lat))
        return sample(d)

    write_png(out, width, height, pixel)
    return width, height


def self_test():
    # Synthetic faces: each face a flat color, so the panorama's longitudes are known.
    colors = [(200, 40, 40), (40, 200, 40), (40, 40, 200), (200, 200, 40), (240, 240, 240), (20, 20, 20)]
    bases = [
        [[1, 0, 0], [0, 1, 0], [0, 0, 1]],  # forward -Z
        [[0, 0, 1], [0, 1, 0], [-1, 0, 0]],  # forward +X
        [[-1, 0, 0], [0, 1, 0], [0, 0, -1]],  # forward +Z
        [[0, 0, -1], [0, 1, 0], [1, 0, 0]],  # forward -X
        [[1, 0, 0], [0, 0, 1], [0, -1, 0]],  # forward +Y
        [[1, 0, 0], [0, 0, -1], [0, 1, 0]],  # forward -Y
    ]
    with tempfile.TemporaryDirectory() as tmp:
        for i, color in enumerate(colors):
            write_png(os.path.join(tmp, f"face_{i}.png"), 16, 16, lambda x, y, c=color: c)
        json.dump({"fov": 90.0, "size": 16, "bases": bases}, open(os.path.join(tmp, "faces.json"), "w"))
        out = os.path.join(tmp, "pano.png")
        w, h = join(tmp, out, 64)
        _, _, c, rows = read_png(out)
        at = lambda lon_deg, lat_deg: tuple(rows[int((90 - lat_deg) / 180 * h)][int((lon_deg + 180) / 360 * w) * c : int((lon_deg + 180) / 360 * w) * c + 3])
        assert at(0, 0) == colors[0], at(0, 0)  # forward in the middle
        assert at(90, 0) == colors[1], at(90, 0)  # +X to the right
        assert at(-90, 0) == colors[3], at(-90, 0)  # -X to the left
        assert at(179, 0) == colors[2], at(179, 0)  # +Z behind, at the edges
        assert at(0, 80) == colors[4], at(0, 80)  # up at the top
        assert at(0, -80) == colors[5], at(0, -80)  # down at the bottom
    print("panorama self-test: ok")
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    width = int(sys.argv[sys.argv.index("--width") + 1]) if "--width" in sys.argv else 1024
    w, h = join(sys.argv[1], sys.argv[2], width)
    print(json.dumps({"ok": True, "command": "panorama", "image": sys.argv[2], "size": [w, h]}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
