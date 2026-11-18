#!/usr/bin/env python3
"""Compare GPU time and screenshots for the B-727 water sample on Vulkan."""

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOOP = HERE.parent
CASES = ("dry", "water", "water_no_reflection")


def run_command(command, log, timeout, env=None):
    with log.open("w", encoding="utf-8") as output:
        subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=True, timeout=timeout, env=env)
    return log.read_text(encoding="utf-8", errors="replace")


def payload(output, prefix):
    for line in reversed(output.splitlines()):
        if line.startswith(prefix):
            data = json.loads(line if prefix == "{" else line[len(prefix) :])
            if data.get("ok"):
                return data
            break
    raise ValueError("command did not report success")


def compare(args, out, results):
    binary = args.godot.resolve()
    if os.name == "nt" and binary.name.endswith(".console.exe"):
        direct = binary.with_name(binary.name.removesuffix(".console.exe") + ".exe")
        if direct.is_file():
            binary = direct
    env = {**os.environ, "GODOT": str(binary)}

    with tempfile.TemporaryDirectory(prefix="uzu-water-") as directory:
        project = Path(directory) / "water"
        shutil.copytree(LOOP / "look_samples" / "water", project, ignore=shutil.ignore_patterns(".godot"))
        cli = [sys.executable, str(LOOP / "uzu.py"), "look", str(project)]
        scene = ["--scene", "res://main.tscn"]
        payload(run_command(cli + ["apply", *scene], out / "prepare.log", args.timeout, env), "{")
        dry = (project / "main.tscn").read_bytes()

        for name in CASES:
            (project / "main.tscn").write_bytes(dry)
            if name != "dry":
                reflection = "0" if name == "water_no_reflection" else "1"
                water = ["water", *scene, "--position", "0,0,-7.5", "--size", "16,21", "--reflection", reflection]
                payload(run_command(cli + water, out / f"{name}-setup.log", args.timeout, env), "{")
            shot = out / f"{name}.png"
            shot.unlink(missing_ok=True)
            log = out / f"{name}.log"
            command = [
                str(binary),
                "--path",
                str(project),
                "--rendering-method",
                "forward_plus",
                "--rendering-driver",
                "vulkan",
                "--audio-driver",
                "Dummy",
                "--max-fps",
                "60",
                "--resolution",
                args.resolution,
                "--script",
                str(HERE / "scene_time.gd"),
                "--",
                "--frames",
                str(args.frames),
                "--shot",
                str(shot),
            ]
            output = run_command(command, log, args.timeout)
            if not shot.is_file() or not shot.stat().st_size or re.search(r"(?m)^(?:SCRIPT )?ERROR:", output):
                raise ValueError(f"{name}: drawing or capture failed, see {log}")
            backend = next(
                (line for line in output.splitlines() if line.startswith("Vulkan ") and "Forward+" in line), None
            )
            if not backend:
                raise ValueError(f"{name}: expected Forward+ Vulkan, see {log}")
            data = payload(output, "UZU_SCENETIME ")
            for key in ("gpu_ms_median", "gpu_ms_p95"):
                if not math.isfinite(data[key]) or data[key] <= 0:
                    raise ValueError(f"{name}: GPU timer unavailable, see {log}")
            if data["size"] != [int(size) for size in args.resolution.split("x")]:
                raise ValueError(f"{name}: capture resolution changed, see {log}")
            results[name] = {**data, "backend": backend, "image": shot.name}
            print(f"{name}: {data['gpu_ms_median']:.3f} ms", file=sys.stderr, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path, help="New directory for JSON, PNGs and logs")
    parser.add_argument("--godot", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=180, help="Samples after scene_time.gd's 60-frame warmup")
    parser.add_argument("--resolution", default="1280x720")
    parser.add_argument("--timeout", type=int, default=180, help="Seconds allowed per engine invocation")
    args = parser.parse_args()
    if args.frames < 1 or args.timeout < 1:
        parser.error("--frames and --timeout must be positive")
    if not re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*", args.resolution):
        parser.error("--resolution must be WIDTHxHEIGHT")
    out = args.out.resolve()
    results = {}
    try:
        out.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 1
    try:
        compare(args, out, results)
        median = {name: results[name]["gpu_ms_median"] for name in CASES}
        report = {
            "ok": True,
            "cases": results,
            "samples_per_case": args.frames,
            "water_over_dry_ms": round(median["water"] - median["dry"], 3),
            "reflection_over_water_no_reflection_ms": round(median["water"] - median["water_no_reflection"], 3),
        }
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report = {"ok": False, "error": str(error), "cases": results}
    text = json.dumps(report, indent=2, ensure_ascii=False)
    (out / "report.json").write_text(text + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
