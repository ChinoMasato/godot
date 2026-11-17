#!/usr/bin/env python3
"""Color and composition check for a screenshot (from ai_loop/screenshot.sh), as JSON.

Usage: ai_loop/palette_check.py <image.png> [--json-only]
       ai_loop/palette_check.py --self-test

Measures painting principles an agent can check by itself (BACKLOG B-711):
- color balance: area share of hue families (neighboring hues merged), as
  base / main / accent (a common guide is about 70 / 25 / 5) and how many
  distinct color families are used (fewer is calmer);
- muddiness: images look muddy when saturation is uniform everywhere or the
  light-dark contrast is weak, so both are measured;
- detail density by height: natural scenes are densest near the eye level
  (horizon), so the row band with the most edges is reported;
- composition: the most detailed area (the likely focal point) and the nearest
  guide point (center or a rule-of-thirds intersection) with its distance.

Warnings are hints for a person to judge, not failures. Exit status is 0 unless
the input cannot be read (2). Standard library only.
"""

import colorsys
import json
import math
import os
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image_diff import read_png  # noqa: E402

HUE_BINS = 12
NEUTRAL_SATURATION = 0.15
STEP = 2  # Sample every other pixel in each direction.


def analyze(path):
    width, height, channels, rows = read_png(path)
    hue_area = [0] * HUE_BINS
    neutral = 0
    saturations = []
    lumas = []
    for y in range(0, height, STEP):
        row = rows[y]
        for x in range(0, width, STEP):
            r, g, b = (row[x * channels + k] / 255.0 for k in range(3))
            h, s, v = colorsys.rgb_to_hsv(r, g, b)
            saturations.append(s)
            lumas.append(0.2126 * r + 0.7152 * g + 0.0722 * b)
            if s < NEUTRAL_SATURATION or v < 0.08:
                neutral += 1
            else:
                hue_area[int(h * HUE_BINS) % HUE_BINS] += 1
    total = len(lumas)

    # Neighboring hues read as one color family (sky blue and blue, green and
    # yellow-green), so contiguous hue bins are merged before counting colors.
    hue_names = ["red", "orange", "yellow", "yellow-green", "green", "green-cyan", "cyan", "azure", "blue", "violet", "magenta", "rose"]
    present = [hue_area[i] / total >= 0.01 for i in range(HUE_BINS)]
    families = []
    if all(present):
        families.append(list(range(HUE_BINS)))
    elif any(present):
        start = next(i for i in range(HUE_BINS) if not present[i])
        run = []
        for k in range(1, HUE_BINS + 1):
            i = (start + k) % HUE_BINS
            if present[i]:
                run.append(i)
            elif run:
                families.append(run)
                run = []
        if run:
            families.append(run)
    colored = sorted(((sum(hue_area[i] for i in fam) / total, fam) for fam in families), reverse=True)
    base = colored[0][0] if colored else 0.0
    main = colored[1][0] if len(colored) > 1 else 0.0
    accent = sum(share for share, _ in colored[2:])
    distinct_hues = sum(1 for share, _ in colored if share >= 0.03)

    # Muddiness.
    mean_s = sum(saturations) / total
    std_s = math.sqrt(sum((s - mean_s) ** 2 for s in saturations) / total)
    lumas.sort()
    p5, p95 = lumas[int(total * 0.05)], lumas[int(total * 0.95)]

    # Edge density per horizontal band (10 bands, top to bottom).
    bands = [0.0] * 10
    grid_w, grid_h = 12, 8
    cells = [[0.0] * grid_w for _ in range(grid_h)]
    for y in range(STEP, height - STEP, STEP):
        band = min(9, y * 10 // height)
        row, above = rows[y], rows[y - STEP]
        for x in range(STEP, width, STEP):
            i, j = x * channels, (x - STEP) * channels
            l = row[i] + row[i + 1] + row[i + 2]
            dx = abs(l - (row[j] + row[j + 1] + row[j + 2]))
            dy = abs(l - (above[i] + above[i + 1] + above[i + 2]))
            bands[band] += dx + dy
            cells[min(grid_h - 1, y * grid_h // height)][min(grid_w - 1, x * grid_w // width)] += dx + dy
    band_total = sum(bands) or 1.0
    densest = max(range(10), key=lambda k: bands[k])

    # Composition: the most detailed area (the likely focal point, smoothed over
    # neighboring cells) and the nearest classic guide point.
    def smoothed(cx, cy):
        return sum(cells[j][i] for j in range(max(0, cy - 1), min(grid_h, cy + 2)) for i in range(max(0, cx - 1), min(grid_w, cx + 2)))

    fx, fy = max(((cx, cy) for cy in range(grid_h) for cx in range(grid_w)), key=lambda c: smoothed(*c))
    focus = ((fx + 0.5) / grid_w, (fy + 0.5) / grid_h)
    guides = {"center": (0.5, 0.5), "thirds_upper_left": (1 / 3, 1 / 3), "thirds_upper_right": (2 / 3, 1 / 3), "thirds_lower_left": (1 / 3, 2 / 3), "thirds_lower_right": (2 / 3, 2 / 3)}
    nearest = min(guides, key=lambda g: math.dist(focus, guides[g]))
    guide_distance = math.dist(focus, guides[nearest])

    warnings = []
    if min(focus[0], focus[1], 1 - focus[0], 1 - focus[1]) < 0.1:
        warnings.append("the most detailed area is at the edge of the frame: move the focal point toward the center or a thirds point")
    if distinct_hues > 3:
        warnings.append(f"{distinct_hues} distinct hues: fewer (up to 3) keeps the color calmer")
    if colored and base < 0.5 and neutral / total < 0.5:
        warnings.append("no dominant base color: about 70% for one base color reads more clearly")
    if accent > 0.15:
        warnings.append(f"accent colors cover {accent:.0%}: accents work best when small (about 5%)")
    if p95 - p5 < 0.35:
        warnings.append(f"weak light-dark contrast ({p95 - p5:.2f}): the image may look muddy or flat")
    if std_s < 0.06 and mean_s > NEUTRAL_SATURATION:
        warnings.append("saturation is nearly uniform: vary it (calmer shadows and highlights, stronger focal points)")

    return {
        "image": os.path.basename(path),
        "size": [width, height],
        "color_balance": {
            "neutral": round(neutral / total, 3),
            "base": round(base, 3),
            "main": round(main, 3),
            "accent": round(accent, 3),
            "hue_families": [{"hues": [hue_names[i] for i in fam], "share": round(share, 3)} for share, fam in colored],
            "distinct_hues": distinct_hues,
        },
        "muddiness": {
            "saturation_mean": round(mean_s, 3),
            "saturation_std": round(std_s, 3),
            "luminance_p5": round(p5, 3),
            "luminance_p95": round(p95, 3),
            "contrast": round(p95 - p5, 3),
        },
        "detail_by_height": {
            "bands_top_to_bottom": [round(b / band_total, 3) for b in bands],
            "densest_band": densest,
        },
        "composition": {
            "focus": [round(focus[0], 3), round(focus[1], 3)],
            "nearest_guide": nearest,
            "guide_distance": round(guide_distance, 3),
        },
        "warnings": warnings,
    }


def write_png(path, width, height, pixel):
    raw = b"".join(b"\x00" + bytes(c for x in range(width) for c in pixel(x, y)) for y in range(height))
    chunk = lambda kind, data: struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        # 70% blue base, 25% green main, 5% red accent, strong contrast, varied saturation.
        good = os.path.join(tmp, "good.png")

        def good_pixel(x, y):
            if x < 70:
                return (30 + y, 60 + y, 160 + y // 2)
            if x < 95:
                return (40, 140, 50)
            return (220, 40, 30)

        write_png(good, 100, 100, good_pixel)
        r = analyze(good)
        balance = r["color_balance"]
        assert set(balance["hue_families"][0]["hues"]) & {"blue", "azure"}, r
        assert 0.6 < balance["base"] < 0.8, r
        assert 0.15 < balance["main"] < 0.35, r
        assert balance["accent"] < 0.1, r
        assert not any("accent" in w for w in r["warnings"]), r

        # Flat mid-grey-green with no contrast: muddy.
        flat = os.path.join(tmp, "flat.png")
        write_png(flat, 100, 100, lambda x, y: (110, 118, 100))
        r = analyze(flat)
        assert any("contrast" in w for w in r["warnings"]), r

        # Five equal, well-separated hues: too many colors, no dominant base.
        many = os.path.join(tmp, "many.png")
        palette = [(220, 40, 30), (200, 210, 40), (40, 200, 120), (40, 60, 220), (200, 40, 200)]
        write_png(many, 100, 60, lambda x, y: palette[x // 20])
        r = analyze(many)
        assert r["color_balance"]["distinct_hues"] == 5, r
        assert any("distinct hues" in w for w in r["warnings"]), r
        assert any("dominant base" in w for w in r["warnings"]), r

        # Two neighboring blues count as one color family.
        blues = os.path.join(tmp, "blues.png")
        write_png(blues, 100, 60, lambda x, y: (60, 140, 220) if x < 50 else (40, 80, 220))
        r = analyze(blues)
        assert r["color_balance"]["distinct_hues"] == 1, r

        # Detail only in the middle band: the densest band is found there.
        mid = os.path.join(tmp, "mid.png")
        write_png(mid, 100, 100, lambda x, y: (255, 255, 255) if 45 <= y < 55 and x % 4 < 2 else (128, 128, 128))
        r = analyze(mid)
        assert r["detail_by_height"]["densest_band"] in (4, 5), r

        # A detailed patch near the upper-left thirds point is found as the focus.
        thirds = os.path.join(tmp, "thirds.png")
        write_png(thirds, 120, 90, lambda x, y: (255, 255, 255) if 30 <= x < 50 and 22 <= y < 38 and (x + y) % 3 == 0 else (90, 100, 120))
        r = analyze(thirds)
        assert r["composition"]["nearest_guide"] == "thirds_upper_left", r
        assert r["composition"]["guide_distance"] < 0.1, r

        # Detail only at the left border: an edge warning.
        edge = os.path.join(tmp, "edge.png")
        write_png(edge, 120, 90, lambda x, y: (255, 255, 255) if x < 6 and (x + y) % 2 == 0 else (90, 100, 120))
        r = analyze(edge)
        assert any("edge of the frame" in w for w in r["warnings"]), r
    print("self-test: ok")
    return 0


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--self-test":
        return self_test()
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        result = analyze(sys.argv[1])
    except (OSError, ValueError, zlib.error) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=None if "--json-only" in sys.argv else 2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
