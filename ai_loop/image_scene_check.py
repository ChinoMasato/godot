#!/usr/bin/env python3
"""End-to-end check for B-1221 (the image-first scaffold), run by hand:

    GODOT=<editor binary> python3 ai_loop/image_scene_check.py

Copies the sample project (ai_loop/samples/image_scene/) to a temp dir,
runs `uzu.py image-scene` on its illustration, and asserts the BACKLOG
judgement: a walkable scaffold is generated (build ok, the capsule walker
reaches every waypoint), the report carries glow and door candidates, and
the generated spec can be edited and rebuilt with the edit landing in the
scene. Prints one JSON line {"ok": bool, ...}.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE = os.path.join(HERE, "samples", "image_scene")


def run_uzu(args):
    p = subprocess.run([sys.executable, os.path.join(HERE, "uzu.py")] + args,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1800)
    text = p.stdout.decode("utf-8", errors="replace")
    line = next((l for l in text.splitlines() if l.startswith("{")), "")
    try:
        return json.loads(line)
    except ValueError:
        print(text[-3000:], file=sys.stderr)
        raise


def main():
    checks = {}
    with tempfile.TemporaryDirectory() as tmp:
        project = os.path.join(tmp, "project")
        shutil.copytree(SAMPLE, project, ignore=shutil.ignore_patterns("image_scene", "__pycache__"))
        shot = os.path.join(tmp, "shot.png")
        result = run_uzu(["image-scene", project, "--image", os.path.join(project, "illustration.png"),
                          "--shot", shot])
        checks["generate_ok"] = bool(result.get("ok"))
        report = result.get("report", {})
        kinds = {c["kind"] for c in report.get("candidates", {}).get("interact", [])}
        checks["candidates"] = sorted(kinds)
        checks["glow_and_door"] = {"glow", "door"} <= kinds
        built = result.get("built", {})
        checks["cards"] = built.get("built", {}).get("cards", 0)
        checks["markers"] = built.get("built", {}).get("markers", 0)
        walk = result.get("walk", {})
        checks["walk"] = {"reached": walk.get("reached"), "targets": walk.get("targets")}
        checks["walk_ok"] = bool(walk.get("ok"))
        checks["shot"] = os.path.isfile(shot)
        # Edit the spec (move the first cut-out card, drop the markers) and
        # rebuild: the edit must land in the saved scene.
        spec_path = os.path.join(project, "image_scene", "spec.json")
        with open(spec_path, encoding="utf-8") as f:
            spec = json.load(f)
        moved = spec["cards"][1]
        moved["position"][0] += 2.0
        spec["markers"] = []
        with open(spec_path, "w", encoding="utf-8") as f:
            json.dump(spec, f)
        rebuilt = run_uzu(["build", project, "--spec", spec_path, "--no-report"])
        checks["rebuild_ok"] = bool(rebuilt.get("ok"))
        with open(os.path.join(project, "image_scene", "image_scene.tscn"), encoding="utf-8") as f:
            tscn = f.read()
        checks["edit_landed"] = ("Candidate_" not in tscn) and checks["rebuild_ok"]
    checks["ok"] = all(v for k, v in checks.items()
                       if k not in ("candidates", "cards", "markers", "walk"))
    print(json.dumps({"command": "image-scene-check", **checks}, ensure_ascii=False))
    return 0 if checks["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
