#!/usr/bin/env python3
"""U.ZU Picture: one entry point for agents. Prints one JSON line; exit 0 on success.

  python picture.py <project> analyze|validate|schema|snap|adopt [options]   (passed to picture_tool.gd)
  python picture.py <project> shot --out DIR [--seconds 0,4,8] [--fps 30]
  python picture.py <project> pbr make|preview|compare|kinds ...      (pbr_tool.gd / pbr_preview.gd)
  python picture.py <project> map import|estimate ...               (map_tool.gd: outside normal/depth maps)

shot validates the spec first, renders on the GPU (Vulkan) and saves one PNG per requested second,
plus motion.png: the first and last shot blended, so near/far parallax and moving effects are visible
in one image. Environment: GODOT = editor console binary.
"""
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

GODOT = os.environ.get("GODOT", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "bin", "godot.windows.editor.x86_64.console.exe"))


def tool(project, args):
    r = subprocess.run([GODOT, "--headless", "--path", project, "--script", "res://picture_tool.gd", "--"] + args,
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    for line in r.stdout.splitlines():
        if line.startswith("UZU_RESULT "):
            return json.loads(line[11:])
    return {"ok": False, "error": "tool failed", "log": (r.stdout + r.stderr)[-1500:]}


def opt(args, name, default):
    return args[args.index("--" + name) + 1] if "--" + name in args else default


def shot(project, args):
    v = tool(project, ["validate"])
    if not v.get("ok"):
        return {"ok": False, "command": "shot", "error": "spec is invalid", "problems": v.get("problems")}
    out = os.path.abspath(opt(args, "out", "shots"))
    os.makedirs(out, exist_ok=True)
    fps = int(opt(args, "fps", "30"))
    secs = [float(s) for s in opt(args, "seconds", "0,4").split(",")]
    work = tempfile.mkdtemp(prefix="uzu_pic_")
    try:
        frames = int(max(secs) * fps) + 1
        r = subprocess.run([GODOT, "--path", project, "--rendering-driver", "vulkan", "--write-movie", os.path.join(work, "f.png"),
                            "--fixed-fps", str(fps), "--quit-after", str(frames)], capture_output=True, text=True, encoding="utf-8", errors="replace")
        errors = [l for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l]
        pngs = sorted(glob.glob(os.path.join(work, "*.png")))
        if not pngs:
            return {"ok": False, "command": "shot", "error": "nothing rendered", "errors": errors[:10]}
        saved = []
        for s in secs:
            i = min(int(s * fps), len(pngs) - 1)
            dst = os.path.join(out, f"t{s:g}s.png")
            shutil.copy(pngs[i], dst)
            saved.append(dst)
        try:
            from PIL import Image
            a, b = Image.open(saved[0]).convert("RGB"), Image.open(saved[-1]).convert("RGB")
            Image.blend(a, b, 0.5).save(os.path.join(out, "motion.png"))
        except ImportError:
            pass
        return {"ok": not errors, "command": "shot", "images": saved, "motion": os.path.join(out, "motion.png"), "errors": errors[:10]}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def pbr(project, args):
    """pbr make|compare|kinds ... → pbr_tool.gd;  pbr preview --dirs a,b --out res://x.png → GPU render."""
    if args and args[0] == "preview":
        r = subprocess.run([GODOT, "--path", project, "--rendering-driver", "vulkan", "--script", "res://pbr_preview.gd", "--"] + args[1:],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
    else:
        r = subprocess.run([GODOT, "--headless", "--path", project, "--script", "res://pbr_tool.gd", "--"] + args,
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
    for line in r.stdout.splitlines():
        if line.startswith("UZU_RESULT "):
            return json.loads(line[11:])
    return {"ok": False, "error": "pbr failed", "log": (r.stdout + r.stderr)[-1500:]}


def main():
    if len(sys.argv) < 3:
        print(json.dumps({"ok": False, "error": __doc__}))
        return 2
    project, cmd, rest = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3:]
    if cmd == "map":
        r = subprocess.run([GODOT, "--headless", "--path", project, "--script", "res://map_tool.gd", "--"] + rest,
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        res = next((json.loads(l[11:]) for l in r.stdout.splitlines() if l.startswith("UZU_RESULT ")),
                   {"ok": False, "error": "map failed", "log": (r.stdout + r.stderr)[-1500:]})
        print(json.dumps(res, ensure_ascii=False))
        return 0 if res.get("ok") else 1
    if cmd == "pbr":
        res = pbr(project, rest)
        print(json.dumps(res, ensure_ascii=False))
        return 0 if res.get("ok") else 1
    res = shot(project, rest) if cmd == "shot" else tool(project, [cmd] + rest)
    print(json.dumps(res, ensure_ascii=False))
    return 0 if res.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
