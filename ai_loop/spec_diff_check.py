#!/usr/bin/env python3
"""End-to-end check for B-1223 (spec-image diff verification), run by hand:

    GODOT=<editor binary> python3 ai_loop/spec_diff_check.py

Copies the B-1221 sample project (ai_loop/samples/image_scene/, scaffold
already generated) to temp dirs and asserts the BACKLOG judgement with the
real engine in the loop:
  * the untouched scaffold passes spec-diff (ok),
  * with one card removed from the spec and its picture region painted
    over in the backdrop, spec-diff fails, the erased card is the one
    that fails, and every other card still passes,
  * with the scene's textures channel-swapped, the colour metric fails.
Prints one JSON line {"ok": bool, ...} with the measured numbers.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE = os.path.join(HERE, "samples", "image_scene")
sys.path.insert(0, HERE)
import image_scene  # noqa: E402

ERASE_CARD = "Card_Midground_3"  # The lamp post: small, textured, distinct.


def run_uzu(args):
    p = subprocess.run([sys.executable, os.path.join(HERE, "uzu.py")] + args,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1800)
    text = p.stdout.decode("utf-8", errors="replace")
    line = next((l for l in reversed(text.splitlines()) if l.startswith("{")), "")
    try:
        return json.loads(line)
    except ValueError:
        print(text[-3000:], file=sys.stderr)
        raise


def run_godot_import(project):
    godot = os.environ.get("GODOT")
    if not godot:
        raise SystemExit("set GODOT to an editor binary")
    p = subprocess.run([godot, "--headless", "--path", project, "--import"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
    if p.returncode != 0:
        print(p.stdout.decode("utf-8", errors="replace")[-2000:], file=sys.stderr)
        raise SystemExit("import failed")


def copy_sample(tmp, name):
    project = os.path.join(tmp, name)
    shutil.copytree(SAMPLE, project, ignore=shutil.ignore_patterns("__pycache__"))
    run_godot_import(project)
    return project


def spec_diff(project):
    return run_uzu(["spec-diff", project, "--spec-image",
                    os.path.join(project, "illustration.png")])


def paint_over(path, bbox, scale):
    """Erase a box from a PNG by continuing each row's edge colours across
    it: what the backdrop would plausibly show if the element had never
    been painted there."""
    w, h, rgba = image_scene.load_rgba(path)
    x0, y0, x1, y1 = [int(round(v * scale)) for v in bbox]
    for y in range(y0, y1):
        li = (y * w + max(x0 - 2, 0)) * 4
        ri = (y * w + min(x1 + 1, w - 1)) * 4
        left = rgba[li:li + 3]
        right = rgba[ri:ri + 3]
        for x in range(x0, x1):
            t = (x - x0) / max(x1 - x0 - 1, 1)
            i = (y * w + x) * 4
            for k in range(3):
                rgba[i + k] = int(left[k] * (1 - t) + right[k] * t)
    image_scene.write_png(path, w, h, rgba)


def swap_channels(path):
    w, h, rgba = image_scene.load_rgba(path)
    for p in range(w * h):
        i = p * 4
        rgba[i], rgba[i + 2] = rgba[i + 2], rgba[i]
    image_scene.write_png(path, w, h, rgba)


def main():
    checks = {}
    with tempfile.TemporaryDirectory() as tmp:
        # Baseline: the scaffold as generated passes.
        project = copy_sample(tmp, "base")
        base = spec_diff(project)
        checks["baseline_ok"] = bool(base.get("ok"))
        checks["baseline_metrics"] = base.get("metrics")

        # Erase one element: out of the spec, and painted out of the
        # backdrop (in this scaffold an element lives in both places).
        project = copy_sample(tmp, "erased")
        gen_dir = os.path.join(project, "image_scene")
        with open(os.path.join(gen_dir, "report.json"), encoding="utf-8") as f:
            report = json.load(f)
        card = next(c for c in report["layers"]["cards"] if c["name"] == ERASE_CARD)
        spec_path = os.path.join(gen_dir, "spec.json")
        with open(spec_path, encoding="utf-8") as f:
            spec = json.load(f)
        spec["cards"] = [c for c in spec["cards"] if c["name"] != ERASE_CARD]
        with open(spec_path, "w", encoding="utf-8") as f:
            json.dump(spec, f)
        iw, ih = report["image"]["width"], report["image"]["height"]
        ww = report["image"]["working_size"][0]
        paint_over(os.path.join(gen_dir, "background.png"), card["image_bbox"], iw / ww)
        run_godot_import(project)
        rebuilt = run_uzu(["build", project, "--spec", spec_path, "--no-report"])
        checks["rebuild_ok"] = bool(rebuilt.get("ok"))
        erased = spec_diff(project)
        by_id = {e["id"]: e for e in erased.get("elements", [])}
        checks["erased_ok"] = bool(erased.get("ok"))
        checks["erased_card_fails"] = not by_id[ERASE_CARD]["pass"]
        others = [e for e in erased.get("elements", [])
                  if e["kind"] == "card" and e["id"] != ERASE_CARD]
        checks["other_cards_pass"] = all(e["pass"] for e in others)
        failing_boxes = sorted({tuple(e["bbox"]) for e in erased.get("elements", [])
                                if not e["pass"]})
        checks["failing_boxes"] = failing_boxes
        checks["erased_box"] = card["image_bbox"]
        checks["only_erased_box_fails"] = failing_boxes == [tuple(card["image_bbox"])]
        checks["erased_metrics"] = erased.get("metrics")

        # Colour variant: every scene texture channel-swapped.
        project = copy_sample(tmp, "colour")
        gen_dir = os.path.join(project, "image_scene")
        for name in os.listdir(gen_dir):
            if name.endswith(".png"):
                swap_channels(os.path.join(gen_dir, name))
        run_godot_import(project)
        colour = spec_diff(project)
        checks["colour_ok"] = bool(colour.get("ok"))
        checks["colour_metric_fails"] = not colour.get("metrics", {}) \
            .get("color", {}).get("pass", True)
        checks["colour_metrics"] = colour.get("metrics")

    checks["ok"] = all(checks[k] for k in ("baseline_ok", "rebuild_ok",
                                           "erased_card_fails", "other_cards_pass",
                                           "only_erased_box_fails", "colour_metric_fails")) \
        and not checks["erased_ok"] and not checks["colour_ok"]
    print(json.dumps({"command": "spec-diff-check", **checks}, ensure_ascii=False))
    return 0 if checks["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
