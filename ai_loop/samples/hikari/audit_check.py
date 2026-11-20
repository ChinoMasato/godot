#!/usr/bin/env python3
"""B-1511 sample: re-read audit of the generated village (B-1508's
village, B-1509's spec set), twice:

    spec_set/scene_audit_report.json          the village as composed
    spec_set/scene_audit_report_removed.json  one part removed from the
                                              saved scene first (the
                                              WindowFrames_0_1 node:
                                              12 window frames)

Flow: copy the project to a temp dir, compose the village scene there
(compose_village.gd -> res://village.tscn), run `uzu.py audit-scene` on
it against the committed spec set, then delete one node block from the
.tscn copy and audit again. The reports are written next to the spec
set they verify.

The baseline is NOT all-match, and the remaining kinds of difference
are the point of the audit (each is documented in
ai_loop/iterations/2026-11-20-B-1511-lead.md):
  * Village/meshes MATCHES since B-1512: the generator's reported
    triangle total used to undercount the window frames (its formula
    divided the frame face count by 3 a second time; the registry said
    6746 against the artifact's 12272). The formula is fixed, the spec
    set regenerated, and the recount now agrees at 12272.
  * Pond/terrain_mesh: main.gd cuts the pond terrain at the city border
    after generation (2048 -> 1024); the spec record's own context says
    the game's adjustments are not part of the record.
  * Land/load_fidelity x4: the composed .tscn stores the nature
    MultiMesh buffers without an instance_count property, and the
    engine loader drops a buffer set while the count is still 0 -- the
    file's instances (537/433/78/3273) recount exactly, the loaded
    scene has none of them.
Everything else -- every part count, the Land terrain, the navigation
mesh, the collision coverage -- must match. The removed run must add
exactly two differences on top: windows 37 -> 25 and the Village mesh
total down by 12 frames x 224 triangles.

Usage: python3 audit_check.py   (the editor binary: $GODOT or bin/)
Exit code 0 only when all of the above holds.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LOOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, LOOP)
from uzu import find_godot  # noqa: E402

REMOVED_NODE = "WindowFrames_0_1"
REMOVED_FRAMES = 12
FRAME_TRIANGLES = 224

BASELINE_DIFFERENCES = {
    "Pond/terrain_mesh": (2048, 1024),
    "Land/load_fidelity/TreesBroadleaf": (537, 0),
    "Land/load_fidelity/TreesConifer": (433, 0),
    "Land/load_fidelity/Rocks": (78, 0),
    "Land/load_fidelity/Grass": (3273, 0),
}


def run(cmd, timeout=900):
    env = dict(os.environ)
    env["GODOT"] = find_godot()
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
    lines = [l for l in proc.stdout.splitlines() if l.startswith("{")]
    if not lines:
        sys.stderr.write(proc.stdout[-2000:] + proc.stderr[-2000:])
        raise SystemExit(f"no JSON result from: {' '.join(cmd)}")
    return json.loads(lines[-1])


def differences(report):
    return {c["id"]: (c["expected"], c["actual"]) for c in report["checks"] if c["status"] == "difference"}


def check(report, cid):
    for c in report["checks"]:
        if c["id"] == cid:
            return c
    raise SystemExit(f"check {cid} missing from the report")


def main():
    spec_set = os.path.join(HERE, "spec_set")
    godot = find_godot()
    with tempfile.TemporaryDirectory() as tmp:
        game = os.path.join(tmp, "hikari")
        shutil.copytree(HERE, game)
        subprocess.run([godot, "--headless", "--path", game, "--import"],
                       capture_output=True, text=True, timeout=900)
        proc = subprocess.run([godot, "--headless", "--path", game, "--script", "res://compose_village.gd"],
                              capture_output=True, text=True, timeout=900)
        lines = [l for l in proc.stdout.splitlines() if l.startswith("UZU_RESULT ")]
        if not lines or not json.loads(lines[-1][len("UZU_RESULT "):]).get("ok"):
            sys.stderr.write(proc.stdout[-2000:] + proc.stderr[-2000:])
            print(json.dumps({"ok": False, "error": "compose_village.gd failed"}))
            return 1
        baseline = run([sys.executable, os.path.join(LOOP, "uzu.py"), "audit-scene", game,
                        "--scene", "res://village.tscn"])
        baseline["spec_set"] = "spec_set"  # the run used a temp copy; keep the saved report path-free
        with open(os.path.join(spec_set, "scene_audit_report.json"), "w", encoding="utf-8") as f:
            json.dump(baseline, f, ensure_ascii=False, indent=2)

        scene_path = os.path.join(game, "village.tscn")
        with open(scene_path, encoding="utf-8") as f:
            text = f.read()
        text, removed = re.subn(
            r'\[node name="%s" type="MultiMeshInstance3D"[^\]]*\]\n(?:.*?\n)*?(?=\[|\Z)' % REMOVED_NODE,
            "", text, count=1)
        if removed != 1:
            print(json.dumps({"ok": False, "error": f"could not remove {REMOVED_NODE} from the scene copy"}))
            return 1
        with open(scene_path, "w", encoding="utf-8") as f:
            f.write(text)
        after = run([sys.executable, os.path.join(LOOP, "uzu.py"), "audit-scene", game,
                     "--scene", "res://village.tscn"])
        after["spec_set"] = "spec_set"
        with open(os.path.join(spec_set, "scene_audit_report_removed.json"), "w", encoding="utf-8") as f:
            json.dump(after, f, ensure_ascii=False, indent=2)

    problems = []
    base_diffs = differences(baseline)
    if base_diffs != BASELINE_DIFFERENCES:
        problems.append(f"baseline differences changed: {base_diffs} != {BASELINE_DIFFERENCES}")
    for cid, expected in (("Village/buildings", 6), ("Village/benches", 4), ("Village/blocks", 9),
                          ("Village/street_trees", 26), ("Village/windows", 37), ("Village/lights", 2),
                          ("Land/terrain_mesh", 8192), ("Land/grass", 3273), ("Land/trees", 970),
                          ("Village/navigation.polygons", 652), ("Village/navigation.vertices", 603)):
        c = check(baseline, cid)
        if c["status"] != "match" or c["actual"] != expected:
            problems.append(f"baseline {cid}: {c['status']} actual={c['actual']} (want match {expected})")
    if baseline["measured"]["texture_count"] != 12:
        problems.append(f"baseline textures: {baseline['measured']['texture_count']} != 12")
    if baseline["measured"]["collision"]["shapes"] != 22:
        problems.append(f"baseline collision shapes: {baseline['measured']['collision']['shapes']} != 22")

    after_diffs = differences(after)
    want_after = dict(BASELINE_DIFFERENCES)
    want_after["Village/windows"] = (37, 37 - REMOVED_FRAMES)
    want_after["Village/meshes"] = (12272, 12272 - REMOVED_FRAMES * FRAME_TRIANGLES)
    if after_diffs != want_after:
        problems.append(f"removed-run differences changed: {after_diffs} != {want_after}")
    base_status = {c["id"]: c["status"] for c in baseline["checks"]}
    for c in after["checks"]:
        if c["id"] in ("Village/windows", "Village/meshes"):
            continue
        if base_status.get(c["id"]) != c["status"]:
            problems.append(f"removed run flipped {c['id']}: {base_status.get(c['id'])} -> {c['status']}")

    print(json.dumps({"ok": not problems, "problems": problems,
                      "baseline_summary": baseline["summary"], "removed_summary": after["summary"]},
                     ensure_ascii=False))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
