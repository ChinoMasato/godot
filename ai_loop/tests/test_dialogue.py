import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
SAMPLE = LOOP / "dialogue" / "lantern_quest.json"


class DialogueTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.source = self.directory / "dialogue.json"
        self.graph = json.loads(SAMPLE.read_text(encoding="utf-8"))

    def cli(self, *args):
        process = subprocess.run(
            [sys.executable, LOOP / "uzu.py", "dialogue", "--spec", self.source, *args],
            capture_output=True,
            encoding="utf-8",
            timeout=15,
        )
        return process.returncode, json.loads(process.stdout)

    def save(self):
        self.source.write_text(json.dumps(self.graph, ensure_ascii=False), encoding="utf-8")

    def remove_branch(self):
        self.graph["nodes"]["shrine"]["choices"].pop()
        self.save()

    def test_build_valid_graph(self):
        self.save()
        output = self.directory / "graph.json"
        code, result = self.cli("--out", output)
        self.assertEqual(code, 0, result)
        self.assertTrue(result["ok"])
        self.assertEqual(result["issues"], [])
        self.assertEqual(result["reachable_nodes"], sorted(self.graph["nodes"]))
        self.assertEqual(result["states_checked"], 7)
        self.assertEqual(result["text_lengths"]["nodes.ask.speaker"], 2)
        self.assertEqual(json.loads(output.read_text(encoding="utf-8")), self.graph)
        self.assertIn("番人".encode(), output.read_bytes())

    def test_missing_choice_after_merge_and_unreachable_ending(self):
        self.remove_branch()
        output = self.directory / "graph.json"
        output.write_text("previous export", encoding="utf-8")
        code, result = self.cli("--out", output)
        self.assertEqual(code, 1, result)
        self.assertFalse(result["ok"])
        self.assertEqual(
            [(issue["code"], issue["where"]) for issue in result["issues"]],
            [("branch_gap", "nodes.shrine.choices"), ("unreachable_node", "nodes.return")],
        )
        self.assertEqual(result["issues"][0]["flags"], {"has_shard": False, "lantern_lit": False})
        self.assertEqual(output.read_text(encoding="utf-8"), "previous export")

    def test_flag_read_requires_initialization_on_each_path(self):
        self.graph["flags"]["has_shard"] = None
        self.save()
        code, result = self.cli()
        self.assertEqual(code, 1, result)
        reads = [issue for issue in result["issues"] if issue["code"] == "uninitialized_flag"]
        self.assertEqual(len(reads), 2)
        self.assertTrue(all(issue["flags"]["has_shard"] is None for issue in reads))
        self.assertIn("light", result["reachable_nodes"])

    def test_wrong_flag_and_target_are_structured_errors(self):
        self.graph["nodes"]["light"]["set"]["lantern-lit"] = 1
        self.graph["nodes"]["shrine"]["choices"][0]["to"] = "missing"
        self.save()
        code, result = self.cli()
        self.assertEqual(code, 1, result)
        self.assertEqual(
            [(issue["code"], issue["where"]) for issue in result["issues"]],
            [
                ("undeclared_flag", "nodes.light.set.lantern-lit"),
                ("invalid_flag_value", "nodes.light.set.lantern-lit"),
                ("unknown_target", "nodes.shrine.choices[0].to"),
            ],
        )

    def test_budget_counts_unicode_without_line_breaks(self):
        self.graph = {"entry": "end", "nodes": {"end": {"text": "灯り\r\n🕯", "end": True}}}
        self.save()
        code, result = self.cli("--max-chars", "2")
        self.assertEqual(code, 1, result)
        self.assertEqual(result["reachable_nodes"], ["end"])
        self.assertEqual(result["text_lengths"], {"nodes.end.text": 3})
        (issue,) = result["issues"]
        self.assertEqual((issue["code"], issue["length"], issue["limit"]), ("text_too_long", 3, 2))

    def test_valid_loop_reuses_an_explored_state(self):
        self.graph["nodes"]["shrine"]["choices"].append({"text": "広場に戻る", "to": "wait"})
        self.save()
        code, result = self.cli()
        self.assertEqual(code, 0, result)
        self.assertEqual(result["states_checked"], 8)

    def test_duplicate_keys_and_source_overwrite_are_rejected(self):
        self.source.write_text('{"entry":"one","entry":"two"}', encoding="utf-8")
        code, result = self.cli()
        self.assertEqual(code, 2)
        self.assertIn("duplicate JSON key", result["error"])
        self.save()
        code, result = self.cli("--out", self.source)
        self.assertEqual(code, 2)
        self.assertIn("--out must differ from --spec", result["error"])
        self.assertEqual(json.loads(self.source.read_text(encoding="utf-8")), self.graph)

    def test_mcp_returns_the_cli_diagnostics(self):
        self.remove_branch()
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "dialogue_validate", "arguments": {"spec": str(self.source)}},
        }
        process = subprocess.run(
            [sys.executable, LOOP / "mcp_server.py", "--project", self.directory],
            input=json.dumps(request) + "\n",
            capture_output=True,
            encoding="utf-8",
            timeout=15,
            check=True,
        )
        rpc = json.loads(process.stdout)
        self.assertEqual(rpc["id"], request["id"])
        self.assertTrue(rpc["result"]["isError"])
        self.assertEqual(json.loads(rpc["result"]["content"][0]["text"]), self.cli()[1])


if __name__ == "__main__":
    unittest.main()
