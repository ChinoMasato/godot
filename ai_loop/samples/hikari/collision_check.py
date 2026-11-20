#!/usr/bin/env python3
"""B-1224 sample: mechanical collision check of the generated village
(B-1508's village, B-1509's spec set), three runs:

    spec_set/collision_report.json          the village as composed,
                                            started from the Land
                                            clearing and the village
                                            spawn (region_graph.json)
    spec_set/collision_report_shifted.json  one building collision
                                            (Village/CityBody/Building0)
                                            shifted +2 m in x in the
                                            saved scene first
    (not saved)                             a small clean fixture scene
                                            built below: must pass

Flow follows audit_check.py: copy the project to a temp dir, compose
the village there (compose_village.gd -> res://village.tscn), then run
`uzu.py collision-check`.

What the baseline must show (each is a measured fact about the
artifact, pinned here so a change in either the generator or the
checker surfaces):

  * ok: the known-good layout passes (no error findings).
  * The foundation slab entry block B-1510's physics walker hit is
    detected statically as direct_route_blocked: the straight approach
    from the Land clearing to the village spawn is walled at the
    slab's south edge by a ledge over the 0.25 m step (measured
    0.31 m). The starts are NOT disconnected: around the west rim the
    terrain rises to within the step of the slab top, and the check
    says so by not raising starts_disconnected.
  * No wall_leak, and no visual_uncovered error: every building-scale
    mesh is carried by its collision.
  * The known artifact gaps stay visible as warnings: the plaza
    landmark (fountain) and the lamp posts have no collision in the
    saved scene (the game derives their obstacles at runtime, B-1508).
  * Village__tallest_roof is place_unreachable at info severity: it
    sits on an elevated surface, reachable only by falling.

The shifted run must point at the moved building: a hole / overhang /
uncovered finding within 6 m of the building's original centre. The
clean fixture (a ground slab and a house whose visual and collision
agree exactly) must come back ok with no warning or error.

Usage: python3 collision_check.py   (the editor binary: $GODOT or bin/)
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

SHIFT_NODE = "Building0"
SHIFT_DX = 2.0

CLEAN_TSCN = """[gd_scene load_steps=5 format=3]

[sub_resource type="BoxShape3D" id="BoxShape3D_ground"]
size = Vector3(40, 1, 40)

[sub_resource type="BoxMesh" id="BoxMesh_ground"]
size = Vector3(40, 1, 40)

[sub_resource type="BoxShape3D" id="BoxShape3D_house"]
size = Vector3(4, 3, 4)

[sub_resource type="BoxMesh" id="BoxMesh_house"]
size = Vector3(4, 3, 4)

[node name="Clean" type="Node3D"]
metadata/uzu_places = {
"spawn": {
"about": "a start on open ground",
"position": [0.0, 0.0, 8.0]
}
}

[node name="GroundBody" type="StaticBody3D" parent="."]

[node name="Shape" type="CollisionShape3D" parent="GroundBody"]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -0.5, 0)
shape = SubResource("BoxShape3D_ground")

[node name="Ground" type="MeshInstance3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -0.5, 0)
mesh = SubResource("BoxMesh_ground")

[node name="HouseBody" type="StaticBody3D" parent="."]

[node name="Shape" type="CollisionShape3D" parent="HouseBody"]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 6, 1.5, 0)
shape = SubResource("BoxShape3D_house")

[node name="House" type="MeshInstance3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 6, 1.5, 0)
mesh = SubResource("BoxMesh_house")
"""


def run(cmd, timeout=900):
    env = dict(os.environ)
    env["GODOT"] = find_godot()
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
    lines = [l for l in proc.stdout.splitlines() if l.startswith("{")]
    if not lines:
        sys.stderr.write(proc.stdout[-2000:] + proc.stderr[-2000:])
        raise SystemExit(f"no JSON result from: {' '.join(cmd)}")
    return json.loads(lines[-1])


def kinds(report):
    return [f["kind"] for f in report["findings"]]


def shift_building(text):
    """Move SHIFT_NODE's convex collision points by SHIFT_DX in x.
    Returns (new_text, original AABB centre [x, z])."""
    m = re.search(
        r'\[node name="%s" type="CollisionShape3D" parent="Village/CityBody"[^\]]*\]\n'
        r'shape = SubResource\("([^"]+)"\)' % SHIFT_NODE, text)
    if not m:
        raise SystemExit(f"could not find {SHIFT_NODE}'s shape in the scene copy")
    shape_id = m.group(1)
    block = re.search(
        r'(\[sub_resource type="ConvexPolygonShape3D" id="%s"\]\npoints = PackedVector3Array\()'
        r'([^)]*)(\))' % re.escape(shape_id), text)
    if not block:
        raise SystemExit(f"could not find sub_resource {shape_id} in the scene copy")
    values = [float(v) for v in block.group(2).split(",")]
    xs = values[0::3]
    zs = values[2::3]
    # The points are local to the Village holder; its transform in the
    # saved scene is a pure translation (identity basis), so the world
    # centre is the local centre plus the holder's origin.
    holder = re.search(
        r'\[node name="Village" type="Node3D"[^\]]*\]\n'
        r'transform = Transform3D\(([^)]*)\)', text)
    ox, oz = 0.0, 0.0
    if holder:
        basis = [float(v) for v in holder.group(1).split(",")]
        ox, oz = basis[9], basis[11]
    centre = [(min(xs) + max(xs)) / 2.0 + ox, (min(zs) + max(zs)) / 2.0 + oz]
    shifted = [v + SHIFT_DX if k % 3 == 0 else v for k, v in enumerate(values)]
    body = ", ".join(repr(round(v, 4)) for v in shifted)
    return text[:block.start()] + block.group(1) + body + block.group(3) + text[block.end():], centre


def main():
    spec_set = os.path.join(HERE, "spec_set")
    godot = find_godot()
    with open(os.path.join(spec_set, "region_graph.json"), encoding="utf-8") as f:
        graph = json.load(f)
    centres = {n["id"]: n["center"] for n in graph["nodes"]}
    starts = []
    for pid in ("Land__clearings_1", "Village__spawn"):
        c = centres[pid]
        starts += ["--start", f"{c[0]},{c[2]}"]

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

        uzu = os.path.join(LOOP, "uzu.py")
        baseline = run([sys.executable, uzu, "collision-check", game,
                        "--scene", "res://village.tscn"] + starts +
                       ["--heatmap", os.path.join(spec_set, "collision_heatmap.png")])
        with open(os.path.join(spec_set, "collision_report.json"), "w", encoding="utf-8") as f:
            json.dump(baseline, f, ensure_ascii=False, indent=2)

        scene_path = os.path.join(game, "village.tscn")
        with open(scene_path, encoding="utf-8") as f:
            text = f.read()
        text, centre = shift_building(text)
        with open(scene_path, "w", encoding="utf-8") as f:
            f.write(text)
        shifted = run([sys.executable, uzu, "collision-check", game,
                       "--scene", "res://village.tscn"] + starts)
        shifted["shifted_from"] = [round(centre[0], 2), 0.0, round(centre[1], 2)]
        shifted["shifted_node"] = f"Village/CityBody/{SHIFT_NODE}"
        shifted["shifted_dx_m"] = SHIFT_DX
        with open(os.path.join(spec_set, "collision_report_shifted.json"), "w", encoding="utf-8") as f:
            json.dump(shifted, f, ensure_ascii=False, indent=2)

        with open(os.path.join(game, "clean.tscn"), "w", encoding="utf-8") as f:
            f.write(CLEAN_TSCN)
        clean = run([sys.executable, uzu, "collision-check", game,
                     "--scene", "res://clean.tscn"])

    problems = []
    if not baseline["ok"]:
        problems.append(f"baseline is not ok: {kinds(baseline)}")
    direct = [f for f in baseline["findings"] if f["kind"] == "direct_route_blocked"]
    if not direct:
        problems.append(f"no direct_route_blocked for the slab approach: {kinds(baseline)}")
    else:
        ev = direct[0]["evidence"]
        if not ev.get("required_rise_m") or ev["required_rise_m"] <= baseline["params"]["step"]:
            problems.append(f"slab ledge does not exceed the step: {ev}")
        bx, bz = direct[0]["position"][0], direct[0]["position"][2]
        if not (-48.5 <= bx <= 6.5 and -29.5 <= bz <= -27.0):
            problems.append(f"slab block not at the south slab edge: {direct[0]['position']}")
    if "starts_disconnected" in kinds(baseline):
        problems.append("starts unexpectedly disconnected (the west rim entry must stay open)")
    if "wall_leak" in kinds(baseline):
        problems.append(f"wall leak in the known-good layout: {kinds(baseline)}")
    uncovered_err = [f for f in baseline["findings"]
                     if f["kind"] == "visual_uncovered" and f["severity"] == "error"]
    if uncovered_err:
        problems.append(f"building-scale visual left uncovered: {uncovered_err}")
    landmark = [f for f in baseline["findings"]
                if f["kind"] == "visual_without_collision"
                and any("Landmark" in o for o in f.get("objects") or [])]
    if not landmark:
        problems.append("the landmark's known missing collision is no longer reported "
                        "(if the generator grew one, update this pin on purpose)")

    cx, cz = shifted["shifted_from"][0], shifted["shifted_from"][2]
    near = [f for f in shifted["findings"]
            if f["kind"] in ("visual_uncovered", "collision_overhang",
                             "wall_leak", "visual_without_collision")
            and f["position"] and abs(f["position"][0] - cx) < 6
            and abs(f["position"][2] - cz) < 6]
    if not near:
        problems.append(f"the shifted building is not pointed at: {kinds(shifted)}")

    bad = [f for f in clean["findings"] if f["severity"] in ("warning", "error")]
    if not clean["ok"] or bad:
        problems.append(f"the clean fixture does not pass: {clean['findings']}")

    print(json.dumps({"ok": not problems, "problems": problems,
                      "baseline_kinds": kinds(baseline),
                      "shifted_kinds": kinds(shifted),
                      "clean_kinds": kinds(clean)}, ensure_ascii=False))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
