import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LOOP))

import rock_recipe  # noqa: E402

ROCK = {"name": "r1", "seed": 5, "radius": 1.2, "cuts": 7}


class DeriveTest(unittest.TestCase):
    def test_deterministic(self):
        a = rock_recipe.derive_rock(dict(ROCK), 0)
        b = rock_recipe.derive_rock(dict(ROCK), 0)
        self.assertEqual(a, b)

    def test_seed_changes_the_rock(self):
        a = rock_recipe.derive_rock(dict(ROCK), 0)
        b = rock_recipe.derive_rock(dict(ROCK, seed=6), 0)
        self.assertNotEqual(a["cut_planes"], b["cut_planes"])

    def test_ground_cut_comes_first_and_planes_add_up(self):
        r = rock_recipe.derive_rock(dict(ROCK), 0)
        self.assertEqual(len(r["cut_planes"]), ROCK["cuts"] + 1)
        ground = r["cut_planes"][0]
        self.assertEqual(ground["normal"], [0.0, 0.0, -1.0])
        self.assertGreater(ground["distance"], 0.0)
        self.assertLess(ground["distance"], r["radius"])
        for plane in r["cut_planes"]:
            n = plane["normal"]
            self.assertAlmostEqual(sum(c * c for c in n), 1.0, places=3)

    def test_validation(self):
        for bad in (dict(ROCK, radius=0.05), dict(ROCK, radius=30.0),
                    dict(ROCK, cuts=-1), dict(ROCK, cuts=25), dict(ROCK, cuts=2.5),
                    dict(ROCK, bevel=0.5), dict(ROCK, seed="x"),
                    dict(ROCK, name="bad name")):
            with self.assertRaises(ValueError, msg=str(bad)):
                rock_recipe.derive_rock(bad, 0)

    def test_spec_level_validation(self):
        with self.assertRaises(ValueError):
            rock_recipe.derive_spec({})
        with self.assertRaises(ValueError):
            rock_recipe.derive_spec({"rocks": []})
        with self.assertRaises(ValueError):  # duplicate names
            rock_recipe.derive_spec({"rocks": [ROCK, ROCK]})


class CheckCliTest(unittest.TestCase):
    def run_cli(self, *argv):
        return subprocess.run([sys.executable, str(LOOP / "uzu.py"), *argv],
                              capture_output=True, text=True, timeout=120)

    def test_check_mode_via_uzu(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec = Path(tmp) / "spec.json"
            spec.write_text(json.dumps({"rocks": [ROCK]}), encoding="utf-8")
            proc = self.run_cli("rock", "--spec", str(spec), "--check")
            self.assertEqual(proc.returncode, 0, proc.stderr[-500:])
            result = json.loads(proc.stdout.strip().splitlines()[-1])
            self.assertTrue(result["ok"])
            self.assertEqual([r["name"] for r in result["rocks"]], ["r1"])


if __name__ == "__main__":
    unittest.main()
