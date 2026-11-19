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
