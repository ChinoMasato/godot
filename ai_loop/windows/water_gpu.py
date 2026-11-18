#!/usr/bin/env python3
"""The B-727 water on a real GPU: shots, GPU frame time and a few image measures, per renderer.

Copies ai_loop/look_samples/water, runs `look apply` (and `look water` for the water cases) on the
copy through uzu.py, then renders it as saved with scene_time.gd. Each renderer gets a "none" shot
(no water) so the water cases can be measured against it.

  python water_gpu.py <out-dir> [--renderers forward_plus,mobile,compatibility]
                      [--cases none,water,no_reflection,no_refraction] [--frames 300] [--resolution 1920x1080]

Measures (on the water shot, against "none" of the same renderer):
  water_px   pixels that changed (luminance difference > 6): where the water is
  foam_px    near-white pixels (all channels > 200) that were not near-white without water
  near/far   mean RGB in a box of shallow water (behind the near shore) and of deep water (the middle)
Prints one JSON line {"ok", "results": [{"renderer", "case", "gpu_ms_median", "gpu_ms_p95", ...}]}.
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
AI_LOOP = os.path.dirname(HERE)
REPO = os.path.dirname(AI_LOOP)
SAMPLE = os.path.join(AI_LOOP, "look_samples", "water")
S = ["--scene", "res://main.tscn"]
WATER = ["water"] + S + ["--position", "0,0,-7.5", "--size", "16,21"]  # as in shoot_all.sh (water_surface)
CASES = {
    "none": [["apply"] + S],
    "water": [["apply"] + S, WATER],
    "no_reflection": [["apply"] + S, WATER + ["--reflection", "0"]],
    "no_refraction": [["apply"] + S, WATER + ["--refraction", "0"]],
}
RENDERERS = {
    "forward_plus": ["--rendering-driver", "vulkan"],
    "mobile": ["--rendering-driver", "vulkan", "--rendering-method", "mobile"],
    "compatibility": ["--rendering-driver", "opengl3"],
}
# Boxes as fractions of the image (x0, y0, x1, y1), inside the water of the pond sample, left of
# the post and tower reflections: shallow water just behind the near shore, and the deep middle.
NEAR = (0.22, 0.60, 0.34, 0.63)
FAR = (0.22, 0.47, 0.34, 0.53)


def godot():
    return os.environ.get("GODOT") or os.path.join(REPO, "bin", "godot.windows.editor.x86_64.console.exe")


def make_project(steps):
    work = tempfile.mkdtemp(prefix="uzu_water_")
    project = os.path.join(work, "water")
    shutil.copytree(SAMPLE, project, ignore=shutil.ignore_patterns(".godot"))
    subprocess.run([godot(), "--headless", "--path", project, "--import"], capture_output=True)
    for st in steps:
        r = subprocess.run([sys.executable, os.path.join(AI_LOOP, "uzu.py"), "look", project] + st,
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        try:
            res = json.loads(r.stdout.strip().splitlines()[-1])
        except (ValueError, IndexError):
            res = {"ok": False, "log": (r.stdout + r.stderr)[-600:]}
        if not res.get("ok"):
            shutil.rmtree(work, ignore_errors=True)
            return None, f"look {st[0]} failed: {res}"
    return work, project


def render(project, renderer, shot, args):
    cmd = [godot(), "--path", project] + RENDERERS[renderer] + ["--resolution", args.resolution,
           "--script", os.path.join(HERE, "scene_time.gd"), "--", "--frames", str(args.frames), "--shot", shot]
    for _ in range(2):  # once more: this machine sometimes loses a render to a GPU reset (GPUFLAKE)
        if os.path.exists(shot):
            os.remove(shot)
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        info = next((json.loads(l[14:]) for l in r.stdout.splitlines() if l.startswith("UZU_SCENETIME ")), None)
        errors = [l.strip() for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l][:5]
        if info and info.get("ok") and os.path.exists(shot):
            return info, errors
    return None, errors or [(r.stdout + r.stderr)[-400:]]


def box_mean(im, box):
    w, h = im.size
    c = im.crop(tuple(int(v * s) for v, s in zip(box, (w, h, w, h))))
    px = list(c.getdata())
    return [round(sum(p[i] for p in px) / len(px)) for i in range(3)]


def measure(shot, none_shot):
    from PIL import Image, ImageChops
    a = Image.open(shot).convert("RGB")
    out = {"near": box_mean(a, NEAR), "far": box_mean(a, FAR)}
    if none_shot and os.path.exists(none_shot):
        b = Image.open(none_shot).convert("RGB")
        if b.size == a.size:
            d = ImageChops.difference(a.convert("L"), b.convert("L"))
            out["water_px"] = sum(1 for v in d.getdata() if v > 6)
            out["foam_px"] = sum(1 for p, q in zip(a.getdata(), b.getdata()) if min(p) > 200 and min(q) <= 200)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--renderers", default=",".join(RENDERERS))
    ap.add_argument("--cases", default=",".join(CASES))
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--resolution", default="1920x1080")
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    results = []
    for case in args.cases.split(","):
        work, project = make_project(CASES[case])
        if work is None:
            results.append({"case": case, "error": project})
            continue
        try:
            for renderer in args.renderers.split(","):
                shot = os.path.join(out, f"{renderer}_{case}.png")
                info, errors = render(project, renderer, shot, args)
                if info is None:
                    results.append({"renderer": renderer, "case": case, "error": "render failed", "log": errors})
                    continue
                r = {"renderer": renderer, "case": case, "gpu_ms_median": info["gpu_ms_median"],
                     "gpu_ms_p95": info["gpu_ms_p95"], "image": shot, "errors": errors}
                r.update(measure(shot, os.path.join(out, f"{renderer}_none.png") if case != "none" else None))
                results.append(r)
        finally:
            shutil.rmtree(work, ignore_errors=True)
    ok = all("error" not in r for r in results)
    print(json.dumps({"ok": ok, "resolution": args.resolution, "results": results}, ensure_ascii=False))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
