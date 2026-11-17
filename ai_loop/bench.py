#!/usr/bin/env python3
"""Benchmark pass/fail against a baseline (BACKLOG B-104).

Runs a project headless with the engine's --benchmark --benchmark-file, takes
the median of each timing mark over several runs, and compares it with a
baseline JSON. A mark regresses when it is slower than the baseline by more
than --threshold (a fraction) AND by more than --min-ms milliseconds; the second
condition keeps tiny marks (microseconds) from failing on noise.

Usage:
  ai_loop/bench.py run      --project P [--out result.json] [--repeat 3] [--frames 60]
  ai_loop/bench.py compare  --baseline base.json --current result.json [--threshold 0.25] [--min-ms 5]
  ai_loop/bench.py check    --project P --baseline base.json [run and compare options]
  ai_loop/bench.py --self-test

A result file is {"marks": {"[Startup] Main::Setup": seconds, ...}, "repeat": n, "frames": n}.
"run --out base.json" makes a baseline. compare and check print one JSON line
{"ok", "regressions": [...], "improvements": [...], "missing": [...]} and exit 1
when anything regressed. Standard library only.
"""

import argparse
import json
import os
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import uzu  # noqa: E402  Finds the editor binary for this OS (B-801).


def run(project, repeat=3, frames=60):
    samples = {}
    with tempfile.TemporaryDirectory() as tmp:
        for i in range(repeat):
            out = os.path.join(tmp, f"bench_{i}.json")
            subprocess.run([uzu.find_godot(), "--headless", "--path", project, "--benchmark", "--benchmark-file", out, "--quit-after", str(frames)], capture_output=True, timeout=600)
            if not os.path.exists(out):
                raise RuntimeError(f"the engine wrote no benchmark file (run {i + 1}); does {project} have a main scene?")
            with open(out, encoding="utf-8") as f:
                for key, seconds in json.load(f).items():
                    samples.setdefault(key, []).append(float(seconds))
    return {"marks": {k: statistics.median(v) for k, v in samples.items()}, "repeat": repeat, "frames": frames}


def compare(baseline, current, threshold=0.25, min_ms=5.0):
    base, cur = baseline["marks"], current["marks"]
    regressions, improvements = [], []
    for key, before in base.items():
        if key not in cur:
            continue
        after = cur[key]
        diff_ms = (after - before) * 1000.0
        entry = {"mark": key, "baseline_ms": round(before * 1000.0, 3), "current_ms": round(after * 1000.0, 3), "change": round(after / before - 1.0, 3) if before > 0 else None}
        if abs(diff_ms) <= min_ms:
            continue
        if after > before * (1.0 + threshold):
            regressions.append(entry)
        elif after < before * (1.0 - threshold):
            improvements.append(entry)
    missing = sorted(k for k in base if k not in cur)
    return {"ok": not regressions, "threshold": threshold, "min_ms": min_ms, "regressions": regressions, "improvements": improvements, "missing": missing, "new": sorted(k for k in cur if k not in base)}


def _load(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    return data if "marks" in data else {"marks": data}  # Also accept a raw --benchmark-file.


def self_test():
    with tempfile.TemporaryDirectory() as project:
        with open(os.path.join(project, "project.godot"), "w") as f:
            f.write('config_version=5\n\n[application]\n\nconfig/name="bench selftest"\nrun/main_scene="res://main.tscn"\n')
        with open(os.path.join(project, "main.tscn"), "w") as f:
            f.write('[gd_scene format=3]\n\n[node name="Main" type="Node"]\n')
        base = run(project, repeat=3, frames=5)
        assert "[Startup] Load Autoloads" in base["marks"], base
        same = compare(base, run(project, repeat=3, frames=5))
        assert same["ok"], same
        # An artificially slow change: an autoload that busy-waits 300 ms at startup.
        with open(os.path.join(project, "slow.gd"), "w") as f:
            f.write("extends Node\n\nfunc _init() -> void:\n\tOS.delay_msec(300)\n")
        with open(os.path.join(project, "project.godot"), "a") as f:
            f.write('\n[autoload]\n\nSlow="*res://slow.gd"\n')
        slow = compare(base, run(project, repeat=3, frames=5))
        assert not slow["ok"], slow
        assert any(r["mark"] == "[Startup] Load Autoloads" for r in slow["regressions"]), slow
        # compare() on its own: noise below min_ms passes, a real slowdown fails.
        assert compare({"marks": {"a": 0.0001}}, {"marks": {"a": 0.0004}})["ok"]
        assert not compare({"marks": {"a": 0.1}}, {"marks": {"a": 0.2}})["ok"]
        assert compare({"marks": {"a": 0.1, "b": 0.1}}, {"marks": {"a": 0.1}})["missing"] == ["b"]
    print("bench selftest: ok")
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["run", "compare", "check"])
    parser.add_argument("--project")
    parser.add_argument("--out")
    parser.add_argument("--baseline")
    parser.add_argument("--current")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--threshold", type=float, default=0.25)
    parser.add_argument("--min-ms", type=float, default=5.0)
    a = parser.parse_args()
    if a.command in ("run", "check") and not a.project:
        parser.error(f"{a.command} needs --project")
    if a.command in ("compare", "check") and not a.baseline:
        parser.error(f"{a.command} needs --baseline")
    if a.command == "compare" and not a.current:
        parser.error("compare needs --current")

    if a.command == "run":
        result = run(os.path.abspath(a.project), a.repeat, a.frames)
        if a.out:
            with open(a.out, "w", encoding="utf-8") as f:
                json.dump(result, f, indent=1, sort_keys=True)
        print(json.dumps({"ok": True, "command": "run", "out": a.out, **result}))
        return 0
    current = run(os.path.abspath(a.project), a.repeat, a.frames) if a.command == "check" else _load(a.current)
    result = compare(_load(a.baseline), current, a.threshold, a.min_ms)
    print(json.dumps({"command": a.command, **result}))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
