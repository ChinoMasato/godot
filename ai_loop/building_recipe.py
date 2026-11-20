#!/usr/bin/env python3
"""Procedural buildings with baked PBR texture sets (BACKLOG B-1219).

One command turns a short JSON spec into game-ready buildings:

    uzu.py building --spec street.json --out ai_loop/look_samples/b1219

Each building is described by parameters only (width, depth, floors,
roof: gable|hip|flat, wall: plaster|brick|wood, kind: house|shop, and a
seed). All variation is derived here from the seed with random.Random,
so the same spec always builds the same buildings; the Blender side
(ai_loop/blender/building_lib.py) only constructs what it is given.

Stage 2 (styles as data): what makes a building read as one regional /
period style -- proportions, opening sizes and rhythm, roof pitch and
eaves, tints, and the procedural-material parameters -- lives in
ai_loop/building_styles.json, not in code. A building entry may name a
"style" (default: "default", which reproduces the stage-1 recipe
exactly); any other style in the file is a partial override deep-merged
over "default", so a new style is a data edit, never a code change.
The fully resolved style travels inside the derived spec, so the
Blender side stays a pure constructor.

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
       [--blender PATH] [--styles building_styles.json] [--timeout 1800]
       [--tex-size 2048] [--samples 48]
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

STYLES_PATH = os.path.join(HERE, "building_styles.json")
_STYLE_SECTIONS = ("geometry", "materials")


def load_styles(path=None):
    """The style definitions (ai_loop/building_styles.json by default).

    Returns the parsed document. Raises ValueError on a malformed file
    or a document without a "default" style: the default style is the
    base every other style is merged over, so it must be complete.
    """
    path = path or STYLES_PATH
    with open(path, encoding="utf-8") as fh:
        doc = json.load(fh)
    if not isinstance(doc, dict) or not isinstance(doc.get("styles"), dict):
        raise ValueError(f"{path}: expected an object with a 'styles' object")
    if "default" not in doc["styles"]:
        raise ValueError(f"{path}: styles must define 'default'")
    return doc


def _check_style_keys(style_name, override, base, where):
    """Reject override keys the default style does not define (typos)."""
    if not isinstance(override, dict):
        raise ValueError(f"style {style_name!r}: {where} must be an object")
    for key, value in override.items():
        if key not in base:
            raise ValueError(f"style {style_name!r}: unknown key {where}.{key} "
                             "(styles may only override keys the default style defines)")
        if isinstance(value, dict):
            _check_style_keys(style_name, value, base[key], f"{where}.{key}")


def _deep_merge(base, override):
    out = dict(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = _deep_merge(out[key], value)
        else:
            out[key] = value
    return out


def resolve_style(doc, name):
    """The full style `name`: the default style with the named style's
    overrides deep-merged over it. Raises ValueError for unknown names
    and unknown override keys."""
    styles = doc["styles"]
    if name not in styles:
        raise ValueError(f"unknown style {name!r}; defined styles: {sorted(styles)}")
    base = styles["default"]
    override = styles[name]
    if not isinstance(override, dict):
        raise ValueError(f"style {name!r} must be an object")
    for key in override:
        if key != "about" and key not in _STYLE_SECTIONS:
            raise ValueError(f"style {name!r}: unknown section {key!r} "
                             f"(expected one of {_STYLE_SECTIONS})")
    resolved = {}
    for section in _STYLE_SECTIONS:
        over = override.get(section, {})
        _check_style_keys(name, over, base[section], section)
        resolved[section] = _deep_merge(base[section], over)
    return resolved


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


def _slots(length, rng, geo):
    """Window centres along a wall, spread between the corner margins."""
    win = geo["window"]
    usable = length - 2 * win["slot_margin"]
    if usable < 0.8:
        return [0.0]
    n = max(1, round(usable / win["slot_spacing"]))
    if n == 1:
        return [0.0]
    jitter = min(0.12, usable / n * 0.15)
    return [round(-usable / 2 + usable * i / (n - 1) + rng.uniform(-jitter, jitter), 3)
            for i in range(n)]


def _windows(length, floors, fh, base, rng, geo, skip=()):
    """Window openings for one wall: one row per floor. skip holds
    (floor, centre) pairs to leave out (e.g. where the door is)."""
    win = geo["window"]
    out = []
    for fl in range(floors):
        for x in _slots(length, rng, geo):
            if any(fl == f and abs(x - c) < win["skip_distance"] for f, c in skip):
                continue
            out.append({"x": x, "z": round(base + fl * fh + win["sill_offset"], 3),
                        "w": win["w"], "h": win["h"], "type": "window"})
    return out


def derive_building(raw, index, styles_doc=None):
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
    style_name = raw.get("style", "default")
    if not isinstance(style_name, str):
        raise ValueError(f"{name}.style must be a string, got {style_name!r}")
    style = resolve_style(styles_doc or load_styles(), style_name)
    geo = style["geometry"]
    width = _num(raw, "width", 6.5, 3.0, 24.0)
    depth = _num(raw, "depth", 5.5, 3.0, 24.0)
    fh = _num(raw, "floor_height", geo["floor_height"], 2.4, 3.6)
    floors_raw = raw.get("floors", 2)
    if isinstance(floors_raw, bool) or not isinstance(floors_raw, int) or not 1 <= floors_raw <= 6:
        raise ValueError(f"{name}.floors must be an integer within [1, 6], got {floors_raw!r}")
    floors = floors_raw
    seed = raw.get("seed", 1)
    if isinstance(seed, bool) or not isinstance(seed, int):
        raise ValueError(f"{name}.seed must be an integer, got {seed!r}")
    rng = random.Random(seed)

    base = geo["foundation_height"]
    wall_t = geo["wall_thickness"]
    tints = geo["tints"]
    wall_tint = _tint(tints["wall"][wall], rng, tints["spread"])
    if roof == "flat":
        roof_tint = _tint(tints["flat_roof"], rng, tints["flat_spread"])
        roof_rise = 0.0
        eaves = geo["roof"]["eaves_flat"]
    else:
        roof_tint = _tint(tints["roof"][rng.choice(sorted(tints["roof"]))], rng, tints["spread"])
        roof_rise = round(min(width, depth) * rng.uniform(geo["roof"]["rise_ratio_min"],
                                                          geo["roof"]["rise_ratio_max"]), 3)
        eaves = geo["roof"]["eaves_sloped"]
    chimney = roof != "flat" and rng.random() < geo["chimney"]["probability"]
    chimney_x = round(rng.uniform(-0.25, 0.25) * max(width, depth), 3)

    door_w, door_h = geo["door"]["w"], geo["door"]["h"]
    openings = {"front": [], "back": [], "left": [], "right": []}
    front_slots = _slots(width, rng, geo)
    if kind == "shop":
        sg = geo["shop"]
        glass_w = round(min(width * sg["glass_width_ratio"], width - sg["glass_min_margin"]), 3)
        glass_x = round(width * sg["glass_x_ratio"], 3)
        door_x = round(width / 2 - door_w, 3)
        openings["front"].append({"x": glass_x, "z": base + sg["glass_z_offset"],
                                  "w": glass_w, "h": sg["glass_h"], "type": "shop_glass"})
        openings["front"].append({"x": door_x, "z": base, "w": door_w, "h": door_h, "type": "door"})
        for fl in range(1, floors):
            for x in front_slots:
                openings["front"].append({"x": x, "z": round(base + fl * fh + geo["window"]["sill_offset"], 3),
                                          "w": geo["window"]["w"], "h": geo["window"]["h"],
                                          "type": "window"})
    else:
        door_slot = min(range(len(front_slots)),
                        key=lambda i: abs(front_slots[i] - rng.uniform(-width / 4, width / 4)))
        door_x = front_slots[door_slot]
        openings["front"].append({"x": door_x, "z": base, "w": door_w, "h": door_h, "type": "door"})
        openings["front"].extend(_windows(width, floors, fh, base, rng, geo, skip=((0, door_x),)))
    openings["back"].extend(_windows(width, floors, fh, base, rng, geo))
    openings["left"].extend(_windows(depth, floors, fh, base, rng, geo))
    openings["right"].extend(_windows(depth, floors, fh, base, rng, geo))

    return {
        "name": name, "kind": kind, "wall": wall, "roof": roof,
        "width": width, "depth": depth, "floors": floors, "floor_height": fh,
        "seed": seed, "wall_thickness": wall_t, "foundation_height": base,
        "wall_tint": wall_tint, "roof_tint": roof_tint, "roof_rise": roof_rise,
        "eaves": eaves, "chimney": chimney, "chimney_x": chimney_x,
        "noise_offset": [round(rng.uniform(0, 50), 3) for _ in range(3)],
        "openings": openings,
        "style_name": style_name, "style": style,
    }


def derive_spec(raw, styles_doc=None):
    if not isinstance(raw, dict) or not isinstance(raw.get("buildings"), list) or not raw["buildings"]:
        raise ValueError("spec must be an object with a non-empty 'buildings' list")
    doc = styles_doc or load_styles()
    buildings = [derive_building(b, i, doc) for i, b in enumerate(raw["buildings"])]
    names = [b["name"] for b in buildings]
    if len(set(names)) != len(names):
        raise ValueError(f"building names must be unique, got {names}")
    return {"buildings": buildings}


def run_building(spec, outdir, blender, timeout, tex_size, samples, styles_doc=None):
    derived = derive_spec(spec, styles_doc)
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
    parser.add_argument("--styles", help="style definitions JSON "
                        "(default: ai_loop/building_styles.json)")
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--tex-size", type=int, default=2048,
                        help="bake resolution (exported textures are capped at 1024)")
    parser.add_argument("--samples", type=int, default=48)
    args = parser.parse_args(argv)
    try:
        with open(args.spec, encoding="utf-8") as fh:
            spec = json.load(fh)
        styles_doc = load_styles(args.styles)
        if args.check:
            result = {"ok": True, "command": "building", "check": True,
                      "buildings": derive_spec(spec, styles_doc)["buildings"]}
        else:
            if not args.out:
                parser.error("--out is required unless --check")
            blender = shutil.which(args.blender)
            if not blender:
                raise ValueError(f"Blender executable not found: {args.blender} (use --blender or $BLENDER)")
            result = run_building(spec, args.out, blender, args.timeout, args.tex_size,
                                  args.samples, styles_doc)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        result = {"ok": False, "command": "building", "error": str(error)}
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
