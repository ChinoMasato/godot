"""B-1509: the generation spec set (ai_loop/spec_set.py).

Pins the judgement of the BACKLOG item mechanically:
  * a generation yields all four documents,
  * scene_spec distinguishes observed (generator-measured, with the result
    field it came from) from inferred (filled in / presumed, with a reason),
  * every validation_report check carries at least one evidence reference
    and one of the three statuses,
  * region_graph edges are bidirectional, join reported nodes, and form a
    spanning tree (what B-1510 will walk),
for synthetic map/build records, for the module CLI, and for the saved
hikari sample set (ai_loop/samples/hikari/spec_set/).
"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LOOP))
import spec_set  # noqa: E402

SAMPLE = LOOP / "samples" / "hikari" / "spec_set"
STATUSES = {"Verified", "Partial", "Unverified"}


def city_record():
    return {
        "kind": "map",
        "generator": "UZUWorldGen.generate_city (ai_loop/map_tool.gd)",
        "scene": "res://main.tscn",
        "spec_source": "inline",
        "input_spec": {"preset": "village", "seed": 3},
        "result": {
            "ok": True, "kind": "city", "preset": "village", "seed": 3, "size": 120.0,
            "options": {"preset": "village", "seed": 3, "block": 16.0, "detail": "high"},
            "counts": {"buildings": 6, "benches": 4, "lamps": 9},
            "places": {
                "spawn": {"position": [0, 0, 0], "about": "a crossing"},
                "plaza": {"position": [10, 0, 0], "about": "the square"},
                "gate_east": {"position": [40, 0, 0], "about": "the east gate"},
            },
            "navigation": {"polygons": 12, "vertices": 300, "islands": 1,
                           "places_connected": True, "unreached": []},
            "triangles": 5000, "generate_ms": 42,
        },
    }


def nature_record():
    return {
        "kind": "map",
        "generator": "UZUWorldGen.generate_nature (ai_loop/map_tool.gd)",
        "scene": "res://main.tscn",
        "spec_source": None,
        "input_spec": {"seed": 5},
        "result": {
            "ok": True, "kind": "nature", "preset": "plains", "seed": 5, "size": 256.0,
            "options": {"seed": 5, "preset": "plains", "trees": 2.0},
            "counts": {"trees": 100, "villages": 2, "roads": 3},
            "places": {"spawn": {"position": [0, 1, 0], "about": "flat ground"},
                       "villages": [{"position": [50, 1, 0], "about": "a village"}]},
            "roads_connected": False, "terrain_triangles": 8192,
        },
    }


def build_record():
    return {
        "kind": "build",
        "generator": "ai_loop/build_tool.gd (B-1207)",
        "scene": "res://village.tscn",
        "spec_source": "spec.json",
        "input_spec": {
            "scene": "res://village.tscn",
            "ground": {"size": [60, 60]},
            "models": [{"mesh": "box", "name": "Hut", "position": [1, 0, 2]}],
            "scatter": [{"mesh": "sphere", "seed": 7}],
        },
        "result": {"ok": True, "scene": "res://village.tscn",
                   "built": {"ground": {"size": [60, 60]}, "models": 1,
                             "scatter": [{"node": "Stones", "count": 40, "target": 100}]}},
        "report": {"ok": False, "summary": {"triangles": 10}, "failures": ["too dark"]},
    }


class SpecSetTest(unittest.TestCase):
    def assert_set_shape(self, bundle):
        self.assertEqual(set(bundle), {"scene_spec", "region_graph", "asset_registry", "validation_report"})
        for check in bundle["validation_report"]["checks"]:
            self.assertIn(check["status"], STATUSES, check)
            self.assertTrue(check["evidence"], f"check {check['id']} has no evidence reference")
            for ev in check["evidence"]:
                self.assertIn(ev["type"], ("value", "file"))
                self.assertTrue(ev["ref"])
        summary = bundle["validation_report"]["summary"]
        self.assertEqual(sum(summary.values()), len(bundle["validation_report"]["checks"]))

    def assert_graph_shape(self, graph):
        ids = {n["id"] for n in graph["nodes"]}
        for edge in graph["edges"]:
            self.assertIn(edge["a"], ids)
            self.assertIn(edge["b"], ids)
            self.assertTrue(edge["bidirectional"])
            self.assertGreaterEqual(edge["length"], 0)
        if len(ids) > 1:
            self.assertEqual(len(graph["edges"]), len(ids) - 1, "edges must span the nodes (a tree)")

    def test_city_set(self):
        bundle = spec_set.build_spec_set(city_record())
        self.assert_set_shape(bundle)
        self.assert_graph_shape(bundle["region_graph"])
        spec = bundle["scene_spec"]
        origins = {p["name"]: p["origin"] for p in spec["parameters"]}
        self.assertEqual(origins["seed"], "given")
        self.assertEqual(origins["block"], "defaulted")
        self.assertTrue(spec["observed"])
        for entry in spec["observed"]:
            self.assertIn("evidence", entry)
            self.assertNotIn("reason", entry)
        self.assertTrue(spec["inferred"])
        for entry in spec["inferred"]:
            self.assertIn("reason", entry)
            self.assertNotIn("evidence", entry)
        inferred_names = {e["name"] for e in spec["inferred"]}
        self.assertIn("parameters.block", inferred_names)
        checks = {c["id"]: c for c in bundle["validation_report"]["checks"]}
        self.assertEqual((checks["places_connected"]["status"], checks["places_connected"]["holds"]),
                         ("Verified", True))
        self.assertEqual(checks["walk_reachability"]["status"], "Unverified")
        families = {f["family"]: f for f in bundle["asset_registry"]["families"]}
        self.assertEqual(families["buildings"]["count"], 6)
        self.assertEqual(families["buildings"]["origin"]["seed"], 3)

    def test_nature_measured_negative(self):
        bundle = spec_set.build_spec_set(nature_record())
        self.assert_set_shape(bundle)
        self.assert_graph_shape(bundle["region_graph"])
        checks = {c["id"]: c for c in bundle["validation_report"]["checks"]}
        # roads_connected=False was measured, so the check is Verified as
        # evidence and holds=False: the report never upgrades or hides it.
        self.assertEqual((checks["roads_connected"]["status"], checks["roads_connected"]["holds"]),
                         ("Verified", False))
        node_ids = {n["id"] for n in bundle["region_graph"]["nodes"]}
        self.assertIn("villages_0", node_ids)

    def test_build_set(self):
        bundle = spec_set.build_spec_set(build_record())
        self.assert_set_shape(bundle)
        self.assert_graph_shape(bundle["region_graph"])
        checks = {c["id"]: c for c in bundle["validation_report"]["checks"]}
        self.assertEqual((checks["richness_report"]["status"], checks["richness_report"]["holds"]),
                         ("Partial", False))
        self.assertEqual((checks["scatter_placed_0"]["status"], checks["scatter_placed_0"]["holds"]),
                         ("Partial", False))
        self.assertEqual((checks["models_placed"]["status"], checks["models_placed"]["holds"]),
                         ("Verified", True))

    def test_cli_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            record = Path(tmp) / "record.json"
            record.write_text(json.dumps(city_record()), encoding="utf-8")
            out = Path(tmp) / "set"
            proc = subprocess.run([sys.executable, LOOP / "spec_set.py", "--record", record, "--out", out],
                                  capture_output=True, encoding="utf-8", timeout=60)
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            for name in ("scene_spec", "region_graph", "asset_registry", "validation_report"):
                doc = json.loads((out / f"{name}.json").read_text(encoding="utf-8"))
                self.assertIn("format", doc)

    def test_hikari_sample(self):
        docs = {name: json.loads((SAMPLE / f"{name}.json").read_text(encoding="utf-8"))
                for name in ("scene_spec", "region_graph", "asset_registry", "validation_report")}
        self.assertEqual(docs["scene_spec"]["kind"], "composite")
        for part in docs["scene_spec"]["parts"]:
            self.assertTrue(part["observed"], part["part"])
            self.assertTrue(part["inferred"], part["part"])
            for entry in part["observed"]:
                self.assertIn("evidence", entry)
            for entry in part["inferred"]:
                self.assertIn("reason", entry)
        self.assert_set_shape({"scene_spec": docs["scene_spec"], "region_graph": docs["region_graph"],
                               "asset_registry": docs["asset_registry"],
                               "validation_report": docs["validation_report"]})
        ids = {n["id"] for n in docs["region_graph"]["nodes"]}
        self.assertIn("Village__spawn", ids)
        self.assertIn("Village__plaza", ids)
        for edge in docs["region_graph"]["edges"]:
            self.assertEqual(edge["a"].split("__")[0], edge["b"].split("__")[0],
                             "the sample asserts no cross-part edges")
        families = {(f.get("part"), f["family"]) for f in docs["asset_registry"]["families"]}
        self.assertIn(("Village", "buildings"), families)


if __name__ == "__main__":
    unittest.main()
