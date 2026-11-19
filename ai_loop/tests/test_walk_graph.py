"""B-1510: region-graph walking verification (ai_loop/uzu.py map walk-graph).

Pins the judgement of the BACKLOG item mechanically, on the parts that do
not need the engine:
  * the region graph loader accepts the B-1509 format and refuses broken
    graphs (wrong format, a node without a center, a duplicated id, an
    edge joining an unknown node),
  * the saved hikari sample reports (spec_set/walk_graph_report.json and
    walk_graph_report_blocked.json, produced by walk_graph_check.py) are
    well-formed per-edge verdicts: two legs per edge, an edge passes only
    when both legs arrived, and the blocked run fails exactly the fenced
    node's edge on top of the baseline's failures — nothing else flips.
The walking itself is exercised end-to-end by walk_graph_check.py with
the real engine; its outputs are what the second group checks.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

LOOP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LOOP))
import uzu  # noqa: E402

SAMPLE = LOOP / "samples" / "hikari" / "spec_set"
BLOCKED_NODE = "Village__gate_east"


def write_graph(payload):
    f = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8")
    json.dump(payload, f)
    f.close()
    return f.name


def mini_graph():
    return {
        "format": "uzu-region-graph/1",
        "scene": "res://village.tscn",
        "nodes": [
            {"id": "A__spawn", "center": [0, 0, 0], "about": "start"},
            {"id": "A__plaza", "center": [10, 0, 0], "about": "square"},
        ],
        "edges": [{"a": "A__spawn", "b": "A__plaza", "length": 10.0, "bidirectional": True}],
    }


class LoadRegionGraph(unittest.TestCase):
    def test_valid_graph_loads(self):
        graph = uzu.load_region_graph(write_graph(mini_graph()))
        self.assertEqual(len(graph["nodes"]), 2)
        self.assertEqual(len(graph["edges"]), 1)

    def test_sample_graph_loads(self):
        graph = uzu.load_region_graph(str(SAMPLE / "region_graph.json"))
        self.assertEqual(len(graph["nodes"]), 16)
        self.assertEqual(len(graph["edges"]), 13)

    def test_wrong_format_refused(self):
        bad = mini_graph()
        bad["format"] = "uzu-something-else/9"
        with self.assertRaises(uzu.ToolError):
            uzu.load_region_graph(write_graph(bad))

    def test_node_without_center_refused(self):
        bad = mini_graph()
        del bad["nodes"][0]["center"]
        with self.assertRaises(uzu.ToolError):
            uzu.load_region_graph(write_graph(bad))

    def test_duplicate_node_refused(self):
        bad = mini_graph()
        bad["nodes"].append(dict(bad["nodes"][0]))
        with self.assertRaises(uzu.ToolError):
            uzu.load_region_graph(write_graph(bad))

    def test_edge_to_unknown_node_refused(self):
        bad = mini_graph()
        bad["edges"][0]["b"] = "A__nowhere"
        with self.assertRaises(uzu.ToolError):
            uzu.load_region_graph(write_graph(bad))

    def test_missing_file_refused(self):
        with self.assertRaises(uzu.ToolError):
            uzu.load_region_graph(str(SAMPLE / "no_such_graph.json"))


def load_report(name):
    path = SAMPLE / name
    if not path.exists():
        raise unittest.SkipTest(f"{name} not generated yet (run walk_graph_check.py)")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def failed_edges(report):
    return {(r["a"], r["b"]) for r in report["results"] if not r["ok"]}


class SampleReports(unittest.TestCase):
    def check_shape(self, report):
        graph = json.loads((SAMPLE / "region_graph.json").read_text(encoding="utf-8"))
        self.assertEqual(report["format"], "uzu-walk-graph-report/1")
        self.assertEqual(report["nodes"], len(graph["nodes"]))
        self.assertEqual(report["edges"], len(graph["edges"]))
        self.assertEqual(report["legs"], 2 * len(graph["edges"]))
        self.assertEqual(report["edges_ok"] + report["edges_failed"], report["edges"])
        self.assertEqual(report["ok"], report["edges_failed"] == 0)
        graph_edges = {(e["a"], e["b"]) for e in graph["edges"]}
        self.assertEqual({(r["a"], r["b"]) for r in report["results"]}, graph_edges)
        for r in report["results"]:
            self.assertEqual(len(r["legs"]), 2)
            self.assertEqual({(l["from"], l["to"]) for l in r["legs"]},
                             {(r["a"], r["b"]), (r["b"], r["a"])})
            self.assertEqual(r["ok"], all(l["reached"] for l in r["legs"]))
            for leg in r["legs"]:
                self.assertGreater(leg["frames"], 0)
                if leg["reached"]:
                    self.assertIsNone(leg["stuck_at"])
                else:
                    self.assertEqual(len(leg["stuck_at"]), 3)

    def test_baseline_report_shape(self):
        self.check_shape(load_report("walk_graph_report.json"))

    def test_blocked_report_shape(self):
        self.check_shape(load_report("walk_graph_report_blocked.json"))

    def test_blocked_flips_exactly_the_fenced_edge(self):
        baseline = load_report("walk_graph_report.json")
        blocked = load_report("walk_graph_report_blocked.json")
        self.assertEqual(blocked["blocked_node"], BLOCKED_NODE)
        graph = json.loads((SAMPLE / "region_graph.json").read_text(encoding="utf-8"))
        fenced = {(e["a"], e["b"]) for e in graph["edges"] if BLOCKED_NODE in (e["a"], e["b"])}
        self.assertEqual(len(fenced), 1)  # A leaf: the flip must be exactly this edge.
        self.assertEqual(failed_edges(blocked), failed_edges(baseline) | fenced)


if __name__ == "__main__":
    unittest.main()
