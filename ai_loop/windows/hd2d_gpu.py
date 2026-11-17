#!/usr/bin/env python3
"""HD-2D on a real GPU (B-715..B-718, B-722): shots and GPU frame time of the hd2d look sample.

Each case copies ai_loop/look_samples/hd2d, runs look commands on the copy through uzu.py,
then renders it as saved with scene_time.gd (Vulkan). Writes <out>/<case>.png and prints one
JSON line {"ok", "results": [{"case", "steps", "gpu_ms_median", "gpu_ms_p95", "image"}]}.

  python hd2d_gpu.py <out-dir> [--cases raw,look,hd2d,...] [--frames 300] [--resolution 1920x1080]
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
SAMPLE = os.path.join(AI_LOOP, "look_samples", "hd2d")
S = ["--scene", "res://main.tscn"]
CASES = {
    "raw": [],
    "look": [["apply"] + S + ["--quality", "high", "--atmosphere", "golden_hour"]],
    "hd2d": [["hd2d"] + S],
    "hd2d_no_tiltshift": [["hd2d"] + S],  # rendered with --drop UZUTiltShift
    "hd2d_no_camera": [["hd2d"] + S + ["--no-camera"]],
    "hd2d_particles": [["hd2d"] + S, ["particles"] + S + ["--kind", "dust"],
                       ["particles"] + S + ["--kind", "embers", "--position", "2,0.5,0"]],
    "hd2d_night_fireflies": [["hd2d"] + S + ["--atmosphere", "night"], ["particles"] + S + ["--kind", "fireflies"]],
    "hd2d_snow": [["hd2d"] + S + ["--atmosphere", "clear"], ["particles"] + S + ["--kind", "snow"]],
}


def godot():
    if os.environ.get("GODOT"):
        return os.environ["GODOT"]
    return os.path.join(REPO, "bin", "godot.windows.editor.x86_64.console.exe")


def run_case(name, steps, args, out):
    work = tempfile.mkdtemp(prefix="uzu_hd2d_")
    try:
        project = os.path.join(work, "hd2d")
        shutil.copytree(SAMPLE, project, ignore=shutil.ignore_patterns(".godot"))
        subprocess.run([godot(), "--headless", "--path", project, "--import"], capture_output=True)
        done = []
        for st in steps:
            r = subprocess.run([sys.executable, os.path.join(AI_LOOP, "uzu.py"), "look", project] + st,
                               capture_output=True, text=True, encoding="utf-8", errors="replace")
            try:
                res = json.loads(r.stdout.strip().splitlines()[-1])
            except (ValueError, IndexError):
                res = {"ok": False, "log": (r.stdout + r.stderr)[-600:]}
            if not res.get("ok"):
                return {"case": name, "error": f"look {st[0]} failed", "result": res}
            done.append(st[0])
        shot = os.path.join(out, f"{name}.png")
        r = subprocess.run([godot(), "--path", project, "--rendering-driver", "vulkan", "--resolution", args.resolution,
                            "--script", os.path.join(HERE, "scene_time.gd"), "--", "--frames", str(args.frames), "--shot", shot]
                           + (["--drop", "UZUTiltShift"] if name.endswith("_no_tiltshift") else []),
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        info = next((json.loads(l[14:]) for l in r.stdout.splitlines() if l.startswith("UZU_SCENETIME ")), None)
        errors = [l for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l][:5]
        if not info or not info.get("ok"):
            return {"case": name, "error": "render failed", "log": (r.stdout + r.stderr)[-600:]}
        return {"case": name, "steps": done, "gpu_ms_median": info["gpu_ms_median"], "gpu_ms_p95": info["gpu_ms_p95"],
                "size": info["size"], "image": shot, "errors": errors}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--cases", default=",".join(CASES))
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--resolution", default="1920x1080")
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    results = [run_case(c, CASES[c], args, out) for c in args.cases.split(",")]
    ok = all("error" not in r for r in results)
    print(json.dumps({"ok": ok, "results": results}, ensure_ascii=False))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
