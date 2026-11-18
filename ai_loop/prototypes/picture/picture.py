#!/usr/bin/env python3
"""U.ZU Picture agent CLI. JSON stdout; exit 0 only on success.

python picture.py PROJECT doctor|analyze|validate|schema|snap|adopt [options]
python picture.py PROJECT shot --out DIR [--seconds 0,4,8] [--fps 30]
python picture.py PROJECT pbr make|preview|compare|kinds [options]
python picture.py PROJECT map import|estimate [options]
GODOT selects the editor. UZU_PICTURE_TIMEOUT_SECONDS bounds each process (default 120).
"""

import importlib.util
import json
import math
import os
import shutil
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]


def find_binary():
    configured = os.environ.get("GODOT")
    if configured:
        candidate = Path(shutil.which(configured) or configured).resolve()
    else:
        names = [
            "godot.windows.editor.x86_64.exe",
            "godot.linuxbsd.editor.x86_64",
            "godot.macos.editor.arm64",
            "godot.macos.editor.x86_64",
        ]
        # Also support a standalone prototype copied next to the local godot checkout.
        roots = [REPO / "bin", HERE.parent / "godot" / "bin"]
        candidates = [folder / name for folder in roots for name in names]
        candidate = next((path for path in candidates if path.is_file()), candidates[0])
    if os.name == "nt" and candidate.name.endswith(".console.exe"):
        direct = candidate.with_name(candidate.name.replace(".console.exe", ".exe"))
        if direct.is_file():
            candidate = direct
    return str(candidate)


GODOT = find_binary()


def timeout_seconds():
    value = float(os.environ.get("UZU_PICTURE_TIMEOUT_SECONDS", "120"))
    if not math.isfinite(value) or value <= 0:
        raise ValueError("UZU_PICTURE_TIMEOUT_SECONDS must be finite and positive")
    return value


def run_process(command):
    timeout = timeout_seconds()
    try:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            start_new_session=os.name != "nt",
        )
    except OSError as error:
        return {"ok": False, "error": "could not start engine", "detail": str(error)}
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        # Estimators can be children of Godot. End only the tree this command started.
        cleanup_warning = None
        try:
            if os.name == "nt":
                killed = subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True, timeout=10
                )
                if killed.returncode != 0:
                    cleanup_warning = "process-tree termination did not succeed"
            else:
                os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        except (OSError, subprocess.SubprocessError) as error:
            cleanup_warning = str(error)
        finally:
            if process.poll() is None:
                process.kill()
        try:
            stdout, stderr = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            return {
                "ok": False,
                "error": "engine cleanup timed out",
                "timeout_seconds": timeout,
                "cleanup_warning": cleanup_warning or "a child process may still hold output pipes",
            }
        result = {
            "ok": False,
            "error": "engine timed out",
            "timeout_seconds": timeout,
            "log": (stdout + stderr)[-1500:],
        }
        if cleanup_warning:
            result["cleanup_warning"] = cleanup_warning
        return result
    errors = [line for line in (stdout + stderr).splitlines() if "ERROR:" in line]
    return {
        "ok": process.returncode == 0 and not errors,
        "exit_code": process.returncode,
        "stdout": stdout,
        "stderr": stderr,
        "errors": errors[:10],
    }


def failure(result, default):
    return {
        **result,
        "ok": False,
        "error": result.get("error", default),
        "log": result.get("log", (result.get("stdout", "") + result.get("stderr", ""))[-1500:]),
        "stdout": "",
        "stderr": "",
    }


def script_tool(project, script, args, gpu=False):
    command = [GODOT, "--path", str(project)]
    command += ["--rendering-driver", "vulkan", "--max-fps", "60"] if gpu else ["--headless"]
    result = run_process(command + ["--script", str(HERE / script), "--"] + args)
    lines = [line[11:] for line in result.get("stdout", "").splitlines() if line.startswith("UZU_RESULT ")]
    try:
        if len(lines) != 1:
            raise ValueError("expected exactly one UZU_RESULT line")
        parsed = json.loads(lines[0])
        if not isinstance(parsed, dict) or not isinstance(parsed.get("ok"), bool):
            raise ValueError("result must be an object with boolean ok")
        if not result["ok"]:
            return {
                **failure(result, "engine failed"),
                **parsed,
                "ok": False,
                "error": parsed.get("error", result.get("error", "engine failed")),
            }
        return parsed
    except (ValueError, TypeError) as error:
        if not result["ok"]:
            return failure(result, "engine failed")
        return {"ok": False, "error": "invalid engine response", "detail": str(error)}


def tool(project, args):
    return script_tool(project, "picture_tool.gd", args)


def opt(args, name, default):
    key = "--" + name
    if key not in args:
        return default
    index = args.index(key) + 1
    if index >= len(args) or args[index].startswith("--"):
        raise ValueError(f"{key} needs a value")
    return args[index]


def shot(project, args):
    fps = int(opt(args, "fps", "30"))
    secs = [float(value) for value in opt(args, "seconds", "0,4").split(",")]
    if fps <= 0 or not secs or any(not math.isfinite(value) or value < 0 for value in secs):
        raise ValueError("fps must be positive; seconds must be finite and non-negative")
    out = Path(opt(args, "out", "shots")).resolve()
    try:
        from PIL import Image
    except ImportError:
        return {"ok": False, "error": "shot needs Pillow to write motion.png"}
    validation = tool(project, ["validate"])
    if not validation.get("ok"):
        return {"ok": False, "command": "shot", "error": "spec validation failed", "validation": validation}
    out.mkdir(parents=True, exist_ok=True)
    indices = [int(value * fps) for value in secs]
    with tempfile.TemporaryDirectory(prefix="uzu_pic_") as work:
        result = run_process([
            GODOT,
            "--path",
            str(project),
            "--rendering-driver",
            "vulkan",
            "--write-movie",
            str(Path(work) / "f.png"),
            "--fixed-fps",
            str(fps),
            "--quit-after",
            str(max(indices) + 1),
        ])
        if not result["ok"]:
            return {**failure(result, "render failed"), "command": "shot"}
        requested = [Path(work) / f"f{index:08d}.png" for index in indices]
        missing = [index for index, path in zip(indices, requested) if not path.is_file()]
        if missing:
            return {
                "ok": False,
                "command": "shot",
                "error": "requested frames were not rendered",
                "missing_frames": missing,
            }
        images = []
        for path in requested:
            with Image.open(path) as image:
                images.append(image.convert("RGB"))
        if any(image.size != images[0].size for image in images):
            return {"ok": False, "error": "rendered image sizes differ"}
        motion = Image.blend(images[0], images[-1], 0.5)
        saved = []
        for second, image in zip(secs, images):
            path = out / f"t{second:g}s.png"
            image.save(path)
            saved.append(str(path))
        motion.save(out / "motion.png")
    return {
        "ok": True,
        "command": "shot",
        "images": saved,
        "motion": str(out / "motion.png"),
        "fps": fps,
        "frame_indices": indices,
        "errors": [],
    }


def pbr(project, args):
    preview = args and args[0] == "preview"
    return script_tool(
        project, "pbr_preview.gd" if preview else "pbr_tool.gd", args[1:] if preview else args, gpu=bool(preview)
    )


def doctor(project):
    scripts = ["picture_tool.gd", "map_tool.gd", "pbr_tool.gd", "pbr_preview.gd"]
    missing = [name for name in scripts if not (HERE / name).is_file()]
    result = {
        "ok": False,
        "command": "doctor",
        "binary": GODOT,
        "project": str(project),
        "project_found": (project / "project.godot").is_file(),
        "missing_scripts": missing,
        "pillow": importlib.util.find_spec("PIL") is not None,
        "timeout_seconds": timeout_seconds(),
    }
    if not Path(GODOT).is_file():
        return {**result, "error": "editor not found; set GODOT to your build"}
    version = run_process([GODOT, "--version"])
    result["version"] = version.get("stdout", "").strip()
    result["ok"] = version["ok"] and result["project_found"] and not missing
    if not version["ok"]:
        result["engine_error"] = failure(version, "engine failed")
    return result


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    try:
        if len(args) < 2:
            raise ValueError(__doc__)
        project, command, rest = Path(args[0]).resolve(), args[1], args[2:]
        if not (project / "project.godot").is_file() and command != "doctor":
            raise ValueError("project.godot not found in project folder")
        if command == "doctor":
            result = doctor(project)
        elif command == "map":
            result = script_tool(project, "map_tool.gd", rest)
        elif command == "pbr":
            result = pbr(project, rest)
        else:
            result = shot(project, rest) if command == "shot" else tool(project, [command] + rest)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        result = {"ok": False, "error": str(error)}
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result.get("ok") is True else 1


if __name__ == "__main__":
    sys.exit(main())
