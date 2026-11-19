#!/usr/bin/env python3
"""B-1510 sample: walk the region graph of the generated village
(B-1508's village, B-1509's region graph) with the graph walker, twice:

    spec_set/walk_graph_report.json          every edge, both directions
    spec_set/walk_graph_report_blocked.json  Village__gate_east fenced in:
                                             exactly its edge must flip

Flow: copy the project to a temp dir, compose the village scene there
(compose_village.gd -> res://village.tscn, the scene the graph names),
then run `uzu.py map <copy> walk-graph` on it for both runs. The reports
are written next to the spec set they verify; the script's exit code is
0 only when every edge got a verdict and the blocked run flips exactly
the fenced node's edge (baseline failures, e.g. the tallest roof's, stay
failed in both — a place a walker cannot stand on is an honest fail).

Usage: python3 walk_graph_check.py   (the editor binary: $GODOT or bin/)
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LOOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, LOOP)
from uzu import find_godot  # noqa: E402

BLOCK_NODE = "Village__gate_east"


def run(cmd, timeout=1800):
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    lines = [l for l in proc.stdout.splitlines() if l.startswith("{")]
    if not lines:
        sys.stderr.write(proc.stdout[-2000:] + proc.stderr[-2000:])
        raise SystemExit(f"no JSON result from: {' '.join(cmd)}")
    return json.loads(lines[-1])


def failed(report):
    return {(r["a"], r["b"]) for r in report["results"] if not r["ok"]}


def main():
    spec_set = os.path.join(HERE, "spec_set")
    graph = os.path.join(spec_set, "region_graph.json")
    godot = find_godot()
    with tempfile.TemporaryDirectory() as tmp:
        game = os.path.join(tmp, "hikari")
        shutil.copytree(HERE, game)
        proc = subprocess.run([godot, "--headless", "--path", game, "--script", "res://compose_village.gd"],
                              capture_output=True, text=True, timeout=900)
        lines = [l for l in proc.stdout.splitlines() if l.startswith("UZU_RESULT ")]
        if not lines or not json.loads(lines[-1][len("UZU_RESULT "):]).get("ok"):
            sys.stderr.write(proc.stdout[-2000:] + proc.stderr[-2000:])
            print(json.dumps({"ok": False, "error": "compose_village.gd failed"}))
            return 1
        reports = {}
        for name, extra in (("walk_graph_report", []),
                            ("walk_graph_report_blocked", ["--block-node", BLOCK_NODE])):
            out = os.path.join(spec_set, name + ".json")
            reports[name] = run([sys.executable, os.path.join(LOOP, "uzu.py"), "map", game,
                                 "walk-graph", "--graph", graph, "--out", out] + extra)
    baseline, blocked = reports["walk_graph_report"], reports["walk_graph_report_blocked"]
    graph_edges = {(e["a"], e["b"]) for e in json.load(open(graph, encoding="utf-8"))["edges"]}
    fenced = {e for e in graph_edges if BLOCK_NODE in e}
    verdicts_complete = all(len(r["legs"]) == 2 for r in baseline["results"]) and baseline["edges"] == len(graph_edges)
    flip_ok = failed(blocked) == failed(baseline) | fenced and len(fenced) == 1
    summary = {
        "ok": verdicts_complete and flip_ok,
        "baseline": {"edges": baseline["edges"], "edges_ok": baseline["edges_ok"],
                     "edges_failed": sorted(failed(baseline))},
        "blocked": {"node": BLOCK_NODE, "edges_failed": sorted(failed(blocked))},
        "exactly_one_flip": flip_ok,
    }
    print(json.dumps(summary, ensure_ascii=False))
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
