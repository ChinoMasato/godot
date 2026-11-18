#!/usr/bin/env python3
"""Measure soft-shadow grain on the street sample (B-714).

Renders the street look sample with the high U.ZU look in a few variants
(ai_loop/windows/look_variant.gd) and scores the grain in the shaded street area:
the mean absolute difference between horizontally neighbouring pixels (luminance,
0-255) inside a fixed crop. High = noisy. The crop is in pixels of a 1920-wide image and
scales with the width; numbers are only comparable at the same resolution.

  python shadow_grain.py <godot-binary> [--variants pre_b714,base] [--resolution 1920x1080]
                         [--driver vulkan] [--frames 120] [--out DIR]
  python shadow_grain.py --score a.png b.png ...      # score existing images only

Works on a real GPU (Windows) and on lavapipe (cloud): pass the binary and driver.
Prints one JSON line: {"ok", "resolution", "results": [{"variant", "grain", "gpu_ms_median", "image"}]}.
Needs Pillow.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE = os.path.join(HERE, "street")
VARIANT_SCRIPT = os.path.join(HERE, "..", "windows", "look_variant.gd")
CROP = (700, 700, 1400, 870)  # shaded street in front of the camera, in pixels of a 1920-wide image


def grain(path):
    from PIL import Image
    im = Image.open(path).convert("L")
    w, h = im.size
    k = w / 1920  # scale by width only: the window may come out a little shorter than asked (1920x1061)
    box = tuple(int(c * k) for c in CROP)
    g = im.crop(box)
    p, (cw, ch) = g.load(), g.size
    total = sum(abs(p[x, y] - p[x + 1, y]) for y in range(ch) for x in range(cw - 1))
    return round(total / (ch * (cw - 1)), 3)


def render(godot, project, variant, args, out):
    shot = os.path.join(out, f"street_{variant}.png")
    cmd = [godot, "--path", project, "--rendering-driver", args.driver, "--resolution", args.resolution,
           "--script", os.path.abspath(VARIANT_SCRIPT), "--", "--variant", variant,
           "--frames", str(args.frames), "--shot", shot]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    info = next((json.loads(l[12:]) for l in r.stdout.splitlines() if l.startswith("UZU_VARIANT ")), None)
    if info is None or not os.path.exists(shot):
        return {"variant": variant, "error": "render failed", "log": (r.stdout + r.stderr)[-800:]}
    return {"variant": variant, "grain": grain(shot), "gpu_ms_median": info.get("gpu_ms_median"), "image": shot}


def once_more(fn, *a):
    """The local RTX 4050 laptop has machine-wide GPU faults (NVIDIA Xid 13 / device lost, also outside Godot;
    see ai_loop/iterations/2026-11-18-GPUFLAKE-claude-local.md). A failed render is tried once more, and the
    first failure is kept in the result as "retried" so flaky runs stay visible."""
    r = fn(*a)
    if "error" in r:
        first = {k: r[k] for k in ("error", "log") if k in r}
        r = fn(*a)
        r["retried"] = first
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("godot", nargs="?")
    ap.add_argument("--variants", default="pre_b714,base")
    ap.add_argument("--resolution", default="1920x1080")
    ap.add_argument("--driver", default="vulkan")
    ap.add_argument("--frames", type=int, default=120)
    ap.add_argument("--out", default="shadow_grain_out")
    ap.add_argument("--score", nargs="+")
    args = ap.parse_args()
    if args.score:
        print(json.dumps({"ok": True, "results": [{"image": p, "grain": grain(p)} for p in args.score]}))
        return 0
    if not args.godot:
        print(json.dumps({"ok": False, "error": "pass the godot binary, or --score images"}))
        return 2
    args.godot = os.path.abspath(args.godot)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="uzu_grain_")
    try:
        project = os.path.join(work, "street")
        shutil.copytree(SAMPLE, project, ignore=shutil.ignore_patterns(".godot"))
        subprocess.run([args.godot, "--headless", "--path", project, "--import"], capture_output=True)
        results = [once_more(render, args.godot, project, v, args, out) for v in args.variants.split(",")]
    finally:
        shutil.rmtree(work, ignore_errors=True)
    ok = all("error" not in r for r in results)
    print(json.dumps({"ok": ok, "resolution": args.resolution, "driver": args.driver, "results": results}))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
