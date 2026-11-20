#!/usr/bin/env python3
"""Unit tests for ai_loop/collision_check.py (BACKLOG B-1224).

The analysis runs on synthetic extractions (the same dictionaries
collision_tool.gd produces), so no engine is needed here; the
end-to-end invariants over the generated village live in the second
half and read the reports the sample check
(ai_loop/samples/hikari/collision_check.py) commits.

Run: python3 ai_loop/tests/test_collision_check.py
"""

import json
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import collision_check as cc  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"ok   {name}")
    else:
        print(f"FAIL {name} {detail}")
        FAILURES.append(name)


def kinds(report):
    return [f["kind"] for f in report["findings"]]


def box(path, cx, cy, cz, sx, sy, sz):
    return {"path": path, "body": path.split("/")[-2], "body_class": "StaticBody3D",
            "kind": "box",
            "aabb": [cx - sx / 2, cy - sy / 2, cz - sz / 2,
                     cx + sx / 2, cy + sy / 2, cz + sz / 2]}


def visual(path, cx, cy, cz, sx, sy, sz):
    return {"path": path,
            "aabb": [cx - sx / 2, cy - sy / 2, cz - sz / 2,
                     cx + sx / 2, cy + sy / 2, cz + sz / 2]}


def heightmap(path, size=20.0, h=0.0):
    n = int(size) + 1
    return {"path": path, "body": "TerrainBody", "body_class": "StaticBody3D",
            "kind": "heightmap", "origin": [0.0, 0.0, 0.0],
            "basis": [1, 0, 0, 0, 1, 0, 0, 0, 1],
            "map_width": n, "map_depth": n, "data": [h] * (n * n)}


def extract(solids, visuals, places=None):
    return {"ok": True, "scene": "res://test.tscn", "root": "Test",
            "solids": solids, "visuals": visuals, "multimeshes": [],
            "places": places or [],
            "counts": {"shapes": len(solids), "shapes_disabled": 0,
                       "areas": 0, "other_bodies": 0, "unsupported": []}}


def wall_pair(prefix, z, gap=None, width=10):
    """A wall along x at height 3, thickness 0.4, spanning `width`
    metres, with a matching visual; optionally with a collision gap
    of `gap` metres left open in the middle (the visual stays whole)."""
    solids, visuals = [], []
    half = width / 2
    if gap is None:
        solids.append(box(f"{prefix}/WallBody/Wall", 0, 1.5, z, width, 3, 0.4))
    else:
        w = (width - gap) / 2
        solids.append(box(f"{prefix}/WallBody/WallL", -half + w / 2, 1.5, z, w, 3, 0.4))
        solids.append(box(f"{prefix}/WallBody/WallR", half - w / 2, 1.5, z, w, 3, 0.4))
    visuals.append(visual(f"{prefix}/WallMesh", 0, 1.5, z, width, 3, 0.4))
    return solids, visuals


def room(door_gap=None):
    """A 10x10 room on a flat 20x20 field, walls with matching visuals;
    the south wall (z=+5) optionally leaves a doorway."""
    solids = [heightmap("Field/TerrainBody/Shape")]
    visuals = [visual("Field/TerrainMesh", 0, -0.05, 0, 20, 0.1, 20)]
    for z, name in ((-5, "North"), (5, "South")):
        if z == 5 and door_gap:
            w = (10 - door_gap) / 2
            solids.append(box(f"Room/WallBody/{name}L", -5 + w / 2, 1.5, z, w, 3, 0.4))
            solids.append(box(f"Room/WallBody/{name}R", 5 - w / 2, 1.5, z, w, 3, 0.4))
            visuals.append(visual(f"Room/{name}LMesh", -5 + w / 2, 1.5, z, w, 3, 0.4))
            visuals.append(visual(f"Room/{name}RMesh", 5 - w / 2, 1.5, z, w, 3, 0.4))
        else:
            solids.append(box(f"Room/WallBody/{name}", 0, 1.5, z, 10.4, 3, 0.4))
            visuals.append(visual(f"Room/{name}Mesh", 0, 1.5, z, 10.4, 3, 0.4))
    for x, name in ((-5, "West"), (5, "East")):
        solids.append(box(f"Room/WallBody/{name}", x, 1.5, 0, 0.4, 3, 9.6))
        visuals.append(visual(f"Room/{name}Mesh", x, 1.5, 0, 0.4, 3, 9.6))
    places = [{"id": "Room__spawn", "holder": "Room", "position": [0, 0, 0]},
              {"id": "Room__shrine", "holder": "Room", "position": [3, 0, -3]}]
    return extract(solids, visuals, places)


def test_clean_room():
    report, _ = cc.analyze(room(door_gap=2.0))
    check("clean room is ok", report["ok"], json.dumps(report["findings"])[:400])
    check("clean room has no error or warning findings",
          all(f["severity"] == "info" for f in report["findings"]),
          json.dumps(report["findings"])[:400])
    check("clean room places reached",
          all(p["reached_from"] for p in report["places"]))


def test_shifted_collision():
    solids = [heightmap("Field/TerrainBody/Shape"),
              box("House/Body/Wall", 1.5, 1.5, 0, 4, 3, 4)]  # shifted +1.5 in x
    visuals = [visual("Field/TerrainMesh", 0, -0.05, 0, 20, 0.1, 20),
               visual("House/Mesh", 0, 1.5, 0, 4, 3, 4)]
    places = [{"id": "Field__spawn", "holder": "Field", "position": [-8, 0, 8]}]
    report, _ = cc.analyze(extract(solids, visuals, places))
    ks = kinds(report)
    check("shifted collision: visual_uncovered", "visual_uncovered" in ks, str(ks))
    check("shifted collision: collision_overhang", "collision_overhang" in ks, str(ks))
    f = next(f for f in report["findings"] if f["kind"] == "visual_uncovered")
    check("shifted collision: finding names the mesh",
          f["objects"] == ["House/Mesh"], str(f["objects"]))
    check("shifted collision: coverage is partial", 0.4 < f["evidence"]["coverage"] < 0.8,
          str(f["evidence"]))


def test_wall_leak():
    solids = [heightmap("Field/TerrainBody/Shape")]
    visuals = [visual("Field/TerrainMesh", 0, -0.05, 0, 20, 0.1, 20)]
    s, v = wall_pair("Divider", 0, gap=2.0, width=20)
    solids += s
    visuals += v
    places = [{"id": "Field__spawn", "holder": "Field", "position": [0, 0, -4]},
              {"id": "Field__far", "holder": "Field", "position": [0, 0, 4]}]
    report, _ = cc.analyze(extract(solids, visuals, places))
    ks = kinds(report)
    check("wall leak detected", "wall_leak" in ks, str(ks))
    check("wall leak fails the check", not report["ok"])
    far = next(p for p in report["places"] if p["id"] == "Field__far")
    check("leak is physically passable (place reached)", bool(far["reached_from"]))


def test_plugged_gap():
    base = room(door_gap=2.0)
    solids = base["solids"] + [box("Plug/Body/Plug", 0, 1.5, 5, 2.0, 3, 0.4)]
    places = base["places"] + [{"id": "Field__outside", "holder": "Field",
                                "position": [0, 0, 8]}]
    report, _ = cc.analyze(extract(solids, base["visuals"], places))
    ks = kinds(report)
    outside = next(p for p in report["places"] if p["id"] == "Field__outside")
    check("plugged doorway: outside place unreachable", not outside["reached_from"])
    check("plugged doorway: place_unreachable", "place_unreachable" in ks, str(ks))
    check("plugged doorway: plugged_gap or phantom reported",
          "plugged_gap" in ks or "phantom_collision" in ks, str(ks))


def test_step_block():
    solids = [box("Low/Body/Floor", -2.5, -0.25, 0, 5, 0.5, 10),
              box("High/Body/Floor", 2.5, 0.0, 0, 5, 1.0, 10)]
    visuals = [visual("Low/Mesh", -2.5, -0.25, 0, 5, 0.5, 10),
               visual("High/Mesh", 2.5, 0.0, 0, 5, 1.0, 10)]
    places = [{"id": "Low__spawn", "holder": "Low", "position": [-2.5, 0, 0]},
              {"id": "High__spawn", "holder": "High", "position": [2.5, 0.5, 0]}]
    report, _ = cc.analyze(extract(solids, visuals, places))
    ks = kinds(report)
    check("step block: starts_disconnected", "starts_disconnected" in ks, str(ks))
    f = next(f for f in report["findings"] if f["kind"] == "starts_disconnected")
    check("step block: required rise is the slab step",
          abs(f["evidence"]["required_rise_m"] - 0.5) < 0.01, str(f["evidence"]))
    check("step block: bottleneck at the boundary",
          abs(f["evidence"]["bottleneck"][0]) < 1.0, str(f["evidence"]))


def test_overlap():
    solids = [heightmap("Field/TerrainBody/Shape"),
              box("A/BodyA/Shape", 0, 1.0, 0, 2, 2, 2),
              box("B/BodyB/Shape", 0.5, 1.0, 0, 2, 2, 2),
              box("A/BodyA/Shape2", -4, 1.0, 0, 2, 2, 2),
              box("A/BodyA/Shape3", -3.5, 1.0, 0, 2, 2, 2)]  # same body: compound, fine
    visuals = [visual("Field/TerrainMesh", 0, -0.05, 0, 20, 0.1, 20),
               visual("A/Mesh", 0, 1.0, 0, 2, 2, 2), visual("B/Mesh", 0.5, 1.0, 0, 2, 2, 2),
               visual("A/Mesh2", -4, 1.0, 0, 2, 2, 2), visual("A/Mesh3", -3.5, 1.0, 0, 2, 2, 2)]
    places = [{"id": "Field__spawn", "holder": "Field", "position": [8, 0, 8]}]
    report, _ = cc.analyze(extract(solids, visuals, places))
    overlaps = [f for f in report["findings"] if f["kind"] == "collision_overlap"]
    check("overlap: exactly one pair (different bodies)", len(overlaps) == 1,
          str(kinds(report)))
    if overlaps:
        check("overlap: pair named",
              set(overlaps[0]["objects"]) == {"A/BodyA/Shape", "B/BodyB/Shape"},
              str(overlaps[0]["objects"]))


def test_pocket():
    base = room(door_gap=None)  # sealed room, start inside via Room__spawn
    base["places"] = [{"id": "Field__spawn", "holder": "Field", "position": [8, 0, 8]}]
    report, _ = cc.analyze(base)
    ks = kinds(report)
    check("sealed room interior is a pocket", "pocket" in ks, str(ks))


def test_heatmap_png():
    report, raster = cc.analyze(room(door_gap=2.0))
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "map.png")
        info = cc.write_heatmap(raster, path)
        with open(path, "rb") as f:
            data = f.read()
    check("heatmap is a PNG", data[:8] == b"\x89PNG\r\n\x1a\n")
    w, h = struct.unpack(">II", data[16:24])
    check("heatmap dimensions match the raster scale",
          (w, h) == (info["width"], info["height"]), f"{w}x{h} vs {info}")


def test_village_reports():
    """Invariants over the committed village reports (the sample check
    regenerates them; see ai_loop/samples/hikari/collision_check.py)."""
    spec = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "samples", "hikari", "spec_set")
    baseline = os.path.join(spec, "collision_report.json")
    mutated = os.path.join(spec, "collision_report_shifted.json")
    if not os.path.isfile(baseline):
        print("skip village invariants (no committed report)")
        return
    with open(baseline, encoding="utf-8") as f:
        report = json.load(f)
    check("village: format", report["format"] == "uzu-collision-check/1")
    # The slab entry block (B-1510's failed Land -> city leg): the two
    # starts ARE connected around the west rim, where the terrain rises
    # to within the step of the slab top, so the honest static signal
    # is the blocked DIRECT approach (or, if the geometry changes so
    # even the rim closes, a starts_disconnected bottleneck).
    direct = [f for f in report["findings"] if f["kind"] == "direct_route_blocked"]
    dis = [f for f in report["findings"] if f["kind"] == "starts_disconnected"]
    check("village: the slab entry block is detected",
          len(direct) + len(dis) >= 1, str(kinds(report)))
    if direct:
        ev = direct[0]["evidence"]
        check("village: the direct approach is walled by an over-step ledge",
              (ev.get("required_rise_m") or 0) > report["params"]["step"], str(ev))
        bx, bz = direct[0]["position"][0], direct[0]["position"][2]
        check("village: the block is at the foundation slab edge",
              -48.5 <= bx <= 6.5 and -29.5 <= bz <= -27.0, str(direct[0]["position"]))
    if dis:
        ev = dis[0]["evidence"]
        check("village: bottleneck rise exceeds the step",
              ev.get("required_rise_m", 0) > report["params"]["step"], str(ev))
        bx, bz = ev["bottleneck"][0], ev["bottleneck"][2]
        check("village: bottleneck is at the foundation slab edge",
              -48.5 <= bx <= 6.5 and -29.5 <= bz <= 25.5, str(ev))
    check("village: no wall leaks in the known-good layout",
          "wall_leak" not in kinds(report), str(kinds(report)))
    uncovered = [f for f in report["findings"]
                 if f["kind"] == "visual_uncovered" and f["severity"] == "error"]
    check("village: no building-scale visual left uncovered", uncovered == [],
          json.dumps(uncovered)[:300])
    if os.path.isfile(mutated):
        with open(mutated, encoding="utf-8") as f:
            mut = json.load(f)
        near = [f for f in mut["findings"]
                if f["kind"] in ("visual_uncovered", "collision_overhang",
                                 "wall_leak", "visual_without_collision")
                and f["position"] and abs(f["position"][0] - mut["shifted_from"][0]) < 6
                and abs(f["position"][2] - mut["shifted_from"][2]) < 6]
        check("village shifted: the moved collision is pointed at", len(near) >= 1,
              str(kinds(mut)))


def main():
    test_clean_room()
    test_shifted_collision()
    test_wall_leak()
    test_plugged_gap()
    test_step_block()
    test_overlap()
    test_pocket()
    test_heatmap_png()
    test_village_reports()
    print(f"\n{len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
