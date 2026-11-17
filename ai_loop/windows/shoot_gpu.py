#!/usr/bin/env python3
"""Windows + real GPU version of ai_loop/look_samples/shoot_all.sh.

Renders every look sample in the same variations as shoot_all.sh, but with the
local GPU (Vulkan) instead of lavapipe + Xvfb, so the owner can judge the real look.

  python shoot_gpu.py <godot-repo> <out-dir> [--frames 8]

Writes <out-dir>/<name>.png, <out-dir>/summary.json (palette_check per shot)
and <out-dir>/index.html (side-by-side sheet).
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

S = "--scene res://main.tscn"
SHOTS = [
    ("landscape_default", "landscape", []),
    ("landscape_clear", "landscape", [f"apply {S} --atmosphere clear"]),
    ("landscape_humid", "landscape", [f"apply {S} --atmosphere humid"]),
    ("landscape_golden_hour", "landscape", [f"apply {S} --atmosphere golden_hour"]),
    ("street_default", "street", []),
    ("street_rig", "street", [f"apply {S}", f"rig {S}"]),
    ("street_night", "street", [f"apply {S} --atmosphere night",
                                f"lamp {S} --position -2.5,2.9,-1.5",
                                f"lamp {S} --position 3,2.9,-3.5",
                                f"lamp {S} --position -1,2.9,-6"]),
    ("street_telephoto", "street", [f"apply {S}", f"camera {S} --lens telephoto --eye standing"]),
    ("materials_default", "materials", []),
    ("materials_presets", "materials", [f"apply {S} --atmosphere golden_hour"] + [
        f"material {S} --node {n} --kind {k}" for n, k in
        [("Skin", "skin"), ("Glass", "glass"), ("Water", "water")] + [(f"Leaf{i}", "foliage") for i in range(5)]]),
]


def run(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo")
    ap.add_argument("out")
    ap.add_argument("--frames", type=int, default=8)
    a = ap.parse_args()

    repo = os.path.abspath(a.repo)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    godot = os.path.join(repo, "bin", "godot.windows.editor.x86_64.console.exe")
    samples = os.path.join(repo, "ai_loop", "look_samples")
    tool = os.path.join(repo, "ai_loop", "look_tool.gd")
    check = os.path.join(repo, "ai_loop", "palette_check.py")
    if not os.path.isfile(godot):
        sys.exit(f"editor binary not found: {godot}")

    summary = []
    work = tempfile.mkdtemp(prefix="uzu_gpu_")
    try:
        for name, sample, steps in SHOTS:
            proj = os.path.join(work, name)
            shutil.copytree(os.path.join(samples, sample), proj)
            # Import once so the project has a .godot folder.
            run([godot, "--headless", "--path", proj, "--import"])
            for step in steps:
                rc, log = run([godot, "--headless", "--path", proj, "--script", tool, "--"] + step.split())
                if rc != 0:
                    print(log[-2000:])
                    sys.exit(f"{name}: step failed: {step}")
            frames = os.path.join(work, name + "_frames")
            os.makedirs(frames)
            rc, log = run([godot, "--path", proj, "--rendering-driver", "vulkan",
                           "--write-movie", os.path.join(frames, "frame.png"),
                           "--fixed-fps", "10", "--quit-after", str(a.frames)])
            pngs = sorted(glob.glob(os.path.join(frames, "*.png")))
            if not pngs:
                print(log[-2000:])
                sys.exit(f"{name}: no frames rendered")
            dst = os.path.join(out, name + ".png")
            shutil.copy(pngs[-1], dst)
            gpu = next((l.strip() for l in log.splitlines() if "Vulkan" in l and "-" in l), "")
            rc, js = run([sys.executable, "-I", check, dst, "--json-only"])
            try:
                chk = json.loads(js.strip().splitlines()[-1])
            except Exception:
                chk = {"raw": js.strip()[-500:]}
            summary.append({"shot": name, "steps": "; ".join(steps), "device": gpu, "check": chk})
            print(f"shot: {name}  {gpu}")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    with open(os.path.join(out, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=1)
    rows = "".join(
        f'<figure><img src="{s["shot"]}.png"><figcaption><b>{s["shot"]}</b><br>{s["steps"] or "Godot default (no U.ZU look)"}</figcaption></figure>'
        for s in summary)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write('<!doctype html><meta charset="utf-8"><title>U.ZU look GPU shots</title>'
                '<style>body{font:14px sans-serif;background:#111;color:#ddd;margin:16px}'
                'main{display:grid;grid-template-columns:repeat(auto-fill,minmax(420px,1fr));gap:12px}'
                'img{width:100%}figure{margin:0}</style><h1>U.ZU look on GPU</h1><main>' + rows + '</main>')
    print("summary:", os.path.join(out, "summary.json"))


if __name__ == "__main__":
    main()
