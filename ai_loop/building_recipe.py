#!/usr/bin/env python3
"""Procedural buildings with baked PBR texture sets (BACKLOG B-1219, stage 1).

One command turns a short JSON spec into game-ready buildings:

    uzu.py building --spec street.json --out ai_loop/look_samples/b1219

Each building is described by parameters only (width, depth, floors,
roof: gable|hip|flat, wall: plaster|brick|wood, kind: house|shop, and a
seed). All variation is derived here from the seed with random.Random,
so the same spec always builds the same buildings; the Blender side
(ai_loop/blender/building_lib.py) only constructs what it is given.

The recipe builds real geometry -- segment walls with actual openings,
window frames and mullions, a stepped foundation, eaves -- makes a
high-density copy (subdivision + displacement, procedural materials),
and bakes its albedo / roughness / normal onto the practical building,
which is exported as one GLB per building. The result reports, per
building, the triangle counts before and after the bake-down and the
bake miss rate, plus a street scene rendered three ways for comparison:
(a) houses in the current UZUWorldGen style, (b) the recipe buildings
in plain grey, (c) the exported GLBs re-imported with their baked sets.

--check validates and derives the spec without running Blender and
prints the derived buildings as JSON.

Usage: ai_loop/uzu.py building --spec spec.json [--out DIR] [--check]
       [--blender PATH] [--timeout 1800] [--tex-size 2048] [--samples 48]
Prints one JSON line {"ok": bool, ...}. Standard library only.
"""

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN_PY = os.path.join(HERE, "blender", "building_main.py")

# Agent capability declaration (BACKLOG B-404), read by ai_loop/make_catalog.py:
# this tool shells out to a Blender executable, which the engine cannot
# provide itself.
CAPABILITIES = {
    "required_capabilities": ["blender"],
    "fallback": "Without Blender, place a ready-made GLB, or build the building inside the engine instead (uzu.py build from a JSON spec, or UZUWorldGen's built-in houses).",
}

WALLS = ("plaster", "brick", "wood")
ROOFS = ("gable", "hip", "flat")
KINDS = ("house", "shop")
NAME_RE = re.compile(r"^[A-Za-z0-9_-]+$")

WALL_TINTS = {"plaster": (0.93, 0.89, 0.82), "brick": (0.62, 0.32, 0.24), "wood": (0.45, 0.33, 0.22)}
ROOF_TINTS = {"slate": (0.32, 0.35, 0.42), "terracotta": (0.55, 0.30, 0.22), "moss": (0.36, 0.38, 0.30)}


def _num(raw, key, default, lo, hi):
    value = raw.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key} must be a number, got {value!r}")
    value = float(value)
    if not lo <= value <= hi:
        raise ValueError(f"{key} must be within [{lo}, {hi}], got {value}")
    return value


def _tint(base, rng, spread=0.06):
    return [round(max(0.0, min(1.0, c * rng.uniform(1.0 - spread, 1.0 + spread))), 4) for c in base]


def _slots(length, rng, margin=1.05, spacing=1.9):
    """Window centres along a wall, spread between the corner margins."""
    usable = length - 2 * margin
    if usable < 0.8:
        return [0.0]
    n = max(1, round(usable / spacing))
    if n == 1:
        return [0.0]
    jitter = min(0.12, usable / n * 0.15)
    return [round(-usable / 2 + usable * i / (n - 1) + rng.uniform(-jitter, jitter), 3)
            for i in range(n)]


def _windows(length, floors, fh, base, rng, skip=()):
    """Window openings for one wall: one row per floor. skip holds
    (floor, centre) pairs to leave out (e.g. where the door is)."""
    out = []
    for fl in range(floors):
        for x in _slots(length, rng):
            if any(fl == f and abs(x - c) < 0.95 for f, c in skip):
                continue
            out.append({"x": x, "z": round(base + fl * fh + 0.9, 3),
                        "w": 0.9, "h": 1.3, "type": "window"})
    return out


def derive_building(raw, index):
    """Validate one user building entry and derive the full build spec."""
    if not isinstance(raw, dict):
        raise ValueError(f"buildings[{index}] must be an object")
    name = raw.get("name", f"building_{index + 1}")
    if not isinstance(name, str) or not NAME_RE.match(name):
        raise ValueError(f"buildings[{index}].name must match [A-Za-z0-9_-]+, got {name!r}")
    kind = raw.get("kind", "house")
    if kind not in KINDS:
        raise ValueError(f"{name}.kind must be one of {KINDS}, got {kind!r}")
    wall = raw.get("wall", "plaster")
    if wall not in WALLS:
        raise ValueError(f"{name}.wall must be one of {WALLS}, got {wall!r}")
    roof = raw.get("roof", "gable")
    if roof not in ROOFS:
        raise ValueError(f"{name}.roof must be one of {ROOFS}, got {roof!r}")
    width = _num(raw, "width", 6.5, 3.0, 24.0)
    depth = _num(raw, "depth", 5.5, 3.0, 24.0)
    fh = _num(raw, "floor_height", 2.8, 2.4, 3.6)
    floors_raw = raw.get("floors", 2)
    if isinstance(floors_raw, bool) or not isinstance(floors_raw, int) or not 1 <= floors_raw <= 6:
        raise ValueError(f"{name}.floors must be an integer within [1, 6], got {floors_raw!r}")
    floors = floors_raw
    seed = raw.get("seed", 1)
    if isinstance(seed, bool) or not isinstance(seed, int):
        raise ValueError(f"{name}.seed must be an integer, got {seed!r}")
    rng = random.Random(seed)

    base = 0.55            # foundation height
    wall_t = 0.18          # wall thickness
    wall_tint = _tint(WALL_TINTS[wall], rng)
    if roof == "flat":
        roof_tint = _tint((0.25, 0.25, 0.26), rng, 0.04)
        roof_rise = 0.0
        eaves = 0.13
    else:
        roof_tint = _tint(ROOF_TINTS[rng.choice(sorted(ROOF_TINTS))], rng)
        roof_rise = round(min(width, depth) * rng.uniform(0.34, 0.42), 3)
        eaves = 0.45
    chimney = roof != "flat" and rng.random() < 0.65
    chimney_x = round(rng.uniform(-0.25, 0.25) * max(width, depth), 3)

    openings = {"front": [], "back": [], "left": [], "right": []}
    front_slots = _slots(width, rng)
    if kind == "shop":
        glass_w = round(min(width * 0.56, width - 2.4), 3)
        glass_x = round(-width * 0.14, 3)
        door_x = round(width / 2 - 0.95, 3)
        openings["front"].append({"x": glass_x, "z": base + 0.08, "w": glass_w,
                                  "h": 2.25, "type": "shop_glass"})
        openings["front"].append({"x": door_x, "z": base, "w": 0.95, "h": 2.1, "type": "door"})
        for fl in range(1, floors):
            for x in front_slots:
                openings["front"].append({"x": x, "z": round(base + fl * fh + 0.9, 3),
                                          "w": 0.9, "h": 1.3, "type": "window"})
    else:
        door_slot = min(range(len(front_slots)),
                        key=lambda i: abs(front_slots[i] - rng.uniform(-width / 4, width / 4)))
        door_x = front_slots[door_slot]
        openings["front"].append({"x": door_x, "z": base, "w": 0.95, "h": 2.1, "type": "door"})
        openings["front"].extend(_windows(width, floors, fh, base, rng, skip=((0, door_x),)))
    openings["back"].extend(_windows(width, floors, fh, base, rng))
    openings["left"].extend(_windows(depth, floors, fh, base, rng))
    openings["right"].extend(_windows(depth, floors, fh, base, rng))

    return {
        "name": name, "kind": kind, "wall": wall, "roof": roof,
        "width": width, "depth": depth, "floors": floors, "floor_height": fh,
        "seed": seed, "wall_thickness": wall_t, "foundation_height": base,
        "wall_tint": wall_tint, "roof_tint": roof_tint, "roof_rise": roof_rise,
        "eaves": eaves, "chimney": chimney, "chimney_x": chimney_x,
        "noise_offset": [round(rng.uniform(0, 50), 3) for _ in range(3)],
        "openings": openings,
    }


def derive_spec(raw):
    if not isinstance(raw, dict) or not isinstance(raw.get("buildings"), list) or not raw["buildings"]:
        raise ValueError("spec must be an object with a non-empty 'buildings' list")
    buildings = [derive_building(b, i) for i, b in enumerate(raw["buildings"])]
    names = [b["name"] for b in buildings]
    if len(set(names)) != len(names):
        raise ValueError(f"building names must be unique, got {names}")
    return {"buildings": buildings}


def run_building(spec, outdir, blender, timeout, tex_size, samples):
    derived = derive_spec(spec)
    derived["texture_size"] = tex_size
    derived["render_samples"] = samples
    os.makedirs(outdir, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="uzu-building-") as tmp:
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
    for entry in stats["buildings"]:
        entry["glb"] = os.path.join(os.path.abspath(outdir), entry["glb"])
    return {"ok": True, "command": "building", "out": os.path.abspath(outdir),
            "buildings": stats["buildings"], "renders": stats["renders"],
            "layout": stats["layout"], "stats": stats_path}


def main(argv):
    parser = argparse.ArgumentParser(description="Procedural buildings (B-1219)")
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
            result = {"ok": True, "command": "building", "check": True,
                      "buildings": derive_spec(spec)["buildings"]}
        else:
            if not args.out:
                parser.error("--out is required unless --check")
            blender = shutil.which(args.blender)
            if not blender:
                raise ValueError(f"Blender executable not found: {args.blender} (use --blender or $BLENDER)")
            result = run_building(spec, args.out, blender, args.timeout, args.tex_size, args.samples)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        result = {"ok": False, "command": "building", "error": str(error)}
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
