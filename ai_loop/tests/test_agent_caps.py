import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LOOP))

import agent_caps  # noqa: E402
import catalog  # noqa: E402
import make_catalog  # noqa: E402
import texture_route  # noqa: E402


def clean_env(**extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith("UZU_AGENT")}
    env.update(extra)
    return env


class ParseCapsTest(unittest.TestCase):
    def test_parse_a_list(self):
        self.assertEqual(agent_caps.parse_caps("image_gen,gpu"), {"image_gen", "gpu"})
        self.assertEqual(agent_caps.parse_caps(" image_gen , GPU "), {"image_gen", "gpu"})
        self.assertEqual(agent_caps.parse_caps(""), frozenset())
        self.assertEqual(agent_caps.parse_caps(None), frozenset())

    def test_unknown_capability_is_an_error(self):
        with self.assertRaises(agent_caps.CapsError) as caught:
            agent_caps.parse_caps("image_gen,teleport")
        self.assertIn("teleport", str(caught.exception))
        self.assertIn("image_gen", str(caught.exception))  # The known names are listed.


class ProfileFileTest(unittest.TestCase):
    def write_profile(self, text):
        tmp = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8")
        tmp.write(text)
        tmp.close()
        self.addCleanup(os.unlink, tmp.name)
        return tmp.name

    def test_object_and_bare_list_forms(self):
        path = self.write_profile('{"capabilities": ["gpu", "blender"]}')
        self.assertEqual(agent_caps.caps_from_file(path), {"gpu", "blender"})
        path = self.write_profile('["browser"]')
        self.assertEqual(agent_caps.caps_from_file(path), {"browser"})

    def test_bad_profiles_are_errors(self):
        with self.assertRaises(agent_caps.CapsError):
            agent_caps.caps_from_file(self.write_profile('{"capabilities": ["nope"]}'))
        with self.assertRaises(agent_caps.CapsError):
            agent_caps.caps_from_file(self.write_profile('{"other": []}'))
        with self.assertRaises(agent_caps.CapsError):
            agent_caps.caps_from_file("/nonexistent/caps.json")


class LoadProfileTest(unittest.TestCase):
    def test_nothing_declared(self):
        caps, sources = agent_caps.load_profile(environ={})
        self.assertEqual(caps, frozenset())
        self.assertEqual(sources, [])

    def test_env_and_argument_merge(self):
        caps, sources = agent_caps.load_profile("blender", environ={"UZU_AGENT_CAPS": "gpu"})
        self.assertEqual(caps, {"gpu", "blender"})
        self.assertEqual(sources, ["env:UZU_AGENT_CAPS", "argument"])

    def test_env_profile_file(self):
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
            f.write('{"capabilities": ["image_gen"]}')
            path = f.name
        self.addCleanup(os.unlink, path)
        caps, sources = agent_caps.load_profile(environ={"UZU_AGENT_PROFILE": path})
        self.assertEqual(caps, {"image_gen"})
        self.assertEqual(sources, [f"file:{path}"])

    def test_unknown_in_env_is_an_error(self):
        with self.assertRaises(agent_caps.CapsError):
            agent_caps.load_profile(environ={"UZU_AGENT_CAPS": "warp"})


class CatalogCapsTest(unittest.TestCase):
    """The committed catalog carries the capability declarations, and a
    profile annotates matches instead of hiding them."""

    @classmethod
    def setUpClass(cls):
        cls.committed = catalog.load_catalog()
        cls.fresh = make_catalog.build_catalog()

    def test_capability_fields_match_a_fresh_extraction(self):
        def fields(cat):
            return {(e["kind"], e.get("tool", ""), e["name"]): (tuple(e["required_capabilities"]), e["fallback"]) for e in cat["entries"]}

        self.assertEqual(fields(self.fresh), fields(self.committed))

    def test_every_entry_has_the_fields(self):
        for e in self.committed["entries"]:
            self.assertIsInstance(e["required_capabilities"], list, e["name"])
            self.assertTrue(set(e["required_capabilities"]) <= set(agent_caps.KNOWN_CAPABILITIES), e["name"])
            self.assertTrue(e["fallback"] is None or isinstance(e["fallback"], str), e["name"])

    def test_the_declared_tools(self):
        by_key = {(e["kind"], e["name"]): e for e in self.committed["entries"]}
        self.assertEqual(by_key[("script", "blender_bridge")]["required_capabilities"], ["blender"])
        self.assertEqual(by_key[("mcp", "frame_benchmark")]["required_capabilities"], ["gpu"])
        self.assertTrue(by_key[("cli", "texture-route")]["fallback"])

    def run_catalog(self, *words, env=None):
        process = subprocess.run(
            [sys.executable, str(LOOP / "uzu.py"), "catalog", *words],
            capture_output=True, text=True, check=False, env=env if env is not None else clean_env(),
        )
        return process.returncode, json.loads(process.stdout)

    def test_profile_annotates_missing_capabilities(self):
        rc, result = self.run_catalog("benchmark", "--caps", "image_gen")
        self.assertEqual(rc, 0)
        self.assertEqual(result["profile"], ["image_gen"])
        bench = next(m for m in result["matches"] if m["name"] == "frame_benchmark")
        self.assertEqual(bench["missing_capabilities"], ["gpu"])
        self.assertIn("headless", bench["fallback"])

    def test_profile_with_the_capability_annotates_nothing(self):
        rc, result = self.run_catalog("benchmark", "--caps", "gpu")
        self.assertEqual(rc, 0)
        bench = next(m for m in result["matches"] if m["name"] == "frame_benchmark")
        self.assertNotIn("missing_capabilities", bench)

    def test_no_profile_no_annotation(self):
        rc, result = self.run_catalog("benchmark")
        self.assertEqual(rc, 0)
        self.assertNotIn("profile", result)
        bench = next(m for m in result["matches"] if m["name"] == "frame_benchmark")
        self.assertNotIn("missing_capabilities", bench)

    def test_profile_from_the_environment(self):
        rc, result = self.run_catalog("blender", env=clean_env(UZU_AGENT_CAPS="gpu"))
        self.assertEqual(rc, 0)
        self.assertEqual(result["profile"], ["gpu"])
        bridge = next(m for m in result["matches"] if m["name"] == "blender_bridge")
        self.assertEqual(bridge["missing_capabilities"], ["blender"])

    def test_unknown_capability_is_an_error(self):
        rc, result = self.run_catalog("benchmark", "--caps", "teleport")
        self.assertEqual(rc, 2)
        self.assertFalse(result["ok"])
        self.assertIn("teleport", result["error"])


class TextureRouteTest(unittest.TestCase):
    def test_image_gen_without_an_image_asks_for_one(self):
        result = texture_route.route({"image_gen"})
        self.assertFalse(result["ok"])
        self.assertEqual(result["route"], "generated_image")
        self.assertEqual(result["needs_input"], "image")
        self.assertIn("image_gen", result["reason"])
        self.assertIn("procedural", result["fallback"])

    def test_image_gen_with_an_image(self):
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            path = f.name
        self.addCleanup(os.unlink, path)
        result = texture_route.route({"image_gen", "gpu"}, image=path)
        self.assertTrue(result["ok"])
        self.assertEqual(result["route"], "generated_image")
        self.assertEqual(result["image"], os.path.abspath(path))
        self.assertTrue(any("pbrmake" in step for step in result["next_steps"]))

    def test_image_gen_with_a_missing_image(self):
        result = texture_route.route({"image_gen"}, image="/nonexistent/tex.png")
        self.assertFalse(result["ok"])
        self.assertIn("not found", result["error"])

    def test_no_image_gen_uses_the_procedural_presets(self):
        result = texture_route.route(frozenset())
        self.assertTrue(result["ok"])
        self.assertEqual(result["route"], "procedural_preset")
        self.assertIn("image_gen", result["reason"])
        self.assertEqual(result["presets"], ["brick", "dirt", "grass", "plaster", "stone", "tile", "wood"])

    def test_no_image_gen_with_a_preset(self):
        result = texture_route.route({"gpu"}, preset="wood")
        self.assertTrue(result["ok"])
        self.assertEqual(result["preset"], "wood")
        self.assertTrue(any("look" in step and "pbr" in step for step in result["next_steps"]))

    def test_unknown_preset_is_an_error(self):
        result = texture_route.route(frozenset(), preset="marble")
        self.assertFalse(result["ok"])
        self.assertIn("wood", result["error"])

    def run_route(self, *args):
        process = subprocess.run(
            [sys.executable, str(LOOP / "uzu.py"), "texture-route", *args],
            capture_output=True, text=True, check=False, env=clean_env(),
        )
        return process.returncode, json.loads(process.stdout)

    def test_cli_both_routes(self):
        rc, result = self.run_route("--caps", "image_gen")
        self.assertEqual(rc, 1)
        self.assertEqual(result["route"], "generated_image")
        rc, result = self.run_route()
        self.assertEqual(rc, 0)
        self.assertEqual(result["route"], "procedural_preset")

    def test_cli_unknown_capability(self):
        rc, result = self.run_route("--caps", "teleport")
        self.assertEqual(rc, 2)
        self.assertFalse(result["ok"])


class CapsCliTest(unittest.TestCase):
    def test_caps_command(self):
        process = subprocess.run(
            [sys.executable, str(LOOP / "uzu.py"), "caps", "--caps", "gpu,image_gen"],
            capture_output=True, text=True, check=False, env=clean_env(),
        )
        self.assertEqual(process.returncode, 0)
        result = json.loads(process.stdout)
        self.assertEqual(result["capabilities"], ["gpu", "image_gen"])
        self.assertEqual(result["known_capabilities"], list(agent_caps.KNOWN_CAPABILITIES))


if __name__ == "__main__":
    unittest.main()
