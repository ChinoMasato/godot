import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LOOP))

from uzu import find_godot  # noqa: E402


def save_png(path, width, height, left, top, visible_width, visible_height):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = []
    for y in range(height):
        row = bytearray(4 * width)
        if top <= y < top + visible_height:
            row[4 * left : 4 * (left + visible_width)] = bytes((32, 180, 72, 255)) * visible_width
        rows.append(b"\x00" + bytes(row))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(b"".join(rows)))
        + chunk(b"IEND", b"")
    )


class AnimationTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.project = Path(temporary.name)
        (self.project / "project.godot").write_text("config_version=5\n", encoding="utf-8")
        bones = [("Root", -1), ("Thigh.L", 0), ("Thigh.R", 0), ("Shin.L", 1), ("Shin.R", 2)]
        bones += [("UpperArm.L", 0), ("UpperArm.R", 0)]
        lines = ["[gd_scene format=3]", "", '[node name="TestRig" type="Skeleton3D"]']
        for index, (name, parent) in enumerate(bones):
            lines.extend([f'bones/{index}/name = "{name}"', f"bones/{index}/parent = {parent}"])
        (self.project / "rig.tscn").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def cli(self, *args):
        result = subprocess.run(
            [sys.executable, LOOP / "uzu.py", "anim", self.project, *args],
            capture_output=True,
            encoding="utf-8",
            timeout=40,
        )
        return result.returncode, json.loads(result.stdout)

    def test_motion_reloads_and_walks(self):
        status, report = self.cli("motion", "--model", "rig.tscn", "--out", "motion.tscn", "--speed", "1.25")
        self.assertEqual(status, 0, report)
        self.assertEqual(set(report["clips"]), {"idle", "walk", "run"})
        self.assertAlmostEqual(report["clips"]["walk"]["seconds"], 0.8)
        for clip in report["clips"].values():
            self.assertLessEqual(clip["rotation_seam_deg"], 0.01)
            self.assertEqual(clip["position_seam_m"], 0)
        result = subprocess.run(
            [
                find_godot(),
                "--headless",
                "--path",
                self.project,
                "--script",
                LOOP / "animation" / "check_motion.gd",
                "--",
                report["scene"],
            ],
            capture_output=True,
            encoding="utf-8",
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        (runtime,) = [
            json.loads(line[len("MOTION_CHECK ") :])
            for line in result.stdout.splitlines()
            if line.startswith("MOTION_CHECK ")
        ]
        self.assertTrue(runtime["ok"])
        self.assertGreater(runtime["walk_distance_m"], 1.0)
        self.assertEqual(runtime["walk_animation"], "walk")

    def test_invalid_bones_and_output_reuse(self):
        mapping = self.project / "map.json"
        mapping.write_text('{"left_thigh":"Thigh.R"}', encoding="utf-8")
        output = self.project / "motion.tscn"
        args = ["motion", "--model", "rig.tscn", "--out", output]
        status, result = self.cli(*args, "--bones", mapping)
        self.assertEqual(status, 1, result)
        self.assertIn("reused joint", result["error"])
        self.assertFalse(output.exists())
        output.write_text("existing scene", encoding="utf-8")
        status, result = self.cli(*args)
        self.assertEqual(status, 1, result)
        self.assertEqual(output.read_text(encoding="utf-8"), "existing scene")

    def test_align_frames_via_mcp(self):
        frames = self.project / "frames"
        frames.mkdir()
        save_png(frames / "01.png", 7, 7, 0, 3, 5, 3)
        save_png(frames / "02.png", 8, 7, 5, 1, 3, 4)
        rpc = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "animation_align", "arguments": {"frames-dir": "frames", "out": "aligned"}},
        }
        process = subprocess.run(
            [sys.executable, LOOP / "mcp_server.py", "--project", self.project],
            input=json.dumps(rpc) + "\n",
            capture_output=True,
            encoding="utf-8",
            timeout=40,
            check=True,
        )
        result = json.loads(process.stdout)["result"]
        report = json.loads(result["content"][0]["text"])
        self.assertFalse(result["isError"], report)
        self.assertEqual(report["canvas"], [5, 4])
        self.assertEqual([frame["offset"] for frame in report["frames"]], [[0, 1], [1, 0]])
        for name in ("01.png", "02.png"):
            image = (self.project / "aligned" / name).read_bytes()
            self.assertEqual(struct.unpack(">II", image[16:24]), (5, 4))

    def test_reject_empty_frame_without_output(self):
        frames = self.project / "frames"
        frames.mkdir()
        save_png(frames / "01.png", 3, 3, 0, 0, 0, 0)
        status, report = self.cli("align", "--frames-dir", "frames", "--out", "aligned")
        self.assertEqual(status, 1, report)
        self.assertIn("no visible pixels", report["error"])
        self.assertFalse((self.project / "aligned").exists())


if __name__ == "__main__":
    unittest.main()
