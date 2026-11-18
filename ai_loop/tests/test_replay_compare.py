import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LOOP))

import replay_compare  # noqa: E402


class ReplayComparisonTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.expected = self.root / "expected.json"
        self.actual = self.root / "actual.jsonl"

    def write_logs(self, expected, records):
        self.expected.write_text(json.dumps(expected), encoding="utf-8")
        self.actual.write_text("".join(json.dumps(row) + "\n" for row in records), encoding="utf-8")

    def compare(self, expected, records, *flags):
        self.write_logs(expected, records)
        process = subprocess.run(
            [
                sys.executable,
                str(LOOP / "uzu.py"),
                "replay-compare",
                "--expected",
                str(self.expected),
                "--actual",
                str(self.actual),
                *flags,
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        return process.returncode, json.loads(process.stdout)["first_mismatch"]

    def test_equal_replays_ignore_object_key_order(self):
        self.assertEqual(self.compare([{"type": "input", "frame": 1}], [{"frame": 1, "type": "input"}]), (0, None))

    def test_first_state_diff_reports_exact_field(self):
        expected = [{"frame": 3}, {"frame": 72, "state": {"Hero/1": {"hp": 4}}}]
        actual = [{"frame": 3}, {"frame": 72, "state": {"Hero/1": {"hp": 3}}}]
        self.assertEqual(
            self.compare(expected, actual),
            (
                1,
                {
                    "record": 2,
                    "frame": 72,
                    "path": "/state/Hero~11/hp",
                    "reason": "value",
                    "expected": 4,
                    "actual": 3,
                },
            ),
        )

    def test_truncated_log_reports_missing_frame(self):
        status, diff = self.compare([{"frame": 1}, {"frame": 2}], [{"frame": 1}])
        self.assertEqual((status, diff["frame"], diff["reason"]), (1, 2, "missing"))

    def test_snapshot_checks_selected_type_and_partial_state(self):
        snapshot = {"frame": 24, "type": "state", "state": {"gold": 2}}
        records = [{"frame": 24, "type": "input"}, {"frame": 24, "type": "state", "state": {"gold": 2, "hp": 7}}]
        self.assertEqual(self.compare(snapshot, records, "--snapshots"), (0, None))
        snapshot["state"]["gold"] = 3
        status, diff = self.compare(snapshot, records, "--snapshots")
        self.assertEqual((status, diff["record"], diff["path"]), (1, 2, "/state/gold"))

    def test_snapshot_requires_ordered_distinct_matches(self):
        status, diff = self.compare([{"frame": 9}, {"frame": 9}], [{"frame": 9}], "--snapshots")
        self.assertEqual((status, diff["snapshot"], diff["reason"]), (1, 2, "missing_snapshot"))

    def test_bad_log_reports_line(self):
        self.actual.write_text('{"frame":1}\n{"frame":2,}\n', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, r"actual.jsonl:2:"):
            replay_compare.read_log(self.actual)

    def test_boolean_is_not_a_number(self):
        self.assertEqual(replay_compare.first_diff(True, 1)["reason"], "value")

    def test_mcp_delivers_structured_mismatch(self):
        self.write_logs([{"frame": 5, "hp": 4}], [{"frame": 5, "hp": 1}])
        (self.root / "project.godot").write_text("config_version=5\n", encoding="utf-8")
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {
                "name": "replay_compare",
                "arguments": {"expected": str(self.expected), "actual": str(self.actual)},
            },
        }
        process = subprocess.run(
            [sys.executable, str(LOOP / "mcp_server.py"), "--project", str(self.root)],
            input=json.dumps(request) + "\n",
            capture_output=True,
            text=True,
            check=True,
        )
        result = json.loads(process.stdout)["result"]
        self.assertTrue(result["isError"])
        self.assertEqual(json.loads(result["content"][0]["text"])["first_mismatch"]["path"], "/hp")


if __name__ == "__main__":
    unittest.main()
