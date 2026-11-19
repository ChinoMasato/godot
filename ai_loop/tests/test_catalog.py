import json
import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
ROOT = LOOP.parent
sys.path.insert(0, str(LOOP))

import catalog  # noqa: E402
import make_catalog  # noqa: E402
import mcp_server  # noqa: E402


class CatalogConsistencyTest(unittest.TestCase):
    """The committed catalog must match the tools' definitions exactly."""

    @classmethod
    def setUpClass(cls):
        cls.committed = catalog.load_catalog()
        cls.entries = cls.committed["entries"]
        cls.fresh = make_catalog.build_catalog()

    def test_no_generation_gaps_or_stale_entries(self):
        # Every real command / tool / preset / script / class is in the
        # catalog, and nothing in the catalog is missing from a fresh
        # extraction (regenerate with ai_loop/make_catalog.py).
        self.assertEqual(make_catalog.entry_keys(self.fresh), make_catalog.entry_keys(self.committed))

    def test_counts_match_entries(self):
        counts = self.committed["counts"]
        self.assertEqual(counts["total"], len(self.entries))
        for kind in ("cli", "command", "mcp", "preset", "script", "class"):
            self.assertEqual(counts[kind], sum(1 for e in self.entries if e["kind"] == kind), kind)

    def test_every_entry_points_at_a_real_thing(self):
        dispatch = make_catalog._uzu_dispatch()
        for e in self.entries:
            source = ROOT / e["source"]
            self.assertTrue(source.is_file(), f"{e['kind']} {e['name']}: no such file {e['source']}")
            if e["kind"] == "mcp":
                self.assertIn(e["name"], mcp_server.TOOLS)
            elif e["kind"] == "cli":
                self.assertIn(e["name"], dispatch)
            elif e["kind"] == "class":
                self.assertEqual(ET.parse(source).getroot().get("name"), e["name"])
            elif e["kind"] == "preset":
                self.assertIn("_about", json.loads(source.read_text(encoding="utf-8")))

    def test_every_mcp_tool_is_searchable_by_its_name(self):
        for name in mcp_server.TOOLS:
            hits = catalog.search(self.entries, name)
            self.assertTrue(any(h["kind"] == "mcp" and h["name"] == name for h in hits), name)


class CatalogSearchCliTest(unittest.TestCase):
    def run_catalog(self, *words):
        process = subprocess.run(
            [sys.executable, str(LOOP / "uzu.py"), "catalog", *words],
            capture_output=True,
            text=True,
            check=False,
        )
        return process.returncode, json.loads(process.stdout)

    def test_real_features_are_found(self):
        rc, result = self.run_catalog("juice")
        self.assertEqual(rc, 0)
        self.assertTrue(result["ok"])
        self.assertIn("UZUJuice", [m["name"] for m in result["matches"]])

        rc, result = self.run_catalog("sfx")
        self.assertEqual(rc, 0)
        names = [(m["kind"], m["name"]) for m in result["matches"]]
        self.assertIn(("command", "sfx"), names)
        self.assertIn(("mcp", "sfx_make"), names)

        rc, result = self.run_catalog("catalog")
        self.assertEqual(rc, 0)
        names = [(m["kind"], m["name"]) for m in result["matches"]]
        self.assertIn(("cli", "catalog"), names)
        self.assertIn(("mcp", "catalog_search"), names)

    def test_nonexistent_feature_is_not_found(self):
        rc, result = self.run_catalog("no-such-feature-zzz")
        self.assertEqual(rc, 0)
        self.assertTrue(result["ok"])
        self.assertEqual(result["count"], 0)
        self.assertEqual(result["matches"], [])

    def test_multiword_search_is_and(self):
        rc, result = self.run_catalog("water", "surface")
        self.assertEqual(rc, 0)
        self.assertTrue(any(m["name"] == "look_water" for m in result["matches"]))
        rc, result = self.run_catalog("water", "no-such-feature-zzz")
        self.assertEqual(result["count"], 0)


if __name__ == "__main__":
    unittest.main()
