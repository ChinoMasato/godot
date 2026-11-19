"""B-1511: re-read audit of a saved scene (ai_loop/audit_scene.py,
uzu.py audit-scene, MCP scene_audit).

Pins the judgement of the BACKLOG item mechanically, on the parts that
do not need the engine:
  * the text .tscn parser counts MultiMesh instances as stored -- from
    the instance_count property when the file carries one, else from
    the buffer (floats / stride, colors and custom data widen it) --
    and refuses binary scenes (None, so the caller falls back to the
    engine's loaded counts),
  * the comparison matches a faithful artifact, and removing one part
    (a window-frame MultiMesh) flips exactly its checks: the family
    count and the mesh triangle total, by the exact amounts,
  * families the artifact cannot express come back not_countable, and
    a MultiMesh stored in the file but dropped on load is a
    load_fidelity difference,
  * the saved hikari sample reports (spec_set/scene_audit_report.json
    and scene_audit_report_removed.json, produced by audit_check.py)
    hold the documented baseline: everything matches except the pond
    terrain the game cut, the generator's undercounted city triangle
    total, and the four nature MultiMeshes the loader drops; the
    removed run adds exactly the removed part's differences.
The recount itself is exercised end-to-end by audit_check.py with the
real engine; its outputs are what the last group checks.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LOOP))
import audit_scene  # noqa: E402

SAMPLE = LOOP / "samples" / "hikari" / "spec_set"

TSCN = """[gd_scene load_steps=4 format=3]

[sub_resource type="MultiMesh" id="MultiMesh_a"]
transform_format = 1
instance_count = 3
mesh = SubResource("ArrayMesh_a")
buffer = PackedFloat32Array(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

[sub_resource type="MultiMesh" id="MultiMesh_b"]
transform_format = 1
use_colors = true
buffer = PackedFloat32Array(%s)

[node name="Root" type="Node3D"]

[node name="Frames" type="MultiMeshInstance3D" parent="City"]
multimesh = SubResource("MultiMesh_a")

[node name="Trees" type="MultiMeshInstance3D" parent="."]
multimesh = SubResource("MultiMesh_b")
""" % ", ".join(["0"] * 32)  # 2 instances x (12 transform + 4 color) floats


def write_tscn(text):
    f = tempfile.NamedTemporaryFile("w", suffix=".tscn", delete=False, encoding="utf-8")
    f.write(text)
    f.close()
    return f.name


def counts(**kw):
    base = {"nodes": 0, "mesh_instances": 0, "multimesh_instances": 0, "multimesh_placements": 0,
            "scene_instances": 0, "triangles": 0, "mesh_triangles": 0, "multimesh_triangles": 0,
            "collision_shapes": 0, "collision_shapes_disabled": 0, "collision_bodies": 0,
            "lights": 0, "navigation_regions": 0, "mesh_nodes": [], "multimesh_nodes": [],
            "collision_shape_names": [], "navigation": [], "scene_instance_paths": []}
    base.update(kw)
    return base


def village_recount():
    """A synthetic recount with the shape of the generated village."""
    city = counts(
        nodes=45, mesh_triangles=100,
        multimesh_nodes=[{"path": "Village/WindowFrames_0_0", "name": "WindowFrames_0_0",
                          "instances": 2, "per_instance_triangles": 10},
                         {"path": "Village/StreetTrees", "name": "StreetTrees",
                          "instances": 3, "per_instance_triangles": 7}],
        collision_shape_names=["Building0", "Bench0", "Block_0_0"], lights=1,
        navigation=[{"path": "Village/Navigation", "polygons": 10, "vertices": 8}])
    return {"ok": True, "scene": "res://village.tscn", "totals": counts(nodes=46),
            "parts": {"Village": city}, "textures": []}


def village_spec_set():
    return {"scene": "res://village.tscn",
            "registry": [
                {"family": "buildings", "count": 1, "count_basis": "observed", "part": "Village"},
                {"family": "benches", "count": 1, "count_basis": "observed", "part": "Village"},
                {"family": "blocks", "count": 1, "count_basis": "observed", "part": "Village"},
                {"family": "street_trees", "count": 3, "count_basis": "observed", "part": "Village"},
                {"family": "windows", "count": 2, "count_basis": "observed", "part": "Village"},
                {"family": "lights", "count": 1, "count_basis": "observed", "part": "Village"},
                {"family": "lamps", "count": 1, "count_basis": "observed", "part": "Village"},
                {"family": "meshes", "count": 120, "measure": "triangles", "count_basis": "observed", "part": "Village"},
            ],
            "observed": {"Village": {"navigation.polygons": 10, "navigation.vertices": 8}}}


def by_id(report):
    return {c["id"]: c for c in report["checks"]}


class TscnParserTest(unittest.TestCase):
    def test_stored_counts_from_property_and_buffer(self):
        stored = audit_scene.parse_tscn_multimeshes(write_tscn(TSCN))
        self.assertEqual(stored["City/Frames"]["stored"], 3)
        self.assertEqual(stored["City/Frames"]["instance_count_prop"], 3)
        self.assertEqual(stored["Trees"]["stored"], 2)  # buffer only: 32 floats / stride 16
        self.assertIsNone(stored["Trees"]["instance_count_prop"])
        self.assertEqual(stored["Trees"]["buffer_instances"], 2)

    def test_binary_scene_is_not_parsed(self):
        path = write_tscn("RSCC....binary")
        self.assertIsNone(audit_scene.parse_tscn_multimeshes(path))

    def test_missing_file_is_none(self):
        self.assertIsNone(audit_scene.parse_tscn_multimeshes("/nonexistent/x.tscn"))


class ComparisonTest(unittest.TestCase):
    def test_faithful_artifact_matches(self):
        report = audit_scene.build_report("res://village.tscn", village_recount(), {}, village_spec_set())
        checks = by_id(report)
        for cid in ("Village/buildings", "Village/benches", "Village/blocks", "Village/street_trees",
                    "Village/windows", "Village/lights", "Village/meshes",
                    "Village/navigation.polygons", "Village/navigation.vertices"):
            self.assertEqual(checks[cid]["status"], "match", cid)
        self.assertEqual(checks["Village/lamps"]["status"], "not_countable")
        self.assertTrue(report["ok"])

    def test_removed_part_flips_exactly_its_checks(self):
        recount = village_recount()
        recount["parts"]["Village"]["multimesh_nodes"] = [
            n for n in recount["parts"]["Village"]["multimesh_nodes"] if n["name"] != "WindowFrames_0_0"]
        report = audit_scene.build_report("res://village.tscn", recount, {}, village_spec_set())
        checks = by_id(report)
        self.assertEqual(checks["Village/windows"]["status"], "difference")
        self.assertEqual((checks["Village/windows"]["expected"], checks["Village/windows"]["actual"]), (2, 0))
        self.assertEqual(checks["Village/meshes"]["status"], "difference")
        self.assertEqual(checks["Village/meshes"]["actual"], 120 - 2 * 10)
        self.assertEqual(checks["Village/buildings"]["status"], "match")
        self.assertFalse(report["ok"])

    def test_load_fidelity_difference(self):
        recount = village_recount()
        stored = {"Village/StreetTrees": {"stored": 5, "instance_count_prop": None, "buffer_instances": 5}}
        report = audit_scene.build_report("res://village.tscn", recount, stored, village_spec_set())
        checks = by_id(report)
        fid = checks["Village/load_fidelity/StreetTrees"]
        self.assertEqual(fid["status"], "difference")
        self.assertEqual((fid["expected"], fid["actual"]), (5, 3))
        # The registry comparison itself uses the stored count.
        self.assertEqual(checks["Village/street_trees"]["actual"], 5)
        self.assertEqual(checks["Village/street_trees"]["status"], "difference")

    def test_zero_family_matches_by_absence(self):
        spec = village_spec_set()
        spec["registry"].append({"family": "roads", "count": 0, "count_basis": "observed", "part": "Village"})
        report = audit_scene.build_report("res://village.tscn", village_recount(), {}, spec)
        self.assertEqual(by_id(report)["Village/roads"]["status"], "match")


class SampleReportsTest(unittest.TestCase):
    def load(self, name):
        with open(SAMPLE / name, encoding="utf-8") as f:
            return json.load(f)

    def test_baseline_differences_are_the_documented_ones(self):
        report = self.load("scene_audit_report.json")
        diffs = {c["id"] for c in report["checks"] if c["status"] == "difference"}
        self.assertEqual(diffs, {"Pond/terrain_mesh", "Village/meshes",
                                 "Land/load_fidelity/TreesBroadleaf", "Land/load_fidelity/TreesConifer",
                                 "Land/load_fidelity/Rocks", "Land/load_fidelity/Grass"})
        checks = by_id(report)
        self.assertEqual(checks["Village/buildings"]["actual"], 6)
        self.assertEqual(checks["Village/windows"]["actual"], 37)
        self.assertEqual(checks["Land/terrain_mesh"]["actual"], 8192)
        self.assertEqual(report["measured"]["texture_count"], 12)
        self.assertEqual(report["measured"]["collision"]["shapes"], 22)

    def test_removed_report_adds_exactly_the_removed_part(self):
        base = by_id(self.load("scene_audit_report.json"))
        after = by_id(self.load("scene_audit_report_removed.json"))
        self.assertEqual(set(base), set(after))
        flipped = {cid for cid in base if base[cid]["status"] != after[cid]["status"]}
        self.assertEqual(flipped, {"Village/windows"})
        changed = {cid for cid in base
                   if (base[cid]["expected"], base[cid]["actual"]) != (after[cid]["expected"], after[cid]["actual"])}
        self.assertEqual(changed, {"Village/windows", "Village/meshes"})
        self.assertEqual((after["Village/windows"]["expected"], after["Village/windows"]["actual"]), (37, 25))
        self.assertEqual(after["Village/meshes"]["actual"], base["Village/meshes"]["actual"] - 12 * 224)


if __name__ == "__main__":
    unittest.main()
