"""Render paired B-720 wind and B-719 light-shaft snapshots on a real GPU.

python effects_gpu.py OUT --godot path/to/editor.console.exe [--frames 180]
Requires Pillow for PNG comparisons. Produces JSON, logs and a contact sheet.
Only the test copy of the wind shader has TIME replaced by a fixed uniform.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageStat


def difference(first, second):
    with Image.open(first) as left, Image.open(second) as right:
        if left.size != right.size:
            raise ValueError("paired image sizes differ")
        delta = ImageChops.difference(left.convert("RGB"), right.convert("RGB"))
        changed = sum(max(pixel) > 8 for pixel in delta.getdata())
        return {"mean_rgb_difference": sum(ImageStat.Stat(delta).mean) / 3,
                "changed_percent_over_8": 100 * changed / (delta.width * delta.height)}


def run(args):
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    binary = Path(args.godot).resolve()
    # The Windows console launcher spawns the real editor. Launch it directly so
    # timeouts and exit codes refer to the process doing the rendering.
    if os.name == "nt" and binary.name.endswith(".console.exe"):
        direct = binary.with_name(binary.name.replace(".console.exe", ".exe"))
        if direct.is_file():
            binary = direct
    script = Path(__file__).with_suffix(".gd").resolve()
    cases = [(f"wind_{strength}_{time}", "wind", strength, time)
             for strength in (0.0, 0.12, 0.8) for time in (0.4, 1.2)]
    cases += [("rays_off", "rays", 0.0, 0.4), ("rays_on", "rays", 1.0, 0.4)]
    cases += [("wind_live", "wind", 0.12, -1.0)]
    results = []
    # No imported assets are needed: fixtures use production APIs and procedural geometry.
    with tempfile.TemporaryDirectory(prefix="uzu_effects_") as project:
        Path(project, "project.godot").write_text(
            'config_version=5\n[rendering]\nrenderer/rendering_method="forward_plus"\n', encoding="utf-8")
        for name, kind, strength, time in cases:
            shot = out / f"{name}.png"
            # Remove a previous result to prevent stale images from passing a failed run.
            shot.unlink(missing_ok=True)
            (out / f"{name}_start.png").unlink(missing_ok=True)
            command = [str(binary), "--path", project, "--rendering-driver", "vulkan",
                       "--max-fps", "60", "--resolution", args.resolution, "--script", str(script), "--",
                       "--case", kind, "--strength", str(strength), "--time", str(time),
                       "--frames", str(args.frames), "--shot", str(shot)]
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, encoding="utf-8", errors="replace")
            try:
                stdout, stderr = process.communicate(timeout=120)
                log = stdout + stderr
                result = next((json.loads(line[len("UZU_EFFECTS "):]) for line in stdout.splitlines()
                               if line.startswith("UZU_EFFECTS ")), {})
                errors = [line for line in log.splitlines() if "ERROR" in line]
                valid = process.returncode == 0 and result.get("ok") and shot.is_file() and not errors
                results.append({**result, "name": name, "ok": bool(valid),
                                "exit_code": process.returncode, "errors": errors})
                (out / f"{name}.log").write_text(log, encoding="utf-8")
            except subprocess.TimeoutExpired:
                process.kill()
                stdout, stderr = process.communicate()
                (out / f"{name}.log").write_text(stdout + stderr, encoding="utf-8")
                results.append({"name": name, "ok": False, "error": "render timed out after 120s"})
            print(f"{name}: {'ok' if results[-1]['ok'] else 'FAILED'}", file=sys.stderr, flush=True)
            if not results[-1]["ok"]:
                break  # Preserve the first failure, especially GPU device loss; do not launch more renders.
    return summarize(results, out)


def summarize(results, out):
    valid = {result["name"] for result in results if result.get("ok")}
    pairs = {}
    for strength in (0.0, 0.12, 0.8):
        first, second = f"wind_{strength}_0.4", f"wind_{strength}_1.2"
        if {first, second} <= valid:
            pairs[f"wind_{strength}"] = difference(out / f"{first}.png", out / f"{second}.png")
    if {"rays_off", "rays_on"} <= valid:
        pairs["rays"] = difference(out / "rays_off.png", out / "rays_on.png")
    if "wind_live" in valid:
        pairs["wind_live"] = difference(out / "wind_live_start.png", out / "wind_live.png")
    def changed(name):
        return pairs[name]["changed_percent_over_8"]
    checks = {"all_renders_succeeded": len(results) == 9 and len(valid) == 9,
              "stationary_control": changed("wind_0.0") == 0 if "wind_0.0" in pairs else None,
              "wind_moves": changed("wind_0.12") > 0 if "wind_0.12" in pairs else None,
              "stronger_wind_changes_more_pixels": changed("wind_0.8") > changed("wind_0.12") if {"wind_0.8", "wind_0.12"} <= pairs.keys() else None,
              "rays_visible": changed("rays") > 0 if "rays" in pairs else None,
              "production_time_animates": changed("wind_live") > 0 if "wind_live" in pairs else None}
    if valid:
        sheet = Image.new("RGB", (960, ((len(results) + 1) // 2) * 294), "#121820")
        draw = ImageDraw.Draw(sheet)
        for index, result in enumerate(results):
            x, y = (index % 2) * 480, (index // 2) * 294
            if result["name"] in valid:
                with Image.open(result["image"]) as im:
                    im.thumbnail((480, 270))
                    sheet.paste(im, (x, y + 24))
            label = result["name"] + ("" if result.get("ok") else " FAILED (see log)")
            draw.text((x + 8, y + 5), label, fill="white")
        sheet.save(out / "comparison.png")
    report = {"ok": all(value is True for value in checks.values()), "results": results, "pairs": pairs, "checks": checks,
              "scope": "Pinned shader time in a test copy; timing includes no U.ZU post effects. Null checks were not completed."}
    (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out")
    parser.add_argument("--godot", default=os.environ.get("GODOT", "bin/godot.windows.editor.x86_64.console.exe"))
    parser.add_argument("--analyze-only", action="store_true", help="Re-score the saved report without starting the GPU")
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--resolution", default="1280x720")
    args = parser.parse_args()
    if args.frames < 1:
        parser.error("--frames must be positive")
    try:
        if args.analyze_only:
            out = Path(args.out).resolve()
            saved = json.loads((out / "report.json").read_text(encoding="utf-8"))
            report = summarize(saved["results"], out)
        else:
            report = run(args)
    except (OSError, ValueError) as error:
        report = {"ok": False, "error": str(error)}
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
