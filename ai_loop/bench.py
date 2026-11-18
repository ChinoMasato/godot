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

Frames must be at least 12 (including 10 recorder warmup frames).
A failed engine or a run without frame samples fails the benchmark.

A result file is {"marks": {"[Startup] Main::Setup": seconds, ...}, "repeat": n, "frames": n}.
"run --out base.json" makes a baseline. compare and check print one JSON line
{"ok", "regressions": [...], "improvements": [...], "missing": [...]} and exit 1
when anything regressed. Standard library only.
"""

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import uzu  # noqa: E402  Finds the editor binary for this OS (B-801).


FRAME_AUTOLOAD = 'UZUFrameTime="*res://addons/uzu_selfplay/frametime.gd"\n'


def run(project, repeat=3, frames=60, render=False):
    """Median of each startup mark (--benchmark-file) and of the frame times (B-803)
    over several runs. The frame times come from a recorder autoload installed in a
    copy of the project. Headless by default (CPU work only); render=True draws too."""
    # The recorder discards ten warmup frames and needs a completed sample.
    if repeat <= 0 or frames < 12:
        raise ValueError("repeat must be positive and frames must be at least 12 (10 warmup frames)")
    samples = {}
    with tempfile.TemporaryDirectory() as tmp:
        game = os.path.join(tmp, "game")
        shutil.copytree(project, game, ignore=shutil.ignore_patterns(".godot"))
        uzu.install_addons(game, ["selfplay/frametime.gd"], FRAME_AUTOLOAD)
        # One discarded warm-up run: the fresh copy has no .godot cache yet, and building
        # it makes the first run's startup and shutdown slower than the rest.
        for i in range(-1, repeat):
            out, frame_out = os.path.join(tmp, f"bench_{i}.json"), os.path.join(tmp, f"frames_{i}.json")
            cmd = [uzu.find_godot(), "--path", game, "--benchmark", "--benchmark-file", out, "--quit-after", str(frames)]
            env = None
            if render:
                cmd, env = uzu.render_command(cmd + uzu.driver())
            else:
                cmd.insert(1, "--headless")
            proc = subprocess.run(cmd + ["--", "--uzu-frametime", frame_out], capture_output=True, timeout=600, env=env)
            if proc.returncode != 0:
                raise RuntimeError(f"the engine failed (run {i + 1}, exit {proc.returncode}): {uzu.tail(proc.stderr.decode(errors='replace'))}")
            if not os.path.exists(out):
                raise RuntimeError(f"the engine wrote no benchmark file (run {i + 1}); does {project} have a main scene?")
            if i < 0:
                continue
            with open(out, encoding="utf-8") as f:
                for key, seconds in json.load(f).items():
                    samples.setdefault(key, []).append(float(seconds))
            if not os.path.exists(frame_out):
                raise RuntimeError(f"no frame samples (run {i + 1}); did the scene quit before warmup finished?")
            with open(frame_out, encoding="utf-8") as f:
                ft = json.load(f)
            for name in ("average", "p95", "worst"):
                samples.setdefault(f"[Frame] work {name}", []).append(ft["work"][f"{name}_ms"] / 1000.0)
                if render:  # Headless frames are padded with a sleep: wall time says nothing there.
                    samples.setdefault(f"[Frame] wall {name}", []).append(ft["wall"][f"{name}_ms"] / 1000.0)
    return {"marks": {k: statistics.median(v) for k, v in samples.items()}, "repeat": repeat, "frames": frames, "render": render}


# Engine shutdown time jumps between two levels (about 20 ms and 90 ms here) from run
# to run, whatever the game does, so by default it is reported but does not fail.
IGNORED_BY_DEFAULT = ("[Shutdown]",)


def compare(baseline, current, threshold=0.25, min_ms=5.0, min_frame_ms=1.0, ignore=IGNORED_BY_DEFAULT):
    base, cur = baseline["marks"], current["marks"]
    regressions, improvements, ignored = [], [], []
    for key, before in base.items():
        if key not in cur:
            continue
        if key.startswith(tuple(ignore)):
            ignored.append(key)
            continue
        after = cur[key]
        diff_ms = (after - before) * 1000.0
        entry = {"mark": key, "baseline_ms": round(before * 1000.0, 3), "current_ms": round(after * 1000.0, 3), "change": round(after / before - 1.0, 3) if before > 0 else None}
        # Frame times are a few milliseconds, so they get their own (smaller) noise floor.
        if abs(diff_ms) <= (min_frame_ms if key.startswith("[Frame]") else min_ms):
            continue
        if after > before * (1.0 + threshold):
            regressions.append(entry)
        elif after < before * (1.0 - threshold):
            improvements.append(entry)
    missing = sorted(k for k in base if k not in cur)
    return {"ok": not regressions, "threshold": threshold, "min_ms": min_ms, "min_frame_ms": min_frame_ms, "regressions": regressions, "improvements": improvements, "missing": missing, "new": sorted(k for k in cur if k not in base), "ignored": ignored}


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
        base = run(project, repeat=3, frames=40)
        assert "[Startup] Load Autoloads" in base["marks"], base
        # Nothing changed: pass. Startup marks on a busy machine can jump by a few ms over
        # the threshold once in a while, so this one check may run up to three times; the
        # checks that something slower fails do not retry.
        for attempt in range(3):
            same = compare(base, run(project, repeat=3, frames=40))
            if same["ok"]:
                break
        assert same["ok"], same
        # An artificially slow change: an autoload that busy-waits 300 ms at startup.
        with open(os.path.join(project, "slow.gd"), "w") as f:
            f.write("extends Node\n\nfunc _init() -> void:\n\tOS.delay_msec(300)\n")
        with open(os.path.join(project, "project.godot"), "a") as f:
            f.write('\n[autoload]\n\nSlow="*res://slow.gd"\n')
        slow = compare(base, run(project, repeat=3, frames=40))
        assert not slow["ok"], slow
        assert any(r["mark"] == "[Startup] Load Autoloads" for r in slow["regressions"]), slow
        # A slow frame (5 ms of work in every _process) is a frame-time regression (B-803).
        base = run(project, repeat=3, frames=40)
        assert "[Frame] work average" in base["marks"] and "[Frame] work worst" in base["marks"], base
        with open(os.path.join(project, "main.gd"), "w") as f:
            f.write("extends Node\n\nfunc _process(_delta: float) -> void:\n\tOS.delay_msec(5)\n")
        with open(os.path.join(project, "main.tscn"), "w") as f:
            f.write('[gd_scene load_steps=2 format=3]\n\n[ext_resource type="Script" path="res://main.gd" id="1"]\n\n[node name="Main" type="Node"]\nscript = ExtResource("1")\n')
        heavy = compare(base, run(project, repeat=3, frames=40))
        assert any(r["mark"] == "[Frame] work average" for r in heavy["regressions"]), heavy
        # Early exit must not masquerade as a successful frame benchmark.
        with open(os.path.join(project, "main.gd"), "w") as f:
            f.write("extends Node\nfunc _ready() -> void:\n\tget_tree().quit()\n")
        try:
            run(project, repeat=1, frames=40)
        except RuntimeError as error:
            assert "no frame samples" in str(error), error
        else:
            raise AssertionError("early exit returned a benchmark without frame samples")
        with open(os.path.join(project, "main.gd"), "w") as f:
            f.write("extends Node\nfunc _ready() -> void:\n\tget_tree().quit(7)\n")
        try:
            run(project, repeat=1, frames=40)
        except RuntimeError as error:
            assert "exit 7" in str(error), error
        else:
            raise AssertionError("failed engine returned a successful benchmark")
        for repeat, frames in ((0, 40), (1, 0), (1, 10)):
            try:
                run(project, repeat=repeat, frames=frames)
            except ValueError:
                pass
            else:
                raise AssertionError("invalid benchmark arguments were accepted")
        # compare() on its own: noise below min_ms passes, a real slowdown fails.
        assert compare({"marks": {"a": 0.0001}}, {"marks": {"a": 0.0004}})["ok"]
        assert not compare({"marks": {"a": 0.1}}, {"marks": {"a": 0.2}})["ok"]
        assert compare({"marks": {"a": 0.1, "b": 0.1}}, {"marks": {"a": 0.1}})["missing"] == ["b"]
        shutdown = {"marks": {"[Shutdown] Main::Cleanup": 0.02}}, {"marks": {"[Shutdown] Main::Cleanup": 0.09}}
        assert compare(*shutdown)["ok"] and compare(*shutdown)["ignored"] == ["[Shutdown] Main::Cleanup"]
        assert not compare(*shutdown, ignore=())["ok"]
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
    parser.add_argument("--min-frame-ms", type=float, default=1.0)
    parser.add_argument("--include-shutdown", action="store_true", help="also judge the shutdown marks (noisy)")
    parser.add_argument("--render", action="store_true", help="draw frames too (GPU, or software rendering without a display)")
    a = parser.parse_args()
    if a.command in ("run", "check") and not a.project:
        parser.error(f"{a.command} needs --project")
    if a.command in ("compare", "check") and not a.baseline:
        parser.error(f"{a.command} needs --baseline")
    if a.command == "compare" and not a.current:
        parser.error("compare needs --current")

    if a.command == "run":
        result = run(os.path.abspath(a.project), a.repeat, a.frames, a.render)
        if a.out:
            with open(a.out, "w", encoding="utf-8") as f:
                json.dump(result, f, indent=1, sort_keys=True)
        print(json.dumps({"ok": True, "command": "run", "out": a.out, **result}))
        return 0
    current = run(os.path.abspath(a.project), a.repeat, a.frames, a.render) if a.command == "check" else _load(a.current)
    result = compare(_load(a.baseline), current, a.threshold, a.min_ms, a.min_frame_ms, () if a.include_shutdown else IGNORED_BY_DEFAULT)
    print(json.dumps({"command": a.command, **result}))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, subprocess.TimeoutExpired, uzu.ToolError) as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        sys.exit(1)
