#!/usr/bin/env python3
"""U.ZU Engine agent tools: one cross-platform entry (BACKLOG B-801).

Runs on Linux, Windows and macOS with Python 3.8+ and the standard library.
The shell scripts (engine.sh, look.sh, selfplay.sh, screenshot.sh, publish.sh)
are thin wrappers around this file; on Windows call it directly:
    py ai_loop\\uzu.py engine my_game tree --scene res://main.tscn
or through ai_loop\\windows\\uzu.bat.

  uzu.py engine   <project> <command> [options]   scene, class and project operations (B-401, B-201..B-203, B-304)
  uzu.py look     <project> <command> [options]   the U.ZU look (B-7xx), shot / check / panorama
  uzu.py selfplay <project> --input replay.json [--frames 1800] [--seed N] [--out r.json] [--movie m.avi] [--log e.jsonl]
                  [--hash-every 30 [--compare earlier.json]]   state hashes for determinism checks (B-307)
                  [--packet DIR] [--expect-clear]   on failure, a diagnosis packet in DIR (B-308)
  uzu.py publish  <build | project> [options]     through the bridge API (B-505)
  uzu.py selftest [engine | selfplay | tools | all | bake]   (all = engine, selfplay, tools; bake runs the editor)

engine commands: run, check, validate, shot, import, export, templates, uids [--fix] (B-205), and every command of ai_loop/engine_tool.gd
(tree, props, get, set, add, instance, remove, reparent, call, connect, script, new,
classes, describe, actions, res-new, res-props, res-get, res-set, setting-get,
setting-set, action-add, action-remove, import-get, import-set, export-preset, presets);
also export (--preset, B-804) and templates. look commands: shot, check, panorama, and those of
ai_loop/look_tool.gd (apply, rig, lamp, camera, material, info), and bake (lightmaps, B-805:
--scene res://x.tscn [--node Path/To/LightmapGI] [--prepare]).
Every command prints one JSON line {"ok": bool, ...}; exit 0 on success.

Environment:
  GODOT                 editor binary (default: found in bin/ for this OS; on Windows the
                        .console.exe is preferred, since it keeps stdout/stdin)
  UZU_SOFTWARE_RENDER   1 forces software Vulkan on a virtual display (Linux; the default
                        when there is no DISPLAY). Elsewhere the real GPU is used.
  UZU_RENDERING_DRIVER  rendering driver for shots and movies (default: vulkan)
"""

import glob
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LAVAPIPE = "/usr/share/vulkan/icd.d/lvp_icd.json"


class ToolError(Exception):
    def __init__(self, message, code=1, **extra):
        super().__init__(message)
        self.message, self.code, self.extra = message, code, extra


# ---------------------------------------------------------------- helpers


def find_godot():
    """The editor binary: $GODOT, else the newest editor build in bin/ for this OS."""
    if os.environ.get("GODOT"):
        return os.environ["GODOT"]
    system = platform.system()
    if system == "Windows":
        patterns = ["godot.windows.editor*.console.exe", "godot.windows.editor*.exe"]
    elif system == "Darwin":
        patterns = ["godot.macos.editor*"]
    else:
        patterns = ["godot.linuxbsd.editor*"]
    for pattern in patterns:
        found = [p for p in glob.glob(os.path.join(ROOT, "bin", pattern)) if os.path.isfile(p)]
        if found:
            return max(found, key=os.path.getmtime)
    raise ToolError("editor binary not found in bin/; build with target=editor or set GODOT", code=2)


def opt(args, name, default=""):
    key = "--" + name
    for i, a in enumerate(args):
        if a == key and i + 1 < len(args):
            return args[i + 1]
    return default


def run(cmd, timeout=300, env=None, cwd=None):
    """(exit code, combined output). A timeout counts as exit code 124, like timeout(1)."""
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, env=env, cwd=cwd)
        return p.returncode, p.stdout.decode("utf-8", errors="replace")
    except subprocess.TimeoutExpired as e:
        return 124, (e.stdout or b"").decode("utf-8", errors="replace") + "\n(timed out)"


def render_command(cmd, size="1280x720"):
    """(command, env) for a run that draws frames. On Linux without a display this uses
    software Vulkan (lavapipe) on a virtual X display; elsewhere the real GPU."""
    env = dict(os.environ)
    software = os.environ.get("UZU_SOFTWARE_RENDER") == "1" or (platform.system() == "Linux" and not os.environ.get("DISPLAY"))
    if software:
        if not shutil.which("xvfb-run"):
            raise ToolError("rendering without a display needs xvfb-run (install xvfb)", code=2)
        if not os.path.exists(LAVAPIPE):
            raise ToolError(f"software rendering needs lavapipe ({LAVAPIPE}; install mesa-vulkan-drivers)", code=2)
        env["VK_ICD_FILENAMES"] = LAVAPIPE
        cmd = ["xvfb-run", "-a", "-s", f"-screen 0 {size}x24"] + cmd
    return cmd, env


def driver():
    return ["--rendering-driver", os.environ.get("UZU_RENDERING_DRIVER", "vulkan")]


def tail(text, n=20):
    return "\n".join(text.splitlines()[-n:])


def result_line(output, prefix):
    for line in output.splitlines():
        if line.startswith(prefix):
            return json.loads(line[len(prefix):])
    return None


def read_jsonl(path):
    items = []
    if os.path.exists(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        items.append(json.loads(line))
                    except ValueError:
                        pass
    return items


def gd_tool(project, script, args, prefix, timeout=300):
    rc, out = run([find_godot(), "--headless", "--path", project, "--script", os.path.join(HERE, script), "--"] + args, timeout=timeout)
    result = result_line(out, prefix)
    if result is None:
        sys.stderr.write(out)
        raise ToolError("no result from the engine (see stderr)")
    return result, rc


# ---------------------------------------------------------------- engine


def engine(project, args):
    command = args[0] if args else ""
    godot = find_godot()
    if command == "run":
        frames = opt(args, "frames", "120")
        scene = opt(args, "scene")
        with tempfile.TemporaryDirectory() as tmp:
            jsonl = os.path.join(tmp, "errors.jsonl")
            rc, out = run([godot, "--headless", "--path", project, "--log-json", jsonl, "--quit-after", frames] + ([scene] if scene else []))
            errors = read_jsonl(jsonl)
        return {"ok": rc == 0, "command": "run", "exit_code": rc, "frames": int(frames), "errors": errors, "log_tail": tail(out)}
    if command == "check":
        path = opt(args, "path")
        if not path:
            raise ToolError("check needs --path res://script.gd")
        rc, out = run([godot, "--headless", "--path", project, "--check-only", "--json", "--script", path], timeout=120)
        result = next((json.loads(l) for l in out.splitlines() if l.startswith('{"ok":')), None)
        if result is None:  # The script could not be loaded at all.
            result = {"ok": False, "file": path, "errors": [{"file": path, "line": -1, "column": -1, "code": "LOAD", "message": tail(out, 10).strip()}], "warnings": []}
        result["ok"] = result["ok"] and rc == 0
        return {"command": "check", "path": path, **result}
    if command == "validate":
        result, _ = gd_tool(project, "engine_tool.gd", ["validate"], "ENGINE_RESULT ")
        for issue in result.get("issues", []):
            if issue["kind"] == "script_error":
                issue["errors"] = engine(project, ["check", "--path", issue["file"]])["errors"]
        # Shaders are checked by the engine's shader compiler, without a renderer (B-803).
        for path in project_files(project, (".gdshader", ".gdshaderinc")):
            result["checked"] = result.get("checked", 0) + 1
            check = engine(project, ["check", "--path", path])
            if not check["ok"]:
                result["issues"].append({"kind": "shader_error", "file": path, "message": f"{path} has errors", "errors": check["errors"]})
        result["ok"] = not result["issues"]
        return result
    if command == "shot":
        return look(project, args)
    if command == "import":
        return import_assets(project)
    if command == "uids":
        return check_uids(project, fix="--fix" in args)
    if command == "export":
        return export(project, opt(args, "preset"), opt(args, "out"), "--debug" in args)
    if command == "templates":
        return fetch_templates() if "--fetch" in args else templates(project)
    if command == "export-preset" and opt(args, "template") == "auto":
        path = shared_template(opt(args, "platform", "web"))
        args = [path if (i > 0 and args[i - 1] == "--template") else a for i, a in enumerate(args)]
    result, _ = gd_tool(project, "engine_tool.gd", args, "ENGINE_RESULT ")
    if command == "import-set" and result.get("ok"):
        imported = import_assets(project)  # The new setting takes effect only once the file is reimported.
        result["ok"] = imported["ok"]
        result["errors"] = imported["errors"]
    return result


def export(project, preset, out="", debug=False):
    """Exports the project with a preset (B-804) and lists what was written."""
    if not preset:
        raise ToolError("export needs --preset <name> (see: engine <project> presets)")
    presets = gd_tool(project, "engine_tool.gd", ["presets"], "ENGINE_RESULT ")[0]["presets"]
    chosen = next((p for p in presets if p["name"] == preset), None)
    if chosen is None:
        raise ToolError(f"no export preset named '{preset}'", presets=[p["name"] for p in presets])
    out = os.path.abspath(out) if out else os.path.join(project, chosen["export_path"])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        jsonl = os.path.join(tmp, "errors.jsonl")
        rc, log = run([find_godot(), "--headless", "--path", project, "--export-debug" if debug else "--export-release", preset, out, "--log-json", jsonl], timeout=1800)
        errors = [e for e in read_jsonl(jsonl) if e.get("level") != "warning"]
    folder = os.path.dirname(out)
    files = sorted(f for f in os.listdir(folder) if os.path.isfile(os.path.join(folder, f)))
    ok = rc == 0 and os.path.exists(out)
    result = {"ok": ok, "command": "export", "preset": preset, "out": out, "files": files, "size_mb": round(sum(os.path.getsize(os.path.join(folder, f)) for f in files) / 1048576, 3), "errors": errors}
    if not ok:
        result["log_tail"] = re.sub(r"\x1b\[[0-9;]*m", "", tail(log, 12))
        if any("template" in (e.get("message", "") + log).lower() for e in errors) or "template" in log.lower():
            result["hint"] = "export templates are missing: see `engine <project> templates`, or give the preset a --template zip"
    return result


TEMPLATES_BRANCH = "export-templates"
SHARED_TEMPLATES = os.path.join(ROOT, "bin", "templates")


def fetch_templates():
    """Gets this fork's export templates from the repository's export-templates branch
    (kept out of the main history) into bin/templates/, checking each sha256."""
    rc, out = run(["git", "-C", ROOT, "fetch", "origin", TEMPLATES_BRANCH], timeout=600)
    if rc != 0:
        raise ToolError("could not fetch the export-templates branch: " + tail(out, 3))
    fetched = []
    rc, listing = run(["git", "-C", ROOT, "ls-tree", "-r", "--name-only", "FETCH_HEAD"], timeout=60)
    for info_path in [l for l in listing.splitlines() if l.endswith("info.json")]:
        info = json.loads(subprocess.run(["git", "-C", ROOT, "show", "FETCH_HEAD:" + info_path], capture_output=True, check=True).stdout)
        folder = os.path.dirname(info_path)
        data = subprocess.run(["git", "-C", ROOT, "show", f"FETCH_HEAD:{folder}/{info['file']}"], capture_output=True, check=True).stdout
        if hashlib.sha256(data).hexdigest() != info["sha256"]:
            raise ToolError(f"{info['file']}: sha256 does not match info.json")
        os.makedirs(os.path.join(SHARED_TEMPLATES, folder), exist_ok=True)
        with open(os.path.join(SHARED_TEMPLATES, folder, info["file"]), "wb") as f:
            f.write(data)
        with open(os.path.join(SHARED_TEMPLATES, folder, "info.json"), "w", encoding="utf-8") as f:
            json.dump(info, f)
        fetched.append(dict(info, platform=folder, path=os.path.join(SHARED_TEMPLATES, folder, info["file"])))
    return {"ok": bool(fetched), "command": "templates", "fetched": fetched}


def shared_template(platform_name):
    """The path of the shared template for a platform, fetching it when missing (--template auto)."""
    folder = os.path.join(SHARED_TEMPLATES, platform_name)
    info_path = os.path.join(folder, "info.json")
    if not os.path.exists(info_path):
        fetch_templates()
    if not os.path.exists(info_path):
        raise ToolError(f"no shared export template for '{platform_name}' on the {TEMPLATES_BRANCH} branch")
    with open(info_path, encoding="utf-8") as f:
        return os.path.join(folder, json.load(f)["file"])


def templates(project):
    """Where the editor looks for export templates, which are there, and the presets' own templates."""
    rc, out = run([find_godot(), "--version"], timeout=60)
    version = out.strip().splitlines()[-1] if out.strip() else ""
    config = ".".join(version.split(".")[:3])  # e.g. 4.8.beta (+ .mono in .NET builds)
    if platform.system() == "Windows":
        data = os.environ.get("APPDATA", "")
    elif platform.system() == "Darwin":
        data = os.path.expanduser("~/Library/Application Support")
    else:
        data = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    folder = os.path.join(data, "godot", "export_templates", config)
    installed = sorted(os.listdir(folder)) if os.path.isdir(folder) else []
    presets = gd_tool(project, "engine_tool.gd", ["presets"], "ENGINE_RESULT ")[0]["presets"]
    for p in presets:
        p["custom_template_exists"] = bool(p["custom_template"]) and os.path.exists(p["custom_template"])
    shared = sorted(os.listdir(SHARED_TEMPLATES)) if os.path.isdir(SHARED_TEMPLATES) else []
    return {"ok": True, "command": "templates", "engine_version": version, "templates_dir": folder, "installed": installed, "shared": shared, "presets": presets}


# ---------------------------------------------------------------- UIDs (B-205)

UID_CHARS = "abcdefghijklmnopqrstuvwxy012345678"  # As core/io/resource_uid.cpp.
UID_OWNER_EXTENSIONS = (".gd", ".gdshader", ".gdshaderinc")  # Files whose UID lives in a .uid file next to them.
HEADER_UID = re.compile(r'^(\[gd_(?:scene|resource)\b[^\]]*?\buid=")(uid://[a-y0-8]+)(")')
EXT_RESOURCE = re.compile(r'^\[ext_resource\b([^\]]*)\]', re.M)
ATTR = re.compile(r'(\w+)="([^"]*)"')


def new_uid(taken):
    import secrets

    while True:
        n = secrets.randbits(63)
        text = ""
        while True:
            text = UID_CHARS[n % 34] + text
            n //= 34
            if n == 0:
                break
        uid = "uid://" + text
        if uid not in taken:
            taken.add(uid)
            return uid


def _res_path(project, absolute):
    return "res://" + os.path.relpath(absolute, project).replace(os.sep, "/")


def _abs(project, res):
    return os.path.join(project, res[len("res://"):].replace("/", os.sep)) if res.startswith("res://") else None


def _scan_uids(project):
    owners = {}  # uid -> [res paths]
    uid_of = {}  # res path -> uid
    issues = []
    texts = {}  # .tscn/.tres res path -> text
    for dirpath, dirs, files in os.walk(project):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in sorted(files):
            absolute = os.path.join(dirpath, name)
            res = _res_path(project, absolute)
            uid, owner = None, None
            if name.endswith((".tscn", ".tres")):
                with open(absolute, encoding="utf-8", errors="replace") as f:
                    texts[res] = f.read()
                m = HEADER_UID.match(texts[res])
                uid, owner = (m.group(2) if m else None), res
            elif name.endswith(".uid"):
                owner = res[: -len(".uid")]
                with open(absolute, encoding="utf-8", errors="replace") as f:
                    uid = f.read().strip()
                if not os.path.exists(absolute[: -len(".uid")]):
                    issues.append({"kind": "orphan_uid_file", "file": res, "uid": uid, "fix": "delete it (its file is gone)"})
                    continue
            elif name.endswith(".import"):
                owner = res[: -len(".import")]
                with open(absolute, encoding="utf-8", errors="replace") as f:
                    m = re.search(r'^uid="(uid://[a-y0-8]+)"', f.read(), re.M)
                uid = m.group(1) if m else None
            elif name.endswith(UID_OWNER_EXTENSIONS) and not os.path.exists(absolute + ".uid"):
                issues.append({"kind": "missing_uid_file", "file": res, "fix": "create %s.uid with a new UID" % res})
            if uid and owner:
                owners.setdefault(uid, []).append(owner)
                uid_of[owner] = uid
    return owners, uid_of, issues, texts


def check_uids(project, fix=False):
    """Checks UIDs against the files (B-205): duplicates, missing and orphan .uid files,
    and ext_resource references whose UID is stale or whose path moved. Proposes a fix
    for each; --fix applies them (new UIDs for duplicates and missing .uid files, the
    owner's UID or path in references, orphan .uid files deleted)."""
    fixed = []
    for _ in range(2):  # Duplicates first: renaming them can make references stale.
        owners, uid_of, issues, texts = _scan_uids(project)
        taken = set(owners)
        # A missing .uid file is best recreated with the UID its references already use.
        referenced = {}
        for text in texts.values():
            for m in EXT_RESOURCE.finditer(text):
                attrs = dict(ATTR.findall(m.group(1)))
                if attrs.get("uid") and attrs.get("path"):
                    referenced.setdefault(attrs["path"], attrs["uid"])
        for issue in issues:
            ref_uid = referenced.get(issue["file"]) if issue["kind"] == "missing_uid_file" else None
            if ref_uid and ref_uid not in owners:
                issue["uid"] = ref_uid
                issue["fix"] = "recreate %s.uid with %s, the UID its references use" % (issue["file"], ref_uid)
                owners[ref_uid] = [issue["file"]]
                uid_of[issue["file"]] = ref_uid
                taken.add(ref_uid)
        for uid, files in sorted(owners.items()):
            if len(files) > 1:
                keep = files[0]
                for other in files[1:]:
                    issues.append({"kind": "duplicate_uid", "file": other, "uid": uid, "also": keep, "fix": "give %s a new UID (keep %s on %s)" % (other, uid, keep)})
        for res, text in texts.items():
            for m in EXT_RESOURCE.finditer(text):
                attrs = dict(ATTR.findall(m.group(1)))
                uid, path = attrs.get("uid"), attrs.get("path", "")
                path_ok = bool(path) and _abs(project, path) is not None and os.path.exists(_abs(project, path))
                owner = (owners.get(uid) or [None])[0] if uid else None
                if path_ok and (not uid or owner == path):
                    continue
                if path_ok:
                    right = uid_of.get(path)
                    issues.append({"kind": "stale_uid", "file": res, "path": path, "uid": uid, "fix": ("use %s, the UID of %s" % (right, path)) if right else "remove the UID (the path is right)", "new_uid": right})
                elif owner and os.path.exists(_abs(project, owner)):
                    issues.append({"kind": "moved_path", "file": res, "path": path, "uid": uid, "fix": "use the path %s (where %s is now)" % (owner, uid), "new_path": owner})
                else:
                    issues.append({"kind": "broken_reference", "file": res, "path": path, "uid": uid, "fix": "none: neither the path nor the UID leads to a file"})
        if not fix or not any(i["kind"] in ("duplicate_uid", "missing_uid_file", "orphan_uid_file", "stale_uid", "moved_path") for i in issues):
            break
        dup = [i for i in issues if i["kind"] == "duplicate_uid"]
        for issue in dup:
            absolute = _abs(project, issue["file"])
            new = new_uid(taken)
            if issue["file"].endswith((".tscn", ".tres")):
                with open(absolute, encoding="utf-8") as f:
                    text = f.read()
                with open(absolute, "w", encoding="utf-8") as f:
                    f.write(HEADER_UID.sub(lambda m: m.group(1) + new + m.group(3), text, count=1))
            else:
                with open(absolute + ".uid", "w", encoding="utf-8") as f:
                    f.write(new + "\n")
            fixed.append(dict(issue, applied=new))
        if dup:
            continue  # Rescan: references to the renamed files are judged again.
        for issue in issues:
            kind = issue["kind"]
            if kind == "missing_uid_file":
                new = issue.get("uid") or new_uid(taken)
                with open(_abs(project, issue["file"]) + ".uid", "w", encoding="utf-8") as f:
                    f.write(new + "\n")
                fixed.append(dict(issue, applied=new))
            elif kind == "orphan_uid_file":
                os.remove(_abs(project, issue["file"]))
                fixed.append(dict(issue, applied="deleted"))
            elif kind in ("stale_uid", "moved_path"):
                absolute = _abs(project, issue["file"])
                with open(absolute, encoding="utf-8") as f:
                    text = f.read()

                def repair(m, issue=issue):
                    attrs = m.group(1)
                    if 'path="%s"' % issue["path"] not in attrs or (issue["uid"] and 'uid="%s"' % issue["uid"] not in attrs):
                        return m.group(0)
                    if kind == "moved_path":
                        attrs = attrs.replace('path="%s"' % issue["path"], 'path="%s"' % issue["new_path"])
                    elif issue.get("new_uid"):
                        attrs = attrs.replace('uid="%s"' % issue["uid"], 'uid="%s"' % issue["new_uid"]) if issue["uid"] else attrs.replace(' path=', ' uid="%s" path=' % issue["new_uid"], 1)
                    else:
                        attrs = attrs.replace(' uid="%s"' % issue["uid"], "")
                    return "[ext_resource" + attrs + "]"

                with open(absolute, "w", encoding="utf-8") as f:
                    f.write(EXT_RESOURCE.sub(repair, text))
                fixed.append(dict(issue, applied=issue.get("new_path") or issue.get("new_uid") or "removed"))
    owners, uid_of, remaining, texts = _scan_uids(project) if fixed else (None, None, issues, None)
    if fixed:
        remaining = check_uids(project, fix=False)["issues"]
    return {"ok": not remaining, "command": "uids", "issues": remaining, "fixed": fixed}


def project_files(project, extensions):
    """res:// paths of the project's files with these extensions (hidden directories skipped)."""
    found = []
    for dirpath, dirs, files in os.walk(project):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for f in sorted(files):
            if f.endswith(tuple(extensions)):
                found.append("res://" + os.path.relpath(os.path.join(dirpath, f), project).replace(os.sep, "/"))
    return found


def import_assets(project):
    """Imports (or reimports) the project's assets: textures, sounds, models... (B-802)."""
    retried = False
    for attempt in range(2):
        with tempfile.TemporaryDirectory() as tmp:
            jsonl = os.path.join(tmp, "errors.jsonl")
            rc, out = run([find_godot(), "--headless", "--path", project, "--import", "--log-json", jsonl], timeout=1800)
            errors = [e for e in read_jsonl(jsonl) if e.get("level") != "warning"]
        # Known engine race (upstream): the editor's file system can run a deferred update after
        # the editor has gone at exit ("Parameter singleton is null" in is_cmdline_mode) and
        # abort. The import itself has finished by then, so a crash on exit is retried once.
        crashed_on_exit = rc < 0 and all("is_cmdline_mode" in e.get("function", "") or "singleton" in e.get("message", "") for e in errors)
        if not crashed_on_exit or attempt == 1:
            break
        retried = True
    imported = []
    for dirpath, dirs, files in os.walk(project):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for f in files:
            if f.endswith(".import"):
                rel = os.path.relpath(os.path.join(dirpath, f[: -len(".import")]), project).replace(os.sep, "/")
                imported.append("res://" + rel)
    return {"ok": rc == 0 and not errors, "command": "import", "exit_code": rc, "retried": retried, "imported": sorted(imported), "errors": errors, "log_tail": tail(out, 10) if rc != 0 else ""}


# ---------------------------------------------------------------- look


def screenshot(project, out, frames=8, size="1280x720"):
    """Renders the main scene and saves the last frame (Movie Maker)."""
    godot = find_godot()
    with tempfile.TemporaryDirectory() as tmp:
        args = driver() + ["--write-movie", os.path.join(tmp, "frame.png"), "--fixed-fps", "10", "--quit-after", str(frames)]
        cmd, env = render_command([godot, "--path", project] + args, size)
        rc, log = run(cmd, timeout=600, env=env, cwd=project)
        frames_written = sorted(glob.glob(os.path.join(tmp, "frame*.png")))
        if not frames_written:
            sys.stderr.write(log)
            raise ToolError("render failed: no frame was written (see stderr)", command="shot")
        os.makedirs(os.path.dirname(os.path.abspath(out)) or ".", exist_ok=True)
        shutil.copyfile(frames_written[-1], out)
    return {"ok": True, "command": "shot", "image": out}


def look(project, args):
    command = args[0] if args else "info"
    if command == "shot":
        out = opt(args, "out")
        if not out:
            raise ToolError("shot needs --out file.png")
        return screenshot(project, os.path.abspath(out), int(opt(args, "frames", "8")))
    if command == "check":
        image = opt(args, "image")
        if not image:
            raise ToolError("check needs --image file.png")
        rc, out = run([sys.executable, os.path.join(HERE, "palette_check.py"), image, "--json-only"], timeout=120)
        result = next((json.loads(l) for l in out.splitlines() if l.startswith("{")), None)
        if result is None:
            raise ToolError("image check failed: " + tail(out, 5))
        result.setdefault("ok", rc == 0)
        result.setdefault("command", "check")
        return result
    if command == "panorama":
        out = opt(args, "out")
        if not out:
            raise ToolError("panorama needs --out file.png")
        with tempfile.TemporaryDirectory() as faces:
            cmd, env = render_command([find_godot(), "--path", project] + driver() + ["--script", os.path.join(HERE, "panorama_capture.gd"), "--", faces])
            rc, log = run(cmd, timeout=900, env=env, cwd=project)
            if not os.path.exists(os.path.join(faces, "faces.json")):
                sys.stderr.write(log)
                raise ToolError("capture failed (see stderr)", command="panorama")
            rc, out_text = run([sys.executable, os.path.join(HERE, "panorama.py"), faces, os.path.abspath(out), "--width", opt(args, "width", "1024")], timeout=600)
        result = next((json.loads(l) for l in out_text.splitlines() if l.startswith("{")), None)
        return result or {"ok": rc == 0, "command": "panorama", "image": os.path.abspath(out), "log": tail(out_text, 5)}
    if command == "bake":
        return bake(project, opt(args, "scene"), opt(args, "node"), "--prepare" in args)
    result, _ = gd_tool(project, "look_tool.gd", args, "UZU_RESULT ")
    return result


BAKE_PLUGIN = "res://addons/uzu_bake/plugin.cfg"


def bake(project, scene, node="", prepare=False):
    """Bakes the scene's LightmapGI (B-805). Lightmap baking needs the editor's import
    system and a GPU-capable renderer, so the editor runs (drawing: software Vulkan
    without a display) with a temporary plugin that bakes and saves the scene. The
    project's project.godot is restored and the plugin removed afterwards."""
    if not scene:
        raise ToolError("bake needs --scene res://level.tscn")
    cfg = os.path.join(project, "project.godot")
    addon = os.path.join(project, "addons", "uzu_bake")
    if os.path.exists(addon):
        raise ToolError("addons/uzu_bake already exists in the project; remove it first")
    with open(cfg, encoding="utf-8") as f:
        original = f.read()
    enabled = re.search(r'^enabled=PackedStringArray\((.*)\)$', original, re.M)
    if "\n[editor_plugins]\n" in original and enabled:
        items = [x for x in enabled.group(1).split(",") if x.strip()]
        text = original.replace(enabled.group(0), "enabled=PackedStringArray(" + ", ".join(items + [f'"{BAKE_PLUGIN}"']) + ")", 1)
    else:
        text = original.rstrip("\n") + f'\n\n[editor_plugins]\n\nenabled=PackedStringArray("{BAKE_PLUGIN}")\n'
    shutil.copytree(os.path.join(HERE, "bake"), addon)
    try:
        with open(cfg, "w", encoding="utf-8") as f:
            f.write(text)
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "bake.json")
            user = ["--", "--uzu-bake-scene", scene, "--uzu-bake-out", out] + (["--uzu-bake-node", node] if node else []) + (["--uzu-bake-prepare"] if prepare else [])
            cmd, env = render_command([find_godot(), "-e", "--path", project] + driver() + user)
            rc, log = run(cmd, timeout=3600, env=env, cwd=project)
            result = json.load(open(out, encoding="utf-8")) if os.path.exists(out) else None
    finally:
        with open(cfg, "w", encoding="utf-8") as f:
            f.write(original)  # Exactly as it was.
        shutil.rmtree(addon, ignore_errors=True)
        for leftover in glob.glob(os.path.join(project, ".godot", "editor", "*uzu_bake*")):
            os.remove(leftover)
    if result is None:
        return {"ok": False, "command": "bake", "error": "the editor did not report a result", "log_tail": re.sub(r"\x1b\[[0-9;]*m", "", tail(log, 15))}
    result["command"] = "bake"
    return result


# ---------------------------------------------------------------- selfplay

AUTOLOADS_REPLAY = 'UZUReplay="*res://addons/uzu_selfplay/replay.gd"\nUZUTelemetry="*res://addons/uzu_selfplay/telemetry.gd"\n'


def install_addons(project_copy, files, autoloads):
    """Copies the given ai_loop files into res://addons/uzu_selfplay/ and prepends the
    autoloads, so they run before the game's own nodes. The original project is not touched."""
    addon = os.path.join(project_copy, "addons", "uzu_selfplay")
    os.makedirs(addon, exist_ok=True)
    for f in files:
        shutil.copy(os.path.join(HERE, f), addon)
    cfg = os.path.join(project_copy, "project.godot")
    with open(cfg, encoding="utf-8") as f:
        text = f.read()
    if "\n[autoload]\n" in text:
        text = text.replace("\n[autoload]\n", "\n[autoload]\n\n" + autoloads, 1)
    else:
        text += "\n[autoload]\n\n" + autoloads
    with open(cfg, "w", encoding="utf-8") as f:
        f.write(text)


def selfplay(project, args):
    replay = opt(args, "input")
    if not replay or not os.path.isfile(replay):
        raise ToolError("selfplay needs --input replay.json")
    replay = os.path.abspath(replay)
    frames, out, movie, events_out, seed = opt(args, "frames", "1800"), opt(args, "out"), opt(args, "movie"), opt(args, "log"), opt(args, "seed")
    hash_every, compare = opt(args, "hash-every"), opt(args, "compare")
    packet = opt(args, "packet")
    godot = find_godot()
    sys.path.insert(0, os.path.join(HERE, "telemetry"))
    import report as telemetry

    with tempfile.TemporaryDirectory() as work:
        game = os.path.join(work, "game")
        shutil.copytree(project, game)
        files, autoloads = ["telemetry/telemetry.gd", "selfplay/replay.gd"], AUTOLOADS_REPLAY
        if hash_every:  # State hashes for determinism checks (B-307).
            files.append("selfplay/state_hash.gd")
            autoloads += 'UZUStateHash="*res://addons/uzu_selfplay/state_hash.gd"\n'
        install_addons(game, files, autoloads)
        log, errors_path = os.path.join(work, "events.jsonl"), os.path.join(work, "errors.jsonl")
        hash_path = os.path.join(work, "state_hash.json")
        base = ["--path", game, "--fixed-fps", "60", "--quit-after", frames, "--log-json", errors_path]
        user = ["--", "--uzu-telemetry", log, "--uzu-replay", replay] + (["--uzu-seed", seed] if seed else [])
        if hash_every:
            user += ["--uzu-state-hash", hash_path, "--uzu-hash-every", hash_every]
        env = None
        if movie:
            movie = os.path.abspath(movie)
            cmd, env = render_command([godot] + driver() + ["--resolution", "640x360", "--write-movie", movie] + base + user, "640x360")
        else:
            cmd = [godot, "--headless"] + base + user
        rc, out_text = run(cmd, timeout=900, env=env)
        if events_out and os.path.exists(log):
            shutil.copyfile(log, events_out)
        if not os.path.exists(log):
            return {"ok": False, "command": "selfplay", "error": "no telemetry was written", "log_tail": tail(out_text)}
        events = telemetry.read_events([log])
        rep = telemetry.report(events)
        errors = read_jsonl(errors_path)
        state_hashes = None
        if hash_every and os.path.exists(hash_path):
            with open(hash_path, encoding="utf-8") as f:
                state_hashes = json.load(f)
    quit_event = next((e for e in reversed(events) if e.get("type") == "quit"), {})
    result = {
        "ok": rc == 0,
        "command": "selfplay",
        "exit_code": rc,
        "cleared": rep["cleared"] == 1,
        "checkpoints": [c["id"] for c in rep["checkpoints"]],
        "deaths": rep["deaths"],
        "frames": max((e.get("frame", 0) for e in events), default=0),
        "fps": quit_event.get("fps"),
        "movie": movie or None,
        "errors": errors,
        "report": rep,
    }
    if "--expect-clear" in args and not result["cleared"]:
        result["ok"] = False
    if packet:
        failure = failure_reason(result, "--expect-clear" in args)
        if failure:
            result["ok"] = False  # With --packet, errors count as a failure (a script error does not stop the game).
            result["packet"] = write_failure_packet(packet, project, replay, seed, result, failure, events)
    if hash_every:
        result["state_hashes"] = state_hashes or {"error": "no state hashes were written"}
        if compare:
            result["comparison"] = compare_state_hashes(compare, result["state_hashes"])
            result["ok"] = result["ok"] and result["comparison"]["same"]
    if out:
        with open(out, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=1)
    return result


def failure_reason(result, expect_clear=False):
    """Why a self-play counts as failed, or None (B-308)."""
    serious = [e for e in result["errors"] if e.get("level") in ("error", "script_error")]
    if serious:
        return "error"
    if result["exit_code"] != 0:
        return "exit_code"
    if expect_clear and not result["cleared"]:
        return "not_cleared"
    return None


def write_failure_packet(folder, project, replay, seed, result, reason, events):
    """Bundles what an agent needs to diagnose a failed self-play without running it
    again (B-308): the structured errors (first one first, with the script line), a
    frame and the declared state (B-307) at the failure, the input replay and seed with
    the command to re-run, and the telemetry. Writes <folder>/packet.json and files."""
    os.makedirs(folder, exist_ok=True)
    serious = [e for e in result["errors"] if e.get("level") in ("error", "script_error")]
    first = serious[0] if serious else None
    # The failure frame: the first error's, else the last frame played.
    frame = int(first.get("frame", 0)) if first else 0
    if frame <= 0:
        frame = max(result["frames"], 1)
    shutil.copyfile(replay, os.path.join(folder, "replay.json"))
    with open(os.path.join(folder, "errors.jsonl"), "w", encoding="utf-8") as f:
        f.writelines(json.dumps(e, ensure_ascii=False) + "\n" for e in result["errors"])
    with open(os.path.join(folder, "events.jsonl"), "w", encoding="utf-8") as f:
        f.writelines(json.dumps(e, ensure_ascii=False) + "\n" for e in events)
    # Replay through the failure frame (quit after the next one, so the state includes
    # what that frame did) with drawing and state hashing: the run is
    # deterministic (fixed input, fixed FPS, same seed), so it stops at the same point.
    shot, state = None, None
    with tempfile.TemporaryDirectory() as work:
        game = os.path.join(work, "game")
        shutil.copytree(project, game)
        install_addons(game, ["telemetry/telemetry.gd", "selfplay/replay.gd", "selfplay/state_hash.gd"], AUTOLOADS_REPLAY + 'UZUStateHash="*res://addons/uzu_selfplay/state_hash.gd"\n')
        hash_path = os.path.join(work, "state.json")
        movie = os.path.join(work, "frame.png")
        user = ["--", "--uzu-telemetry", os.path.join(work, "e.jsonl"), "--uzu-replay", replay, "--uzu-state-hash", hash_path, "--uzu-hash-every", "1"] + (["--uzu-seed", seed] if seed else [])
        try:
            cmd, env = render_command([find_godot(), "--path", game] + driver() + ["--resolution", "640x360", "--write-movie", movie, "--fixed-fps", "60", "--quit-after", str(frame + 1)] + user, "640x360")
            run(cmd, timeout=900, env=env)
        except ToolError:
            cmd = [find_godot(), "--headless", "--path", game, "--fixed-fps", "60", "--quit-after", str(frame + 1)] + user
            run(cmd, timeout=900)
        frames = sorted(glob.glob(os.path.join(work, "frame*.png")))
        if frames:
            shot = os.path.join(folder, "failure.png")
            shutil.copyfile(frames[-1], shot)
        if os.path.exists(hash_path):
            with open(hash_path, encoding="utf-8") as f:
                state = json.load(f).get("state")
    with open(os.path.join(folder, "state.json"), "w", encoding="utf-8") as f:
        json.dump(state, f, ensure_ascii=False, indent=1)
    where = None
    if first:
        # The script position: the script error's own, or the script line that raised an engine error.
        where = {"file": first.get("script_file") or first.get("file"), "line": first.get("script_line", first.get("line")), "function": first.get("script_function") or first.get("function")}
    rerun = ["python3", "ai_loop/uzu.py", "selfplay", project, "--input", os.path.join(folder, "replay.json"), "--frames", str(result["frames"] or 1800)] + (["--seed", seed] if seed else [])
    packet = {
        "reason": reason,
        "where": where,
        "first_error": first,
        "frame": frame,
        "errors": len(result["errors"]),
        "screenshot": "failure.png" if shot else None,
        "state": state,
        "checkpoints": result["checkpoints"],
        "deaths": result["deaths"],
        "cleared": result["cleared"],
        "report": result["report"],
        "replay": "replay.json",
        "seed": seed or None,
        "rerun": rerun,
        "files": sorted(os.listdir(folder)) + ["packet.json"],
    }
    with open(os.path.join(folder, "packet.json"), "w", encoding="utf-8") as f:
        json.dump(packet, f, ensure_ascii=False, indent=1)
    return {"folder": os.path.abspath(folder), "reason": reason, "where": where, "frame": frame}


def compare_state_hashes(baseline, current):
    """Compares state hashes with an earlier selfplay result (B-307): the first frame
    where they differ, and the declared state at the end of both runs."""
    if isinstance(baseline, str):
        with open(baseline, encoding="utf-8") as f:
            baseline = json.load(f)
    base = (baseline.get("state_hashes") or baseline).get("hashes", {})
    cur = current.get("hashes", {}) if isinstance(current, dict) else {}
    frames = sorted({int(k) for k in base} | {int(k) for k in cur})
    first = next((f for f in frames if base.get(str(f)) != cur.get(str(f))), None)
    result = {"same": first is None and bool(frames), "compared_frames": len(frames), "first_difference": first}
    if first is not None:
        before = (baseline.get("state_hashes") or baseline).get("state", {})
        after = current.get("state", {})
        result["state_differences"] = {path: {"baseline": before.get(path), "current": after.get(path)} for path in sorted(set(before) | set(after)) if before.get(path) != after.get(path)}
    return result


# ---------------------------------------------------------------- self-tests


class Checks:
    def __init__(self):
        self.failed = 0

    def expect(self, name, condition, detail=""):
        if condition:
            print(f"ok   {name}")
        else:
            print(f"FAIL {name}: {detail}")
            self.failed += 1


def _call(fn, *a):
    try:
        return fn(*a)
    except ToolError as e:
        return dict({"ok": False, "error": e.message}, **e.extra)


def selftest_engine():
    """The engine.sh self-test, in Python so it also runs on Windows."""
    c = Checks()
    with tempfile.TemporaryDirectory() as p:
        with open(os.path.join(p, "project.godot"), "w", encoding="utf-8") as f:
            f.write('config_version=5\n\n[application]\n\nconfig/name="engine selftest"\nrun/main_scene="res://main.tscn"\n')
            f.write('\n[input]\n\njump={\n"deadzone": 0.5,\n"events": [Object(InputEventKey,"resource_local_to_scene":false,"resource_name":"","device":-1,"window_id":0,"alt_pressed":false,"shift_pressed":false,"ctrl_pressed":false,"meta_pressed":false,"pressed":false,"keycode":0,"physical_keycode":32,"key_label":0,"unicode":32,"location":0,"echo":false,"script":null)\n]\n}\n')
        with open(os.path.join(p, "hello.gd"), "w", encoding="utf-8") as f:
            f.write('extends Node3D\nsignal poked\nfunc greet(who: String) -> String:\n\treturn "hello " + who\nfunc on_poked() -> void:\n\tpass\nfunc _ready() -> void:\n\tpush_error("selftest error from _ready")\n')
        with open(os.path.join(p, "broken.gd"), "w", encoding="utf-8") as f:
            f.write("extends Node\nfunc broken(:\n")
        e = lambda *a: _call(engine, p, list(a))  # noqa: E731
        S = ["--scene", "res://main.tscn"]
        c.expect("new", e("new", *S, "--type", "Node3D", "--name", "Main")["ok"])
        r = e("add", *S, "--parent", ".", "--type", "MeshInstance3D", "--name", "Box", "--set", '{"mesh": "BoxMesh", "position": "Vector3(0, 1, 0)"}')
        c.expect("add mesh", r["ok"] and r["node"]["type"] == "MeshInstance3D", r)
        r = e("get", *S, "--node", "Box", "--property", "position")
        c.expect("get", r.get("value") == "Vector3(0, 1, 0)", r)
        r = e("set", *S, "--node", "Box", "--property", "position", "--value", "Vector3(2, 0, -1)")
        c.expect("set", r["ok"] and r["value"] == "Vector3(2, 0, -1)", r)
        r = e("set", *S, "--node", "Box", "--property", "position:y", "--value", "3")
        c.expect("set indexed", r["ok"] and r["value"] == 3.0, r)
        c.expect("add group", e("add", *S, "--parent", ".", "--type", "Node3D", "--name", "Group")["ok"])
        r = e("reparent", *S, "--node", "Box", "--parent", "Group")
        c.expect("reparent", r["ok"] and r["node"]["path"] == "Group/Box", r)
        r = e("tree", *S)
        c.expect("tree", r["ok"] and [n["path"] for n in r["nodes"]] == [".", "Group", "Group/Box"], r)
        r = e("props", *S, "--node", "Group/Box")
        c.expect("props", r["ok"] and r["properties"]["mesh"] == {"class": "BoxMesh", "embedded": True} and r["properties"]["position"] == "Vector3(2, 3, -1)", r)
        r = e("script", *S, "--node", ".", "--path", "res://hello.gd")
        c.expect("script", r["ok"] and r["node"]["script"] == "res://hello.gd", r)
        r = e("call", *S, "--node", ".", "--method", "greet", "--args", '["agent"]')
        c.expect("call", r["ok"] and r["result"] == "hello agent", r)
        c.expect("connect", e("connect", *S, "--from", ".", "--signal", "poked", "--to", ".", "--method", "on_poked")["ok"])
        c.expect("connection saved", 'connection signal="poked"' in open(os.path.join(p, "main.tscn"), encoding="utf-8").read())
        r = e("classes", "--inherits", "Light3D")
        c.expect("classes", r["ok"] and "OmniLight3D" in r["classes"] and "Node2D" not in r["classes"], "")
        r = e("describe", "--class", "OmniLight3D")
        c.expect("describe", r["ok"] and any(x["name"] == "omni_range" for x in r["properties"]), "")
        c.expect("check good", e("check", "--path", "res://hello.gd")["ok"])
        r = e("check", "--path", "res://broken.gd")
        c.expect("check bad", not r["ok"] and r["errors"] and all(k in r["errors"][0] for k in ("file", "line", "column", "code", "message")) and r["errors"][0]["line"] > 0, r)
        r = e("run", "--frames", "5")
        c.expect("run", r["ok"] and any("selftest error from _ready" in x["message"] and x["code"].startswith("E") and x["file"] for x in r["errors"]), r)
        with open(os.path.join(p, "ref.tres"), "w", encoding="utf-8") as f:
            f.write('[gd_resource type="Resource" load_steps=2 format=3]\n\n[ext_resource type="Texture2D" path="res://missing.png" id="1"]\n\n[resource]\nmetadata/tex = ExtResource("1")\n')
        r = e("validate")
        c.expect("validate broken", not r["ok"] and {("script_error", "res://broken.gd"), ("missing_dependency", "res://ref.tres")} <= {(i["kind"], i["file"]) for i in r["issues"]} and any(x["line"] == 2 for i in r["issues"] for x in i.get("errors", [])), r)
        os.remove(os.path.join(p, "broken.gd"))
        os.remove(os.path.join(p, "ref.tres"))
        r = e("validate")
        c.expect("validate clean", r["ok"] and r["issues"] == [] and r["checked"] >= 2, r)
        r = e("actions")
        c.expect("actions", r["ok"] and [a["name"] for a in r["actions"]] == ["jump"] and r["actions"][0]["events"][0]["class"] == "InputEventKey" and "Space" in r["actions"][0]["events"][0]["text"], r)
        r = e("remove", *S, "--node", "Group/Box")
        c.expect("remove", r["ok"] and r["removed"] == "Group/Box", r)
        c.expect("bad node", not e("get", *S, "--node", "Nope", "--property", "position")["ok"])
        c.expect("bad scene", not e("tree", "--scene", "res://nope.tscn")["ok"])
        # B-802: resources, settings, input map, import settings.
        r = e("res-new", "--path", "res://mat/red.tres", "--type", "StandardMaterial3D", "--set", '{"albedo_color": "Color(1, 0, 0, 1)"}')
        c.expect("res-new", r["ok"] and os.path.exists(os.path.join(p, "mat", "red.tres")), r)
        r = e("res-set", "--path", "res://mat/red.tres", "--property", "roughness", "--value", "0.25")
        c.expect("res-set", r["ok"] and r["value"] == 0.25, r)
        r = e("res-props", "--path", "res://mat/red.tres")
        c.expect("res-props", r["ok"] and r["properties"]["albedo_color"] == "Color(1, 0, 0, 1)" and r["properties"]["roughness"] == 0.25, r)
        c.expect("res-get bad", not e("res-get", "--path", "res://mat/red.tres", "--property", "nope")["ok"])
        c.expect("res-new bad type", not e("res-new", "--path", "res://x.tres", "--type", "Node3D")["ok"])
        r = e("setting-set", "--name", "display/window/size/viewport_width", "--value", "1280")
        c.expect("setting-set", r["ok"] and e("setting-get", "--name", "display/window/size/viewport_width")["value"] == 1280, r)
        c.expect("setting kept", 'run/main_scene="res://main.tscn"' in open(os.path.join(p, "project.godot"), encoding="utf-8").read())
        r = e("action-add", "--name", "fire", "--mouse", "left", "--key", "F")
        fire = next((a for a in r.get("actions", []) if a["name"] == "fire"), {})
        c.expect("action-add", r["ok"] and [x["class"] for x in fire.get("events", [])] == ["InputEventKey", "InputEventMouseButton"], r)
        c.expect("action-add bad key", not e("action-add", "--name", "x", "--key", "NoSuchKey")["ok"])
        r = e("action-remove", "--name", "fire")
        c.expect("action-remove", r["ok"] and [a["name"] for a in r["actions"]] == ["jump"], r)
        with open(os.path.join(p, "tex.png"), "wb") as f:
            import struct
            import zlib

            raw = b"".join(b"\x00" + b"\xc8\x64\x32" * 4 for _ in range(4))
            chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)  # noqa: E731
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
        r = e("import")
        c.expect("import", r["ok"] and "res://tex.png" in r["imported"], r)
        r = e("import-set", "--path", "res://tex.png", "--param", "compress/mode", "--value", "2")
        c.expect("import-set", r["ok"] and e("import-get", "--path", "res://tex.png")["params"]["compress/mode"] == 2, r)
        c.expect("import-set bad", not e("import-set", "--path", "res://tex.png", "--param", "nope", "--value", "1")["ok"])
        # B-803: shaders are checked like scripts, and validate finds broken ones.
        with open(os.path.join(p, "ok.gdshader"), "w", encoding="utf-8") as f:
            f.write("shader_type spatial;\nvoid fragment() {\n\tALBEDO = vec3(1.0);\n}\n")
        c.expect("shader good", e("check", "--path", "res://ok.gdshader")["ok"])
        with open(os.path.join(p, "bad.gdshader"), "w", encoding="utf-8") as f:
            f.write("shader_type spatial;\n\nvoid fragment() {\n\tALBEDO = vec3(nope);\n}\n")
        r = e("check", "--path", "res://bad.gdshader")
        c.expect("shader bad", not r["ok"] and r["errors"][0]["line"] == 4 and r["errors"][0]["code"].startswith("SH"), r)
        r = e("validate")
        c.expect("validate shader", not r["ok"] and [(i["kind"], i["file"]) for i in r["issues"]] == [("shader_error", "res://bad.gdshader")] and r["issues"][0]["errors"][0]["line"] == 4, r)
        os.remove(os.path.join(p, "bad.gdshader"))
        # B-804: export presets and templates (the export itself needs a template; see the B-804 record).
        r = e("export-preset", "--platform", "web", "--template", "/nonexistent/web.zip")
        c.expect("export-preset", r["ok"] and r["platform"] == "Web" and r["export_path"] == "build/web/index.html", r)
        r = e("export-preset", "--platform", "linux", "--name", "Desk")
        c.expect("export-preset second", r["ok"] and [x["name"] for x in r["presets"]] == ["Web", "Desk"], r)
        r = e("export-preset", "--platform", "web", "--out", "out/index.html")
        c.expect("export-preset update", r["ok"] and len(r["presets"]) == 2 and r["presets"][0]["export_path"] == "out/index.html", r)
        c.expect("export-preset bad", not e("export-preset", "--platform", "ps9")["ok"])
        r = e("templates")
        c.expect("templates", r["ok"] and r["engine_version"] and r["presets"][0]["custom_template"] == "/nonexistent/web.zip" and not r["presets"][0]["custom_template_exists"], r)
        r = e("export", "--preset", "Nope")
        c.expect("export unknown preset", not r["ok"] and r.get("presets") == ["Web", "Desk"], r)
        # B-717: texel density of sprites, and the pixel discipline preset.
        with open(os.path.join(p, "sprites.tscn"), "w", encoding="utf-8") as f:
            f.write('[gd_scene format=3]\n\n[node name="S" type="Node3D"]\n\n'
                    '[node name="A" type="Sprite3D" parent="."]\npixel_size = 0.05\ntexture_filter = 0\n\n'
                    '[node name="B" type="Sprite3D" parent="."]\npixel_size = 0.05\ntexture_filter = 0\nscale = Vector3(1.5, 1.5, 1.5)\n\n'
                    '[node name="C" type="Sprite3D" parent="."]\npixel_size = 0.05\n')
        r = _call(look, p, ["texels", "--scene", "res://sprites.tscn"])
        c.expect("texels warns", not r["ok"] and {(w["kind"], w["node"]) for w in r["warnings"]} == {("texel_density", "B"), ("blurry_filter", "C")}, r)
        r = _call(look, p, ["pixel", "--scene", "res://sprites.tscn", "--snap"])
        c.expect("pixel preset", r["ok"] and r["texels"]["ok"] and abs(r["texel"] - 0.05) < 1e-6, r)
        r = _call(look, p, ["texels", "--scene", "res://sprites.tscn"])
        c.expect("texels clean", r["ok"] and r["warnings"] == [], r)
        # B-204: the project map (a separate small project: scenes, an instanced scene, a
        # script with preload and class_name, signal connections, an autoload).
        with tempfile.TemporaryDirectory() as m:
            files = {
                "project.godot": 'config_version=5\n\n[application]\n\nrun/main_scene="res://main.tscn"\n\n[autoload]\n\nGame="*res://game.gd"\n',
                "game.gd": "extends Node\n",
                "coin.gd": 'class_name Coin\nextends Area3D\nconst SPARK = preload("res://spark.tres")\nsignal picked\n',
                "spark.tres": '[gd_resource type="StandardMaterial3D" format=3]\n\n[resource]\n',
                "coin.tscn": '[gd_scene load_steps=2 format=3]\n\n[ext_resource type="Script" path="res://coin.gd" id="1"]\n\n[node name="Coin" type="Area3D"]\nscript = ExtResource("1")\n',
                "main.tscn": '[gd_scene load_steps=2 format=3]\n\n[ext_resource type="PackedScene" path="res://coin.tscn" id="1"]\n\n[node name="Main" type="Node3D"]\n\n[node name="Coin1" parent="." instance=ExtResource("1")]\n\n[connection signal="picked" from="Coin1" to="." method="_on_coin_picked"]\n',
            }
            for name, text in files.items():
                with open(os.path.join(m, name), "w", encoding="utf-8") as f:
                    f.write(text)
            r = _call(engine, m, ["map"])
            ok = r.get("ok") and r["depends_on"]["res://main.tscn"] == ["res://coin.tscn"] and r["depends_on"]["res://coin.gd"] == ["res://spark.tres"]
            ok = ok and r["used_by"]["res://coin.tscn"] == ["res://main.tscn"] and r["main_scene"] == "res://main.tscn"
            ok = ok and r["scenes"]["res://main.tscn"]["connections"] == [{"from": "Coin1", "signal": "picked", "to": ".", "method": "_on_coin_picked"}]
            ok = ok and r["scenes"]["res://main.tscn"]["instances"] == [{"node": "Coin1", "scene": "res://coin.tscn"}]
            ok = ok and r["scenes"]["res://coin.tscn"]["scripts"] == [{"node": ".", "script": "res://coin.gd"}]
            ok = ok and {"class": "Coin", "base": "Area3D", "path": "res://coin.gd"} in r["classes"] and r["autoloads"] == [{"name": "Game", "path": "res://game.gd", "singleton": True}]
            c.expect("map", ok, r)
        # B-205: UID problems are found, fixed, and validate (B-203) is clean afterwards.
        with tempfile.TemporaryDirectory() as q:
            os.makedirs(os.path.join(q, "sub"))
            taken = set()
            u = {k: new_uid(taken) for k in ("main", "child", "coin_gd", "main_gd")}
            files = {
                "project.godot": 'config_version=5\n\n[application]\n\nrun/main_scene="res://main.tscn"\n',
                "coin.gd": "extends Node3D\n", "coin.gd.uid": u["coin_gd"] + "\n",
                "main.gd": "extends Node3D\n",  # Its .uid file is missing.
                "ghost.gd.uid": "uid://abcdefg\n",  # An orphan.
                "sub/child.tscn": '[gd_scene load_steps=2 format=3 uid="%s"]\n\n[ext_resource type="Script" uid="uid://b2xstale" path="res://coin.gd" id="1"]\n\n[node name="Child" type="Node3D"]\nscript = ExtResource("1")\n' % u["child"],
                "main.tscn": '[gd_scene load_steps=3 format=3 uid="%s"]\n\n[ext_resource type="Script" uid="%s" path="res://main.gd" id="1"]\n[ext_resource type="PackedScene" uid="%s" path="res://child.tscn" id="2"]\n\n[node name="Main" type="Node3D"]\nscript = ExtResource("1")\n\n[node name="Coin" parent="." instance=ExtResource("2")]\n' % (u["main"], u["main_gd"], u["child"]),
            }
            files["main_copy.tscn"] = files["main.tscn"]  # A duplicate UID.
            for name, text in files.items():
                with open(os.path.join(q, name), "w", encoding="utf-8") as f:
                    f.write(text)
            r = _call(engine, q, ["uids"])
            kinds = sorted(i["kind"] for i in r.get("issues", []))
            c.expect("uids found", not r["ok"] and kinds == ["duplicate_uid", "missing_uid_file", "moved_path", "moved_path", "orphan_uid_file", "stale_uid"], kinds)
            c.expect("validate sees the breakage", not _call(engine, q, ["validate"])["ok"])
            r = _call(engine, q, ["uids", "--fix"])
            c.expect("uids fixed", r["ok"] and len(r["fixed"]) == 6, r)
            with open(os.path.join(q, "main.gd.uid"), encoding="utf-8") as f:
                c.expect("uid restored from references", f.read().strip() == u["main_gd"])
            r = _call(engine, q, ["validate"])
            c.expect("validate clean after uids --fix", r["ok"] and r["issues"] == [], r)
        r = _call(look, p, ["info"])
        c.expect("look info", r["ok"] and "apply" in r["commands"], r)
    print("engine selftest: " + ("ok" if not c.failed else "FAILED"))
    return c.failed == 0


def selftest_selfplay():
    """The selfplay.sh self-test: same input, same play; seeds fix the random numbers."""
    game = os.path.join(HERE, "selfplay", "sample_game")
    replay = os.path.join(HERE, "selfplay", "sample_replay.json")
    with tempfile.TemporaryDirectory() as work:
        a = selfplay(game, ["--input", replay, "--frames", "600"])
        b = selfplay(game, ["--input", replay, "--frames", "600"])
        no_jump = os.path.join(work, "no_jump.json")
        with open(no_jump, "w") as f:
            f.write('{"events": [{"frame": 0, "action": "move_right", "pressed": true}]}\n')
        c = selfplay(game, ["--input", no_jump, "--frames", "600"])

        def main_items(r):
            rep = dict(r["report"])
            rep.pop("fps")  # Wall-clock speed differs between runs; nothing else may.
            return dict({k: r[k] for k in ("ok", "cleared", "checkpoints", "deaths", "frames", "errors")}, report=rep)

        assert a["ok"] and a["cleared"] and a["checkpoints"] == ["cp1", "cp2", "cp3"] and a["deaths"] == 1, a
        assert isinstance(a["fps"], (int, float)) and a["fps"] > 0, a
        assert main_items(a) == main_items(b), (main_items(a), main_items(b))
        assert not c["cleared"] and c["deaths"] > 1 and c["checkpoints"] == ["cp1"], c
        assert c["report"]["stuck_points"] and c["report"]["stuck_points"][0]["deaths"] == c["deaths"], c
        rand = os.path.join(work, "random_game")
        os.makedirs(rand)
        with open(os.path.join(rand, "project.godot"), "w") as f:
            f.write('config_version=5\n\n[application]\n\nrun/main_scene="res://main.tscn"\n')
        with open(os.path.join(rand, "main.tscn"), "w") as f:
            f.write('[gd_scene load_steps=2 format=3]\n\n[ext_resource type="Script" path="res://main.gd" id="1"]\n\n[node name="Main" type="Node"]\nscript = ExtResource("1")\n')
        with open(os.path.join(rand, "main.gd"), "w") as f:
            f.write('extends Node\n\nfunc _ready() -> void:\n\tget_node("/root/UZUTelemetry").checkpoint("r%d" % (randi() % 1000000))\n')
        # B-307: state hashes. The same input gives the same hashes; a behavior change
        # (a faster player) changes them, and the comparison names the first frame and the state.
        h1 = selfplay(game, ["--input", replay, "--frames", "600", "--hash-every", "30", "--out", os.path.join(work, "h1.json")])
        assert h1["state_hashes"]["hashes"] and "/root/Main" in h1["state_hashes"]["state"], h1["state_hashes"]
        h2 = selfplay(game, ["--input", replay, "--frames", "600", "--hash-every", "30", "--compare", os.path.join(work, "h1.json")])
        assert h2["ok"] and h2["comparison"]["same"] and h2["comparison"]["compared_frames"] >= 5, h2["comparison"]
        changed = os.path.join(work, "changed_game")
        shutil.copytree(game, changed)
        with open(os.path.join(changed, "main.gd"), encoding="utf-8") as f:
            code = f.read()
        with open(os.path.join(changed, "main.gd"), "w", encoding="utf-8") as f:
            f.write(code.replace("const SPEED := 4.0", "const SPEED := 4.2"))
        h3 = selfplay(changed, ["--input", replay, "--frames", "600", "--hash-every", "30", "--compare", os.path.join(work, "h1.json")])
        assert not h3["ok"] and not h3["comparison"]["same"] and h3["comparison"]["first_difference"] == 30, h3["comparison"]
        assert h3["comparison"]["state_differences"], h3["comparison"]
        # B-308: a failure packet. A game that breaks at the second checkpoint: the packet
        # alone names the script and line, and holds a frame, the state and the replay.
        broken = os.path.join(work, "broken_game")
        shutil.copytree(game, broken)
        with open(os.path.join(broken, "main.gd"), encoding="utf-8") as f:
            lines = f.read().split("\n")
        at = next(i for i, l in enumerate(lines) if '_telemetry("checkpoint", CHECKPOINTS[cx])' in l)
        indent = lines[at][: len(lines[at]) - len(lines[at].lstrip())]
        lines[at + 1 : at + 1] = [indent + 'if CHECKPOINTS[cx] == "cp2":', indent + "\tvar broken_list := []", indent + "\tprint(broken_list[3])"]
        with open(os.path.join(broken, "main.gd"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
        bad_line = at + 4  # 1-based line of print(broken_list[3]).
        packet_dir = os.path.join(work, "packet")
        p = selfplay(broken, ["--input", replay, "--frames", "600", "--packet", packet_dir, "--expect-clear"])
        assert not p["ok"] and p["packet"]["reason"] == "error", p.get("packet")
        with open(os.path.join(packet_dir, "packet.json"), encoding="utf-8") as f:
            pk = json.load(f)
        assert pk["where"] == {"file": "res://main.gd", "line": bad_line, "function": "_physics_process"}, pk["where"]
        # The state at the failure: just at the second checkpoint (the game itself plays on after a script error).
        assert "cp2" in pk["checkpoints"] and pk["state"]["/root/Main"]["respawn_x"] == "500.0", pk["state"]
        assert pk["screenshot"] in (None, "failure.png")
        assert os.path.exists(os.path.join(packet_dir, "replay.json")) and os.path.exists(os.path.join(packet_dir, "errors.jsonl"))
        assert pk["rerun"][:3] == ["python3", "ai_loop/uzu.py", "selfplay"]
        assert "packet" not in selfplay(game, ["--input", replay, "--frames", "600", "--packet", os.path.join(work, "none")]), "no packet when nothing fails"
        s1, s1b, s2 = (selfplay(rand, ["--input", no_jump, "--frames", "5", "--seed", s])["checkpoints"] for s in ("1", "1", "2"))
        assert s1 == s1b and s1 != s2 and len(s1) == 1, (s1, s1b, s2)
    print("selfplay selftest: ok")
    return True


BAKE_SCENE = """[gd_scene load_steps=4 format=3]

[sub_resource type="PlaneMesh" id="1"]
size = Vector2(8, 8)

[sub_resource type="BoxMesh" id="2"]

[sub_resource type="Environment" id="3"]
background_mode = 1
background_color = Color(0.4, 0.5, 0.7, 1)

[node name="Level" type="Node3D"]

[node name="Floor" type="MeshInstance3D" parent="."]
mesh = SubResource("1")

[node name="Box" type="MeshInstance3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0.5, 0)
mesh = SubResource("2")

[node name="Sun" type="DirectionalLight3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 0.5, 0.866, 0, -0.866, 0.5, 0, 4, 0)
light_bake_mode = 2

[node name="WorldEnvironment" type="WorldEnvironment" parent="."]
environment = SubResource("3")

[node name="LightmapGI" type="LightmapGI" parent="."]
quality = 0
bounces = 1
"""


def selftest_bake():
    """Lightmap baking (B-805): runs the editor with a renderer, so it is slow and not part of 'all'."""
    with tempfile.TemporaryDirectory() as p:
        original = 'config_version=5\n\n[application]\n\nconfig/name="bake selftest"\nrun/main_scene="res://level.tscn"\n'
        with open(os.path.join(p, "project.godot"), "w", encoding="utf-8") as f:
            f.write(original)
        with open(os.path.join(p, "level.tscn"), "w", encoding="utf-8") as f:
            f.write(BAKE_SCENE)
        r = bake(p, "res://level.tscn", prepare=True)
        assert r["ok"] and r["error"] == "BAKE_ERROR_OK" and r["prepared"] == 2 and r["light_data"].endswith("level.lmbake"), r
        assert os.path.exists(os.path.join(p, "level.lmbake")) and glob.glob(os.path.join(p, "level*.exr")), os.listdir(p)
        text = open(os.path.join(p, "level.tscn"), encoding="utf-8").read()
        assert "level.lmbake" in text and "add_uv2 = true" in text, text
        assert open(os.path.join(p, "project.godot"), encoding="utf-8").read() == original, "project.godot must be restored"
        assert not os.path.exists(os.path.join(p, "addons", "uzu_bake")), "the plugin must be removed"
        # Without a LightmapGI: a clear error, and the project is still restored.
        with open(os.path.join(p, "empty.tscn"), "w", encoding="utf-8") as f:
            f.write('[gd_scene format=3]\n\n[node name="E" type="Node3D"]\n')
        r = bake(p, "res://empty.tscn")
        assert not r["ok"] and "LightmapGI" in r["error"], r
        assert open(os.path.join(p, "project.godot"), encoding="utf-8").read() == original
    print("bake selftest: ok")
    return True


def selftest_tools():
    """The self-tests of the Python tools; each runs in its own interpreter."""
    tools = ["mcp_server.py", "bench.py", "prepublish.py", "aiplayer.py", "telemetry/report.py", "feedback/to_backlog.py", "bridge/mock_server.py", "bridge/client.py"]
    ok = True
    for tool in tools:
        rc, out = run([sys.executable, os.path.join(HERE, tool), "--self-test"], timeout=900)
        print(("ok   " if rc == 0 else "FAIL ") + tool + ("" if rc == 0 else ": " + tail(out, 8)))
        ok = ok and rc == 0
    print("tools selftest: " + ("ok" if ok else "FAILED"))
    return ok


def selftest(which):
    steps = {"engine": selftest_engine, "selfplay": selftest_selfplay, "tools": selftest_tools, "bake": selftest_bake}
    names = ["engine", "selfplay", "tools"] if which == "all" else [which]  # bake is slow: run it on its own.
    ok = True
    for name in names:
        try:
            ok = steps[name]() and ok
        except (AssertionError, ToolError) as e:
            print(f"{name} selftest: FAILED: {e}")
            ok = False
    return 0 if ok else 1


# ---------------------------------------------------------------- main


def main(argv):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8")  # Windows consoles default to a local code page.
        except (AttributeError, ValueError):
            pass
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__)
        return 0 if argv else 2
    tool, rest = argv[0], argv[1:]
    if tool == "selftest":
        return selftest(rest[0] if rest else "all")
    if tool == "publish":
        sys.path.insert(0, os.path.join(HERE, "bridge"))
        import client

        return client.main(["publish"] + rest)
    if tool not in ("engine", "look", "selfplay") or not rest:
        print(__doc__, file=sys.stderr)
        return 2
    project = os.path.abspath(rest[0])
    if not os.path.exists(os.path.join(project, "project.godot")):
        print(json.dumps({"ok": False, "error": f"{rest[0]} is not a project (no project.godot)"}))
        return 2
    try:
        result = {"engine": engine, "look": look, "selfplay": selfplay}[tool](project, rest[1:])
    except ToolError as e:
        result = {"ok": False, "error": e.message, **e.extra}
        print(json.dumps(result, ensure_ascii=False))
        return e.code
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
