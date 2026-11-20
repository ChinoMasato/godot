#!/usr/bin/env python3
"""Procedural rocks with baked PBR texture sets (BACKLOG B-1219, stage 2).

    uzu.py rock --spec rocks.json --out ai_loop/look_samples/b1219_rock

The stage-1 PoC found the limit of the noise-only route: displacement
over a smooth primitive stays an organic blob -- a rock reads as a rock
through its flat facets and sharp ridges. So this recipe makes the
faces first: a subdivided icosphere is cut by a handful of half-spaces
(boolean differences against large rotated cubes), which leaves flat
facets meeting at real ridges; a high-density copy (simple subdivision
+ fine displacement, procedural stone material) is then baked onto the
faceted practical mesh -- albedo / roughness / normal / AO packed as
ORM -- exactly like the building route. The comparison renders show
(a) the noise-only way (the PoC), (b) the faceted high model in grey,
(c) the exported GLBs re-imported.

Each rock is described by parameters only (seed, radius, cuts, facet
bevel, squash). All variation is derived here from the seed with
random.Random; the Blender side (ai_loop/blender/rock_lib.py) only
constructs what it is given, so the same spec always builds the same
rocks.

--check validates and derives the spec without running Blender and
prints the derived rocks as JSON.

Usage: ai_loop/uzu.py rock --spec spec.json [--out DIR] [--check]
       [--blender PATH] [--timeout 1800] [--tex-size 2048] [--samples 48]
Prints one JSON line {"ok": bool, ...}. Standard library only.
"""

import argparse
import json
import math
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN_PY = os.path.join(HERE, "blender", "rock_main.py")

# Agent capability declaration (BACKLOG B-404), read by ai_loop/make_catalog.py:
# this tool shells out to a Blender executable, which the engine cannot
# provide itself.
CAPABILITIES = {
    "required_capabilities": ["blender"],
    "fallback": "Without Blender, place a ready-made GLB, or scatter the engine's built-in rocks instead (UZUWorldGen nature maps place rocks from the spec).",
}

NAME_RE = re.compile(r"^[A-Za-z0-9_-]+$")


def _num(raw, key, default, lo, hi):
    value = raw.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key} must be a number, got {value!r}")
    value = float(value)
    if not lo <= value <= hi:
        raise ValueError(f"{key} must be within [{lo}, {hi}], got {value}")
    return value


def _unit(rng):
    """A random unit vector (gaussian sampling, normalized)."""
    while True:
        v = [rng.gauss(0.0, 1.0) for _ in range(3)]
        n = math.sqrt(sum(c * c for c in v))
        if n > 1e-6:
            return [round(c / n, 4) for c in v]


def derive_rock(raw, index):
    """Validate one user rock entry and derive the full build spec:
    the cut planes (normal + distance from the centre) that facet the
    base sphere, the proportions, and the noise offset."""
    if not isinstance(raw, dict):
        raise ValueError(f"rocks[{index}] must be an object")
    name = raw.get("name", f"rock_{index + 1}")
    if not isinstance(name, str) or not NAME_RE.match(name):
        raise ValueError(f"rocks[{index}].name must match [A-Za-z0-9_-]+, got {name!r}")
    seed = raw.get("seed", 1)
    if isinstance(seed, bool) or not isinstance(seed, int):
        raise ValueError(f"{name}.seed must be an integer, got {seed!r}")
    radius = _num(raw, "radius", 1.0, 0.2, 20.0)
    cuts_raw = raw.get("cuts", 7)
    if isinstance(cuts_raw, bool) or not isinstance(cuts_raw, int) or not 0 <= cuts_raw <= 24:
        raise ValueError(f"{name}.cuts must be an integer within [0, 24], got {cuts_raw!r}")
    bevel = _num(raw, "bevel", 0.03, 0.0, 0.2)
    rng = random.Random(seed)

    # proportions: a slightly squashed, uneven ellipsoid reads more
    # like a boulder than a ball does
    scale = [round(rng.uniform(0.85, 1.15), 3), round(rng.uniform(0.85, 1.15), 3),
             round(rng.uniform(0.62, 0.85), 3)]
    # one cut is (almost) always the ground cut: a flat bottom the rock
    # sits on, slightly below the centre
    planes = [{"normal": [0.0, 0.0, -1.0], "distance": round(radius * rng.uniform(0.25, 0.45), 3)}]
    for _ in range(cuts_raw):
        n = _unit(rng)
        if n[2] < -0.55:  # keep extra cuts off the underside
            n[2] = -n[2]
        planes.append({"normal": n,
                       "distance": round(radius * rng.uniform(0.45, 0.8), 3)})
    return {
        "name": name, "seed": seed, "radius": radius, "cuts": cuts_raw,
        "bevel": bevel, "scale": scale, "cut_planes": planes,
        "noise_offset": [round(rng.uniform(0, 50), 3) for _ in range(3)],
        "tint": [round(c, 4) for c in (rng.uniform(0.42, 0.52), rng.uniform(0.40, 0.48),
                                       rng.uniform(0.36, 0.44))],
    }


def derive_spec(raw):
    if not isinstance(raw, dict) or not isinstance(raw.get("rocks"), list) or not raw["rocks"]:
        raise ValueError("spec must be an object with a non-empty 'rocks' list")
    rocks = [derive_rock(r, i) for i, r in enumerate(raw["rocks"])]
    names = [r["name"] for r in rocks]
    if len(set(names)) != len(names):
        raise ValueError(f"rock names must be unique, got {names}")
    return {"rocks": rocks}


def run_rock(spec, outdir, blender, timeout, tex_size, samples):
    derived = derive_spec(spec)
    derived["texture_size"] = tex_size
    derived["render_samples"] = samples
    os.makedirs(outdir, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="uzu-rock-") as tmp:
        spec_path = os.path.join(tmp, "derived_spec.json")
        with open(spec_path, "w", encoding="utf-8") as fh:
            json.dump(derived, fh, ensure_ascii=False)
        command = [blender, "--background", "--factory-startup", "--disable-autoexec",
                   "--threads", "2", "--python-exit-code", "1",
                   "--python", MAIN_PY, "--", spec_path, os.path.abspath(outdir)]
        result = subprocess.run(command, capture_output=True, encoding="utf-8",
                                errors="replace", timeout=timeout)
        if result.returncode:
            tail = "\n".join((result.stdout + result.stderr).splitlines()[-18:])
            raise RuntimeError(f"Blender exited {result.returncode}:\n{tail}")
    stats_path = os.path.join(outdir, "stats.json")
    if not os.path.isfile(stats_path):
        raise RuntimeError("Blender finished without writing stats.json")
    with open(stats_path, encoding="utf-8") as fh:
        stats = json.load(fh)
    for entry in stats["rocks"]:
        entry["glb"] = os.path.join(os.path.abspath(outdir), entry["glb"])
    return {"ok": True, "command": "rock", "out": os.path.abspath(outdir),
            "rocks": stats["rocks"], "renders": stats["renders"],
            "layout": stats["layout"], "stats": stats_path}


def main(argv):
    parser = argparse.ArgumentParser(description="Procedural rocks (B-1219)")
    parser.add_argument("--spec", required=True, help="JSON spec file")
    parser.add_argument("--out", help="output directory (required unless --check)")
    parser.add_argument("--check", action="store_true",
                        help="validate and derive the spec, print it, do not run Blender")
    parser.add_argument("--blender", default=os.environ.get("BLENDER", "blender"))
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--tex-size", type=int, default=2048,
                        help="bake resolution (exported textures are capped at 1024)")
    parser.add_argument("--samples", type=int, default=48)
    args = parser.parse_args(argv)
    try:
        with open(args.spec, encoding="utf-8") as fh:
            spec = json.load(fh)
        if args.check:
            result = {"ok": True, "command": "rock", "check": True,
                      "rocks": derive_spec(spec)["rocks"]}
        else:
            if not args.out:
                parser.error("--out is required unless --check")
            blender = shutil.which(args.blender)
            if not blender:
                raise ValueError(f"Blender executable not found: {args.blender} (use --blender or $BLENDER)")
            result = run_rock(spec, args.out, blender, args.timeout, args.tex_size, args.samples)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        result = {"ok": False, "command": "rock", "error": str(error)}
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
