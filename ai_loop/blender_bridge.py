#!/usr/bin/env python3
"""Produce a Godot-ready GLB and PNG from a trusted Blender Python script."""

import argparse
import json
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

DRIVER = Path(__file__).resolve().with_name("blender") / "export_asset.py"

# Agent capability declaration (BACKLOG B-404), read by ai_loop/make_catalog.py:
# this bridge shells out to a Blender executable, which the engine cannot
# provide itself.
CAPABILITIES = {
    "required_capabilities": ["blender"],
    "fallback": "Without Blender, use a ready-made GLB/PNG, or build the model inside the engine instead (uzu.py build from a JSON spec, or engine add/set on a scene).",
}


def ensure_outputs(folder):
    glb = folder / "model.glb"
    with glb.open("rb") as file:
        header = file.read(12)
    if len(header) != 12 or struct.unpack("<4sII", header) != (b"glTF", 2, glb.stat().st_size):
        raise ValueError("Blender did not produce a complete GLB 2.0 file")
    png = folder / "preview.png"
    with png.open("rb") as file:
        if file.read(8) != b"\x89PNG\r\n\x1a\n" or png.stat().st_size <= 24:
            raise ValueError("Blender did not produce a PNG preview")


def bridge(args):
    project = args.project.resolve()
    if not (project / "project.godot").is_file():
        raise ValueError("project directory must contain project.godot")
    raw = args.out
    if raw.is_absolute() or raw.drive or ".." in raw.parts or raw.suffix.lower() != ".glb":
        raise ValueError("--out must be a .glb path relative to the project")
    destination = project / raw
    if not destination.parent.resolve().is_relative_to(project):
        raise ValueError("--out must stay within the project directory")
    preview = destination.with_suffix(".preview.png")
    if any(os.path.lexists(path) for path in (destination, preview)):
        raise ValueError("GLB or preview already exists; choose a new --out path")
    script = args.script.resolve(strict=True)
    if not script.is_file():
        raise ValueError("--script must be a Blender Python file")
    blender = shutil.which(args.blender)
    if not blender:
        raise ValueError("Blender executable not found; use --blender")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".uzu-blender-", dir=destination.parent) as temporary:
        staging = Path(temporary)
        command = [
            blender,
            "--background",
            "--factory-startup",
            "--disable-autoexec",
            "--threads",
            str(args.threads),
            "--python-exit-code",
            "1",
            "--python-expr",
            "import bpy; bpy.ops.wm.read_factory_settings(use_empty=True)",
            "--python",
            str(script),
            "--python",
            str(DRIVER),
            "--",
            str(staging),
        ]
        result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", timeout=args.timeout)
        if result.returncode:
            raise RuntimeError(
                f"Blender exited {result.returncode}:\n" + "\n".join((result.stdout + result.stderr).splitlines()[-18:])
            )
        ensure_outputs(staging)
        # Link the complete files into place without overwriting a concurrent export.
        os.link(staging / "model.glb", destination)
        try:
            os.link(staging / "preview.png", preview)
        except OSError:
            if destination.exists() and destination.samefile(staging / "model.glb"):
                destination.unlink()
            raise
    return {
        "ok": True,
        "glb": "res://" + destination.relative_to(project).as_posix(),
        "preview": "res://" + preview.relative_to(project).as_posix(),
        "glb_bytes": destination.stat().st_size,
        "preview_bytes": preview.stat().st_size,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--script", type=Path, required=True, help="Trusted Python file that builds the Blender scene")
    parser.add_argument("--out", type=Path, required=True, help="Project-relative .glb path")
    parser.add_argument("--blender", default=os.environ.get("BLENDER", "blender"), help="Blender executable")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=300, help="Seconds allowed for Blender")
    args = parser.parse_args()
    if args.threads < 1 or args.timeout < 1:
        parser.error("--threads and --timeout must be positive")
    try:
        report = bridge(args)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report = {"ok": False, "error": str(error)}
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
