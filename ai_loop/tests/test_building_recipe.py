import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LOOP))

import building_recipe  # noqa: E402

HOUSE = {"name": "h1", "kind": "house", "wall": "plaster", "roof": "gable",
         "width": 6.5, "depth": 5.5, "floors": 2, "seed": 11}
SHOP = {"name": "s1", "kind": "shop", "wall": "wood", "roof": "flat",
        "width": 7.0, "depth": 6.0, "floors": 2, "seed": 37}


class DeriveTest(unittest.TestCase):
    def test_deterministic(self):
        a = building_recipe.derive_building(dict(HOUSE), 0)
        b = building_recipe.derive_building(dict(HOUSE), 0)
        self.assertEqual(a, b)

    def test_seed_changes_the_building(self):
        a = building_recipe.derive_building(dict(HOUSE), 0)
        other = dict(HOUSE, seed=99)
        b = building_recipe.derive_building(other, 0)
        self.assertNotEqual(a, b)

    def test_validation(self):
        for bad in (dict(HOUSE, roof="dome"), dict(HOUSE, wall="glass"),
                    dict(HOUSE, kind="castle"), dict(HOUSE, width=2.0),
                    dict(HOUSE, width=30.0), dict(HOUSE, floors=0),
                    dict(HOUSE, floors=2.5), dict(HOUSE, seed="x"),
                    dict(HOUSE, name="bad name")):
            with self.assertRaises(ValueError, msg=str(bad)):
                building_recipe.derive_building(bad, 0)

    def test_spec_level_validation(self):
        with self.assertRaises(ValueError):
            building_recipe.derive_spec({})
        with self.assertRaises(ValueError):
            building_recipe.derive_spec({"buildings": []})
        with self.assertRaises(ValueError):  # duplicate names
            building_recipe.derive_spec({"buildings": [HOUSE, HOUSE]})

    def test_openings_stay_inside_the_walls(self):
        for raw in (HOUSE, SHOP):
            b = building_recipe.derive_building(dict(raw), 0)
            top = b["foundation_height"] + b["floors"] * b["floor_height"]
            for wall, length in (("front", b["width"]), ("back", b["width"]),
                                 ("left", b["depth"]), ("right", b["depth"])):
                for o in b["openings"][wall]:
                    self.assertLessEqual(abs(o["x"]) + o["w"] / 2, length / 2 + 1e-6,
                                         f"{wall} {o}")
                    self.assertGreaterEqual(o["z"], b["foundation_height"] - 1e-6)
                    self.assertLessEqual(o["z"] + o["h"], top + 1e-6, f"{wall} {o}")

    def test_house_has_a_front_door_and_no_ground_window_on_it(self):
        b = building_recipe.derive_building(dict(HOUSE), 0)
        doors = [o for o in b["openings"]["front"] if o["type"] == "door"]
        self.assertEqual(len(doors), 1)
        door = doors[0]
        for o in b["openings"]["front"]:
            if o["type"] == "window" and o["z"] < b["foundation_height"] + b["floor_height"]:
                self.assertGreaterEqual(abs(o["x"] - door["x"]), 0.95)

    def test_shop_has_a_storefront(self):
        b = building_recipe.derive_building(dict(SHOP), 0)
        glass = [o for o in b["openings"]["front"] if o["type"] == "shop_glass"]
        self.assertEqual(len(glass), 1)
        self.assertGreater(glass[0]["w"], 2.5)
        self.assertEqual(b["roof"], "flat")
        self.assertFalse(b["chimney"])


class StyleTest(unittest.TestCase):
    """B-1219 stage 2: styles are data (ai_loop/building_styles.json)."""

    def test_default_style_reproduces_stage1_constants(self):
        b = building_recipe.derive_building(dict(HOUSE), 0)
        self.assertEqual(b["style_name"], "default")
        self.assertEqual(b["floor_height"], 2.8)
        self.assertEqual(b["foundation_height"], 0.55)
        self.assertEqual(b["wall_thickness"], 0.18)
        self.assertEqual(b["eaves"], 0.45)
        win = [o for o in b["openings"]["back"] if o["type"] == "window"][0]
        self.assertEqual((win["w"], win["h"]), (0.9, 1.3))
        geo = b["style"]["geometry"]
        self.assertEqual(geo["tints"]["wall"]["plaster"], [0.93, 0.89, 0.82])

    def test_a_style_is_a_data_only_change(self):
        plain = building_recipe.derive_building(dict(HOUSE), 0)
        alpine = building_recipe.derive_building(dict(HOUSE, style="alpine"), 0)
        self.assertEqual(alpine["style_name"], "alpine")
        # steeper roof, deeper eaves, taller floors, smaller windows
        self.assertGreater(alpine["roof_rise"], plain["roof_rise"])
        self.assertEqual(alpine["eaves"], 0.7)
        self.assertEqual(alpine["floor_height"], 3.0)
        win = [o for o in alpine["openings"]["back"] if o["type"] == "window"][0]
        self.assertEqual((win["w"], win["h"]), (0.8, 1.1))
        # untouched keys fall back to the default style (deep merge)
        self.assertEqual(alpine["style"]["geometry"]["wall_thickness"], 0.18)
        self.assertEqual(alpine["style"]["materials"]["wood"]["board_scale"], 3.4)
        self.assertEqual(alpine["style"]["materials"]["plaster"]["roughness"], 0.92)

    def test_unknown_style_and_unknown_key_are_errors(self):
        with self.assertRaises(ValueError):
            building_recipe.derive_building(dict(HOUSE, style="baroque"), 0)
        doc = building_recipe.load_styles()
        bad = {"styles": {"default": doc["styles"]["default"],
                          "x": {"geometry": {"bogus": 1}}}}
        with self.assertRaises(ValueError):
            building_recipe.resolve_style(bad, "x")
        with self.assertRaises(ValueError):
            building_recipe.resolve_style({"styles": {}}, "default")

    def test_styles_file_is_valid_and_complete(self):
        doc = building_recipe.load_styles()
        self.assertIn("default", doc["styles"])
        resolved = building_recipe.resolve_style(doc, "default")
        for section in ("geometry", "materials"):
            self.assertIn(section, resolved)
        for key in ("floor_height", "foundation_height", "wall_thickness",
                    "window", "door", "roof", "tints", "roof_tiles"):
            self.assertIn(key, resolved["geometry"])


class SampleStatsTest(unittest.TestCase):
    """The committed sample street (regenerated in stage 2) carries the
    model_quality block on the engine report's scale."""

    def test_sample_stats_model_quality_keys(self):
        stats = json.loads((LOOP / "look_samples" / "b1219" / "stats.json")
                           .read_text(encoding="utf-8"))
        self.assertEqual(len(stats["buildings"]), 5)
        for b in stats["buildings"]:
            mq = b["model_quality"]
            for key in ("triangles", "chamfered_edges", "sharp_edges",
                        "bevel_share", "triangles_per_m2", "pbr_coverage", "size_m"):
                self.assertIn(key, mq, f"{b['name']}.{key}")
            self.assertEqual(mq["pbr_coverage"], 1.0)
            self.assertIn("orm_texture", b)
            self.assertIn("interior_culling", b)


class CheckCliTest(unittest.TestCase):
    def run_cli(self, *argv):
        return subprocess.run([sys.executable, str(LOOP / "uzu.py"), *argv],
                              capture_output=True, text=True, timeout=120)

    def test_check_mode_via_uzu(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec = Path(tmp) / "spec.json"
            spec.write_text(json.dumps({"buildings": [HOUSE, SHOP]}), encoding="utf-8")
            proc = self.run_cli("building", "--spec", str(spec), "--check")
            self.assertEqual(proc.returncode, 0, proc.stderr[-500:])
            result = json.loads(proc.stdout.strip().splitlines()[-1])
            self.assertTrue(result["ok"])
            self.assertEqual([b["name"] for b in result["buildings"]], ["h1", "s1"])

    def test_check_mode_rejects_a_bad_spec(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec = Path(tmp) / "spec.json"
            spec.write_text(json.dumps({"buildings": [dict(HOUSE, roof="dome")]}), encoding="utf-8")
            proc = self.run_cli("building", "--spec", str(spec), "--check")
            self.assertEqual(proc.returncode, 1)
            result = json.loads(proc.stdout.strip().splitlines()[-1])
            self.assertFalse(result["ok"])
            self.assertIn("roof", result["error"])


if __name__ == "__main__":
    unittest.main()
